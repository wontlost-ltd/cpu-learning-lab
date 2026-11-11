#pragma once
#include <portaudio.h>
#include <array>
#include <cstdint>
#include <csignal>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <string>

namespace cpuai {
// Blocking native adapter. No inference, file I/O or user feedback on an audio callback.
enum class MicrophoneStatus {data,overflow,stopped};
class MicrophoneInput {
    PaStream* stream_=nullptr;
    bool initialized_=false;
    static void check(PaError err){if(err!=paNoError)throw std::runtime_error(std::string("PortAudio: ")+Pa_GetErrorText(err));}
public:
    explicit MicrophoneInput(std::uint32_t rate){
        try{
            check(Pa_Initialize());initialized_=true;
            check(Pa_OpenDefaultStream(&stream_,1,0,paInt16,rate,256,nullptr,nullptr));
            check(Pa_StartStream(stream_));
        }catch(...){close();throw;}
    }
    MicrophoneInput(const MicrophoneInput&)=delete;
    MicrophoneInput& operator=(const MicrophoneInput&)=delete;
    ~MicrophoneInput(){close();}
    void close()noexcept{
        if(stream_){Pa_AbortStream(stream_);Pa_CloseStream(stream_);stream_=nullptr;}
        if(initialized_){Pa_Terminate();initialized_=false;}
    }
    // On overflow, discard this block and notify caller: missing duration is unknown.
    MicrophoneStatus read(std::array<std::int16_t,256>& block,const volatile std::sig_atomic_t& stop){
        while(!stop){
            const auto available=Pa_GetStreamReadAvailable(stream_);
            if(available<0)check(static_cast<PaError>(available));
            if(available>=static_cast<long>(block.size()))break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if(stop)return MicrophoneStatus::stopped;
        const auto error=Pa_ReadStream(stream_,block.data(),static_cast<unsigned long>(block.size()));
        if(error==paInputOverflowed)return MicrophoneStatus::overflow;
        check(error);return MicrophoneStatus::data;
    }
};
}
