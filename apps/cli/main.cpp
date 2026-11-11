#include "cpuai/skills.hpp"
#include <iostream>
#include <sstream>

namespace {
void endOfCommand(std::istringstream& in) {
    std::string extra; if (in>>extra) throw std::runtime_error("unexpected trailing argument");
}
std::string argument(std::istringstream& in) {
    std::string s; if (!(in>>std::quoted(s)) || s.empty()) throw std::runtime_error("missing argument");
    return s;
}
}
int main() {
    cpuai::Learner learner;
    cpuai::SkillLibrary library;
    cpuai::SearchOptions limits;
    std::size_t sliceLimit=std::numeric_limits<std::size_t>::max();
    unsigned long deadlineMs=1000;
    bool cancelNext=false;
    cpuai::QueryMetrics lastMetrics;
    cpuai::StopReason lastReason=cpuai::StopReason::complete;
    auto control=[&]{
        cpuai::SearchControl c;c.maxCombinations=sliceLimit;
        c.deadline=cpuai::SteadyClock::now()+std::chrono::milliseconds(deadlineMs);
        if(cancelNext){std::stop_source source;source.request_stop();c.cancellation=source.get_token();cancelNext=false;}
        return c;
    };
    std::cout<<"CPU Learning Lab P3: teach NAME X Y | ask NAME X | show NAME | next NAME | exam NAME FILE | save PATH | load PATH | limits OPS MS BYTES | slice OPS | cancel-next | stats | publish/compose NAME EXAM | plan NAME | run NAME X | versions NAME | rollback NAME ID | mine | abstract NAME INDEX EXAM | replay | save-library/load-library PATH | quit\n";
    std::string line;
    while (std::getline(std::cin,line)) try {
        std::istringstream in(line); std::string cmd; in>>cmd;
        if (cmd.empty()) continue;
        if (cmd=="quit") { endOfCommand(in); break; }
        if (cmd=="teach") {
            const auto name=argument(in); double x,y;
            if (!(in>>x>>y)) throw std::runtime_error("teach NAME X Y");
            endOfCommand(in); learner.teach(name,x,y); std::cout<<"lesson recorded\n";
        } else if (cmd=="ask" || cmd=="show" || cmd=="next") {
            const auto name=argument(in); double x=0;
            if (cmd=="ask" && !(in>>x)) throw std::runtime_error("ask NAME X");
            endOfCommand(in); const auto r=learner.learn(name,limits,control());
            lastMetrics=r.metrics;lastReason=r.stopReason;
            if (cmd=="ask") {
                const auto p=cpuai::predict(r,x);
                if (p.status==cpuai::PredictionStatus::consensus)
                    std::cout<<*p.value<<" [candidate consensus within bounded grammar; not certified]\n";
                else if (p.status==cpuai::PredictionStatus::disagreement)
                    std::cout<<"uncertain: candidates disagree; use next NAME\n";
                else if (p.status==cpuai::PredictionStatus::incomplete_search)
                    std::cout<<"uncertain: incomplete search or candidate limit\n";
                else std::cout<<"no hypothesis; teach more or expand grammar\n";
            } else if (cmd=="show") {
                std::cout<<"hypotheses="<<r.hypotheses.size()<<" consistent="<<r.consistent
                    <<" evaluated="<<r.evaluated<<" unique="<<r.unique<<" complete="<<r.complete()<<" stop="<<cpuai::reasonName(r.stopReason)<<'\n';
                for (std::size_t i=0;i<std::min<std::size_t>(8,r.hypotheses.size());++i)
                    std::cout<<"  "<<r.hypotheses[i].text<<'\n';
                std::cout<<"consistent with teaching examples; no mastery claim\n";
            } else {
                const auto* s=learner.skill(name);
                const auto q=s?cpuai::suggestQuestion(r,s->examples):std::nullopt;
                if (q) std::cout<<"please teach "<<name<<" at x="<<q->x
                    <<"; distinct_predictions="<<q->distinctPredictions
                    <<"; retained_candidates_only="<<!r.complete()<<'\n';
                else std::cout<<"no discriminating question within probe grid; not proof of mastery\n";
            }
        } else if (cmd=="exam") {
            const auto name=argument(in),path=argument(in); endOfCommand(in);
            const auto frozen=cpuai::freeze(learner,name,limits,control()); // before opening exam file
            const auto report=cpuai::evaluateFrozen(frozen,cpuai::loadExam(path));
            std::cout<<"suite="<<report.suiteId<<" candidate="<<frozen.hypothesis.text
                <<" revision="<<frozen.revision<<" accuracy="<<report.accuracy()
                <<" passed_on_suite="<<report.passedOnSuite()
                <<" current="<<cpuai::isCurrent(learner,frozen)<<"; evaluation only, no learning\n";
        } else if(cmd=="publish" || cmd=="compose" || cmd=="abstract") {
            const auto name=argument(in);unsigned long index=0;
            if(cmd=="abstract" && !(in>>index))throw std::runtime_error("abstract NAME INDEX EXAM");
            const auto path=argument(in);endOfCommand(in);auto c=control();
            auto candidate=cmd=="publish"?library.prepareLearned(learner,name,limits,c)
                :cmd=="compose"?library.prepareComposition(learner,name,{},c)
                :library.prepareAbstraction(learner,name,index,c);
            const auto& version=library.accept(learner,candidate,cpuai::loadExam(path),c);
            std::cout<<"accepted "<<name<<"@"<<version.ref.id<<" steps="<<version.program.steps.size()
                <<"; scoped to suite "<<version.exam.id<<'\n';
        } else if(cmd=="plan") {
            const auto name=argument(in);endOfCommand(in);
            const auto p=library.plan(learner,name,{},control());
            std::cout<<"programs_tested="<<p.tested<<" hypotheses="<<p.candidates.size()<<" complete="<<p.complete<<'\n';
            for(const auto& c:p.candidates)std::cout<<"  "<<c.expression.text<<'\n';
        } else if(cmd=="run") {
            const auto name=argument(in);double x;if(!(in>>x))throw std::runtime_error("run NAME X");endOfCommand(in);
            const auto y=library.run(name,x);std::cout<<y<<" [accepted "<<name<<"@"<<library.active(name).ref.id<<"]\n";
        } else if(cmd=="versions") {
            const auto name=argument(in);endOfCommand(in);const auto versions=library.history(name);
            for(const auto& ref:versions)std::cout<<ref.name<<"@"<<ref.id<<(library.active(name).ref.id==ref.id?" active":" historical")<<'\n';
            if(versions.empty())std::cout<<"no accepted versions\n";
        } else if(cmd=="rollback") {
            const auto name=argument(in);cpuai::VersionId id;if(!(in>>id))throw std::runtime_error("rollback NAME ID");endOfCommand(in);
            library.rollback(name,id);std::cout<<"activated "<<name<<"@"<<id<<'\n';
        } else if(cmd=="mine") {
            endOfCommand(in);const auto patterns=library.patterns();
            for(std::size_t i=0;i<patterns.size();++i){std::cout<<"pattern="<<i<<" support="<<patterns[i].support;
                for(const auto& ref:patterns[i].steps){std::cout<<" "<<ref.name<<"@"<<ref.id;}
                std::cout<<'\n';}
            if(patterns.empty())std::cout<<"no repeated accepted subroutine\n";
        } else if(cmd=="replay") {
            endOfCommand(in);const auto r=library.replay(control());
            std::cout<<"replay cases="<<r.cases<<" correct="<<r.correct<<" calls="<<r.calls<<" complete="<<r.complete<<" failed="<<r.failed.size()<<'\n';
        } else if(cmd=="save-library" || cmd=="load-library") {
            const auto path=argument(in);endOfCommand(in);
            if(cmd=="save-library")library.save(path);else library=cpuai::SkillLibrary::load(path);
            std::cout<<(cmd=="save-library"?"skill library saved":"skill library loaded")<<'\n';
        } else if (cmd=="limits") {
            unsigned long ops,ms,bytes;
            if(!(in>>ops>>ms>>bytes) || ms>60000)throw std::runtime_error("limits OPS MS BYTES; MS <=60000");
            endOfCommand(in);auto next=limits;next.budget=ops;next.maxStorageBytes=bytes;
            cpuai::validateOptions(next);limits=next;deadlineMs=ms;std::cout<<"limits updated\n";
        } else if (cmd=="slice") {
            unsigned long ops;if(!(in>>ops) || ops>1000000)throw std::runtime_error("slice OPS; OPS <=1000000");
            endOfCommand(in);sliceLimit=ops;std::cout<<"slice updated\n";
        } else if (cmd=="cancel-next") {
            endOfCommand(in);cancelNext=true;std::cout<<"next search cancelled on request\n";
        } else if (cmd=="stats") {
            endOfCommand(in);std::cout<<"new_combinations="<<lastMetrics.newCombinations
                <<" example_checks="<<lastMetrics.exampleChecks<<" cache_hit="<<lastMetrics.cacheHit
                <<" incremental="<<lastMetrics.incremental<<" storage_bytes="<<lastMetrics.storageBytes
                <<" peak_storage_bytes="<<lastMetrics.peakStorageBytes<<" dag_edges="<<lastMetrics.dagEdges
                <<" elapsed_us="<<lastMetrics.elapsedMicros<<" stop="<<cpuai::reasonName(lastReason)<<'\n';
        } else if (cmd=="save" || cmd=="load") {
            const auto path=argument(in); endOfCommand(in);
            if (cmd=="save") learner.save(path); else learner=cpuai::Learner::load(path);
            std::cout<<(cmd=="save"?"saved":"loaded")<<'\n';
        } else throw std::runtime_error("unknown command");
    } catch (const std::exception& e) { std::cerr<<"error: "<<e.what()<<'\n'; }
}
