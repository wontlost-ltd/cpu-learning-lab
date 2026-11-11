#include "cpuai/learner.hpp"
#include <iostream>
#include <vector>

using namespace cpuai;
double micros(SteadyClock::time_point start) {
    return std::chrono::duration<double,std::micro>(SteadyClock::now()-start).count();
}
double percentile(std::vector<double> times,double p) {
    std::sort(times.begin(),times.end());return times[static_cast<std::size_t>((times.size()-1)*p)];
}
int main() {
    constexpr int count=200;
    const std::vector<Example> affine={{-2,-3},{0,1},{3,7}};
    std::vector<double> cold,warm,incremental;
    std::size_t coldCombinations=0,warmCombinations=0,warmChecks=0,deltaChecks=0;
    std::size_t bytes=0,peak=0;int correct=0;
    Learner cached;for(auto e:affine)cached.teach("a",e.x,e.y);cached.learn("a");
    for(int i=0;i<count;++i) {
        auto start=SteadyClock::now();const auto c=infer(affine);cold.push_back(micros(start));
        start=SteadyClock::now();const auto w=cached.learn("a");warm.push_back(micros(start));
        auto cp=predict(c,9),wp=predict(w,9);
        if(cp.value && wp.value && close(*cp.value,19) && close(*wp.value,19))++correct;
        coldCombinations+=c.metrics.newCombinations;warmCombinations+=w.metrics.newCombinations;
        warmChecks+=w.metrics.exampleChecks;bytes=w.metrics.storageBytes;peak=w.metrics.peakStorageBytes;
        Learner delta;delta.teach("s",0,0);delta.teach("s",1,1);delta.learn("s");delta.teach("s",2,4);
        start=SteadyClock::now();const auto d=delta.learn("s");incremental.push_back(micros(start));
        deltaChecks+=d.metrics.exampleChecks;
        const auto dp=predict(d,3);if(!dp.value || !close(*dp.value,9))return 2;
    }
    std::cout<<"{\n  \"iterations\":"<<count<<",\n  \"correct_pairs\":"<<correct
        <<",\n  \"cold_p50_us\":"<<percentile(cold,.50)<<",\n  \"cold_p95_us\":"<<percentile(cold,.95)
        <<",\n  \"warm_p50_us\":"<<percentile(warm,.50)<<",\n  \"warm_p95_us\":"<<percentile(warm,.95)
        <<",\n  \"incremental_p50_us\":"<<percentile(incremental,.50)
        <<",\n  \"incremental_p95_us\":"<<percentile(incremental,.95)
        <<",\n  \"cold_combinations_total\":"<<coldCombinations
        <<",\n  \"warm_combinations_total\":"<<warmCombinations<<",\n  \"warm_example_checks_total\":"<<warmChecks
        <<",\n  \"incremental_example_checks_total\":"<<deltaChecks
        <<",\n  \"search_storage_bytes\":"<<bytes<<",\n  \"search_peak_requested_bytes\":"<<peak<<"\n}\n";
    return correct==count?0:1;
}
