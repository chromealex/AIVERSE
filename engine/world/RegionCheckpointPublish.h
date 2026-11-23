#pragma once
#include "RegionCheckpointManifest.h"
#include <filesystem>
#include <fstream>
#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <cerrno>
#endif
namespace aiverse::world {
enum class RegionCheckpointPublishResult { Ok, Unsupported, Busy, Invalid, IoError, Ambiguous };
class RegionCheckpointPublisher {
public:
    // Caller must hold the authoritative save barrier and stop the old WAL
    // writer. Directory must already exist and be private to this world.
    // Digest must have been computed by the save owner over checkpoint bytes.
    static RegionCheckpointPublishResult publish(const std::filesystem::path& dir,
        const RegionCheckpointManifest& m,std::span<const uint8_t> checkpoint) {
#if defined(__unix__) || defined(__APPLE__)
        using R=RegionCheckpointPublishResult;
        if(!m.valid()||m.checkpointBytes!=checkpoint.size())return R::Invalid;
        std::vector<uint8_t> manifest;
        if(!RegionCheckpointManifestCodec::encode(m,manifest))return R::Invalid;
        struct Fd {int n;~Fd(){if(n>=0)::close(n);}};
        Fd lock{::open((dir/"publish.lock").c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600)};
        if(lock.n<0)return R::IoError;
        if(::flock(lock.n,LOCK_EX|LOCK_NB)!=0)return errno==EWOULDBLOCK?R::Busy:R::IoError;
        Fd directory{::open(dir.c_str(),O_RDONLY|O_CLOEXEC)};
        if(directory.n<0)return R::IoError;
        const auto current=dir/"manifest.bin";
        std::error_code ec;
        if(std::filesystem::exists(current,ec)){
            std::ifstream in(current,std::ios::binary);
            std::vector<uint8_t> b(RegionCheckpointManifestCodec::size);
            in.read(reinterpret_cast<char*>(b.data()),b.size());
            RegionCheckpointManifest old;
            if(!in||in.peek()!=std::char_traits<char>::eof()||
               !RegionCheckpointManifestCodec::decode(b,old)||
               old.identity.world!=m.identity.world||old.generation>=m.generation||
               old.identity.sequence>m.identity.sequence||old.identity.revision>m.identity.revision)return R::Invalid;
        }else if(ec)return R::IoError;
        // Exclusive creation: never overwrite an old or orphan generation.
        if(!writeNew(dir/m.checkpointName(),checkpoint))return R::IoError;
        if(!writeNew(dir/m.walName(),{}))return R::IoError;
        if(!sync(directory.n))return R::IoError;
        const auto temp=dir/("manifest-"+std::to_string(m.generation)+".tmp");
        if(!writeNew(temp,manifest))return R::IoError;
        if(::rename(temp.c_str(),current.c_str())!=0)return R::IoError;
        if(!sync(directory.n))return R::Ambiguous;
        return R::Ok;
#else
        (void)dir;(void)m;(void)checkpoint;return RegionCheckpointPublishResult::Unsupported;
#endif
    }
private:
#if defined(__unix__) || defined(__APPLE__)
    static bool sync(int fd){int r;do{r=::fsync(fd);}while(r<0&&errno==EINTR);return r==0;}
    static bool writeNew(const std::filesystem::path& p,std::span<const uint8_t> b){
        int fd=::open(p.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);
        if(fd<0)return false;
        size_t offset=0;bool ok=true;
        while(offset<b.size()){
            auto n=::write(fd,b.data()+offset,std::min<size_t>(b.size()-offset,1024*1024));
            if(n<0&&errno==EINTR)continue;
            if(n<=0){ok=false;break;}offset+=size_t(n);
        }
        if(ok)ok=sync(fd);
        if(::close(fd)!=0)ok=false;
        return ok;
    }
#endif
};
}
