#include "include/CPUBridge.h"
#include "../../../../include/cpuai/audio_stream.hpp"
#include "../../../../include/cpuai/network_protocol.hpp"
#include <atomic>
#include <cstring>
#include <chrono>
struct CPLJournal {cpuai::DeviceJournal value;explicit CPLJournal(const char* p,const char* d):value(p,d){}};
static_assert(std::atomic<uint64_t>::is_always_lock_free,"Capture ring requires lock-free 64-bit atomics");
struct CPLCapture {
    struct Slot {std::array<int16_t,4096> pcm{};size_t frames=0;uint64_t start=0;};
    std::array<Slot,8> slots;
    std::atomic<uint64_t> written{0},read{0},dropped{0};
    uint64_t producerFrame=0,expectedFrame=0,sequence=0;bool finished=false,failed=false;
    uint32_t rate;std::string session;CPLJournal* journal;
    cpuai::PcmWindowStream stream;
    CPLCapture(CPLJournal* j,uint32_t r,const char* s):rate(r),session(s),journal(j),stream(r,100,2){cpuai::validateDeviceId(session);}
};
namespace {
void failure(char* out,size_t size,const char* text){if(out&&size){std::strncpy(out,text,size-1);out[size-1]=0;}}
void stats(CPLCapture* c,CPLStats* s){if(s){const auto& m=c->stream.metrics();*s={c->sequence,c->dropped.load(),m.discardedPartialFrames,m.discontinuities,c->journal->value.events().size()};}}
}
extern "C" {
CPLJournal* cpl_open(const char* path,const char* device,char* error,size_t size){try{if(!path||!device)throw std::runtime_error("missing journal identity");cpuai::validateDeviceId(device);return new CPLJournal(path,device);}catch(const std::exception& e){failure(error,size,e.what());return nullptr;}}
void cpl_close(CPLJournal* j){delete j;}
size_t cpl_event_count(CPLJournal* j){return j?j->value.events().size():0;}
int cpl_batch(CPLJournal* j,size_t offset,uint8_t* bytes,size_t cap,size_t* size,size_t* count,char* error,size_t error_size){try{
    if(!j||!bytes||!size||!count)throw std::runtime_error("missing batch argument");
    std::vector<cpuai::DeviceEvent> all;
    for(const auto& [id,event]:j->value.events())if(id.device==j->value.node())all.push_back(event);
    if(offset>=all.size()){*size=0;*count=0;return 0;}
    std::vector<cpuai::DeviceEvent> batch(all.begin()+static_cast<std::ptrdiff_t>(offset),all.begin()+static_cast<std::ptrdiff_t>(std::min(offset+128,all.size())));
    const auto payload=cpuai::encodeBatch(batch);if(payload.size()>cap)throw std::runtime_error("batch buffer too small");
    std::memcpy(bytes,payload.data(),payload.size());*size=payload.size();*count=batch.size();return 0;
}catch(const std::exception& e){failure(error,error_size,e.what());return -1;}}
int cpl_ack(CPLJournal* j,const char* peer,const uint8_t* batch,size_t size,char* error,size_t error_size){try{
    if(!j||!peer||!batch||size>131072)throw std::runtime_error("invalid acknowledgement");
    const auto events=cpuai::decodeBatch(std::string(reinterpret_cast<const char*>(batch),size),j->value.node());j->value.acknowledge(peer,events);return 0;
}catch(const std::exception& e){failure(error,error_size,e.what());return -1;}}
CPLCapture* cpl_capture_create(CPLJournal* j,uint32_t rate,const char* session,char* error,size_t size){try{
    if(!j||!session)throw std::runtime_error("missing capture argument");
    return new CPLCapture(j,rate,session);
}catch(const std::exception& e){failure(error,size,e.what());return nullptr;}}
int cpl_push_float(CPLCapture* c,const float* left,const float* right,size_t frames){
    if(!c||frames==0)return 0;
    const auto start=c->producerFrame;c->producerFrame+=frames;
    const auto w=c->written.load(std::memory_order_relaxed),r=c->read.load(std::memory_order_acquire);
    if(!left||frames>4096||w-r>=8){c->dropped.fetch_add(frames,std::memory_order_relaxed);return 0;}
    auto& slot=c->slots[w%8];slot.frames=frames;slot.start=start;
    for(size_t i=0;i<frames;++i){float value=right?(left[i]+right[i])*0.5f:left[i];if(!std::isfinite(value)){c->dropped.fetch_add(frames,std::memory_order_relaxed);return 0;}
        value=std::clamp(value,-1.0f,1.0f);slot.pcm[i]=static_cast<int16_t>(std::round(value*32767.0f));}
    c->written.store(w+1,std::memory_order_release);return 1;
}
int cpl_poll(CPLCapture* c,size_t max_blocks,CPLStats* out,char* error,size_t size){try{
    if(!c||c->finished||c->failed||max_blocks>8)throw std::runtime_error("capture unavailable or work limit exceeded");
    for(size_t block=0;block<max_blocks;++block){const auto r=c->read.load(std::memory_order_relaxed);if(r==c->written.load(std::memory_order_acquire))break;
        const auto& slot=c->slots[r%8];if(slot.start!=c->expectedFrame)c->stream.discontinuity();c->expectedFrame=slot.start+slot.frames;
        size_t offset=0;while(offset<slot.frames){const auto fed=c->stream.feed(std::span(slot.pcm).subspan(offset,slot.frames-offset),{},1);offset+=fed.consumed;
            while(auto window=c->stream.pop()){cpuai::DeviceEvent event;event.id={c->journal->value.node(),c->session,c->sequence+1};event.sample=std::move(window->sample);
                event.recordedAtMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();event.segment=window->segment;event.startFrame=window->startFrame;
                c->journal->value.receive({event});++c->sequence;}
            if(fed.consumed==0)throw std::runtime_error("capture made no progress");}
        c->read.store(r+1,std::memory_order_release);
    }
    stats(c,out);return 0;
}catch(const std::exception& e){if(c)c->failed=true;failure(error,size,e.what());return -1;}}
int cpl_finish(CPLCapture* c,CPLStats* out,char* error,size_t size){
    if(!c){failure(error,size,"missing capture");return -1;}
    if(!c->finished&&!c->failed&&cpl_poll(c,8,out,error,size)!=0)return -1;
    c->stream.finish();c->finished=true;stats(c,out);return 0;
}
void cpl_capture_destroy(CPLCapture* c){delete c;}
}
