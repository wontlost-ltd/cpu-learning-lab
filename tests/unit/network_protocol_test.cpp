#include "cpuai/network_protocol.hpp"
#include <iostream>
using namespace cpuai;
void require(bool value){if(!value)throw std::runtime_error("network protocol assertion");}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}require(failed);}
int main(){DeviceEvent e;e.id={"watch","recording",1};e.recordedAtMs=1800000000000LL;e.sample=analyzePcm16(std::vector<std::int16_t>(800),8000);e.userLabel="user label";
    const auto payload=encodeBatch({e});require(decodeBatch(payload,"watch").at(0)==e);
    rejects([&]{decodeBatch(payload,"another-device");});rejects([&]{decodeBatch(payload+"junk","watch");});
    rejects([&]{decodeBatch("CPUAI_BATCH2 1\n","watch");});rejects([&]{decodeBatch("CPUAI_BATCH1 129\n","watch");});
    rejects([&]{decodeBatch("CPUAI_BATCH1 0\n","watch");});rejects([&]{decodeBatch(payload.substr(0,payload.size()/2),"watch");});
    rejects([&]{decodeBatch(encodeBatch({e,e}),"watch");});rejects([&]{encodeBatch({});});
    std::cout<<"network protocol tests passed: version, origin binding, bounds, duplicates and truncation\n";
}
