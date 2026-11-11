#include "cpuai/observations.hpp"
#include <iostream>
#include <limits>
using namespace cpuai;
void require(bool ok){if(!ok)throw std::runtime_error("observation assertion");}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::runtime_error&){failed=true;}require(failed);}
int main(){
    ObservationStream s(3,100);Observation a{"watch",1,1000,-10,4,{}};
    require(s.push(a)==StreamStatus::accepted);
    auto b=a;b.source="phone";b.timestampMs=970;b.offsetMs=20;b.uncertaintyMs=7;
    require(s.push(b)==StreamStatus::accepted);require(s.align("watch","phone",10).empty());
    require(s.align("watch","phone",11).size()==1);
    require(s.push(a)==StreamStatus::duplicateOrReordered);
    auto late=a;late.sequence=2;late.timestampMs=899;require(s.push(late)==StreamStatus::late);
    auto earlier=a;earlier.sequence=2;earlier.timestampMs=950;require(s.push(earlier)==StreamStatus::accepted);
    require(s.events().front().sequence==2);
    auto retry=a;retry.sequence=3;require(s.push(retry)==StreamStatus::full);
    require(s.pop());require(s.push(retry)==StreamStatus::accepted); // rejected sequence was not acknowledged
    while(s.pop()){}require(s.push(a)==StreamStatus::duplicateOrReordered); // watermarks survive drain
    require(s.metrics().accepted==4&&s.metrics().rejected==4&&s.metrics().peak==3);
    rejects([&]{auto bad=a;bad.timestampMs=std::numeric_limits<std::int64_t>::max();s.push(bad);});
    rejects([&]{auto bad=a;bad.features[0]=std::numeric_limits<double>::quiet_NaN();s.push(bad);});
    rejects([&]{s.align("watch","watch",1);});rejects([]{ObservationStream bad(0);});
    ObservationStream many;for(int i=0;i<16;++i){auto o=a;o.source=std::to_string(i);require(many.push(o)==StreamStatus::accepted);}
    auto extra=a;extra.source="seventeen";require(many.push(extra)==StreamStatus::sourceLimit);
    std::cout<<"observation stream tests passed\n";
}
