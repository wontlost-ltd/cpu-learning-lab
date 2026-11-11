#pragma once
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace cpuai {
struct Example { double x, y; };
struct Expr { std::string text; std::vector<double> values; };
struct Result { std::vector<Expr> hypotheses; std::size_t evaluated=0; bool exhausted=false; };
inline bool close(double a,double b) { return std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=1e-8*(1+std::abs(b)); }
// Finite, explicit inductive bias: x, constants -3..3, +,-,*; bounded expression depth.
inline Result infer(const std::vector<Example>& data, std::size_t budget=50000, int depth=3) {
  Result r; if(data.empty()) return r;
  std::vector<Expr> pool; std::map<std::vector<double>,bool> signatures;
  auto add=[&](Expr e){
    for(double v:e.values) if(!std::isfinite(v)||std::abs(v)>1e12) return;
    if(signatures.emplace(e.values,true).second) pool.push_back(std::move(e));
  };
  Expr x{"x",{}}; for(auto e:data)x.values.push_back(e.x); add(x);
  for(int c=-3;c<=3;c++)add({std::to_string(c),std::vector<double>(data.size(),c)});
  auto matches=[&](){std::vector<Expr> out; for(auto &p:pool){bool ok=true;for(std::size_t k=0;k<data.size();k++)if(!close(p.values[k],data[k].y)){ok=false;break;}if(ok)out.push_back(p);}return out;};
  r.hypotheses=matches(); if(!r.hypotheses.empty())return r;
  for(int d=1;d<=depth;d++){
    const auto n=pool.size(); std::vector<Expr> next;
    for(std::size_t i=0;i<n;i++)for(std::size_t j=0;j<n;j++)for(char op:std::string("+-*")){
      if((op=='+'||op=='*')&&j<i)continue;
      if(r.evaluated>=budget){r.exhausted=true;goto finished;}
      ++r.evaluated;
      Expr e{"("+pool[i].text+op+pool[j].text+")",{}};
      bool valid=true;
      for(std::size_t k=0;k<data.size();k++){
        double a=pool[i].values[k],b=pool[j].values[k];double v=op=='+'?a+b:op=='-'?a-b:a*b;
        if(!std::isfinite(v)||std::abs(v)>1e12){valid=false;break;}e.values.push_back(v);
      }
      if(valid&&signatures.emplace(e.values,true).second)next.push_back(std::move(e));
    }
    pool.insert(pool.end(),next.begin(),next.end());
    r.hypotheses=matches(); if(!r.hypotheses.empty())return r;
  }
finished:
  // A budget-truncated level is not accepted: report exhaustion rather than a misleading winner.
  return r;
}
// Parse only our arithmetic DSL; never execute host code.
inline double eval(const std::string& s,double x,std::size_t& p){
  if(p>=s.size())throw std::runtime_error("invalid expression");
  if(s[p]=='x'){++p;return x;}
  if(s[p]=='('){++p;double a=eval(s,x,p);if(p>=s.size())throw std::runtime_error("missing operator");char op=s[p++];double b=eval(s,x,p);if(p>=s.size()||s[p++]!=')')throw std::runtime_error("missing close");if(op=='+')return a+b;if(op=='-')return a-b;if(op=='*')return a*b;throw std::runtime_error("invalid operator");}
  std::size_t n=0;double v=std::stod(s.substr(p),&n);p+=n;return v;
}
inline double evaluate(const Expr&e,double x){std::size_t p=0;double y=eval(e.text,x,p);if(p!=e.text.size()||!std::isfinite(y))throw std::runtime_error("invalid result");return y;}
class Learner {
 public:
  std::map<std::string,std::vector<Example>> skills;
  void teach(const std::string&name,double x,double y){
    if(name.empty()||!std::isfinite(x)||!std::isfinite(y)||std::abs(x)>1e6||std::abs(y)>1e12)throw std::runtime_error("invalid lesson");
    auto &v=skills[name];for(auto &e:v)if(e.x==x){e.y=y;return;}if(v.size()>=128)throw std::runtime_error("example limit reached");v.push_back({x,y});
  }
  Result learn(const std::string&name)const{auto it=skills.find(name);if(it==skills.end())return {};return infer(it->second);}
  void save(const std::string&path)const{
    std::ofstream f(path);if(!f)throw std::runtime_error("cannot save");f<<"CPUAI1\n"<<std::setprecision(17);for(auto &[name,v]:skills)for(auto e:v)f<<std::quoted(name)<<' '<<e.x<<' '<<e.y<<'\n';f.flush();if(!f)throw std::runtime_error("save failed");
  }
  static Learner load(const std::string&path){
    std::ifstream f(path);if(!f)throw std::runtime_error("cannot load");std::string header;std::getline(f,header);if(header!="CPUAI1")throw std::runtime_error("invalid state version");Learner l;std::string name;double x,y;while(f>>std::ws&&!f.eof()){if(!(f>>std::quoted(name)>>x>>y))throw std::runtime_error("corrupt state");if(!l.skills.contains(name)&&l.skills.size()>=64)throw std::runtime_error("skill limit");l.teach(name,x,y);}return l;
  }
};
}
