#pragma once
// Cooperative project asset writer exclusion. OS releases the lease on process
// exit. Keep the lock file (inode) permanently; unlinking it could split owners.
// Old directory locks are deliberately not migrated without offline inspection.
#include <filesystem>
#include <map>
#include <stdexcept>
#include <system_error>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#error Unsupported host for project asset writer lock
#endif
namespace aiverse::editor {
class ProjectAssetWriteLock {
    using Path=std::filesystem::path;
#if defined(_WIN32)
    using Handle=HANDLE;
    static Handle acquire(const Path& path){
        const auto h=::CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Project asset writer busy or lock unavailable");
        BY_HANDLE_FILE_INFORMATION info{};
        if(!::GetFileInformationByHandle(h,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))){
            ::CloseHandle(h);throw std::runtime_error("Unsafe project asset lock file");
        }
        return h;
    }
    static void release(Handle h) noexcept {::CloseHandle(h);}
#else
    using Handle=int;
    static Handle acquire(const Path& path){
        const int fd=::open(path.c_str(),O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK,0600);
        if(fd<0)throw std::runtime_error("Cannot open project asset lock");
        struct stat info{};
        if(::fstat(fd,&info)!=0||!S_ISREG(info.st_mode)||info.st_nlink!=1||::flock(fd,LOCK_EX|LOCK_NB)!=0){
            ::close(fd);throw std::runtime_error("Project asset writer busy or unsafe lock");
        }
        return fd;
    }
    static void release(Handle fd) noexcept {::close(fd);}
#endif
    struct Lease{size_t depth;Handle handle;};
    inline static thread_local std::map<Path,Lease> held_;
    Path lock_;
public:
    explicit ProjectAssetWriteLock(const Path& project) {
        namespace fs=std::filesystem;
        if(project.empty()||!fs::is_directory(project))throw std::runtime_error("Invalid project asset lock root");
        const auto root=fs::canonical(project),cache=root/".aiverse-cache";
        if(fs::is_symlink(cache))throw std::runtime_error("Unsafe project asset lock cache");
        fs::create_directories(cache);
        // Stage 74 directory uses the same name: do not bypass a legacy owner.
        lock_=cache/"asset-writer.lock";
        if(fs::is_symlink(lock_)||fs::is_directory(lock_))throw std::runtime_error("Legacy or unsafe asset lock; inspect offline");
        if(auto it=held_.find(lock_);it!=held_.end()){++it->second.depth;return;}
        const auto handle=acquire(lock_);
        try{held_.emplace(lock_,Lease{1,handle});}
        catch(...){release(handle);throw;}
    }
    ProjectAssetWriteLock(const ProjectAssetWriteLock&)=delete;
    ProjectAssetWriteLock& operator=(const ProjectAssetWriteLock&)=delete;
    ~ProjectAssetWriteLock(){
        auto it=held_.find(lock_);
        if(it==held_.end()||--it->second.depth)return;
        const auto handle=it->second.handle;held_.erase(it);release(handle);
    }
};
} // namespace aiverse::editor
