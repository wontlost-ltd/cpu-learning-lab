#include "cpuai/audio_stream.hpp"
#include "cpuai/device_sync.hpp"
#include "stdin_input.hpp"
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#ifdef CPUAI_WITH_PORTAUDIO
#include "portaudio_input.hpp"
#endif

namespace {
volatile std::sig_atomic_t interrupted=0;
void stop(int){interrupted=1;}
std::uint64_t number(const char* text,std::uint64_t maximum){
    const std::string value(text);if(value.empty()||value.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("expected unsigned integer");
    std::size_t used=0;const auto n=std::stoull(value,&used);if(used!=value.size()||n>maximum)throw std::runtime_error("argument outside limit");return n;
}
// JSON byte escaping preserves UTF-8 names while handling control characters.
std::string json(const std::string& s){
    constexpr char hex[]="0123456789abcdef";std::string out="\"";
    for(unsigned char c:s){if(c=='"'||c=='\\'){out+='\\';out+=static_cast<char>(c);}else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=static_cast<char>(c);}
    return out+'"';
}
}
int main(int argc,char** argv)try{
    if(argc==2&&std::string(argv[1])=="--capabilities"){
#ifdef CPUAI_WITH_PORTAUDIO
        std::cout<<"{\"microphone\":true,\"stdin_pcm16le\":true}\n";
#else
        std::cout<<"{\"microphone\":false,\"stdin_pcm16le\":true}\n";
#endif
        return 0;
    }
    if(argc==2&&std::string(argv[1])=="--help"){
        std::cout<<"cpu-listen --stdin|--microphone RATE WINDOW_MS MODEL MAX_WINDOWS [--capture NEW_DIR MAX_FILES] [--reload] [--journal PATH DEVICE]\n"
            <<"MODEL '-' starts untaught. Input: mono signed PCM16LE (stdin) or default PortAudio device.\n"
            <<"Limits: RATE 8000..48000, WINDOW_MS 50..2000, MAX_WINDOWS 1..1000000, MAX_FILES 1..256.\n"
            <<"JSON lines on stdout. No raw recordings unless --capture. --reload checks explicit MODEL at window boundaries.\n";
        return 0;
    }
    if(argc<6)throw std::runtime_error("see cpu-listen --help");
    const std::string mode=argv[1],modelPath=argv[4];
    if(mode!="--stdin"&&mode!="--microphone")throw std::runtime_error("choose --stdin or --microphone");
#ifndef CPUAI_WITH_PORTAUDIO
    if(mode=="--microphone")throw std::runtime_error("microphone adapter not built; enable CPUAI_WITH_PORTAUDIO in CMake");
#endif
    const auto rate=static_cast<std::uint32_t>(number(argv[2],48000));
    const auto windowMs=static_cast<unsigned>(number(argv[3],2000));
    const auto maximum=number(argv[5],1000000);if(maximum==0)throw std::runtime_error("MAX_WINDOWS must be positive");
    std::filesystem::path journalPath;std::string device;
    std::filesystem::path capture;std::size_t maxFiles=0;bool reload=false;
    for(int i=6;i<argc;++i){const std::string arg=argv[i];
        if(arg=="--journal"&&journalPath.empty()&&i+2<argc){journalPath=argv[++i];device=argv[++i];if(journalPath.empty())throw std::runtime_error("empty journal path");}
        else if(arg=="--reload"&&!reload){reload=true;}
        else if(arg=="--capture"&&capture.empty()&&i+2<argc){capture=argv[++i];maxFiles=static_cast<std::size_t>(number(argv[++i],256));if(capture.empty()||maxFiles==0)throw std::runtime_error("invalid capture settings");}
        else throw std::runtime_error("invalid or duplicate option");
    }
    if(reload&&modelPath=="-")throw std::runtime_error("--reload requires model file");
    cpuai::PcmWindowStream pipeline(rate,windowMs,2);
    auto model=modelPath=="-"?cpuai::AudioLearner{}:cpuai::AudioLearner::load(modelPath);
    if(!capture.empty()&&!std::filesystem::create_directory(capture))throw std::runtime_error("capture directory must be new; refusing overwrite");
    std::optional<cpuai::DeviceJournal> journal;std::string session;
    if(!journalPath.empty()){cpuai::validateDeviceId(device);journal.emplace(journalPath,device);session=cpuai::newSessionId();}
    std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
    std::optional<cpuai::StdinPcmInput> input;
    if(mode=="--stdin")input.emplace();
#ifdef CPUAI_WITH_PORTAUDIO
    std::optional<cpuai::MicrophoneInput> microphone;
    if(mode=="--microphone")microphone.emplace(rate);
#endif
    using Clock=std::chrono::steady_clock;
    std::uint64_t emitted=0,overflows=0,discardedCaptureFrames=0,reloadFailures=0,files=0;
    double totalProcessingMs=0,maxProcessingMs=0;
    std::array<std::int16_t,256> block{};
    bool eof=false;std::string end="eof";
    while(!interrupted&&emitted<maximum){
        std::size_t count=0;
#ifdef CPUAI_WITH_PORTAUDIO
        if(microphone){
            const auto status=microphone->read(block,interrupted);
            if(status==cpuai::MicrophoneStatus::stopped)break;
            if(status==cpuai::MicrophoneStatus::overflow){pipeline.discontinuity();++overflows;discardedCaptureFrames+=block.size();
                std::cout<<"{\"type\":\"discontinuity\",\"reason\":\"device_overflow\",\"missing_frames\":null}"<<std::endl;continue;}
            count=block.size();
        }else
#endif
        {
            const auto read=input->read(block,interrupted);
            if(read.status==cpuai::InputStatus::stopped)break;
            eof=read.status==cpuai::InputStatus::eof;count=read.frames;
        }
        std::size_t offset=0;
        while(offset<count&&!interrupted&&emitted<maximum){
            const auto begin=Clock::now();
            // At most one feature window per slice; service output and feedback between windows.
            const auto fed=pipeline.feed(std::span<const std::int16_t>(block.data()+offset,count-offset),{},1);offset+=fed.consumed;
            while(auto window=pipeline.pop()){
                if(reload){
                    try{auto next=cpuai::AudioLearner::load(modelPath);model=std::move(next);}
                    catch(const std::exception&){++reloadFailures;} // Preserve the prior snapshot on partial writes.
                }
                const auto prediction=model.predict(window->sample);
                const double elapsed=std::chrono::duration<double,std::milli>(Clock::now()-begin).count();
                totalProcessingMs+=elapsed;maxProcessingMs=std::max(maxProcessingMs,elapsed);
                std::string captured;
                if(files<maxFiles){const auto path=capture/("window-"+std::to_string(window->sequence)+".wav");
                    cpuai::writeWave(path.string(),window->pcm,rate);captured=path.generic_string();++files;}
                std::string eventId;
                if(journal){cpuai::DeviceEvent event;event.id={device,session,window->sequence};
                    event.recordedAtMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                    event.segment=window->segment;event.startFrame=window->startFrame;event.sample=window->sample;
                    journal->receive({event});eventId=device+"/"+session+"/"+std::to_string(event.id.sequence);}
                std::cout<<"{\"type\":\"prediction\",\"sequence\":"<<window->sequence<<",\"segment\":"<<window->segment
                    <<",\"start_frame\":"<<window->startFrame<<",\"frames\":"<<window->sample.frames
                    <<",\"revision\":"<<prediction.revision<<",\"label\":"<<(prediction.label?json(*prediction.label):"null")
                    <<",\"distance\":"<<prediction.distance<<",\"fingerprint\":"<<json(window->sample.fingerprint)
                    <<",\"durable_event\":"<<(eventId.empty()?"null":json(eventId))
                    <<",\"processing_ms\":"<<elapsed<<",\"capture\":"<<(captured.empty()?"null":json(captured))<<"}"<<std::endl;
                ++emitted;
            }
            if(fed.consumed==0&&fed.windows==0)throw std::runtime_error("audio pipeline made no progress");
        }
        discardedCaptureFrames+=count-offset;
        if(eof)break;
    }
    if(interrupted)end="signal";else if(emitted>=maximum)end="window_limit";
    pipeline.finish();const auto& m=pipeline.metrics();
    std::cout<<"{\"type\":\"summary\",\"end\":"<<json(end)<<",\"windows\":"<<emitted
        <<",\"consumed_frames\":"<<m.consumedFrames<<",\"discarded_partial_frames\":"<<m.discardedPartialFrames
        <<",\"discarded_capture_frames\":"<<discardedCaptureFrames<<",\"device_overflows\":"<<overflows
        <<",\"reload_failures\":"<<reloadFailures<<",\"captures\":"<<files
        <<",\"processing_total_ms\":"<<totalProcessingMs<<",\"processing_max_ms\":"<<maxProcessingMs<<"}"<<std::endl;
    return 0;
}catch(const std::exception& e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}
