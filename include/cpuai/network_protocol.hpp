#pragma once
#include "device_sync.hpp"
namespace cpuai {
inline std::string encodeBatch(const std::vector<DeviceEvent>& events){
    if(events.empty()||events.size()>128)throw std::runtime_error("network batch requires 1..128 events");
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17)<<"CPUAI_BATCH1 "<<events.size()<<'\n';
    for(const auto& e:events){validateDeviceEvent(e);journal_detail::writeEvent(out,e);}return out.str();
}
inline std::vector<DeviceEvent> decodeBatch(const std::string& payload,const std::string& authorizedDevice){
    if(payload.size()>131072)throw std::runtime_error("network payload limit");
    validateDeviceId(authorizedDevice);
    std::istringstream in(payload);in.imbue(std::locale::classic());std::string magic;std::size_t count;
    if(!(in>>magic>>count)||magic!="CPUAI_BATCH1"||count==0||count>128)throw std::runtime_error("unsupported network batch");
    std::vector<DeviceEvent> events;std::set<EventId> ids;
    for(std::size_t i=0;i<count;++i){auto e=journal_detail::readEvent(in);if(e.id.device!=authorizedDevice||!ids.insert(e.id).second)throw std::runtime_error("unauthorized origin or duplicate batch ID");events.push_back(std::move(e));}
    std::string extra;if(in>>extra)throw std::runtime_error("trailing network payload");return events;
}
}
