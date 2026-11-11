#include "cpuai/evaluation.hpp"
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
    std::cout<<"CPU Learning Lab P1: teach NAME X Y | ask NAME X | show NAME | next NAME | exam NAME FILE | save PATH | load PATH | quit\n";
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
            endOfCommand(in); const auto r=learner.learn(name);
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
                    <<" evaluated="<<r.evaluated<<" unique="<<r.unique<<" complete="<<r.complete()<<'\n';
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
            const auto frozen=cpuai::freeze(learner,name); // before opening exam file
            const auto report=cpuai::evaluateFrozen(frozen,cpuai::loadExam(path));
            std::cout<<"suite="<<report.suiteId<<" candidate="<<frozen.hypothesis.text
                <<" revision="<<frozen.revision<<" accuracy="<<report.accuracy()
                <<" passed_on_suite="<<report.passedOnSuite()
                <<" current="<<cpuai::isCurrent(learner,frozen)<<"; evaluation only, no learning\n";
        } else if (cmd=="save" || cmd=="load") {
            const auto path=argument(in); endOfCommand(in);
            if (cmd=="save") learner.save(path); else learner=cpuai::Learner::load(path);
            std::cout<<(cmd=="save"?"saved":"loaded")<<'\n';
        } else throw std::runtime_error("unknown command");
    } catch (const std::exception& e) { std::cerr<<"error: "<<e.what()<<'\n'; }
}
