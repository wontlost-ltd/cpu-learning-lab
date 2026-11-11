#pragma once
#include "search.hpp"
#include <fstream>
#include <iomanip>
#include <memory>

namespace cpuai {
struct SkillState { std::vector<Example> examples; std::uint64_t revision=0; };
class Learner {
    std::map<std::string,SkillState> skills_;
    struct FitCache {std::string name;std::vector<Example> examples;std::vector<std::size_t> ids;};
    mutable std::unique_ptr<SearchSession> session_;
    mutable std::optional<FitCache> cache_;

public:
    const SkillState* skill(const std::string& name) const {
        const auto it=skills_.find(name); return it==skills_.end()?nullptr:&it->second;
    }
    void teach(const std::string& name,double x,double y) {
        validateExample({x,y});
        if (name.empty() || name.size()>128) throw std::runtime_error("invalid skill name");
        if (!skills_.contains(name) && skills_.size()>=64) throw std::runtime_error("skill limit reached");
        auto& s=skills_[name];
        for (auto& e:s.examples) if (e.x==x) {
            if (e.y!=y) { e.y=y; ++s.revision; } return;
        }
        if (s.examples.size()>=128) throw std::runtime_error("example limit reached");
        s.examples.push_back({x,y}); ++s.revision;
    }
    Result learn(const std::string& name,SearchOptions options={},SearchControl control={}) const {
        validateOptions(options);
        const auto* s=skill(name);if(!s)return {};
        const auto start=SteadyClock::now();
        if(!session_ || session_->options()!=options){
            cache_.reset();session_=std::make_unique<SearchSession>(options);
        }
        const auto before=session_->evaluated();const auto reason=session_->run(control);
        bool hit=false,incremental=false;
        const std::vector<std::size_t>* previous=nullptr;std::size_t first=0;
        if(session_->complete() && cache_ && cache_->name==name
            && cache_->examples.size()<=s->examples.size()
            && std::equal(cache_->examples.begin(),cache_->examples.end(),s->examples.begin())){
            previous=&cache_->ids;first=cache_->examples.size();
            hit=first==s->examples.size();incremental=!hit;
        }
        auto fit=session_->fit(s->examples,control,previous,first);
        const bool finishedFit=!fit.interrupted && reason==StopReason::complete;
        // Keep all consistent IDs, including hypotheses omitted from the display cap.
        if(session_->complete() && reason==StopReason::complete && !fit.interrupted)
            cache_=FitCache{name,s->examples,fit.ids};
        auto r=materialize(*session_,std::move(fit),reason);
        r.metrics.cacheHit=hit && finishedFit;r.metrics.incremental=incremental && finishedFit;
        r.metrics.newCombinations=session_->evaluated()-before;
        r.metrics.elapsedMicros=std::chrono::duration_cast<std::chrono::microseconds>(SteadyClock::now()-start).count();
        return r;
    }
    void save(const std::string& path) const {
        std::ofstream f(path); if (!f) throw std::runtime_error("cannot save");
        // Persist observations, never persist claims of mastery; CPUAI1 stays readable.
        f<<"CPUAI1\n"<<std::setprecision(17);
        for (const auto& [name,s]:skills_) for (auto e:s.examples)
            f<<std::quoted(name)<<' '<<e.x<<' '<<e.y<<'\n';
        f.flush(); if (!f) throw std::runtime_error("save failed");
    }
    static Learner load(const std::string& path) {
        std::ifstream f(path); if (!f) throw std::runtime_error("cannot load");
        std::string header; std::getline(f,header);
        if (header!="CPUAI1") throw std::runtime_error("invalid state version");
        Learner l; std::string name; double x,y;
        while (f>>std::ws && !f.eof()) {
            if (!(f>>std::quoted(name)>>x>>y)) throw std::runtime_error("corrupt state");
            l.teach(name,x,y);
        }
        return l;
    }
};
}
