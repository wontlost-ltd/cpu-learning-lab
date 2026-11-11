#include "cpuai/evaluation.hpp"
#include <iostream>
#define CHECK(c) do {if(!(c))throw std::runtime_error(#c);}while(false)
std::vector<cpuai::Polynomial> identities(const cpuai::Result& r) {
    std::vector<cpuai::Polynomial> ids;
    for(const auto& h:r.hypotheses)ids.push_back(h.coefficients);
    return ids;
}
int main() {
    const std::vector<cpuai::Example> examples={{0,0},{1,1}};
    const auto baseline=cpuai::infer(examples);
    cpuai::Learner l;l.teach("square",0,0);l.teach("square",1,1);
    const auto cold=l.learn("square"),warm=l.learn("square");
    CHECK(identities(cold)==identities(warm));
    CHECK(cold.metrics.newCombinations>0 && !cold.metrics.cacheHit);
    CHECK(warm.metrics.cacheHit && warm.metrics.newCombinations==0 && warm.metrics.exampleChecks==0);
    l.teach("square",2,4);
    const auto delta=l.learn("square");
    CHECK(delta.metrics.incremental && delta.metrics.newCombinations==0);
    CHECK(delta.metrics.exampleChecks==cold.consistent);
    CHECK(identities(delta)==identities(cpuai::infer(l.skill("square")->examples)));
    // Corrections must recover candidates previously eliminated by a wrong lesson.
    l.teach("square",2,2);
    const auto corrected=l.learn("square");
    CHECK(!corrected.metrics.incremental && !corrected.metrics.cacheHit);
    CHECK(corrected.metrics.exampleChecks>delta.metrics.exampleChecks);
    CHECK(corrected.hypotheses.front().coefficients==cpuai::Polynomial({0,1}));
    CHECK(identities(corrected)==identities(cpuai::infer(l.skill("square")->examples)));
    // Cache invalidation is by data/configuration, never by a display-limited set.
    cpuai::SearchOptions cap;cap.maxHypotheses=1;
    cpuai::Learner capped;capped.teach("s",0,0);capped.teach("s",1,1);
    CHECK(capped.learn("s",cap).truncated);
    capped.teach("s",2,4);
    const auto narrowed=capped.learn("s",cap);
    CHECK(narrowed.metrics.incremental && narrowed.complete());
    CHECK(narrowed.hypotheses.front().coefficients==cpuai::Polynomial({0,0,1}));
    cpuai::SearchOptions config;config.maxDepth=1;
    CHECK(capped.learn("s",config).metrics.newCombinations>0);
    // Work slices resume exact enumeration instead of restarting or skipping a pair.
    cpuai::SearchSession session;
    cpuai::SearchControl slice;slice.maxCombinations=17;
    int turns=0;
    while(session.run(slice)!=cpuai::StopReason::complete){CHECK(++turns<1000);CHECK(session.validDag());}
    CHECK(session.validDag() && session.evaluated()==baseline.evaluated);
    for(std::size_t id=0;id<session.size();++id)for(double x:{-3.,0.5,7.})
        CHECK(cpuai::close(session.value(id,x),session.valueViaDag(id,x)));
    auto resumed=cpuai::materialize(session,session.fit(examples,{}),cpuai::StopReason::complete);
    CHECK(identities(resumed)==identities(baseline));
    // Cooperative cancellation and deadline work before and during search; neither poisons a session.
    std::stop_source source;source.request_stop();
    cpuai::SearchControl cancelled;cancelled.cancellation=source.get_token();
    CHECK(session.run(cancelled)==cpuai::StopReason::cancelled);
    CHECK(!l.learn("square",{},cancelled).complete());
    CHECK(l.learn("square").complete());
    cpuai::SearchSession interrupted;std::stop_source mid;int ticks=0;
    cpuai::SearchControl during;during.cancellation=mid.get_token();
    const auto now=cpuai::SteadyClock::now();during.deadline=now+std::chrono::hours(1);
    during.now=[&]{if(++ticks==12)mid.request_stop();return now;};
    CHECK(interrupted.run(during)==cpuai::StopReason::cancelled);
    CHECK(interrupted.evaluated()>0 && interrupted.evaluated()<baseline.evaluated);
    CHECK(interrupted.run()==cpuai::StopReason::complete);
    CHECK(interrupted.evaluated()==baseline.evaluated && interrupted.validDag());
    cpuai::SearchControl expired;expired.deadline=cpuai::SteadyClock::time_point{};
    CHECK(!l.learn("square",{},expired).complete());
    CHECK(l.learn("square").metrics.cacheHit);
    int fitTicks=0;cpuai::SearchControl fitDeadline;fitDeadline.deadline=now+std::chrono::seconds(10);
    fitDeadline.now=[&]{return ++fitTicks>4?now+std::chrono::seconds(20):now;};
    CHECK(session.fit(examples,fitDeadline).interrupted==cpuai::StopReason::deadline);
    // PMR accounts both DAG buffers and canonical index; failed insertions remain consistent.
    cpuai::SearchOptions small;small.maxStorageBytes=30000;
    cpuai::SearchSession bounded(small);
    CHECK(bounded.run()==cpuai::StopReason::memory_limit);
    CHECK(bounded.peakStorageBytes()<=small.maxStorageBytes && bounded.validDag());
    CHECK(bounded.run()==cpuai::StopReason::memory_limit);
    cpuai::SearchOptions empty;empty.maxStorageBytes=0;
    const auto noMemory=cpuai::infer(examples,empty);
    CHECK(noMemory.stopReason==cpuai::StopReason::memory_limit && !noMemory.complete());
    // A cancelled or memory-limited inference cannot be reported as an accepted skill.
    cpuai::SearchOptions low;low.budget=0;
    auto frozen=cpuai::freeze(l,"square",low);
    auto report=cpuai::evaluateFrozen(frozen,{"identity",{{-3,-3},{3,3},{4,4}}});
    CHECK(!report.passedOnSuite());
    std::cout<<"PASS: DAG invariants, warm/incremental caches, corrections, slices, cancellation, deadline and PMR budget\n";
}
