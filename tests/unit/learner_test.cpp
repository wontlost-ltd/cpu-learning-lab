#include "cpuai/learner.hpp"
#include <filesystem>
#include <iostream>
#define CHECK(c) do {if(!(c))throw std::runtime_error(#c);}while(false)
int main(){
 cpuai::Learner l;CHECK(l.learn("unknown").hypotheses.empty());
 for(double x:{-2.,0.,3.})l.teach("affine",x,2*x+1);
 auto a=l.learn("affine");CHECK(!a.hypotheses.empty());for(double x:{-7.,1.,9.})CHECK(cpuai::close(cpuai::evaluate(a.hypotheses[0],x),2*x+1));
 for(double x:{-2.,0.,3.})l.teach("square",x,x*x);
 auto b=l.learn("square");CHECK(!b.hypotheses.empty());CHECK(cpuai::close(cpuai::evaluate(b.hypotheses[0],7),49));
 // Misleading initial examples: x and x*x agree at 0,1. New counterexample forces revision.
 l.teach("revise",0,0);l.teach("revise",1,1);CHECK(l.learn("revise").hypotheses[0].text=="x");l.teach("revise",2,4);CHECK(cpuai::close(cpuai::evaluate(l.learn("revise").hypotheses[0],3),9));
 auto path=(std::filesystem::temp_directory_path()/"cpuai-test-state.txt").string();l.save(path);auto restored=cpuai::Learner::load(path);std::filesystem::remove(path);CHECK(cpuai::close(cpuai::evaluate(restored.learn("affine").hypotheses[0],11),23));CHECK(cpuai::close(cpuai::evaluate(restored.learn("square").hypotheses[0],8),64));
 CHECK(cpuai::infer({{0,5},{1,7},{2,9}},0).exhausted);
 bool bad=false;try{l.teach("bad",NAN,0);}catch(...){bad=true;}CHECK(bad);
 std::cout<<"PASS: held-out generalization, counterexample revision, persistence, isolation, budget and input checks\n";
}
