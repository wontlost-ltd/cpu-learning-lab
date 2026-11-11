#include "cpuai/device_sync.hpp"
#include <chrono>
#include <iostream>
int main(int argc,char** argv)try{
    if(argc<2||std::string(argv[1])=="--help"){
        std::cout<<"cpu-sync init STORE NODE | record STORE SESSION LABEL_OR_- WAV | stats STORE | sync SOURCE TARGET [BATCH] [--stop-before-ack]\n";return 0;
    }
    const std::string command=argv[1];
    if(command=="init"&&argc==4){cpuai::DeviceJournal journal(argv[2],argv[3]);std::cout<<"journal initialized node="<<journal.node()<<'\n';}
    else if(command=="record"&&argc==6){cpuai::DeviceJournal journal(argv[2]);cpuai::DeviceEvent e;
        e.id={journal.node(),argv[3],journal.nextSequence(argv[3])};e.sample=cpuai::readWave(argv[5]);e.userLabel=std::string(argv[4])=="-"?"":argv[4];
        e.recordedAtMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        journal.receive({e});std::cout<<"durable event="<<e.id.device<<'/'<<e.id.session<<'/'<<e.id.sequence<<'\n';}
    else if(command=="stats"&&argc==3){cpuai::DeviceJournal journal(argv[2]);std::cout<<"node="<<journal.node()<<" events="<<journal.events().size()<<" peers="<<journal.peerCount()<<" recovered_bytes="<<journal.recoveredBytes()<<" bytes="<<journal.bytes()<<'\n';}
    else if(command=="sync"&&argc>=4&&argc<=6){std::size_t batch=64;bool stop=false,batchSpecified=false;
        for(int i=4;i<argc;++i){const std::string arg=argv[i];if(arg=="--stop-before-ack"&&!stop)stop=true;
            else{if(batchSpecified)throw std::runtime_error("duplicate sync batch");batchSpecified=true;if(arg.empty()||arg.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("invalid sync argument");batch=static_cast<std::size_t>(std::stoull(arg));}}
        cpuai::DeviceJournal source(argv[2]),target(argv[3]);const auto r=source.syncTo(target,batch,stop);
        std::cout<<"delivered="<<r.delivered<<" duplicates="<<r.duplicates<<" acknowledged="<<r.acknowledged<<" complete="<<r.complete<<'\n';return r.complete?0:3;
    }else throw std::runtime_error("invalid command; see cpu-sync --help");
    return 0;
}catch(const std::exception& e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}
