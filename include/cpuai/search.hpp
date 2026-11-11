#pragma once
#include "polynomial.hpp"
#include <map>

namespace cpuai {
struct SearchOptions {
    std::size_t budget=50000;
    int maxDepth=2;
    std::size_t maxUnique=4096;
    std::size_t maxHypotheses=64;
    bool operator==(const SearchOptions&) const = default;
};
struct Result {
    std::vector<Expr> hypotheses;
    std::size_t evaluated=0, unique=0, consistent=0;
    bool exhausted=false, truncated=false;
    bool complete() const { return !exhausted && !truncated; }
};
inline bool simpler(const Expr& a, const Expr& b) {
    if (a.nodes != b.nodes) return a.nodes < b.nodes;
    return a.text < b.text;
}
inline Result infer(const std::vector<Example>& data, SearchOptions options={}) {
    if (data.size()>128 || options.budget>1000000 || options.maxDepth<0 || options.maxDepth>4
        || options.maxUnique<8 || options.maxUnique>50000
        || options.maxHypotheses==0 || options.maxHypotheses>256)
        throw std::runtime_error("invalid search limits");
    for (auto e:data) validateExample(e);
    Result r; if (data.empty()) return r;
    std::vector<Expr> pool;
    std::map<Polynomial, std::size_t> seen;
    auto insert=[&](Expr e) {
        seen.emplace(e.coefficients,pool.size()); pool.push_back(std::move(e));
    };
    insert({"x",{0,1}});
    for (int c=-3; c<=3; ++c) insert({std::to_string(c),{c}});
    bool stopped=false;
    for (int depth=1; depth<=options.maxDepth && !stopped; ++depth) {
        const auto n=pool.size(); // Previous-level operands only, never newly appended entries.
        for (std::size_t i=0; i<n && !stopped; ++i) {
            for (std::size_t j=0; j<n && !stopped; ++j) {
                if (std::max(pool[i].depth,pool[j].depth)!=depth-1) continue;
                for (char op:std::string("+-*")) {
                    if (op!='-' && j<i) continue;
                    if (r.evaluated==options.budget) { r.exhausted=true; stopped=true; break; }
                    ++r.evaluated;
                    auto p=combine(pool[i].coefficients,pool[j].coefficients,op);
                    if (!p || seen.contains(*p)) continue;
                    if (pool.size()==options.maxUnique) { r.exhausted=true; stopped=true; break; }
                    Expr e{"("+pool[i].text+op+pool[j].text+")",*p,depth,1+pool[i].nodes+pool[j].nodes};
                    insert(std::move(e));
                }
            }
        }
    }
    r.unique=pool.size();
    for (const auto& p:pool) {
        bool fits=true;
        for (auto e:data) if (!close(evaluate(p,e.x),e.y)) { fits=false; break; }
        if (fits) r.hypotheses.push_back(p);
    }
    r.consistent=r.hypotheses.size();
    std::sort(r.hypotheses.begin(),r.hypotheses.end(),simpler);
    if (r.hypotheses.size()>options.maxHypotheses) {
        r.truncated=true; r.hypotheses.resize(options.maxHypotheses);
    }
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
