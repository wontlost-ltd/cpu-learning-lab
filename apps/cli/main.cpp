#include "cpuai/learner.hpp"
#include <iostream>
#include <sstream>
int main(){
 cpuai::Learner l;std::cout<<"CPU Learning Lab: teach NAME X Y | ask NAME X | show NAME | save PATH | load PATH | quit\n";
 std::string line;
 while(std::getline(std::cin,line))try{
  std::istringstream in(line);std::string cmd,name;in>>cmd;if(cmd=="quit")break;
  if(cmd=="teach"){double x,y;if(!(in>>name>>x>>y))throw std::runtime_error("teach NAME X Y");l.teach(name,x,y);std::cout<<"lesson recorded\n";}
  else if(cmd=="show"||cmd=="ask"){
   in>>name;auto r=l.learn(name);if(r.hypotheses.empty()){std::cout<<(r.exhausted?"search budget exhausted":"no hypothesis; teach more or expand grammar")<<"; evaluated="<<r.evaluated<<'\n';continue;}
   if(cmd=="show"){std::cout<<r.hypotheses.front().text<<" (consistent hypothesis, not proven); candidates="<<r.evaluated<<'\n';continue;}
   double x;if(!(in>>x)||!std::isfinite(x)||std::abs(x)>1e6)throw std::runtime_error("ask NAME finite-X within +/-1e6");
   if(l.skills.at(name).size()<3){std::cout<<"insufficient evidence: need at least 3 examples\n";continue;}
   std::cout<<cpuai::evaluate(r.hypotheses.front(),x)<<" [hypothesis: "<<r.hypotheses.front().text<<"]\n";
  }else if(cmd=="save"){in>>name;l.save(name);std::cout<<"saved\n";}else if(cmd=="load"){in>>name;l=cpuai::Learner::load(name);std::cout<<"loaded\n";}else std::cout<<"unknown command\n";
 }catch(const std::exception&e){std::cerr<<"error: "<<e.what()<<'\n';}
}
