#include <algorithm>
#pragma once
#include "RegionWalReader.h"
#include <filesystem>
#include <fstream>
#include <utility>
#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <cerrno>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace aiverse::world {
enum class RegionWalWrite { Ok, Unsupported, Busy, IoError, InvalidSequence, InvalidFrame, NeedsRecovery };
class RegionWalWriter {
public:
    RegionWalWriter()=default;
    RegionWalWriter(const RegionWalWriter&)=delete;
    RegionWalWriter& operator=(const RegionWalWriter&)=delete;
    ~RegionWalWriter(){close();}
    RegionWalWrite open(const std::filesystem::path& path,uint64_t previous=0,bool repairIncompleteTail=false,std::span<const uint8_t> identity={}) {
        close();
#if defined(__unix__) || defined(__APPLE__)
        fd_=::open(path.c_str(),O_RDWR|O_APPEND|O_CREAT|O_CLOEXEC,0600);
        if(fd_<0)return RegionWalWrite::IoError;
        if(::flock(fd_,LOCK_EX|LOCK_NB)!=0){auto e=errno;close();return e==EWOULDBLOCK?RegionWalWrite::Busy:RegionWalWrite::IoError;}
        std::ifstream input(path,std::ios::binary);
        if(!input){close();return RegionWalWrite::IoError;}
        if(!identity.empty()) {
            std::vector<uint8_t> actual(identity.size());
            input.read(reinterpret_cast<char*>(actual.data()),actual.size());
            if(!input||!std::equal(actual.begin(),actual.end(),identity.begin())){close();return RegionWalWrite::NeedsRecovery;}
        }
        auto scan=RegionWalReader::scan(input,previous,[](const RegionWalFrame&){return true;});
        if(scan.status==RegionWalScan::Incomplete && repairIncompleteTail) {
            if((scan.validBytes+identity.size())>uint64_t(std::numeric_limits<off_t>::max()) ||
               ::ftruncate(fd_,static_cast<off_t>((scan.validBytes+identity.size())))!=0 || !syncFd(fd_)) {
                close();return RegionWalWrite::IoError;
            }
        } else if(scan.status!=RegionWalScan::End){close();return RegionWalWrite::NeedsRecovery;}
        // Persist directory entry before accepting any durable append.
        auto parent=path.parent_path();if(parent.empty())parent=".";
        int dir=::open(parent.c_str(),O_RDONLY|O_CLOEXEC);
        if(dir<0){close();return RegionWalWrite::IoError;}
        bool synced=syncFd(dir);::close(dir);
        if(!synced){close();return RegionWalWrite::IoError;}
        last_=scan.lastSequence;return RegionWalWrite::Ok;
#elif defined(_WIN32)
        handle_=::CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,
            nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle_==INVALID_HANDLE_VALUE)return ::GetLastError()==ERROR_SHARING_VIOLATION?RegionWalWrite::Busy:RegionWalWrite::IoError;
        std::ifstream input(path,std::ios::binary);
        if(!input){close();return RegionWalWrite::IoError;}
        if(!identity.empty()) {
            std::vector<uint8_t> actual(identity.size());
            input.read(reinterpret_cast<char*>(actual.data()),actual.size());
            if(!input||!std::equal(actual.begin(),actual.end(),identity.begin())){close();return RegionWalWrite::NeedsRecovery;}
        }
        auto scan=RegionWalReader::scan(input,previous,[](const RegionWalFrame&){return true;});
        input.close();
        if(scan.status==RegionWalScan::Incomplete && repairIncompleteTail){
            LARGE_INTEGER pos;pos.QuadPart=static_cast<LONGLONG>((scan.validBytes+identity.size()));
            if((scan.validBytes+identity.size())>uint64_t(INT64_MAX)||!::SetFilePointerEx(handle_,pos,nullptr,FILE_BEGIN)||
               !::SetEndOfFile(handle_)||!::FlushFileBuffers(handle_)){close();return RegionWalWrite::IoError;}
        }else if(scan.status!=RegionWalScan::End){close();return RegionWalWrite::NeedsRecovery;}
        LARGE_INTEGER zero{};
        if(!::SetFilePointerEx(handle_,zero,nullptr,FILE_END)){close();return RegionWalWrite::IoError;}
        last_=scan.lastSequence;return RegionWalWrite::Ok;
#else
        (void)path;(void)previous;(void)repairIncompleteTail;(void)identity;return RegionWalWrite::Unsupported;
#endif
    }
    RegionWalWrite append(const RegionWalFrame& frame) {
#if defined(__unix__) || defined(__APPLE__)
        if(fd_<0 || poisoned_)return RegionWalWrite::NeedsRecovery;
        if(last_==UINT64_MAX || frame.sequence!=last_+1)return RegionWalWrite::InvalidSequence;
        std::vector<uint8_t> bytes;
        if(!RegionWalCodec::encode(frame,bytes))return RegionWalWrite::InvalidFrame;
        size_t offset=0;
        while(offset<bytes.size()){
            auto n=::write(fd_,bytes.data()+offset,bytes.size()-offset);
            if(n<0 && errno==EINTR)continue;
            if(n<=0){poisoned_=true;return RegionWalWrite::IoError;}
            offset+=size_t(n);
        }
        if(!syncData(fd_)){poisoned_=true;return RegionWalWrite::IoError;}
        last_=frame.sequence;return RegionWalWrite::Ok;
#elif defined(_WIN32)
        if(handle_==INVALID_HANDLE_VALUE||poisoned_)return RegionWalWrite::NeedsRecovery;
        if(last_==UINT64_MAX||frame.sequence!=last_+1)return RegionWalWrite::InvalidSequence;
        std::vector<uint8_t> bytes;
        if(!RegionWalCodec::encode(frame,bytes))return RegionWalWrite::InvalidFrame;
        size_t offset=0;
        while(offset<bytes.size()){
            DWORD written=0;
            if(!::WriteFile(handle_,bytes.data()+offset,static_cast<DWORD>(bytes.size()-offset),&written,nullptr)||!written){
                poisoned_=true;return RegionWalWrite::IoError;
            }
            offset+=written;
        }
        if(!::FlushFileBuffers(handle_)){poisoned_=true;return RegionWalWrite::IoError;}
        last_=frame.sequence;return RegionWalWrite::Ok;
#else
        (void)frame;return RegionWalWrite::Unsupported;
#endif
    }
    uint64_t lastSequence()const{return last_;}
    void close(){
#if defined(__unix__) || defined(__APPLE__)
        if(fd_>=0)::close(fd_);
#elif defined(_WIN32)
        if(handle_!=INVALID_HANDLE_VALUE)::CloseHandle(handle_);
        handle_=INVALID_HANDLE_VALUE;
#endif
        fd_=-1;last_=0;poisoned_=false;
    }
private:
#if defined(__unix__) || defined(__APPLE__)
    static bool syncData(int fd){
#if defined(__APPLE__) && defined(F_FULLFSYNC)
        int r;do{r=::fcntl(fd,F_FULLFSYNC);}while(r<0&&errno==EINTR);
        return r==0;
#else
        return syncFd(fd);
#endif
    }
    static bool syncFd(int fd){int r;do{r=::fsync(fd);}while(r<0&&errno==EINTR);return r==0;}
#endif
 #if defined(_WIN32)
    HANDLE handle_=INVALID_HANDLE_VALUE;
#endif
    int fd_=-1;uint64_t last_=0;bool poisoned_=false;
};
}
