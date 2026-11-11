#pragma once
#include "audio.hpp"
#include <stop_token>

namespace cpuai {
struct AudioWindow {
    std::uint64_t sequence=0,segment=0,startFrame=0;
    AudioSample sample;
    std::vector<std::int16_t> pcm;
};
enum class FeedStop {inputConsumed,backpressure,paused,cancelled,workSlice,finished};
struct FeedResult {std::size_t consumed=0,windows=0;FeedStop stop=FeedStop::inputConsumed;};
struct AudioStreamMetrics {
    std::uint64_t consumedFrames=0,windows=0,discardedPartialFrames=0,discontinuities=0;
    std::size_t peakWindows=0;
};
// Single-owner incremental framing; hardware threads must not call this concurrently.
// Exact accepted-frame count is the acknowledgement for caller-owned input.
class PcmWindowStream {
    std::uint32_t rate_;std::size_t windowFrames_,capacity_;
    std::uint64_t segment_=0,frame_=0,nextSequence_=1;
    bool paused_=false,finished_=false;
    std::vector<std::int16_t> pending_;
    std::deque<AudioWindow> ready_;
    AudioStreamMetrics metrics_;
public:
    PcmWindowStream(std::uint32_t rate,unsigned windowMs=100,std::size_t capacity=8):rate_(rate),capacity_(capacity){
        if(rate<8000||rate>48000||windowMs<50||windowMs>2000||capacity==0||capacity>32||
           (static_cast<std::uint64_t>(rate)*windowMs)%1000!=0)throw std::runtime_error("invalid PCM window settings");
        windowFrames_=static_cast<std::size_t>(rate)*windowMs/1000;pending_.reserve(windowFrames_);
    }
    FeedResult feed(std::span<const std::int16_t> pcm,std::stop_token token={},std::size_t maxWindows=8){
        if(pcm.size()>65536||maxWindows>32)throw std::runtime_error("feed limit: 65536 frames / 32 windows");
        FeedResult r;
        while(r.consumed<pcm.size()){
            if(finished_){r.stop=FeedStop::finished;break;}
            if(paused_){r.stop=FeedStop::paused;break;}
            if(token.stop_requested()){r.stop=FeedStop::cancelled;break;}
            if(r.windows>=maxWindows){r.stop=FeedStop::workSlice;break;}
            if(ready_.size()>=capacity_){r.stop=FeedStop::backpressure;break;}
            const auto n=std::min(windowFrames_-pending_.size(),pcm.size()-r.consumed);
            // A practical lifetime limit keeps frame/sequence arithmetic bounded.
            if(metrics_.consumedFrames>1000000000000ULL-n)throw std::runtime_error("audio session frame limit");
            pending_.insert(pending_.end(),pcm.begin()+static_cast<std::ptrdiff_t>(r.consumed),pcm.begin()+static_cast<std::ptrdiff_t>(r.consumed+n));
            r.consumed+=n;frame_+=n;metrics_.consumedFrames+=n;
            if(pending_.size()==windowFrames_){
                AudioWindow w;w.sequence=nextSequence_++;w.segment=segment_;w.startFrame=frame_-windowFrames_;
                w.sample=analyzePcm16(pending_,rate_);w.pcm=std::move(pending_);ready_.push_back(std::move(w));
                pending_=std::vector<std::int16_t>();pending_.reserve(windowFrames_);
                ++r.windows;++metrics_.windows;metrics_.peakWindows=std::max(metrics_.peakWindows,ready_.size());
            }
        }
        return r;
    }
    void pause(){if(finished_)throw std::runtime_error("audio stream finished");paused_=true;}
    // A user pause may lose device audio; resume declares a new segment unless the
    // caller explicitly guarantees a lossless pause of the producer.
    void resume(bool continuous=false){
        if(finished_)throw std::runtime_error("audio stream finished");
        if(paused_&&!continuous)discontinuity();
        paused_=false;
    }
    void discontinuity(){
        if(finished_)throw std::runtime_error("audio stream finished");
        metrics_.discardedPartialFrames+=pending_.size();pending_.clear();frame_=0;
        ++segment_;++metrics_.discontinuities;
    }
    void finish(){
        if(!finished_){metrics_.discardedPartialFrames+=pending_.size();pending_.clear();finished_=true;}
    }
    bool finished()const{return finished_;}
    const auto& ready()const{return ready_;}
    std::optional<AudioWindow> pop(){if(ready_.empty())return std::nullopt;auto w=std::move(ready_.front());ready_.pop_front();return w;}
    const auto& metrics()const{return metrics_;}
    std::size_t pendingFrames()const{return pending_.size();}
    std::size_t windowFrames()const{return windowFrames_;}
};
inline void writeWave(const std::string& path,std::span<const std::int16_t> pcm,std::uint32_t rate){
    if(rate<8000||rate>48000||pcm.size()<rate/20||pcm.size()>static_cast<std::size_t>(rate)*2)
        throw std::runtime_error("capture rate/duration outside audio bounds");
    std::ofstream out(path,std::ios::binary);if(!out)throw std::runtime_error("cannot write capture WAV");
    auto u16=[&](std::uint16_t v){out.put(static_cast<char>(v&255));out.put(static_cast<char>(v>>8));};
    auto u32=[&](std::uint32_t v){u16(static_cast<std::uint16_t>(v&65535));u16(static_cast<std::uint16_t>(v>>16));};
    const auto bytes=static_cast<std::uint32_t>(pcm.size()*2);
    out.write("RIFF",4);u32(36+bytes);out.write("WAVEfmt ",8);u32(16);u16(1);u16(1);u32(rate);u32(rate*2);u16(2);u16(16);
    out.write("data",4);u32(bytes);for(auto v:pcm)u16(static_cast<std::uint16_t>(v));
    if(!out)throw std::runtime_error("capture WAV write failed");
}
}
