#pragma once
#include "audio.hpp"
#include "durable_file.hpp"
#include <compare>
#include <locale>
#include <random>
#include <set>

namespace cpuai {
inline void validateDeviceId(const std::string& id){
    if(id.empty()||id.size()>64||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.")!=std::string::npos)
        throw std::runtime_error("device/session IDs require 1..64 ASCII letters, digits, -_.");
}
inline std::string newSessionId(){std::random_device random;std::ostringstream out;out<<std::hex<<std::setfill('0');for(int i=0;i<4;++i)out<<std::setw(8)<<static_cast<std::uint32_t>(random());return out.str();}
struct EventId {std::string device,session;std::uint64_t sequence=0;auto operator<=>(const EventId&)const=default;};
struct DeviceEvent {
    EventId id;std::int64_t recordedAtMs=0;std::uint64_t segment=0,startFrame=0;
    AudioSample sample;std::string userLabel; // Empty means observation, never a generated label.
    bool operator==(const DeviceEvent& b)const{
        return id==b.id&&recordedAtMs==b.recordedAtMs&&segment==b.segment&&startFrame==b.startFrame&&
            sample.features==b.sample.features&&sample.fingerprint==b.sample.fingerprint&&sample.rate==b.sample.rate&&sample.frames==b.sample.frames&&userLabel==b.userLabel;
    }
};
inline void validateDeviceEvent(const DeviceEvent& e){
    validateDeviceId(e.id.device);validateDeviceId(e.id.session);validateFeatures(e.sample.features);
    if(e.id.sequence==0||e.id.sequence>1000000000000ULL||e.recordedAtMs<0||e.recordedAtMs>1000000000000000LL||
       e.segment>1000000000000ULL||e.startFrame>1000000000000ULL||e.userLabel.size()>64||
       e.sample.fingerprint.empty()||e.sample.fingerprint.size()>80||e.sample.fingerprint.find_first_not_of("0123456789abcdef-")!=std::string::npos||
       e.sample.rate<8000||e.sample.rate>48000||e.sample.frames<e.sample.rate/20||e.sample.frames>static_cast<std::size_t>(e.sample.rate)*2)
        throw std::runtime_error("invalid device audio event");
}
namespace journal_detail {
inline std::uint32_t crc(std::span<const unsigned char> bytes){std::uint32_t value=0xffffffffU;for(auto b:bytes){value^=b;for(int i=0;i<8;++i)value=(value>>1)^(0xedb88320U&(0U-(value&1)));}return ~value;}
inline void u32(std::vector<unsigned char>& out,std::uint32_t n){for(int i=0;i<4;++i)out.push_back(static_cast<unsigned char>((n>>(8*i))&255));}
inline std::uint32_t u32(std::span<const unsigned char> in,std::size_t p){std::uint32_t n=0;for(int i=0;i<4;++i)n|=static_cast<std::uint32_t>(in[p+i])<<(8*i);return n;}
inline std::vector<unsigned char> frame(const std::string& payload){
    if(payload.size()>131072)throw std::runtime_error("journal frame limit");
    std::vector<unsigned char> out;u32(out,0x314c4c43);u32(out,static_cast<std::uint32_t>(payload.size()));u32(out,crc(out));
    out.insert(out.end(),payload.begin(),payload.end());u32(out,crc(std::span(out).subspan(12)));u32(out,0x54494d43);return out;
}
inline void writeId(std::ostream& out,const EventId& id){out<<std::quoted(id.device)<<' '<<std::quoted(id.session)<<' '<<id.sequence<<' ';}
inline EventId readId(std::istream& in){EventId id;if(!(in>>std::quoted(id.device)>>std::quoted(id.session)>>id.sequence))throw std::runtime_error("invalid journal event ID");return id;}
inline void writeEvent(std::ostream& out,const DeviceEvent& e){writeId(out,e.id);out<<e.recordedAtMs<<' '<<e.segment<<' '<<e.startFrame<<' '<<e.sample.rate<<' '<<e.sample.frames<<' '<<std::quoted(e.sample.fingerprint)<<' '<<std::quoted(e.userLabel);for(double v:e.sample.features)out<<' '<<v;out<<'\n';}
inline DeviceEvent readEvent(std::istream& in){DeviceEvent e;e.id=readId(in);if(!(in>>e.recordedAtMs>>e.segment>>e.startFrame>>e.sample.rate>>e.sample.frames>>std::quoted(e.sample.fingerprint)>>std::quoted(e.userLabel)))throw std::runtime_error("invalid journal event");for(auto& v:e.sample.features)if(!(in>>v))throw std::runtime_error("invalid journal features");validateDeviceEvent(e);return e;}
}
class DeviceJournal {
    DurableFile file_;std::string node_;
    std::map<EventId,DeviceEvent> events_;
    std::map<std::string,std::set<EventId>> receipts_;
    bool poisoned_=false;std::size_t recovered_=0;
    static std::ostringstream output(){std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17);return out;}
    void healthy()const{if(poisoned_)throw std::runtime_error("journal I/O uncertain; close and reopen before retry");}
    void commit(const std::string& payload){healthy();const auto bytes=journal_detail::frame(payload);
        try{file_.append(bytes);file_.sync();}catch(...){poisoned_=true;throw;}
    }
    std::vector<DeviceEvent> newEvents(const std::vector<DeviceEvent>& input)const{
        if(input.size()>128)throw std::runtime_error("device event batch limit 128");
        std::map<EventId,DeviceEvent> fresh;
        for(const auto& e:input){validateDeviceEvent(e);auto old=events_.find(e.id);
            if(old!=events_.end()){if(!(old->second==e))throw std::runtime_error("event ID payload conflict");continue;}
            auto [it,added]=fresh.emplace(e.id,e);if(!added&&!(it->second==e))throw std::runtime_error("event ID conflict within batch");
        }
        if(events_.size()+fresh.size()>4096)throw std::runtime_error("journal event limit 4096; no eviction");
        std::set<std::pair<std::string,std::string>> origins;for(const auto& [id,e]:events_){(void)e;origins.emplace(id.device,id.session);}for(const auto& [id,e]:fresh){(void)e;origins.emplace(id.device,id.session);}
        if(origins.size()>64)throw std::runtime_error("journal origin limit 64");
        std::vector<DeviceEvent> result;for(auto& [id,e]:fresh){(void)id;result.push_back(std::move(e));}return result;
    }
    void replay(const std::string& payload,bool first){
        std::istringstream in(payload);in.imbue(std::locale::classic());std::string type;if(!(in>>type))throw std::runtime_error("empty journal frame");
        if(type=="CPUAI_DEVICELOG1"&&first){if(!(in>>std::quoted(node_)))throw std::runtime_error("invalid journal identity");validateDeviceId(node_);}
        else if(type=="E"&&!first){std::size_t count;if(!(in>>count)||count==0||count>128)throw std::runtime_error("invalid event frame count");std::vector<DeviceEvent> input;
            for(std::size_t i=0;i<count;++i)input.push_back(journal_detail::readEvent(in));
            auto fresh=newEvents(input);
            if(fresh.size()!=input.size())throw std::runtime_error("duplicate event in journal");
            for(auto& e:fresh)events_.emplace(e.id,std::move(e));
        }else if(type=="A"&&!first){std::string peer;std::size_t count;if(!(in>>std::quoted(peer)>>count)||count==0||count>128)throw std::runtime_error("invalid receipt frame");validateDeviceId(peer);
            if(receipts_.find(peer)==receipts_.end()&&receipts_.size()>=8)throw std::runtime_error("receipt peer limit 8");
            auto& known=receipts_[peer];for(std::size_t i=0;i<count;++i){const auto id=journal_detail::readId(in);if(!events_.contains(id)||!known.insert(id).second)throw std::runtime_error("receipt missing event or repeated");}
        }else throw std::runtime_error("unsupported journal frame type/order");
        std::string extra;if(in>>extra)throw std::runtime_error("trailing journal frame content");
    }
public:
    // Providing a node permits creation; existing identity must match. No implicit empty reset.
    explicit DeviceJournal(const std::filesystem::path& path,const std::string& node=""):file_(path,!node.empty()){
        if(!node.empty())validateDeviceId(node);
        auto bytes=file_.readAll();std::size_t position=0,frames=0;
        while(position<bytes.size()){
            const auto remaining=bytes.size()-position;if(remaining<12)break;
            const auto all=std::span<const unsigned char>(bytes);const auto magic=journal_detail::u32(all,position),length=journal_detail::u32(all,position+4);
            if(magic!=0x314c4c43||journal_detail::u32(all,position+8)!=journal_detail::crc(all.subspan(position,8))||length>131072)throw std::runtime_error("corrupt journal frame header");
            if(remaining<static_cast<std::size_t>(length)+20)break;
            const auto data=all.subspan(position+12,length);
            if(journal_detail::u32(all,position+12+length)!=journal_detail::crc(data)||journal_detail::u32(all,position+16+length)!=0x54494d43)throw std::runtime_error("corrupt committed journal frame");
            replay(std::string(data.begin(),data.end()),frames==0);++frames;position+=length+20;
        }
        if(frames==0){if(!bytes.empty()||node.empty())throw std::runtime_error("journal has no complete identity; refusing reset");
            auto out=output();out<<"CPUAI_DEVICELOG1 "<<std::quoted(node)<<'\n';commit(out.str());node_=node;
        }else{
            if(!node.empty()&&node_!=node)throw std::runtime_error("journal node identity mismatch");
            if(position<bytes.size()){recovered_=bytes.size()-position;file_.truncate(position);}
            // Sync an uncertain complete tail before returning any duplicate acknowledgement.
            file_.sync();
        }
        // Also persist directory metadata on reopen: a previous creation may have failed here.
        DurableFile::syncParent(path);
    }
    const auto& node()const{return node_;}const auto& events()const{return events_;}
    std::size_t recoveredBytes()const{return recovered_;}
    std::uint64_t bytes()const{return file_.size();}
    std::size_t peerCount()const{return receipts_.size();}
    std::uint64_t nextSequence(const std::string& session)const{validateDeviceId(session);std::uint64_t seq=0;for(const auto& [id,e]:events_){(void)e;if(id.device==node_&&id.session==session)seq=std::max(seq,id.sequence);}if(seq>=1000000000000ULL)throw std::runtime_error("event sequence limit");return seq+1;}
    std::size_t receive(const std::vector<DeviceEvent>& input){healthy();auto fresh=newEvents(input);if(fresh.empty())return 0;
        auto out=output();out<<"E "<<fresh.size()<<'\n';for(const auto& e:fresh)journal_detail::writeEvent(out,e);
        // Prepare state before I/O; after durable commit use a nonallocating map swap.
        auto next=events_;for(const auto& e:fresh)next.emplace(e.id,e);commit(out.str());events_.swap(next);return fresh.size();
    }
    bool acknowledged(const std::string& peer,const EventId& id)const{const auto p=receipts_.find(peer);return p!=receipts_.end()&&p->second.contains(id);}
    void acknowledge(const std::string& peer,const std::vector<DeviceEvent>& input){healthy();validateDeviceId(peer);if(input.size()>128)throw std::runtime_error("receipt batch limit 128");
        if(input.empty())return;
        if(!receipts_.contains(peer)&&receipts_.size()>=8)throw std::runtime_error("receipt peer limit 8");
        std::set<EventId> fresh;for(const auto& e:input){const auto it=events_.find(e.id);if(it==events_.end()||!(it->second==e))throw std::runtime_error("cannot acknowledge missing/conflicting event");if(!acknowledged(peer,e.id))fresh.insert(e.id);}
        if(fresh.empty())return;
        auto out=output();out<<"A "<<std::quoted(peer)<<' '<<fresh.size()<<'\n';for(const auto& id:fresh)journal_detail::writeId(out,id);
        auto next=receipts_;next[peer].insert(fresh.begin(),fresh.end());commit(out.str());receipts_.swap(next);
    }
    struct SyncReport{std::size_t delivered=0,duplicates=0,acknowledged=0;bool complete=false;};
    SyncReport syncTo(DeviceJournal& target,std::size_t batch=64,bool stopBeforeAck=false){healthy();target.healthy();
        if(node_==target.node_)throw std::runtime_error("sync requires different node identities");
        if(batch==0||batch>128)throw std::runtime_error("sync batch 1..128");
        std::vector<DeviceEvent> selected;
        for(const auto& [id,e]:events_){const auto it=target.events_.find(id);if(it!=target.events_.end()&&!(it->second==e))throw std::runtime_error("receiver event ID conflict");
            // Reconcile possession too: recover even if receiver restored an older snapshot.
            if(selected.size()<batch&&(!acknowledged(target.node_,id)||it==target.events_.end()))selected.push_back(e);
        }
        SyncReport r;r.delivered=target.receive(selected);r.duplicates=selected.size()-r.delivered;
        if(stopBeforeAck)return r;
        acknowledge(target.node_,selected);r.acknowledged=selected.size();r.complete=true;
        for(const auto& [id,e]:events_){(void)e;if(!target.events_.contains(id)||!acknowledged(target.node_,id)){r.complete=false;break;}}
        return r;
    }
};
}
