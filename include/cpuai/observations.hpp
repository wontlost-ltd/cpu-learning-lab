#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace cpuai {
using AudioFeatures=std::array<double,10>;
struct Observation {
    std::string source;
    std::uint64_t sequence=0;
    std::int64_t timestampMs=0, offsetMs=0, uncertaintyMs=0;
    AudioFeatures features{};
    std::int64_t alignedTime() const {return timestampMs+offsetMs;}
};
inline void validateFeatures(const AudioFeatures& f) {
    for(double v:f)if(!std::isfinite(v)||v<0||v>1)throw std::runtime_error("invalid audio features");
}
inline void validateObservation(const Observation& o) {
    if(o.source.empty()||o.source.size()>64||o.sequence==0||o.timestampMs<0||o.timestampMs>1000000000000LL||
       o.offsetMs < -1000000000LL||o.offsetMs>1000000000LL||o.uncertaintyMs<0||o.uncertaintyMs>60000)
        throw std::runtime_error("invalid observation metadata");
    validateFeatures(o.features);
}
enum class StreamStatus {accepted,duplicateOrReordered,late,full,sourceLimit};
inline const char* streamStatusName(StreamStatus s) {
    switch(s){case StreamStatus::accepted:return "accepted";case StreamStatus::duplicateOrReordered:return "duplicate_or_reordered_sequence";
    case StreamStatus::late:return "late";case StreamStatus::full:return "full_retry_after_pop";case StreamStatus::sourceLimit:return "source_limit";}
    return "invalid";
}
struct StreamMetrics {std::size_t accepted=0,rejected=0,peak=0;};
// Time may arrive out of order within latenessMs; transport sequence must increase.
// Reject-new backpressure never silently evicts an accepted observation.
class ObservationStream {
    std::size_t capacity_;std::int64_t latenessMs_;
    std::deque<Observation> events_;
    std::map<std::string,std::pair<std::uint64_t,std::int64_t>> sources_;
    StreamMetrics metrics_;
public:
    explicit ObservationStream(std::size_t capacity=64,std::int64_t latenessMs=1000):capacity_(capacity),latenessMs_(latenessMs) {
        if(capacity==0||capacity>4096||latenessMs<0||latenessMs>60000)throw std::runtime_error("invalid stream limits");
    }
    StreamStatus push(const Observation& o) {
        validateObservation(o);auto it=sources_.find(o.source);
        auto reject=[&](StreamStatus s){++metrics_.rejected;return s;};
        if(it!=sources_.end()) {
            if(o.sequence<=it->second.first)return reject(StreamStatus::duplicateOrReordered);
            if(o.alignedTime()<it->second.second-latenessMs_)return reject(StreamStatus::late);
        } else if(sources_.size()>=16)return reject(StreamStatus::sourceLimit);
        if(events_.size()>=capacity_)return reject(StreamStatus::full);
        auto position=std::upper_bound(events_.begin(),events_.end(),o.alignedTime(),[](auto t,const auto& e){return t<e.alignedTime();});
        events_.insert(position,o);
        if(it==sources_.end())sources_.emplace(o.source,std::pair{o.sequence,o.alignedTime()});
        else {it->second.first=o.sequence;it->second.second=std::max(it->second.second,o.alignedTime());}
        ++metrics_.accepted;metrics_.peak=std::max(metrics_.peak,events_.size());return StreamStatus::accepted;
    }
    bool pop(){if(events_.empty())return false;events_.pop_front();return true;}
    const auto& events()const{return events_;}
    const auto& metrics()const{return metrics_;}
    struct Pair {std::uint64_t first,second;std::int64_t worstSeparationMs;};
    std::vector<Pair> align(const std::string& a,const std::string& b,std::int64_t toleranceMs)const {
        if(a==b||toleranceMs<0||toleranceMs>60000)throw std::runtime_error("alignment requires distinct sources and tolerance <=60000 ms");
        std::vector<Pair> result;
        // All qualifying pairs, not a causal or exclusive matching assertion.
        for(const auto& x:events_)if(x.source==a)for(const auto& y:events_)if(y.source==b){
            const auto worst=std::abs(x.alignedTime()-y.alignedTime())+x.uncertaintyMs+y.uncertaintyMs;
            if(worst<=toleranceMs){
                if(result.size()>=4096)throw std::runtime_error("alignment pair limit 4096; drain buffer");
                result.push_back({x.sequence,y.sequence,worst});
            }
        }
        return result;
    }
};
}
