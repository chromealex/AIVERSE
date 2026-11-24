#pragma once
// Cooperative project asset writer exclusion. Owner-thread reentrancy supports
// nested native calls; other threads/processes fail closed. A crash can leave
// a stale directory: never remove it automatically or infer process liveness.
#include <filesystem>
#include <map>
#include <stdexcept>
#include <system_error>
namespace aiverse::editor {
class ProjectAssetWriteLock {
    using Path=std::filesystem::path;
    inline static thread_local std::map<Path,size_t> held_;
    Path lock_;
public:
    explicit ProjectAssetWriteLock(const Path& project) {
        namespace fs=std::filesystem;
        if(project.empty()||!fs::is_directory(project))throw std::runtime_error("Invalid project asset lock root");
        const auto root=fs::canonical(project),cache=root/".aiverse-cache";
        if(fs::is_symlink(cache))throw std::runtime_error("Unsafe project asset lock cache");
        fs::create_directories(cache);
        lock_=cache/"asset-writer.lock";
        auto [it,inserted]=held_.try_emplace(lock_,0);
        if(!inserted){++it->second;return;}
        try {
            if(!fs::create_directory(lock_))throw std::runtime_error("Project asset writer busy or stale lock; inspect before recovery");
            it->second=1;
        }catch(...){held_.erase(it);throw;}
    }
    ProjectAssetWriteLock(const ProjectAssetWriteLock&)=delete;
    ProjectAssetWriteLock& operator=(const ProjectAssetWriteLock&)=delete;
    ~ProjectAssetWriteLock(){
        auto it=held_.find(lock_);
        if(it==held_.end()||--it->second)return;
        held_.erase(it);std::error_code ec;std::filesystem::remove(lock_,ec);
    }
};
} // namespace aiverse::editor
