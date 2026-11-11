#pragma once
#include "observations.hpp"
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>

namespace cpuai {
struct AudioSample {AudioFeatures features{};std::string fingerprint;std::uint32_t rate=0;std::size_t frames=0;};
inline AudioSample readWave(const std::string& path) {
    std::ifstream in(path,std::ios::binary|std::ios::ate);
    if(!in)throw std::runtime_error("cannot open WAV");
    const auto length=in.tellg();
    if(length<44||length>2000000)throw std::runtime_error("WAV size outside 44..2000000 bytes");
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));in.seekg(0);
    if(!in.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())))throw std::runtime_error("truncated WAV");
    auto u16=[&](std::size_t p){return static_cast<std::uint32_t>(bytes.at(p))|(static_cast<std::uint32_t>(bytes.at(p+1))<<8);};
    auto u32=[&](std::size_t p){return u16(p)|(u16(p+2)<<16);};
    auto tag=[&](std::size_t p,const char* t){return p+4<=bytes.size()&&std::equal(bytes.begin()+static_cast<std::ptrdiff_t>(p),bytes.begin()+static_cast<std::ptrdiff_t>(p+4),t);};
    if(!tag(0,"RIFF")||!tag(8,"WAVE")||static_cast<std::uint64_t>(u32(4))+8!=bytes.size())throw std::runtime_error("invalid RIFF/WAVE container");
    bool format=false,data=false;std::uint32_t rate=0;std::size_t start=0,count=0;
    for(std::size_t p=12;p<bytes.size();){
        if(bytes.size()-p<8)throw std::runtime_error("truncated WAV chunk");
        const std::size_t size=u32(p+4),body=p+8;
        if(size>bytes.size()-body)throw std::runtime_error("truncated WAV chunk data");
        if(tag(p,"fmt ")){
            if(format||size<16||u16(body)!=1||u16(body+2)!=1||u16(body+12)!=2||u16(body+14)!=16)
                throw std::runtime_error("only mono PCM16 WAV supported");
            rate=u32(body+4);if(rate<8000||rate>48000||u32(body+8)!=rate*2)throw std::runtime_error("invalid sample rate/byte rate");format=true;
        }else if(tag(p,"data")){if(data||size%2)throw std::runtime_error("invalid duplicate/odd WAV data");data=true;start=body;count=size/2;}
        const auto padded=size+(size%2);if(padded>bytes.size()-body)throw std::runtime_error("missing WAV padding");p=body+padded;
    }
    if(!format||!data||count<rate/20||count>static_cast<std::size_t>(rate)*2)throw std::runtime_error("WAV duration must be 50 ms..2 s");
    AudioSample result;result.rate=rate;result.frames=count;
    std::vector<double> samples(count);double energy=0;std::size_t crossings=0;
    std::uint64_t hash=14695981039346656037ULL;
    auto hashByte=[&](unsigned char b){hash^=b;hash*=1099511628211ULL;};
    for(int i=0;i<4;++i)hashByte(static_cast<unsigned char>((rate>>(8*i))&255));
    for(std::size_t i=0;i<count;++i){const auto raw=u16(start+2*i);const auto value=raw>=32768?static_cast<int>(raw)-65536:static_cast<int>(raw);
        samples[i]=value/32768.0;energy+=samples[i]*samples[i];
        if(i&&((samples[i]>=0)!=(samples[i-1]>=0)))++crossings;
        hashByte(bytes[start+2*i]);hashByte(bytes[start+2*i+1]);
    }
    std::ostringstream id;id<<std::hex<<hash<<'-'<<rate<<'-'<<count;result.fingerprint=id.str();
    result.features[0]=std::sqrt(energy/count);result.features[1]=static_cast<double>(crossings)/(count-1);
    // Eight fixed Goertzel probes: explicit signal-processing priors, no learned encoder.
    // Hann window and energy normalization reduce sensitivity to phase and gain.
    constexpr std::array<double,8> frequencies{125,250,500,1000,2000,3000,4000,6000};
    constexpr double pi=3.14159265358979323846;double total=0;
    for(std::size_t i=0;i<count;++i)samples[i]*=0.5-0.5*std::cos(2*pi*i/(count-1));
    for(std::size_t band=0;band<frequencies.size();++band){
        if(frequencies[band]>=rate/2.0)continue;
        const double coefficient=2*std::cos(2*pi*frequencies[band]/rate);double s1=0,s2=0;
        for(std::size_t i=0;i<count;++i){
            const double s=samples[i]+coefficient*s1-s2;s2=s1;s1=s;}
        const double power=std::max(0.0,s1*s1+s2*s2-coefficient*s1*s2);
        result.features[band+2]=power;total+=power;
    }
    for(std::size_t i=2;i<10;++i)result.features[i]=total>1e-12?result.features[i]/total:0;
    validateFeatures(result.features);return result;
}
struct AudioPrediction {std::optional<std::string> label;double distance=0;std::uint64_t revision=0;};
class AudioLearner {
public:
    struct Lesson {std::string id,label,fingerprint;AudioFeatures features;};
private:
    std::vector<Lesson> lessons_;std::uint64_t revision_=0;
    static double distance(const AudioFeatures& a,const AudioFeatures& b){double sum=0;
        for(std::size_t i=0;i<a.size();++i){const double d=(a[i]-b[i])*(i==0?0.25:1);sum+=d*d;}return std::sqrt(sum);}
public:
    const auto& lessons()const{return lessons_;}std::uint64_t revision()const{return revision_;}
    bool seen(const std::string& fingerprint)const {return std::any_of(lessons_.begin(),lessons_.end(),[&](const auto& l){return l.fingerprint==fingerprint;});}
    void teach(const std::string& id,const std::string& label,const AudioSample& sample){
        if(id.empty()||id.size()>64||label.empty()||label.size()>64||sample.fingerprint.empty()||sample.fingerprint.size()>80)
            throw std::runtime_error("audio lesson IDs/labels limited to 64 bytes");
        validateFeatures(sample.features);
        auto current=std::find_if(lessons_.begin(),lessons_.end(),[&](const auto& l){return l.id==id;});
        for(const auto& l:lessons_)if(l.id!=id&&l.fingerprint==sample.fingerprint)throw std::runtime_error("duplicate PCM lesson; correct its existing ID");
        if(current==lessons_.end()&&lessons_.size()>=256)throw std::runtime_error("audio lesson limit 256");
        if(revision_==std::numeric_limits<std::uint64_t>::max())throw std::runtime_error("audio revision limit");
        Lesson l{id,label,sample.fingerprint,sample.features};if(current==lessons_.end())lessons_.push_back(l);else *current=l;++revision_;
    }
    AudioPrediction predict(const AudioSample& sample)const {
        validateFeatures(sample.features);std::map<std::string,double> distances;
        for(const auto& l:lessons_){const double d=distance(l.features,sample.features);auto it=distances.find(l.label);
            if(it==distances.end()||d<it->second)distances[l.label]=d;}
        std::vector<std::pair<double,std::string>> ranked;for(const auto& [label,d]:distances)ranked.emplace_back(d,label);
        std::sort(ranked.begin(),ranked.end());AudioPrediction p;p.revision=revision_;
        if(ranked.empty())return p;
        p.distance=ranked[0].first;
        if(p.distance<=0.30&&(ranked.size()==1||ranked[1].first-p.distance>=0.03))p.label=ranked[0].second;
        return p;
    }
    void save(const std::string& path)const {
        std::ofstream out(path);if(!out)throw std::runtime_error("cannot save audio model");
        out<<"CPUAI_AUDIO1 "<<revision_<<' '<<lessons_.size()<<'\n'<<std::setprecision(17);
        for(const auto& l:lessons_){out<<std::quoted(l.id)<<' '<<std::quoted(l.label)<<' '<<std::quoted(l.fingerprint);for(double v:l.features)out<<' '<<v;out<<'\n';}
        if(!out)throw std::runtime_error("audio model write failed");
    }
    static AudioLearner load(const std::string& path){
        std::ifstream in(path,std::ios::ate);if(!in||in.tellg()>262144)throw std::runtime_error("cannot load audio model / size limit");in.seekg(0);
        std::string magic;std::uint64_t revision;std::size_t count;
        if(!(in>>magic>>revision>>count)||magic!="CPUAI_AUDIO1"||count>256||revision<count)throw std::runtime_error("invalid audio model header");
        AudioLearner result;
        for(std::size_t i=0;i<count;++i){std::string id,label;AudioSample s;
            if(!(in>>std::quoted(id)>>std::quoted(label)>>std::quoted(s.fingerprint)))throw std::runtime_error("invalid audio lesson");
            for(auto& v:s.features)if(!(in>>v))throw std::runtime_error("invalid audio features");
            if(std::any_of(result.lessons_.begin(),result.lessons_.end(),[&](const auto& l){return l.id==id;}))throw std::runtime_error("duplicate audio ID");
            result.teach(id,label,s);
        }
        std::string extra;if(in>>extra)throw std::runtime_error("trailing audio model data");result.revision_=revision;return result;
    }
};
// Freeze by value before opening the manifest. Exams never teach or adjust thresholds.
struct AudioExamReport {std::size_t cases=0,correct=0,unknown=0;std::uint64_t revision=0;};
inline AudioExamReport audioExam(AudioLearner frozen,const std::string& path){
    std::ifstream in(path,std::ios::ate);if(!in||in.tellg()>65536)throw std::runtime_error("cannot open audio exam / size limit");in.seekg(0);
    std::string magic;if(!(in>>magic)||magic!="CPUAI_AUDIO_EXAM1")throw std::runtime_error("invalid audio exam");
    std::vector<std::pair<std::string,AudioSample>> cases;std::string label,file;
    std::string line;std::getline(in,line);
    if(line.find_first_not_of(" \t\r")!=std::string::npos)throw std::runtime_error("trailing audio exam header");
    while(std::getline(in,line)){
        if(line.find_first_not_of(" \t\r")==std::string::npos)continue;
        std::istringstream row(line);std::string extra;
        if(!(row>>std::quoted(label)>>std::quoted(file))||(row>>extra)||label.empty()||label.size()>64||cases.size()>=128)
            throw std::runtime_error("invalid audio exam case");
        auto sample=readWave(file);
        if(frozen.seen(sample.fingerprint)||std::any_of(cases.begin(),cases.end(),[&](const auto& c){return c.second.fingerprint==sample.fingerprint;}))
            throw std::runtime_error("audio exam overlaps teaching or repeats PCM");
        cases.emplace_back(label,std::move(sample));
    }
    if(!in.eof()||cases.size()<3)throw std::runtime_error("audio exam requires 3..128 cases");
    AudioExamReport r;r.revision=frozen.revision();r.cases=cases.size();
    for(const auto& [expected,sample]:cases){const auto p=frozen.predict(sample);if(!p.label)++r.unknown;else if(*p.label==expected)++r.correct;}
    return r;
}
}
