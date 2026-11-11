#include "cpuai/audio_stream.hpp"
#include <iostream>
using namespace cpuai;
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}require(failed,"expected rejection");}
std::vector<std::int16_t> tone(std::size_t count){std::vector<std::int16_t> pcm(count);for(std::size_t i=0;i<count;++i)pcm[i]=static_cast<std::int16_t>(20000*std::sin(2*3.14159265358979323846*500*i/16000));return pcm;}
int main(){
    const auto pcm=tone(4800);PcmWindowStream s(16000,100,2);
    auto r=s.feed(pcm);require(r.consumed==3200&&r.windows==2&&r.stop==FeedStop::backpressure,"backpressure acknowledgement");
    auto first=s.pop();require(first&&first->sequence==1&&first->startFrame==0&&first->segment==0,"first window identity");
    require(first->sample.fingerprint==analyzePcm16(std::span(pcm).first(1600),16000).fingerprint,"direct/stream equivalence");
    r=s.feed(std::span(pcm).subspan(3200));require(r.consumed==1600&&r.windows==1,"retry consumes remaining input");
    auto second=s.pop();auto third=s.pop();require(second->sequence==2&&third->sequence==3&&third->startFrame==3200,"no loss/reordering after retry");
    PcmWindowStream chunks(16000);
    for(std::size_t i=0;i<1600;){const auto n=std::min<std::size_t>(i%79+1,1600-i);auto part=chunks.feed(std::span(pcm).subspan(i,n));require(part.consumed==n,"chunk consumed");i+=n;}
    require(chunks.pop()->sample.fingerprint==first->sample.fingerprint,"arbitrary chunk boundaries");
    require(s.metrics().consumedFrames==4800&&s.metrics().windows==3&&s.metrics().peakWindows==2,"stream metrics");
    PcmWindowStream pause(16000);pause.feed(std::span(pcm).first(300));pause.pause();
    require(pause.feed(pcm).stop==FeedStop::paused&&pause.pendingFrames()==300,"pause acknowledges no input");
    pause.resume(true);pause.feed(std::span(pcm).subspan(300,1300));require(pause.pop()->sample.fingerprint==first->sample.fingerprint,"lossless pause/resume");
    pause.feed(std::span(pcm).first(200));pause.pause();pause.resume();
    require(pause.pendingFrames()==0&&pause.metrics().discardedPartialFrames==200,"resume declares uncertain continuity");
    pause.feed(std::span(pcm).first(1600));auto after=pause.pop();require(after->segment==1&&after->startFrame==0&&after->sequence==2,"segment reanchor");
    pause.feed(std::span(pcm).first(100));pause.discontinuity();pause.feed(std::span(pcm).first(1600));after=pause.pop();
    require(after->segment==2&&after->startFrame==0,"device discontinuity drops partial");
    std::stop_source stop;stop.request_stop();r=pause.feed(pcm,stop.get_token());require(r.stop==FeedStop::cancelled&&r.consumed==0,"cancel at slice boundary");
    r=pause.feed(pcm,{},0);require(r.stop==FeedStop::workSlice&&r.consumed==0,"zero slice");
    r=pause.feed(pcm,{},1);require(r.windows==1&&r.consumed==1600&&r.stop==FeedStop::workSlice,"bounded one-window work slice");
    pause.feed(std::span(pcm).first(50));pause.finish();pause.finish();
    require(pause.metrics().discardedPartialFrames==350&&pause.feed(pcm).stop==FeedStop::finished,"finish counts tail exactly once");
    require(pause.pop().has_value(),"finish preserves completed windows");rejects([&]{pause.resume();});
    rejects([]{PcmWindowStream bad(7999);});rejects([]{PcmWindowStream bad(8001,51);});
    rejects([]{PcmWindowStream bad(16000,49);});rejects([]{PcmWindowStream bad(16000,100,33);});
    rejects([&]{chunks.feed(std::vector<std::int16_t>(65537));});rejects([&]{chunks.feed(pcm,{},33);});
    rejects([&]{analyzePcm16({},16000);});
    std::cout<<"audio stream tests passed: framing, retry, pause, gaps, stop, slicing and bounds\n";
}
