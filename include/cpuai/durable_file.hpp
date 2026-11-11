#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace cpuai {
// Exclusive, single-owner local journal. Never copy or concurrently access this handle.
class DurableFile {
#ifdef _WIN32
    HANDLE handle_=INVALID_HANDLE_VALUE;
#else
    int fd_=-1;
#endif
    std::uint64_t size_=0;
    void close()noexcept{
#ifdef _WIN32
        if(handle_!=INVALID_HANDLE_VALUE){CloseHandle(handle_);handle_=INVALID_HANDLE_VALUE;}
#else
        if(fd_>=0){::close(fd_);fd_=-1;}
#endif
    }
public:
    explicit DurableFile(const std::filesystem::path& path,bool create){
        try{
#ifdef _WIN32
            handle_=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,create?OPEN_ALWAYS:OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(handle_==INVALID_HANDLE_VALUE)throw std::runtime_error("cannot open or lock journal");
            LARGE_INTEGER size;if(!GetFileSizeEx(handle_,&size)||size.QuadPart<0)throw std::runtime_error("cannot stat journal");size_=static_cast<std::uint64_t>(size.QuadPart);
#else
            fd_=::open(path.c_str(),O_RDWR|O_CLOEXEC|(create?O_CREAT:0),0600);
            if(fd_<0||flock(fd_,LOCK_EX|LOCK_NB)!=0)throw std::runtime_error("cannot open or lock journal");
            struct stat st{};if(fstat(fd_,&st)!=0||!S_ISREG(st.st_mode)||st.st_size<0)throw std::runtime_error("journal must be a regular file");size_=static_cast<std::uint64_t>(st.st_size);
#endif
            if(size_>16*1024*1024)throw std::runtime_error("journal size limit 16 MiB");
        }catch(...){close();throw;}
    }
    ~DurableFile(){close();}
    DurableFile(const DurableFile&)=delete;DurableFile& operator=(const DurableFile&)=delete;
    std::uint64_t size()const{return size_;}
    void seek(std::uint64_t offset){
#ifdef _WIN32
        LARGE_INTEGER n;n.QuadPart=static_cast<LONGLONG>(offset);if(!SetFilePointerEx(handle_,n,nullptr,FILE_BEGIN))throw std::runtime_error("journal seek failed");
#else
        if(lseek(fd_,static_cast<off_t>(offset),SEEK_SET)<0)throw std::runtime_error("journal seek failed");
#endif
    }
    std::vector<unsigned char> readAll(){
        seek(0);std::vector<unsigned char> bytes(static_cast<std::size_t>(size_));std::size_t done=0;
        while(done<bytes.size()){
#ifdef _WIN32
            DWORD n=0;if(!ReadFile(handle_,bytes.data()+done,static_cast<DWORD>(bytes.size()-done),&n,nullptr)||n==0)throw std::runtime_error("journal read failed");
#else
            const auto n=::read(fd_,bytes.data()+done,bytes.size()-done);if(n<0&&errno==EINTR)continue;
            if(n<=0)throw std::runtime_error("journal read failed");
#endif
            done+=static_cast<std::size_t>(n);
        }
        return bytes;
    }
    void append(std::span<const unsigned char> bytes){
        if(bytes.size()>16*1024*1024-size_)throw std::runtime_error("journal size limit 16 MiB");
        seek(size_);std::size_t done=0;
        while(done<bytes.size()){
#ifdef _WIN32
            DWORD n=0;if(!WriteFile(handle_,bytes.data()+done,static_cast<DWORD>(bytes.size()-done),&n,nullptr)||n==0)throw std::runtime_error("journal write failed");
#else
            const auto n=::write(fd_,bytes.data()+done,bytes.size()-done);if(n<0&&errno==EINTR)continue;
            if(n<=0)throw std::runtime_error("journal write failed");
#endif
            done+=static_cast<std::size_t>(n);
        }
        size_+=bytes.size();
    }
    void sync(){
#ifdef _WIN32
        if(!FlushFileBuffers(handle_))throw std::runtime_error("journal flush failed");
#elif defined(__APPLE__)
        int result;do{result=fcntl(fd_,F_FULLFSYNC);}while(result<0&&errno==EINTR);
        if(result<0)throw std::runtime_error("journal full sync unsupported or failed");
#else
        int result;do{result=fsync(fd_);}while(result<0&&errno==EINTR);
        if(result<0)throw std::runtime_error("journal sync failed");
#endif
    }
    void truncate(std::uint64_t offset){
        seek(offset);
#ifdef _WIN32
        if(!SetEndOfFile(handle_))throw std::runtime_error("journal recovery truncate failed");
#else
        if(ftruncate(fd_,static_cast<off_t>(offset))!=0)throw std::runtime_error("journal recovery truncate failed");
#endif
        size_=offset;sync();
    }
    static void syncParent(const std::filesystem::path& path){
#ifndef _WIN32
        const auto parent=path.has_parent_path()?path.parent_path():std::filesystem::path(".");
        const int fd=::open(parent.c_str(),O_RDONLY|O_CLOEXEC);if(fd<0)throw std::runtime_error("cannot open journal directory");
        int result;do{result=fsync(fd);}while(result<0&&errno==EINTR);::close(fd);
        if(result<0)throw std::runtime_error("journal directory sync failed");
#else
        (void)path; // FlushFileBuffers file path; directory fsync is not provided here.
#endif
    }
};
}
