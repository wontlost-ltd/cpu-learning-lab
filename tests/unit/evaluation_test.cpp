#include "cpuai/evaluation.hpp"
#include <iostream>
#define CHECK(c) do {if(!(c))throw std::runtime_error(#c);}while(false)
template<class F> void rejects(F f) {bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}CHECK(rejected);}
int main(int argc,char** argv) {
    CHECK(argc==2);
    cpuai::Learner l; l.teach("square",0,0); l.teach("square",1,1);
    const auto before=cpuai::freeze(l,"square");
    const auto suite=cpuai::loadExam(argv[1]); // held-out labels never enter infer()
    const auto failed=cpuai::evaluateFrozen(before,suite);
    CHECK(!failed.passedOnSuite()); CHECK(failed.correct==0);
    CHECK(cpuai::isCurrent(l,before));
    CHECK(l.skill("square")->examples==before.training); // exam is read-only
    CHECK(cpuai::freeze(l,"square").hypothesis.coefficients==before.hypothesis.coefficients);
    l.teach("square",2,4); // teacher feedback, not an exam input
    CHECK(!cpuai::isCurrent(l,before));
    const auto after=cpuai::freeze(l,"square");
    const auto passed=cpuai::evaluateFrozen(after,suite);
    CHECK(passed.passedOnSuite()); CHECK(passed.accuracy()==1);
    CHECK(cpuai::isCurrent(l,passed.candidate));
    CHECK(l.skill("square")->examples==after.training);
    // Reject empty, contaminated and duplicated exams, even if answers are correct.
    rejects([&]{cpuai::evaluateFrozen(after,{"empty",{}});});
    rejects([&]{cpuai::evaluateFrozen(after,{"overlap",{{2,4},{7,49},{8,64}}});});
    rejects([&]{cpuai::evaluateFrozen(after,{"duplicate",{{7,49},{7,49},{8,64}}});});
    rejects([&]{cpuai::evaluateFrozen(after,{"nonfinite",{{NAN,1},{7,49},{8,64}}});});
    cpuai::SearchOptions opt; opt.budget=0;
    cpuai::Learner shortSearch; shortSearch.teach("identity",0,0); shortSearch.teach("identity",1,1);
    const auto partial=cpuai::freeze(shortSearch,"identity",opt);
    const auto incompleteReport=cpuai::evaluateFrozen(partial,{"identity-v1",{{-7,-7},{5,5},{9,9}}});
    CHECK(incompleteReport.accuracy()==1 && !incompleteReport.passedOnSuite());
    std::cout<<"PASS: independent evaluation, no answer leakage and stale revision checks\n";
}
