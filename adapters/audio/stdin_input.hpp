#pragma once
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdint>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

namespace cpuai {
enum class InputStatus {data,eof,stopped};
struct InputResult {InputStatus status;std::size_t frames;};
// Byte fragmentation is transport framing, not a truncated sample until actual EOF.
// Polling allows cooperative SIGINT/SIGTERM shutdown while a pipe stays open but idle.
class StdinPcmInput {
    std::array<unsigned char,513> bytes_{};std::size_t carried_=0;
public:
    StdinPcmInput(){
#ifdef _WIN32
        if(_setmode(_fileno(stdin),_O_BINARY)==-1)throw std::runtime_error("cannot set binary stdin");
#endif
    }
    InputResult read(std::array<std::int16_t,256>& output,const volatile std::sig_atomic_t& stop){
        while(!stop){
            int count=0;
#ifdef _WIN32
            const HANDLE handle=reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stdin)));
            const auto kind=GetFileType(handle);
            unsigned long available=512;
            if(kind==FILE_TYPE_PIPE){
                if(!PeekNamedPipe(handle,nullptr,0,nullptr,&available,nullptr)){
                    if(GetLastError()==ERROR_BROKEN_PIPE)available=0;else throw std::runtime_error("PCM pipe polling failed");
                    if(carried_)throw std::runtime_error("truncated PCM16 sample at EOF");
                    return {InputStatus::eof,0};
                }
                if(available==0){Sleep(20);continue;}
            }else if(kind!=FILE_TYPE_DISK)throw std::runtime_error("PCM stdin requires a pipe or regular file");
            const auto request=static_cast<unsigned>(available<512-carried_?available:512-carried_);
            count=_read(_fileno(stdin),bytes_.data()+carried_,request);
#else
            pollfd descriptor{STDIN_FILENO,POLLIN,0};const int ready=::poll(&descriptor,1,20);
            if(ready<0){if(errno==EINTR)continue;throw std::runtime_error("PCM input polling failed");}
            if(ready==0)continue;
            const auto n=::read(STDIN_FILENO,bytes_.data()+carried_,512-carried_);
            count=static_cast<int>(n);
#endif
            if(count<0){if(errno==EINTR)continue;throw std::runtime_error("PCM input read failed");}
            if(count==0){if(carried_)throw std::runtime_error("truncated PCM16 sample at EOF");return {InputStatus::eof,0};}
            const auto size=static_cast<std::size_t>(count)+carried_,frames=size/2;
            for(std::size_t i=0;i<frames;++i){const unsigned value=bytes_[2*i]|(static_cast<unsigned>(bytes_[2*i+1])<<8);
                output[i]=static_cast<std::int16_t>(value>=32768?static_cast<int>(value)-65536:static_cast<int>(value));}
            carried_=size%2;if(carried_)bytes_[0]=bytes_[size-1];
            if(frames)return {InputStatus::data,frames};
        }
        return {InputStatus::stopped,0};
    }
};
}
