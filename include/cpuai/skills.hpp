#pragma once
#include "evaluation.hpp"
#include <atomic>
#include <filesystem>
#include <set>

namespace cpuai {
using VersionId=std::uint64_t;
struct SkillRef {
    std::string name;VersionId id;
    auto operator<=>(const SkillRef&)const=default;
};
struct RoutineProgram {std::vector<SkillRef> steps;Expr expression;std::size_t cost=1,depth=1;};
enum class SkillOrigin { learned, composed, abstraction };
struct CompositionOptions {std::size_t maxPrograms=512,maxCandidates=64;int maxLength=3;};
struct CompositionPlan {std::vector<RoutineProgram> candidates;std::size_t tested=0;bool complete=true;};
struct AbstractionPattern {std::vector<SkillRef> steps;std::size_t support;};
class SkillLibrary;
class RoutineCandidate {
    friend class SkillLibrary;
    FrozenCandidate frozen_;
    RoutineProgram program_;
    SkillOrigin origin_;
    std::uint64_t instance_,epoch_;
    RoutineCandidate(FrozenCandidate frozen,RoutineProgram program,SkillOrigin origin,
        std::uint64_t instance,std::uint64_t epoch):frozen_(std::move(frozen)),program_(std::move(program)),origin_(origin),instance_(instance),epoch_(epoch){}
public:
    const FrozenCandidate& snapshot()const{return frozen_;}
    const RoutineProgram& program()const{return program_;}
};
struct SkillVersion {
    SkillRef ref;SkillOrigin origin;RoutineProgram program;FrozenCandidate source;ExamSuite exam;
};
struct ReplayReport {std::size_t cases=0,correct=0,calls=0;bool complete=true;std::vector<std::string> failed;};
inline Polynomial composePolynomial(const Polynomial& outer,const Polynomial& inner) {
    Polynomial result{0};
    for(auto c=outer.rbegin();c!=outer.rend();++c){
        auto product=combine(result,inner,'*');
        if(!product)throw std::runtime_error("composition degree/coefficient limit");
        auto sum=combine(*product,Polynomial{*c},'+');
        if(!sum)throw std::runtime_error("composition coefficient limit");
        result=*sum;
    }
    return result;
}
class SkillLibrary {
    static inline std::atomic<std::uint64_t> serial_{0};
    std::uint64_t instance_=++serial_,epoch_=0;
    std::map<VersionId,SkillVersion> versions_;
    std::map<std::string,VersionId> active_;
    VersionId next_=1;
    static void validName(const std::string& name){if(name.empty()||name.size()>128)throw std::runtime_error("invalid skill name");}
    const SkillVersion& resolve(const SkillRef& ref)const {
        auto it=versions_.find(ref.id);
        if(it==versions_.end() || it->second.ref.name!=ref.name)throw std::runtime_error("unknown pinned skill version");
        return it->second;
    }
    static void checkpoint(const SearchControl& control){if(auto reason=interrupt(control))throw std::runtime_error(reasonName(*reason));}
    double invoke(const SkillRef& ref,double x,std::size_t& remaining)const {
        if(remaining==0)throw std::runtime_error("execution call budget");
        --remaining;validateExample({x,0});const auto& v=resolve(ref);double y=x;
        if(v.program.steps.empty())y=evaluate(v.program.expression,x);
        else for(const auto& child:v.program.steps)y=invoke(child,y,remaining);
        validateExample({x,y});return y;
    }
    double execute(const RoutineProgram& program,double x,std::size_t& remaining)const {
        validateExample({x,0});
        if(remaining==0)throw std::runtime_error("execution call budget");
        --remaining;double y=x;
        if(program.steps.empty())y=evaluate(program.expression,x);
        else for(const auto& step:program.steps)y=invoke(step,y,remaining);
        validateExample({x,y});return y;
    }
    void leaves(const SkillRef& ref,std::vector<SkillRef>& out)const {
        const auto& v=resolve(ref);
        if(v.program.steps.empty()){out.push_back(ref);return;}
        for(const auto& child:v.program.steps)leaves(child,out);
    }
    RoutineProgram build(const std::vector<SkillRef>& refs)const {
        if(refs.empty() || refs.size()>3)throw std::runtime_error("routine needs 1..3 steps");
        RoutineProgram p;p.steps=refs;p.expression={"pipeline",{0,1}};
        for(const auto& ref:refs){
            const auto& v=resolve(ref);
            p.cost+=v.program.cost;p.depth=std::max(p.depth,1+v.program.depth);
            if(p.cost>64 || p.depth>8)throw std::runtime_error("routine depth/call limit");
            p.expression.coefficients=composePolynomial(v.program.expression.coefficients,p.expression.coefficients);
            p.expression.text+=" "+ref.name+"@"+std::to_string(ref.id);
        }
        return p;
    }
    bool fits(const RoutineProgram& p,const std::vector<Example>& examples,const SearchControl& control)const {
        for(auto e:examples){if(interrupt(control))return false;std::size_t calls=64;
            try{if(!close(execute(p,e.x,calls),e.y))return false;}catch(const std::runtime_error&){return false;}
        }
        return true;
    }
    FrozenCandidate binding(const Learner& l,const std::string& name,const Expr& expression)const {
        auto* s=l.skill(name);if(!s || s->examples.empty())throw std::runtime_error("teach target examples first");
        return {name,s->revision,s->examples,expression,{},1,true};
    }
    ReplayReport replayExcept(const std::string& excluded,const SearchControl& control,std::size_t callBudget)const {
        ReplayReport r;const auto initial=callBudget;
        for(const auto& [name,id]:active_){
            if(name==excluded)continue;
            const auto& v=versions_.at(id);bool failed=false;
            for(auto e:v.exam.cases){
                if(interrupt(control) || callBudget<v.program.cost){r.complete=false;r.calls=initial-callBudget;return r;}
                ++r.cases;
                try{if(close(invoke(v.ref,e.x,callBudget),e.y))++r.correct;else failed=true;}
                catch(const std::runtime_error&){failed=true;}
            }
            if(failed)r.failed.push_back(name);
        }
        r.calls=initial-callBudget;return r;
    }
    void verify(const FrozenCandidate& source,const RoutineProgram& p,const ExamSuite& exam,SearchControl control={})const {
        const auto report=evaluateFrozen(source,exam);
        if(!report.passedOnSuite())throw std::runtime_error("candidate failed held-out exam");
        for(auto e:exam.cases){checkpoint(control);std::size_t calls=64;
            if(!close(execute(p,e.x,calls),e.y))throw std::runtime_error("pipeline failed execution exam");
        }
    }
public:
    SkillLibrary()=default;
    SkillLibrary(const SkillLibrary&)=delete;
    SkillLibrary& operator=(const SkillLibrary&)=delete;
    SkillLibrary(SkillLibrary&& other):SkillLibrary(){*this=std::move(other);}
    SkillLibrary& operator=(SkillLibrary&& other) {
        if(this!=&other){
            versions_=std::move(other.versions_);active_=std::move(other.active_);
            next_=other.next_;epoch_=other.epoch_;instance_=++serial_;
            other.versions_.clear();other.active_.clear();other.next_=1;other.epoch_=0;other.instance_=++serial_;
        }
        return *this;
    }
    const SkillVersion& active(const std::string& name)const {
        auto it=active_.find(name);if(it==active_.end())throw std::runtime_error("skill has no accepted version");
        return versions_.at(it->second);
    }
    std::vector<SkillRef> history(const std::string& name)const {
        std::vector<SkillRef> out;for(const auto& [id,v]:versions_)if(v.ref.name==name)out.push_back(v.ref);return out;
    }
    double run(const std::string& name,double x,std::size_t callBudget=64)const{return invoke(active(name).ref,x,callBudget);}
    RoutineCandidate prepareLearned(const Learner& l,const std::string& name,SearchOptions options={},SearchControl control={})const {
        auto source=freeze(l,name,options,control);
        if(!source.searchComplete || source.consistentCandidates!=1)throw std::runtime_error("publication requires one complete hypothesis");
        RoutineProgram p;p.expression=source.hypothesis;
        return {std::move(source),std::move(p),SkillOrigin::learned,instance_,epoch_};
    }
    CompositionPlan plan(const Learner& l,const std::string& name,CompositionOptions options={},SearchControl control={})const {
        if(options.maxPrograms==0 || options.maxPrograms>4096 || options.maxCandidates==0
            || options.maxCandidates>256 || options.maxLength<1 || options.maxLength>3)
            throw std::runtime_error("invalid composition limits");
        auto* target=l.skill(name);if(!target || target->examples.empty())throw std::runtime_error("teach target examples first");
        std::vector<SkillRef> sources;for(const auto& [n,id]:active_)if(n!=name)sources.push_back({n,id});
        CompositionPlan result;if(sources.empty())return result;
        std::set<Polynomial> identities;
        for(int length=1;length<=options.maxLength;++length){
            std::vector<std::size_t> digits(length,0);bool done=false;
            while(!done){
                if(interrupt(control)){result.complete=false;return result;}
                if(result.tested>=std::min(options.maxPrograms,control.maxCombinations)){result.complete=false;return result;}
                ++result.tested;std::vector<SkillRef> refs;for(auto d:digits)refs.push_back(sources[d]);
                std::optional<RoutineProgram> p;
                try{p=build(refs);}catch(const std::runtime_error&){}
                if(p && fits(*p,target->examples,control) && identities.insert(p->expression.coefficients).second){
                    if(result.candidates.size()==options.maxCandidates){result.complete=false;return result;}
                    result.candidates.push_back(std::move(*p));
                }
                if(interrupt(control)){result.complete=false;return result;}
                for(int i=length-1;i>=0;--i){if(++digits[i]<sources.size())break;digits[i]=0;if(i==0)done=true;}
            }
        }
        return result;
    }
    RoutineCandidate prepareComposition(const Learner& l,const std::string& name,CompositionOptions options={},SearchControl control={})const {
        auto found=plan(l,name,options,control);
        if(!found.complete || found.candidates.size()!=1)throw std::runtime_error("composition needs one complete algebraic hypothesis");
        auto p=std::move(found.candidates.front());auto source=binding(l,name,p.expression);
        return {std::move(source),std::move(p),SkillOrigin::composed,instance_,epoch_};
    }
    std::vector<AbstractionPattern> patterns()const {
        std::map<std::vector<SkillRef>,std::set<std::string>> uses;
        for(const auto& [name,id]:active_){std::vector<SkillRef> steps;leaves({name,id},steps);
            for(std::size_t start=0;start<steps.size();++start)for(std::size_t n=2;n<=3 && start+n<=steps.size();++n)
                uses[{steps.begin()+start,steps.begin()+start+n}].insert(name);
        }
        std::vector<AbstractionPattern> out;for(const auto& [steps,names]:uses)if(names.size()>=2)out.push_back({steps,names.size()});
        std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){return a.support==b.support?a.steps<b.steps:a.support>b.support;});
        return out;
    }
    RoutineCandidate prepareAbstraction(const Learner& l,const std::string& name,std::size_t index,SearchControl control={})const {
        checkpoint(control);const auto found=patterns();
        if(index>=found.size())throw std::runtime_error("no such mined abstraction");
        auto p=build(found[index].steps);auto source=binding(l,name,p.expression);
        if(!fits(p,source.training,control))throw std::runtime_error("abstraction does not fit target lessons");
        return {std::move(source),std::move(p),SkillOrigin::abstraction,instance_,epoch_};
    }
    const SkillVersion& accept(const Learner& l,const RoutineCandidate& c,const ExamSuite& exam,SearchControl control={}) {
        checkpoint(control);validName(c.frozen_.skillName);
        if(c.instance_!=instance_ || c.epoch_!=epoch_ || !isCurrent(l,c.frozen_))throw std::runtime_error("stale routine candidate");
        if(versions_.size()>=256 || (!active_.contains(c.frozen_.skillName) && active_.size()>=64))throw std::runtime_error("skill library capacity");
        verify(c.frozen_,c.program_,exam,control);checkpoint(control);
        const auto old=replayExcept(c.frozen_.skillName,control,1000000);
        if(!old.complete || !old.failed.empty())throw std::runtime_error("old ability replay failed/incomplete");
        SkillVersion version{{c.frozen_.skillName,next_},c.origin_,c.program_,c.frozen_,exam};
        // Only activate after independent execution and old-skill replay succeed.
        auto [it,inserted]=versions_.emplace(next_,std::move(version));
        if(!inserted)throw std::runtime_error("duplicate skill version");
        try{active_.insert_or_assign(c.frozen_.skillName,next_);}catch(...){versions_.erase(it);throw;}
        ++next_;++epoch_;return it->second;
    }
    void rollback(const std::string& name,VersionId id) {
        const auto& v=resolve({name,id});
        if(!active_.contains(name) && active_.size()>=64)throw std::runtime_error("active skill capacity");
        active_.insert_or_assign(name,v.ref.id);++epoch_;
    }
    ReplayReport replay(SearchControl control={},std::size_t callBudget=1000000)const {
        return replayExcept("",control,callBudget);
    }
    void save(const std::string& path)const;
    static SkillLibrary load(const std::string& path);
};
}

namespace cpuai {
inline void SkillLibrary::save(const std::string& path)const {
    std::ofstream out(path);if(!out)throw std::runtime_error("cannot save skill library");
    out<<"CPUAI_LIBRARY1\n"<<versions_.size()<<' '<<active_.size()<<'\n'<<std::setprecision(17);
    for(const auto& [id,v]:versions_){
        const auto& s=v.source;const auto& o=s.searchOptions;
        out<<id<<' '<<std::quoted(v.ref.name)<<' '<<static_cast<int>(v.origin)<<'\n';
        out<<s.revision<<' '<<s.training.size()<<'\n';for(auto e:s.training)out<<e.x<<' '<<e.y<<'\n';
        out<<o.budget<<' '<<o.maxDepth<<' '<<o.maxUnique<<' '<<o.maxHypotheses<<' '<<o.maxStorageBytes<<'\n';
        out<<s.hypothesis.coefficients.size();for(auto c:s.hypothesis.coefficients)out<<' '<<c;
        out<<' '<<std::quoted(s.hypothesis.text)<<'\n';
        out<<v.program.steps.size()<<'\n';for(const auto& ref:v.program.steps)out<<std::quoted(ref.name)<<' '<<ref.id<<'\n';
        out<<std::quoted(v.exam.id)<<' '<<v.exam.cases.size()<<'\n';for(auto e:v.exam.cases)out<<e.x<<' '<<e.y<<'\n';
    }
    for(const auto& [name,id]:active_)out<<std::quoted(name)<<' '<<id<<'\n';
    out<<"END\n";out.flush();if(!out)throw std::runtime_error("skill library write failed");
}
inline SkillLibrary SkillLibrary::load(const std::string& path) {
    if(std::filesystem::file_size(path)>4*1024*1024)throw std::runtime_error("skill archive size limit");
    std::ifstream in(path);std::string header;std::getline(in,header);
    if(header!="CPUAI_LIBRARY1")throw std::runtime_error("invalid skill archive version");
    auto require=[&](bool condition){if(!condition || !in)throw std::runtime_error("invalid skill archive");};
    std::size_t count=0,activeCount=0;in>>count>>activeCount;require(count<=256 && activeCount<=64);
    SkillLibrary book;std::set<std::string> names;
    for(std::size_t i=0;i<count;++i){
        VersionId id=0;std::string name;int kind=-1;in>>id>>std::quoted(name)>>kind;
        require(id==book.next_ && kind>=0 && kind<=2);validName(name);names.insert(name);require(names.size()<=64);
        std::uint64_t revision=0;std::size_t n=0;in>>revision>>n;require(n>=1 && n<=128);
        std::vector<Example> lessons;
        for(std::size_t j=0;j<n;++j){Example e{};in>>e.x>>e.y;require(true);validateExample(e);
            require(std::none_of(lessons.begin(),lessons.end(),[&](auto old){return old.x==e.x;}));lessons.push_back(e);
        }
        SearchOptions options;in>>options.budget>>options.maxDepth>>options.maxUnique>>options.maxHypotheses>>options.maxStorageBytes;
        require(true);validateOptions(options);
        std::size_t coefficientCount=0;in>>coefficientCount;require(coefficientCount>=1 && coefficientCount<=17);
        Polynomial coefficients(coefficientCount);for(auto& c:coefficients){in>>c;require(c>=-1000000 && c<=1000000);}
        require(coefficientCount==1 || coefficients.back()!=0);
        std::string text;in>>std::quoted(text);require(!text.empty() && text.size()<=4096);
        std::size_t refsCount=0;in>>refsCount;
        require(refsCount<=3 && ((kind==0)==(refsCount==0)) && (kind!=2 || refsCount>=2));
        std::vector<SkillRef> refs;
        for(std::size_t j=0;j<refsCount;++j){SkillRef ref;in>>std::quoted(ref.name)>>ref.id;require(true);book.resolve(ref);refs.push_back(ref);}
        RoutineProgram program;
        if(refs.empty())program.expression={text,coefficients};else {program=book.build(refs);require(program.expression.coefficients==coefficients);}
        if(refs.empty()){
            const auto learned=infer(lessons,options);
            require(learned.complete() && learned.consistent==1
                && learned.hypotheses.front().coefficients==coefficients);
            program.expression=learned.hypotheses.front(); // render from verified semantics, not archive text
        }
        FrozenCandidate source{name,revision,lessons,program.expression,options,1,true};
        require(book.fits(program,lessons,{}));
        ExamSuite exam;std::size_t cases=0;in>>std::quoted(exam.id)>>cases;require(cases>=3 && cases<=128);
        for(std::size_t j=0;j<cases;++j){Example e{};in>>e.x>>e.y;require(true);exam.cases.push_back(e);}
        book.verify(source,program,exam);
        book.versions_.emplace(id,SkillVersion{{name,id},static_cast<SkillOrigin>(kind),std::move(program),std::move(source),std::move(exam)});
        ++book.next_;
    }
    for(std::size_t i=0;i<activeCount;++i){std::string name;VersionId id=0;in>>std::quoted(name)>>id;require(true);
        book.resolve({name,id});require(book.active_.emplace(name,id).second);
    }
    std::string end,extra;in>>end;require(end=="END");if(in>>extra)throw std::runtime_error("trailing archive content");
    if(book.active_.size()!=activeCount || names.size()!=activeCount)throw std::runtime_error("invalid active skill count");
    const auto replay=book.replay();
    if(!replay.complete || !replay.failed.empty())throw std::runtime_error("archive replay failed");
    return book;
}
}
