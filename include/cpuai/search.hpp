#pragma once
#include "session.hpp"

namespace cpuai {
struct Result {
    std::vector<Expr> hypotheses;
    std::size_t evaluated=0, unique=0, consistent=0;
    bool exhausted=false, truncated=false;
    StopReason stopReason=StopReason::complete;
    QueryMetrics metrics;
    bool complete() const { return !exhausted && !truncated; }
};
inline Result materialize(SearchSession& session,FitResult fit,StopReason reason) {
    Result r;
    r.stopReason=fit.interrupted.value_or(reason);
    r.exhausted=r.stopReason!=StopReason::complete;
    r.evaluated=session.evaluated();r.unique=session.size();r.consistent=fit.ids.size();
    r.metrics.exampleChecks=fit.checks;
    r.metrics.storageBytes=session.storageBytes();r.metrics.peakStorageBytes=session.peakStorageBytes();
    r.metrics.dagEdges=session.edges();
    session.rank(fit.ids);
    r.truncated=fit.ids.size()>session.options().maxHypotheses;
    if(fit.ids.size()>session.options().maxHypotheses)fit.ids.resize(session.options().maxHypotheses);
    for(auto id:fit.ids)r.hypotheses.push_back(session.expression(id));
    return r;
}
inline Result infer(const std::vector<Example>& data,SearchOptions options={},SearchControl control={}) {
    validateOptions(options);
    if(data.size()>128)throw std::runtime_error("example limit");
    for(auto e:data)validateExample(e);
    if(data.empty())return {};
    const auto start=SteadyClock::now();SearchSession session(options);
    const auto reason=session.run(control);
    auto r=materialize(session,session.fit(data,control),reason);
    r.metrics.newCombinations=session.evaluated();
    r.metrics.elapsedMicros=std::chrono::duration_cast<std::chrono::microseconds>(SteadyClock::now()-start).count();
    return r;
}
enum class PredictionStatus { no_hypothesis, incomplete_search, disagreement, consensus };
struct Prediction { PredictionStatus status; std::optional<double> value; };
inline Prediction predict(const Result& r, double x) {
    if (!std::isfinite(x) || std::abs(x)>1e6) throw std::runtime_error("invalid query input");
    if (!r.complete()) return {PredictionStatus::incomplete_search,std::nullopt};
    if (r.hypotheses.empty()) return {PredictionStatus::no_hypothesis,std::nullopt};
    const double y=evaluate(r.hypotheses.front(),x);
    for (const auto& h:r.hypotheses)
        if (!close(y,evaluate(h,x))) return {PredictionStatus::disagreement,std::nullopt};
    return {PredictionStatus::consensus,y};
}
struct TeachingQuestion { double x; std::size_t distinctPredictions, worstCaseRemaining; };
inline std::optional<TeachingQuestion> suggestQuestion(const Result& r,
    const std::vector<Example>& training, const std::vector<double>& probes={-3,-2,-1,0,1,2,3,4}) {
    if (probes.size()>256) throw std::runtime_error("too many query probes");
    if (r.hypotheses.size()<2) return std::nullopt;
    std::optional<TeachingQuestion> best;
    for (double x:probes) {
        if (!std::isfinite(x) || std::abs(x)>1e6) throw std::runtime_error("invalid probe");
        if (std::any_of(training.begin(),training.end(),[&](Example e){return close(x,e.x);})) continue;
        std::vector<std::pair<double,std::size_t>> buckets;
        for (const auto& h:r.hypotheses) {
            const double y=evaluate(h,x);
            auto it=std::find_if(buckets.begin(),buckets.end(),[&](auto b){return close(b.first,y);});
            if (it==buckets.end()) buckets.emplace_back(y,1); else ++it->second;
        }
        if (buckets.size()<2) continue;
        std::size_t worst=0; for (auto b:buckets) worst=std::max(worst,b.second);
        if (!best || worst<best->worstCaseRemaining
            || (worst==best->worstCaseRemaining && buckets.size()>best->distinctPredictions))
            best=TeachingQuestion{x,buckets.size(),worst};
    }
    return best;
}
}
