#pragma once
#include "learner.hpp"

namespace cpuai {
struct FrozenCandidate {
    std::string skillName;
    std::uint64_t revision;
    std::vector<Example> training;
    Expr hypothesis;
    SearchOptions searchOptions;
    std::size_t consistentCandidates;
    bool searchComplete;
};
// Freeze before exam data is loaded; exam scores never select or filter candidates.
inline FrozenCandidate freeze(const Learner& l,const std::string& name,SearchOptions options={},SearchControl control={}) {
    const auto* s=l.skill(name);
    if (!s || s->examples.empty()) throw std::runtime_error("skill has no teaching examples");
    const auto r=l.learn(name,options,control);
    if (r.hypotheses.empty()) throw std::runtime_error("no candidate to evaluate");
    return {name,s->revision,s->examples,r.hypotheses.front(),options,r.consistent,r.complete()};
}
inline bool isCurrent(const Learner& l,const FrozenCandidate& c) {
    const auto* s=l.skill(c.skillName);
    return s && s->revision==c.revision && s->examples==c.training;
}
struct ExamSuite { std::string id; std::vector<Example> cases; };
struct CaseResult { double x, expected, actual; bool correct; };
struct EvaluationReport {
    std::string suiteId;
    FrozenCandidate candidate;
    std::vector<CaseResult> cases;
    std::size_t correct=0;
    double accuracy() const { return cases.empty()?0:static_cast<double>(correct)/cases.size(); }
    // Only this frozen candidate on this suite, never a general mastery claim.
    bool passedOnSuite() const { return !cases.empty() && candidate.searchComplete && correct==cases.size(); }
};
inline EvaluationReport evaluateFrozen(const FrozenCandidate& c,const ExamSuite& suite) {
    if (suite.id.empty() || suite.id.size()>128 || suite.cases.size()<3 || suite.cases.size()>128)
        throw std::runtime_error("exam needs an id and 3..128 held-out cases");
    for (std::size_t i=0;i<suite.cases.size();++i) {
        const auto e=suite.cases[i]; validateExample(e);
        for (auto t:c.training) if (close(e.x,t.x)) throw std::runtime_error("exam overlaps teaching input");
        for (std::size_t j=0;j<i;++j) if (close(e.x,suite.cases[j].x))
            throw std::runtime_error("duplicate exam input");
    }
    EvaluationReport report{suite.id,c,{},0};
    for (auto e:suite.cases) {
        const double actual=evaluate(c.hypothesis,e.x);
        const bool correct=close(actual,e.y);
        report.cases.push_back({e.x,e.y,actual,correct});
        if (correct) ++report.correct;
    }
    return report;
}
inline ExamSuite loadExam(const std::string& path) {
    std::ifstream f(path); if (!f) throw std::runtime_error("cannot load exam");
    std::string header,id; std::getline(f,header);
    if (header!="CPUAI_EXAM1" || !(f>>std::quoted(id))) throw std::runtime_error("invalid exam version/id");
    ExamSuite s{id,{}};
    while (f>>std::ws && !f.eof()) {
        double x,y;
        if (!(f>>x>>y) || s.cases.size()>=128) throw std::runtime_error("invalid exam case/limit");
        validateExample({x,y}); s.cases.push_back({x,y});
    }
    return s;
}
}
