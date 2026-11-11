#include "cpuai/audio.hpp"
#include <chrono>
#include <iostream>
int main(int argc,char** argv)try{
    if(argc!=2)throw std::runtime_error("audio-benchmark WAV");
    cpuai::AudioLearner learner;const auto seed=cpuai::readWave(argv[1]);learner.teach("seed","sample",seed);
    std::vector<double> times;
    for(int i=0;i<31;++i){const auto begin=std::chrono::steady_clock::now();
        const auto sample=cpuai::readWave(argv[1]);const auto p=learner.predict(sample);
        if(!p.label)throw std::runtime_error("benchmark prediction failed");
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        if(i)times.push_back(ms);
    }
    std::sort(times.begin(),times.end());
    std::cout<<"{\"iterations\":30,\"sample_rate\":"<<seed.rate<<",\"frames\":"<<seed.frames
        <<",\"lessons\":1,\"p50_ms\":"<<times[14]<<",\"p95_ms\":"<<times[28]<<"}\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
