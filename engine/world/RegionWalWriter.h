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
#endif
namespace aiverse::world {
enum class RegionWalWrite { Ok, Unsupported, Busy, IoError, InvalidSequence, InvalidFrame, NeedsRecovery };
class RegionWalWriter {
public:
    RegionWalWriter()=default;
    RegionWalWriter(const RegionWalWriter&)=delete;
    RegionWalWriter& operator=(const RegionWalWriter&)=delete;
    ~RegionWalWriter(){close();}
    RegionWalWrite open(const std::filesystem::path& path,uint64_t previous=0,bool repairIncompleteTail=false) {
        close();
#if defined(__unix__) || defined(__APPLE__)
        fd_=::open(path.c_str(),O_RDWR|O_APPEND|O_CREAT|O_CLOEXEC,0600);
        if(fd_<0)return RegionWalWrite::IoError;
        if(::flock(fd_,LOCK_EX|LOCK_NB)!=0){auto e=errno;close();return e==EWOULDBLOCK?RegionWalWrite::Busy:RegionWalWrite::IoError;}
        std::ifstream input(path,std::ios::binary);
        if(!input){close();return RegionWalWrite::IoError;}
        auto scan=RegionWalReader::scan(input,previous,[](const RegionWalFrame&){return true;});
        if(scan.status==RegionWalScan::Incomplete && repairIncompleteTail) {
            if(scan.validBytes>uint64_t(std::numeric_limits<off_t>::max()) ||
               ::ftruncate(fd_,static_cast<off_t>(scan.validBytes))!=0 || !syncFd(fd_)) {
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
#else
        (void)path;(void)previous;(void)repairIncompleteTail;return RegionWalWrite::Unsupported;
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
#else
        (void)frame;return RegionWalWrite::Unsupported;
#endif
    }
    uint64_t lastSequence()const{return last_;}
    void close(){
#if defined(__unix__) || defined(__APPLE__)
        if(fd_>=0)::close(fd_);
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
    int fd_=-1;uint64_t last_=0;bool poisoned_=false;
};
}
