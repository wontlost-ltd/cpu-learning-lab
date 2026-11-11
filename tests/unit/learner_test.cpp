#include "cpuai/learner.hpp"
#include <filesystem>
#include <iostream>
#include <limits>
#include <set>
#define CHECK(c) do {if(!(c))throw std::runtime_error(#c);}while(false)
template<class F> void rejects(F f) {bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}CHECK(rejected);}
bool contains(const cpuai::Result& r,const cpuai::Polynomial& p) {
    return std::any_of(r.hypotheses.begin(),r.hypotheses.end(),[&](auto& h){return h.coefficients==p;});
}
int main() {
    cpuai::Learner l;
    CHECK(l.learn("unknown").hypotheses.empty());
    CHECK(cpuai::infer({{0,0},{2,1},{4,2}}).hypotheses.empty());
    l.teach("square",0,0); l.teach("square",1,1);
    const auto ambiguous=l.learn("square");
    CHECK(ambiguous.complete());
    CHECK(contains(ambiguous,{0,1})); CHECK(contains(ambiguous,{0,0,1}));
    CHECK(cpuai::predict(ambiguous,2).status==cpuai::PredictionStatus::disagreement);
    CHECK(cpuai::predict(ambiguous,0).status==cpuai::PredictionStatus::consensus);
    const auto q=cpuai::suggestQuestion(ambiguous,l.skill("square")->examples);
    CHECK(q && q->x!=0 && q->x!=1 && q->distinctPredictions>1);
    CHECK(!cpuai::suggestQuestion(ambiguous,l.skill("square")->examples,{0,1}));
    l.teach("square",q->x,q->x*q->x);
    const auto refined=l.learn("square");
    CHECK(contains(refined,{0,0,1})); CHECK(!contains(refined,{0,1}));
    CHECK(refined.consistent<ambiguous.consistent);
    // Canonically identical polynomials have one representative, not one per syntax tree.
    std::set<cpuai::Polynomial> identities;
    for (auto& h:refined.hypotheses) CHECK(identities.insert(h.coefficients).second);
    for (double x:{-2.,0.,3.}) l.teach("affine",x,2*x+1);
    const auto affine=l.learn("affine"); CHECK(affine.complete());
    for (double x:{-7.,1.,9.}) CHECK(cpuai::close(cpuai::evaluate(affine.hypotheses.front(),x),2*x+1));
    auto p=cpuai::predict(affine,9); CHECK(p.value && cpuai::close(*p.value,19));
    const auto oldSquare=l.learn("square").hypotheses.front().coefficients;
    l.teach("affine",4,9); CHECK(l.learn("square").hypotheses.front().coefficients==oldSquare);
    const auto rev=l.skill("affine")->revision;
    l.teach("affine",4,9); CHECK(l.skill("affine")->revision==rev);
    l.teach("affine",4,10); CHECK(l.skill("affine")->revision==rev+1);
    l.teach("affine",4,9);
    cpuai::SearchOptions limited; limited.budget=0;
    const auto incomplete=cpuai::infer({{0,0},{1,1}},limited);
    CHECK(incomplete.exhausted && incomplete.evaluated==0);
    CHECK(cpuai::predict(incomplete,0).status==cpuai::PredictionStatus::incomplete_search);
    limited={}; limited.maxUnique=8;
    CHECK(cpuai::infer({{0,0},{1,1}},limited).exhausted);
    limited={}; limited.maxHypotheses=1;
    const auto capped=cpuai::infer({{0,0},{1,1}},limited);
    CHECK(capped.truncated && capped.hypotheses.size()==1 && capped.consistent>1);
    CHECK(cpuai::predict(capped,2).status==cpuai::PredictionStatus::incomplete_search);
    rejects([&]{l.teach("bad",NAN,0);});
    rejects([&]{cpuai::predict(affine,INFINITY);});
    rejects([&]{cpuai::infer({{NAN,0}});});
    rejects([&]{cpuai::combine({std::numeric_limits<std::int64_t>::max()},{2},'*');});
    CHECK(!cpuai::combine({1000000},{1000000},'*'));
    const auto path=(std::filesystem::temp_directory_path()/"cpuai-p1-test.state").string();
    l.save(path); const auto restored=cpuai::Learner::load(path); std::filesystem::remove(path);
    CHECK(restored.skill("square")->examples==l.skill("square")->examples);
    CHECK(restored.learn("square").hypotheses.front().coefficients==oldSquare);
    CHECK(cpuai::close(cpuai::evaluate(restored.learn("affine").hypotheses.front(),11),23));
    std::cout<<"PASS: ambiguity, algebraic identity, active teaching, revision, persistence and budgets\n";
}
