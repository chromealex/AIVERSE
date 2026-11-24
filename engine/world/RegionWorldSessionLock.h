#pragma once
#include <filesystem>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <cerrno>
#endif
namespace aiverse::world {
enum class RegionWorldLockResult { Ok, Busy, IoError, Unsupported };
class RegionWorldSessionLock {
public:
    RegionWorldSessionLock()=default;
    RegionWorldSessionLock(const RegionWorldSessionLock&)=delete;
    RegionWorldSessionLock& operator=(const RegionWorldSessionLock&)=delete;
    ~RegionWorldSessionLock(){close();}
    // Private, existing, stable world directory. Never unlink the lock file.
    RegionWorldLockResult acquire(const std::filesystem::path& dir){
        using R=RegionWorldLockResult;
        if(held())return R::Busy;
#if defined(_WIN32)
        handle_=::CreateFileW((dir/"world-session.lock").c_str(),GENERIC_READ|GENERIC_WRITE,
            0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle_==INVALID_HANDLE_VALUE)return ::GetLastError()==ERROR_SHARING_VIOLATION?R::Busy:R::IoError;
        return R::Ok;
#elif defined(__unix__) || defined(__APPLE__)
        fd_=::open((dir/"world-session.lock").c_str(),O_RDWR|O_CREAT|O_CLOEXEC,0600);
        if(fd_<0)return R::IoError;
        int result;do{result=::flock(fd_,LOCK_EX|LOCK_NB);}while(result<0&&errno==EINTR);
        if(result!=0){const int error=errno;close();return error==EWOULDBLOCK?R::Busy:R::IoError;}
        return R::Ok;
#else
        (void)dir;return R::Unsupported;
#endif
    }
    bool held()const noexcept{
#if defined(_WIN32)
        return handle_!=INVALID_HANDLE_VALUE;
#elif defined(__unix__) || defined(__APPLE__)
        return fd_>=0;
#else
        return false;
#endif
    }
    void close()noexcept{
#if defined(_WIN32)
        if(held())::CloseHandle(handle_);
        handle_=INVALID_HANDLE_VALUE;
#elif defined(__unix__) || defined(__APPLE__)
        if(held())::close(fd_);
        fd_=-1;
#endif
    }
private:
#if defined(_WIN32)
    HANDLE handle_=INVALID_HANDLE_VALUE;
#elif defined(__unix__) || defined(__APPLE__)
    int fd_=-1;
#endif
};
}
