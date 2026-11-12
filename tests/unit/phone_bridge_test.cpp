#include "../../mobile/ios/Sources/CPUBridge/include/CPUBridge.h"
#include "cpuai/network_protocol.hpp"
#include <iostream>
void require(bool ok){if(!ok)throw std::runtime_error("phone bridge assertion");}
int main(){const auto root=std::filesystem::temp_directory_path()/("cpuai-phone-"+cpuai::newSessionId());std::filesystem::create_directory(root);
    char error[512]{};auto journal=cpl_open((root/"phone.journal").string().c_str(),"phone",error,sizeof(error));require(journal);
    auto capture=cpl_capture_create(journal,16000,"session",error,sizeof(error));require(capture);
    std::array<float,1600> pcm{};for(size_t i=0;i<pcm.size();++i)pcm[i]=static_cast<float>(0.5*std::sin(2*3.14159265358979323846*500*i/16000));
    for(int i=0;i<8;++i)require(cpl_push_float(capture,pcm.data(),nullptr,pcm.size())==1);
    require(cpl_push_float(capture,pcm.data(),nullptr,pcm.size())==0);CPLStats stats{};
    require(cpl_poll(capture,8,&stats,error,sizeof(error))==0&&stats.committed_windows==8&&stats.dropped_frames==1600);
    require(cpl_push_float(capture,pcm.data(),nullptr,pcm.size())==1);require(cpl_poll(capture,8,&stats,error,sizeof(error))==0&&stats.discontinuities==1&&stats.committed_windows==9);
    require(cpl_push_float(capture,pcm.data(),nullptr,100)==1);require(cpl_finish(capture,&stats,error,sizeof(error))==0&&stats.discarded_tail==100);
    cpl_capture_destroy(capture);std::array<uint8_t,131072> bytes{};size_t size=0,count=0;
    require(cpl_batch(journal,0,bytes.data(),bytes.size(),&size,&count,error,sizeof(error))==0&&count==9);
    const auto events=cpuai::decodeBatch(std::string(reinterpret_cast<char*>(bytes.data()),size),"phone");require(events.back().segment==1);
    require(cpl_ack(journal,"desktop",bytes.data(),size,error,sizeof(error))==0);
    require(cpl_receipt_count(journal,"desktop")==9);
    require(cpl_batch_limited(journal,0,4,bytes.data(),bytes.size(),&size,&count,error,sizeof(error))==0&&count==4);
    auto relay=cpl_open((root/"relay.journal").string().c_str(),"phone",error,sizeof(error));require(relay);size_t inserted=0;
    require(cpl_receive(relay,bytes.data(),size,3,&inserted,error,sizeof(error))==-1&&cpl_event_count(relay)==0);
    require(cpl_receive(relay,bytes.data(),size,4,&inserted,error,sizeof(error))==0&&inserted==4);
    require(cpl_receive(relay,bytes.data(),size,4,&inserted,error,sizeof(error))==0&&inserted==0);
    auto wrong=cpl_open((root/"wrong.journal").string().c_str(),"other-watch",error,sizeof(error));require(wrong);
    require(cpl_receive(wrong,bytes.data(),size,4,&inserted,error,sizeof(error))==-1&&cpl_event_count(wrong)==0);
    require(cpl_batch_limited(journal,0,0,bytes.data(),bytes.size(),&size,&count,error,sizeof(error))==-1);
    cpl_close(wrong);cpl_close(relay);cpl_close(journal);
    {cpuai::DeviceJournal saved(root/"phone.journal");require(saved.events().size()==9&&saved.acknowledged("desktop",events.front().id));}
    std::filesystem::remove_all(root);std::cout<<"phone bridge tests passed: fixed queue, overflow gaps, durable windows, batch and receipts\n";
}
