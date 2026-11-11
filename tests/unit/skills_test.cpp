#include "cpuai/skills.hpp"
#include <iostream>
#define CHECK(c) do {if(!(c))throw std::runtime_error(#c);}while(false)
template<class F> void rejects(F f){bool bad=false;try{f();}catch(const std::exception&){bad=true;}CHECK(bad);}
void teach(cpuai::Learner& l,const std::string& name,const std::vector<cpuai::Example>& examples){for(auto e:examples)l.teach(name,e.x,e.y);}
int main() {
    using namespace cpuai;
    Learner l;SkillLibrary book;
    teach(l,"shift",{{0,1},{1,2},{2,3}});
    const auto shift=book.prepareLearned(l,"shift");
    const ExamSuite shiftExam{"shift-v1",{{-7,-6},{5,6},{9,10}}};
    const auto shiftId=book.accept(l,shift,shiftExam).ref.id;
    teach(l,"square",{{0,0},{1,1},{2,4}});
    const auto square=book.prepareLearned(l,"square");
    const ExamSuite squareExam{"square-v1",{{-7,49},{5,25},{9,81}}};
    const auto squareId=book.accept(l,square,squareExam).ref.id;
    CHECK(book.run("shift",10)==11 && book.run("square",10)==100);
    rejects([&]{book.run("unknown",1);});rejects([&]{book.run("square",1,0);});
    rejects([&]{book.run("square",INFINITY);});
    teach(l,"ambiguous",{{0,0},{1,1}});
    rejects([&]{book.prepareLearned(l,"ambiguous");});
    rejects([&]{book.prepareComposition(l,"ambiguous");});
    // Reuse accepted skills to infer a three-stage target from examples, not from its exam.
    teach(l,"fourth",{{0,1},{1,16},{2,81}});
    CHECK(l.learn("fourth").hypotheses.empty()); // beyond the default two-level primitive grammar
    const auto plan=book.plan(l,"fourth");CHECK(plan.complete && plan.candidates.size()==1);
    CHECK(plan.candidates.front().steps==std::vector<SkillRef>({{"shift",shiftId},{"square",squareId},{"square",squareId}}));
    CompositionOptions small;small.maxPrograms=1;CHECK(!book.plan(l,"fourth",small).complete);
    rejects([&]{book.prepareComposition(l,"fourth",small);});
    auto fourth=book.prepareComposition(l,"fourth");
    const auto before=l.skill("fourth")->examples;
    const ExamSuite fourthExam{"fourth-v1",{{-3,16},{3,256},{4,625}}};
    rejects([&]{book.accept(l,fourth,{"wrong",{{-3,1},{3,1},{4,1}}});});
    rejects([&]{book.accept(l,fourth,{"overlap",{{0,1},{3,256},{4,625}}});});
    CHECK(book.history("fourth").empty() && l.skill("fourth")->examples==before);
    book.accept(l,fourth,fourthExam);CHECK(book.run("fourth",4)==625);
    rejects([&]{book.run("fourth",1000000);}); // intermediate input leaves the shared domain
    // A separate target shares the shift/square prefix; mining expands pinned dependencies.
    teach(l,"shifted_square",{{0,1},{1,4},{2,9}});
    auto shifted=book.prepareComposition(l,"shifted_square");
    book.accept(l,shifted,{"shifted-square-v1",{{-3,4},{3,16},{4,25}}});
    const auto patterns=book.patterns();CHECK(!patterns.empty());
    CHECK(patterns.front().steps==std::vector<SkillRef>({{"shift",shiftId},{"square",squareId}}));
    CHECK(patterns.front().support>=2);
    teach(l,"chunk",{{0,1},{1,4},{2,9}});
    auto chunk=book.prepareAbstraction(l,"chunk",0);
    const auto chunkId=book.accept(l,chunk,{"chunk-v1",{{-4,9},{4,25},{6,49}}}).ref.id;
    CHECK(book.run("chunk",5)==36 && book.active("chunk").ref.id==chunkId);
    // Working lessons cannot silently mutate published behavior.
    l.teach("shift",0,2);l.teach("shift",1,3);l.teach("shift",2,4);
    CHECK(book.run("shift",10)==11 && book.run("fourth",4)==625);
    auto changed=book.prepareLearned(l,"shift");
    const auto shift2=book.accept(l,changed,{"shift-v2",{{-7,-5},{5,7},{9,11}}}).ref.id;
    CHECK(shift2!=shiftId && book.run("shift",10)==12);
    CHECK(book.run("fourth",4)==625 && book.run("chunk",5)==36); // pinned to v1
    auto replay=book.replay();CHECK(replay.complete && replay.failed.empty() && replay.correct==replay.cases);
    CHECK(!book.replay({},0).complete);
    book.rollback("shift",shiftId);CHECK(book.run("shift",10)==11);
    CHECK(book.history("shift").size()==2);
    rejects([&]{book.rollback("square",shiftId);});
    // Both draft revisions and library activations invalidate frozen proposals.
    auto pending=book.prepareLearned(l,"shift");book.rollback("shift",shift2);
    rejects([&]{book.accept(l,pending,{"shift-v2",{{-7,-5},{5,7},{9,11}}});});
    pending=book.prepareLearned(l,"shift");l.teach("shift",3,5);
    rejects([&]{book.accept(l,pending,{"shift-v2",{{-7,-5},{5,7},{9,11}}});});
    // Restoring a catalog gives a new instance identity; old pending proposals cannot cross it.
    const auto path=(std::filesystem::temp_directory_path()/"cpuai-p3-skills.library").string();
    book.save(path);auto restored=SkillLibrary::load(path);
    CHECK(restored.run("fourth",4)==625 && restored.run("shift",10)==12);
    CHECK(restored.history("shift").size()==2 && restored.replay().failed.empty());
    {std::ofstream trailing(path,std::ios::app);trailing<<"unexpected content\n";}
    rejects([&]{SkillLibrary::load(path);});
    auto oldProposal=book.prepareLearned(l,"shift");
    rejects([&]{restored.accept(l,oldProposal,{"shift-v2",{{-7,-5},{5,7},{9,11}}});});
    {std::ofstream corrupt(path);corrupt<<"CPUAI_LIBRARY1\n999999 1\n";}
    rejects([&]{SkillLibrary::load(path);});std::filesystem::remove(path);
    CHECK(restored.run("fourth",4)==625);
    auto transfer=restored.prepareLearned(l,"shift");SkillLibrary moved(std::move(restored));
    CHECK(moved.run("fourth",4)==625 && restored.history("shift").empty());
    rejects([&]{moved.accept(l,transfer,{"shift-v2",{{-7,-5},{5,7},{9,11}}});});
    std::stop_source cancelled;cancelled.request_stop();SearchControl control;control.cancellation=cancelled.get_token();
    CHECK(!book.plan(l,"fourth",{},control).complete);
    CHECK(!book.replay(control).complete);
    CHECK(composePolynomial({0,0,1},{1,1})==Polynomial({1,2,1}));
    std::cout<<"PASS: held-out skill composition, abstraction mining, version pinning, rollback, replay and persistence\n";
}
