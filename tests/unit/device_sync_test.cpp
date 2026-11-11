#include "cpuai/device_sync.hpp"
#include <fstream>
#include <iostream>
using namespace cpuai;
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}require(failed,"expected rejection");}
std::vector<unsigned char> read(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void write(const std::filesystem::path& p,std::span<const unsigned char> data){std::ofstream out(p,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));}
DeviceEvent event(std::uint64_t seq){DeviceEvent e;e.id={"watch","session-one",seq};e.recordedAtMs=1800000000000LL+static_cast<std::int64_t>(seq);e.sample=analyzePcm16(std::vector<std::int16_t>(800,1000),8000);e.startFrame=(seq-1)*800;return e;}
int main(){
    const auto root=std::filesystem::temp_directory_path()/("cpuai-sync-"+newSessionId());std::filesystem::create_directory(root);
    struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove_all(path,ec);}} cleanup{root};
    const auto a=root/"a.log",b=root/"b.log";auto first=event(1),second=event(2);
    {
        DeviceJournal source(a,"watch"),target(b,"phone");
        require(source.receive({first,second})==2,"atomic insert");const auto size=source.bytes();
        require(source.receive({first})==0&&source.bytes()==size,"duplicate is not a write");
        auto conflicting=first;conflicting.userLabel="different";
        rejects([&]{target.receive({second,first,conflicting});});require(target.events().empty(),"conflicting batch does not partially commit");
        rejects([&]{source.receive({conflicting});});require(source.events().at(first.id)==first,"immutable event ID");
        rejects([&]{DeviceJournal locked(a);});
        auto r=source.syncTo(target,128,true);require(r.delivered==2&&!r.complete&&!source.acknowledged("phone",first.id),"disconnect after receiver commit before receipt");
    }
    {
        DeviceJournal source(a),target(b);auto r=source.syncTo(target);require(r.delivered==0&&r.duplicates==2&&r.complete,"reopen retry is idempotent");
        require(source.acknowledged("phone",first.id),"receipt persisted");const auto size=source.bytes();r=source.syncTo(target);require(r.complete&&r.delivered==0&&r.acknowledged==0&&source.bytes()==size,"repeat sync no journal growth");
    }
    {
        DeviceJournal source(a),target(b);require(source.acknowledged("phone",first.id)&&target.events().size()==2,"both sides survive restart");
        auto third=event(3);third.userLabel="user label";source.receive({third});auto fourth=event(4);source.receive({fourth});
        auto r=source.syncTo(target,1);require(!r.complete&&r.delivered==1,"bounded sync batch");r=source.syncTo(target,1);require(r.complete&&r.delivered==1,"next bounded batch");
        rejects([&]{source.syncTo(target,0);});rejects([&]{source.syncTo(target,129);});
    }
    // A restored old receiver must be reconciled even if the sender already has receipts.
    const auto restored=root/"restored.log";std::vector<unsigned char> old;
    {DeviceJournal t(restored,"replacement-phone");}
    old=read(restored);
    {DeviceJournal source(a),t(restored);require(source.syncTo(t).delivered==4,"initial delivery to replacement peer");}
    write(restored,old);
    {DeviceJournal source(a),t(restored);require(source.syncTo(t).delivered==4&&t.events().size()==4,"restored receiver possession checked");}
    // Committed prefix + torn, unacknowledged last frame: preserve prefix and truncate only tail.
    const auto base=root/"base.log";std::vector<unsigned char> prefix,full;
    {DeviceJournal s(base,"watch");s.receive({first});}
    prefix=read(base);
    {DeviceJournal s(base);s.receive({second});}
    full=read(base);
    const auto extra=full.size()-prefix.size();
    for(const auto cut:std::vector<std::size_t>{1,4,8,11,12,20,31,extra-1}){
        const auto p=root/("torn-"+std::to_string(cut)+".log");write(p,std::span(full).first(prefix.size()+cut));
        {DeviceJournal s(p);require(s.events().size()==1&&s.recoveredBytes()==cut&&s.bytes()==prefix.size(),"torn tail preserves confirmed prefix");s.receive({second});}
        {DeviceJournal s(p);require(s.events().size()==2&&s.recoveredBytes()==0,"append after recovery");}
    }
    // Complete damaged records are corruption, not a reason to discard acknowledged history.
    for(auto at:std::vector<std::size_t>{prefix.size(),prefix.size()+12,full.size()-1}){
        auto damaged=full;damaged[at]^=1;const auto p=root/"corrupt.log";write(p,damaged);
        rejects([&]{DeviceJournal s(p);});require(read(p)==damaged,"corruption remains intact for inspection");
    }
    const auto wrong=root/"wrong.log";write(wrong,prefix);rejects([&]{DeviceJournal s(wrong,"another-node");});require(read(wrong)==prefix,"identity mismatch cannot rewrite history");
    rejects([&]{DeviceJournal missing(root/"missing.log");});
    auto bad=first;bad.sample.features[0]=2;rejects([&]{validateDeviceEvent(bad);});
    rejects([]{validateDeviceId("../phone");});
    // Out-of-order receiver delivery is permitted: no high-water-only deduplication.
    const auto order=root/"order.log";
    {DeviceJournal s(order,"watch");s.receive({second});s.receive({first});require(s.nextSequence("session-one")==3,"out-of-order delivery retains both events");
        auto third=event(3);s.receive({third});for(int i=0;i<8;++i)s.acknowledge("peer-"+std::to_string(i),{first});
        rejects([&]{s.acknowledge("peer-nine",{first});});
        std::vector<DeviceEvent> oversized(129,first);rejects([&]{s.receive(oversized);});
    }
    const auto origins=root/"origins.log";
    {DeviceJournal s(origins,"watch");std::vector<DeviceEvent> input;
        for(int i=0;i<64;++i){auto e=first;e.id.session="session-"+std::to_string(i);input.push_back(e);}s.receive(input);
        auto e=first;e.id.session="session-extra";const auto bytes=s.bytes();rejects([&]{s.receive({e});});require(s.bytes()==bytes,"origin bound preserves journal");
    }
    const auto capacity=root/"capacity.log";
    {DeviceJournal s(capacity,"watch");for(int batch=0;batch<32;++batch){std::vector<DeviceEvent> input;
        for(int i=1;i<=128;++i)input.push_back(event(static_cast<std::uint64_t>(batch*128+i)));
        s.receive(input);}
        require(s.events().size()==4096,"event capacity reached");const auto bytes=s.bytes();rejects([&]{s.receive({event(4097)});});require(s.bytes()==bytes,"full journal does not evict confirmed history");
    }
    const auto huge=root/"huge.log";write(huge,{});std::filesystem::resize_file(huge,17*1024*1024);rejects([&]{DeviceJournal s(huge);});
    std::cout<<"device sync tests passed: durable prefix, torn tails, corruption, conflicts, reconnect, receipts and bounds\n";
}
