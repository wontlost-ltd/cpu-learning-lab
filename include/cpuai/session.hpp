#pragma once
#include "polynomial.hpp"
#include "runtime.hpp"
#include <array>
#include <map>

namespace cpuai {
using Coefficients=std::array<std::int64_t,17>;
struct DagNode {
    Coefficients coefficients{};
    std::size_t left=0,right=0,nodes=1;
    int depth=0,degree=0;
    char operation='c';
};
struct FitResult {std::vector<std::size_t> ids;std::optional<StopReason> interrupted;std::size_t checks=0;};
class SearchSession {
    SearchOptions options_;
    BudgetResource memory_;
    std::pmr::vector<DagNode> graph_;
    std::optional<std::pmr::map<Coefficients,std::size_t>> index_;
    std::optional<StopReason> terminal_;
    int depth_=1, operation_=0;
    std::size_t width_=8,i_=0,j_=0,evaluated_=0,edges_=0;
    void add(DagNode node) {
        // Roll back the graph on index allocation failure: no dangling index/child IDs.
        graph_.push_back(node);
        try { index_->emplace(node.coefficients,graph_.size()-1); }
        catch(...) {graph_.pop_back();throw;}
        if(node.operation!='x' && node.operation!='c')edges_+=2;
    }
    static std::optional<DagNode> combineNodes(const DagNode& a,const DagNode& b,char op,
        std::size_t left,std::size_t right,int depth) {
        DagNode n;n.left=left;n.right=right;n.depth=depth;n.nodes=1+a.nodes+b.nodes;n.operation=op;
        if(op=='*') {
            n.degree=a.degree+b.degree;if(n.degree>16)return std::nullopt;
            for(int i=0;i<=a.degree;++i)for(int j=0;j<=b.degree;++j)
                n.coefficients[i+j]+=a.coefficients[i]*b.coefficients[j];
        } else {
            n.degree=std::max(a.degree,b.degree);
            for(int i=0;i<=n.degree;++i)n.coefficients[i]=a.coefficients[i]+(op=='+'?b.coefficients[i]:-b.coefficients[i]);
        }
        while(n.degree>0 && n.coefficients[n.degree]==0)--n.degree;
        for(auto c:n.coefficients)if(c < -1000000 || c > 1000000)return std::nullopt;
        return n;
    }
    std::string text(std::size_t id)const {
        const auto& n=graph_[id];
        if(n.operation=='x')return "x";
        if(n.operation=='c')return std::to_string(n.coefficients[0]);
        return "("+text(n.left)+n.operation+text(n.right)+")";
    }
public:
    explicit SearchSession(SearchOptions options={}):options_(options),memory_(options.maxStorageBytes),graph_(&memory_) {
        validateOptions(options_);
        try {
            // MSVC allocates the empty map sentinel here; keep it inside the budget guard.
            index_.emplace(&memory_);
            DagNode x;x.coefficients[1]=1;x.degree=1;x.operation='x';add(x);
            for(int c=-3;c<=3;++c){DagNode n;n.coefficients[0]=c;add(n);}
        } catch(const MemoryLimit&){terminal_=StopReason::memory_limit;}
    }
    SearchSession(const SearchSession&)=delete;
    SearchSession& operator=(const SearchSession&)=delete;
    const SearchOptions& options()const{return options_;}
    bool complete()const{return terminal_==StopReason::complete;}
    std::size_t evaluated()const{return evaluated_;}
    std::size_t size()const{return graph_.size();}
    std::size_t storageBytes()const{return memory_.used();}
    std::size_t peakStorageBytes()const{return memory_.peak();}
    std::size_t edges()const{return edges_;}
    bool validDag()const {
        if(!index_)return graph_.empty();
        if(index_->size()!=graph_.size())return false;
        for(std::size_t id=0;id<graph_.size();++id){const auto& n=graph_[id];
            if(index_->at(n.coefficients)!=id)return false;
            if(n.operation!='c' && n.operation!='x' && (n.left>=id || n.right>=id))return false;
        }
        return true;
    }
    StopReason run(const SearchControl& control={}) {
        // Even a cache-ready session honors cancellation and deadlines.
        if(auto reason=interrupt(control))return *reason;
        if(terminal_)return *terminal_;
        const auto start=evaluated_;
        while(true) {
            if(auto reason=interrupt(control))return *reason;
            if(depth_>options_.maxDepth){terminal_=StopReason::complete;return *terminal_;}
            if(i_>=width_){++depth_;width_=graph_.size();i_=j_=0;operation_=0;continue;}
            if(j_>=width_){++i_;j_=0;operation_=0;continue;}
            if(operation_==3){++j_;operation_=0;continue;}
            if(std::max(graph_[i_].depth,graph_[j_].depth)!=depth_-1){++j_;operation_=0;continue;}
            const char op="+-*"[operation_];
            if(op!='-' && j_<i_){++operation_;continue;}
            if(evaluated_==options_.budget){terminal_=StopReason::candidate_budget;return *terminal_;}
            if(evaluated_-start==control.maxCombinations)return StopReason::work_slice;
            ++operation_;++evaluated_;
            const auto node=combineNodes(graph_[i_],graph_[j_],op,i_,j_,depth_);
            if(!node || index_->contains(node->coefficients))continue;
            if(graph_.size()==options_.maxUnique){terminal_=StopReason::unique_limit;return *terminal_;}
            try {add(*node);}catch(const MemoryLimit&){terminal_=StopReason::memory_limit;return *terminal_;}
        }
    }
    double value(std::size_t id,double x)const {
        const auto& n=graph_.at(id);long double y=0;
        for(int k=n.degree;k>=0;--k)y=y*x+n.coefficients[k];
        return static_cast<double>(y);
    }
    double valueViaDag(std::size_t id,double x)const {
        const auto& n=graph_.at(id);
        if(n.operation=='x')return x;
        if(n.operation=='c')return static_cast<double>(n.coefficients[0]);
        const auto a=valueViaDag(n.left,x),b=valueViaDag(n.right,x);
        return n.operation=='+'?a+b:n.operation=='-'?a-b:a*b;
    }
    Expr expression(std::size_t id)const {
        const auto& n=graph_.at(id);
        return {text(id),Polynomial(n.coefficients.begin(),n.coefficients.begin()+n.degree+1),n.depth,n.nodes};
    }
    void rank(std::vector<std::size_t>& ids)const {
        std::sort(ids.begin(),ids.end(),[&](auto a,auto b){
            return graph_[a].nodes==graph_[b].nodes?a<b:graph_[a].nodes<graph_[b].nodes;
        });
    }
    FitResult fit(const std::vector<Example>& data,const SearchControl& control,
        const std::vector<std::size_t>* previous=nullptr,std::size_t firstExample=0)const {
        if(data.size()>128 || firstExample>data.size() || (previous && previous->size()>graph_.size()))
            throw std::runtime_error("invalid fit input limits");
        for(auto e:data)validateExample(e);
        FitResult fit;
        const auto count=previous?previous->size():graph_.size();
        for(std::size_t pos=0;pos<count;++pos){
            if(auto reason=interrupt(control)){fit.interrupted=reason;return fit;}
            const auto id=previous?previous->at(pos):pos;bool matches=true;
            for(std::size_t k=firstExample;k<data.size();++k){
                if(auto reason=interrupt(control)){fit.interrupted=reason;return fit;}
                ++fit.checks;if(!close(value(id,data[k].x),data[k].y)){matches=false;break;}
            }
            if(matches)fit.ids.push_back(id);
        }
        return fit;
    }
};
}
