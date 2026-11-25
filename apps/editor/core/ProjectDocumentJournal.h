// Durable owner-side transactions for existing project documents.
// The pending marker owns the entire before/after plan; a committed marker
// means the new bytes won. Recovery refuses unknown bytes or unsafe paths.
// Does not provide a lock against tools that ignore the editor advisory lock.
#pragma once
#include "engine/data/Json.h"
#include "engine/pack/Sha256.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <set>
#include <utility>
#include <cstdint>
#include <system_error>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef MoveFile // windows.h macro clashes with ProjectHistory::MoveFile; this header calls MoveFileExW directly
#elif defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#endif

#include "ProjectFileEdits.h"
#include <optional>
namespace aiverse::editor {
namespace project_file_detail {
namespace fs=std::filesystem;
inline bool journalExists(const fs::path& p){
    std::error_code ec;const auto state=fs::symlink_status(p,ec);
    if(ec==std::errc::no_such_file_or_directory)return false;
    if(ec)throw std::runtime_error("Cannot inspect project journal path: "+p.string());
    return fs::exists(state);
}
inline void directory(const fs::path& p){
    if(!journalExists(p))fs::create_directory(p);
    if(!fs::is_directory(fs::symlink_status(p)))throw std::runtime_error("Unsafe project journal directory: "+p.string());
}
inline void sync(const fs::path& p){
#if defined(_WIN32)
    // Windows replacement uses MOVEFILE_WRITE_THROUGH below. Directory
    // FlushFileBuffers is not a portable Win32 guarantee.
    if(fs::is_directory(fs::symlink_status(p)))return;
    const auto handle=CreateFileW(p.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open project journal for flush: "+p.string());
    const bool ok=FlushFileBuffers(handle)!=0;CloseHandle(handle);
    if(!ok)throw std::runtime_error("Cannot flush project journal: "+p.string());
#elif defined(__unix__) || defined(__APPLE__)
    const int fd=::open(p.c_str(),O_RDONLY|O_NOFOLLOW);
    if(fd<0)throw std::runtime_error("Cannot open project journal for sync: "+p.string());
    int code;
#if defined(__APPLE__)
    code=::fcntl(fd,F_FULLFSYNC);if(code!=0)code=::fsync(fd);
#else
    code=::fsync(fd);
#endif
    ::close(fd);
    if(code!=0)throw std::runtime_error("Cannot sync project journal: "+p.string());
#else
    (void)p;throw std::runtime_error("Project document durability is unsupported on this platform");
#endif
}
inline void replace(const fs::path& source,const fs::path& target){
#if defined(_WIN32)
    if(!MoveFileExW(source.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot publish project journal file: "+target.string());
#else
    fs::rename(source,target);
#endif
    sync(target.parent_path());
    if(source.parent_path()!=target.parent_path())sync(source.parent_path());
}
inline void write(const fs::path& p,const std::string& bytes){
    std::ofstream out(p,std::ios::binary|std::ios::trunc);
    if(!out||!(out.write(bytes.data(),static_cast<std::streamsize>(bytes.size())))||!out.flush())
        throw std::runtime_error("Cannot stage project document: "+p.string());
    out.close();if(!out)throw std::runtime_error("Cannot close staged project document: "+p.string());
    sync(p);
}
inline std::string digest(const std::string& bytes){
    const auto hash=pack::sha256(bytes.data(),bytes.size());
    return pack::toHex(hash.data(),hash.size());
}
// A stable advisory lock file is never removed. All journal owners acquire it;
// recovery cannot mistake another cooperating editor's live save for a crash.
class Lock {
public:
    explicit Lock(const fs::path& path){
        if(journalExists(path)&&!fs::is_regular_file(fs::symlink_status(path)))
            throw std::runtime_error("Unsafe project journal lock");
#if defined(_WIN32)
        handle_=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(handle_==INVALID_HANDLE_VALUE)throw std::runtime_error("Project document journal is busy");
#elif defined(__unix__) || defined(__APPLE__)
        fd_=::open(path.c_str(),O_RDWR|O_CREAT|O_NOFOLLOW,0600);
        if(fd_<0)throw std::runtime_error("Cannot open project journal lock");
        if(::flock(fd_,LOCK_EX|LOCK_NB)!=0){::close(fd_);fd_=-1;throw std::runtime_error("Project document journal is busy");}
#else
        (void)path;throw std::runtime_error("Project document locking is unsupported");
#endif
    }
    Lock(const Lock&)=delete;Lock& operator=(const Lock&)=delete;
    ~Lock(){
#if defined(_WIN32)
        if(handle_!=INVALID_HANDLE_VALUE)CloseHandle(handle_);
#elif defined(__unix__) || defined(__APPLE__)
        if(fd_>=0)::close(fd_);
#endif
    }
private:
#if defined(_WIN32)
    HANDLE handle_=INVALID_HANDLE_VALUE;
#elif defined(__unix__) || defined(__APPLE__)
    int fd_=-1;
#endif
};
inline fs::path safeTarget(const fs::path& root,const std::string& relative,bool allowCreation=false){
    const fs::path rel(relative);
    if(relative.empty()||relative.size()>2048||rel.is_absolute()||rel.has_root_name()||
       relative.find('\\')!=std::string::npos||relative.find(':')!=std::string::npos)
        throw std::runtime_error("Unsafe project document journal target");
    // Mixed Studio history may update only an exact quarantined sidecar.
    // Never admit arbitrary recovery payloads or cache documents here.
    const std::string trashPrefix=".aiverse-cache/studio-asset-trash/";
    const auto trashRest=relative.starts_with(trashPrefix)?relative.substr(trashPrefix.size()):std::string{};
    const bool trashMeta=trashRest.size()==24+11&&trashRest.substr(24)=="/asset.meta"&&
        trashRest.substr(0,24).find_first_not_of("0123456789abcdef")==std::string::npos;
    const auto filename=rel.filename().string();
    const std::string mapPrefix="map-history-";
    const bool mapCheckpoint=relative.starts_with("maps/")&&rel.parent_path().filename()==".aiverse-cache"&&
        filename.size()==mapPrefix.size()+64+5&&filename.starts_with(mapPrefix)&&filename.ends_with(".json")&&
        filename.substr(mapPrefix.size(),64).find_first_not_of("0123456789abcdef")==std::string::npos;
    if(allowCreation){
        const auto ext=rel.extension().string();
        if(relative!=".aiverse-cache/asset-history.json"&&
           !(relative.starts_with("scripts/")&&ext==".lua")&&
           (!relative.starts_with("maps/")||(ext!=".json"&&ext!=".r16"&&ext!=".splat")))
            throw std::runtime_error("Unsupported new save output");
    }
    fs::path p=root;
    for(const auto& segment:rel){
        const auto name=segment.string();
        const bool historyCache=((relative==".aiverse-cache/project-history.json"||relative==".aiverse-cache/asset-history.json")&&p==root&&name==".aiverse-cache")||
            (mapCheckpoint&&name==".aiverse-cache"&&p/segment==root/rel.parent_path());
        if(name.empty()||name=="."||name==".."||(name==".aiverse-cache"&&!historyCache&&!(trashMeta&&p==root))||name==".git"||
           name.starts_with(".aiverse-save-")||name.find('\0')!=std::string::npos)
            throw std::runtime_error("Unsafe project document journal segment");
        p/=segment;
        if(fs::is_symlink(fs::symlink_status(p)))throw std::runtime_error("Symlink in project document journal target");
    }
    if(allowCreation&&!journalExists(p)){
        if(!fs::is_directory(fs::symlink_status(p.parent_path()))||fs::canonical(p.parent_path())!=p.parent_path())
            throw std::runtime_error("Unsafe new save output parent");
        return p;
    }
    if(!fs::is_regular_file(fs::symlink_status(p))||fs::canonical(p)!=p)
        throw std::runtime_error("Invalid project document journal target: "+p.string());
    return p;
}
}
class ProjectDocumentJournal {
public:
    static constexpr size_t MaxFiles=4096, MaxBytes=256u*1024u*1024u;
    explicit ProjectDocumentJournal(const std::filesystem::path& project):
        root_(std::filesystem::canonical(project)),cache_(root_/".aiverse-cache"),folder_(cache_/"project-document-journal"),
        lock_(lockPath(cache_)){}
    ProjectDocumentJournal(const ProjectDocumentJournal&)=delete;
    ProjectDocumentJournal& operator=(const ProjectDocumentJournal&)=delete;
    // Prepare only; useful for fault-injection at each publication boundary.
    bool begin(const std::vector<ProjectFileEdit>& edits){
        namespace fs=std::filesystem;using namespace project_file_detail;
        if(journalExists(folder_))throw std::runtime_error("Pending project document recovery; reopen the project");
        if(edits.size()>MaxFiles)throw std::runtime_error("Too many project document dependencies");
        std::set<fs::path> paths;size_t bytes=0;bool changed=false;
        Json rows=Json::array();
        for(const auto& edit:edits){
            if(!edit.tailDigest.empty()||edit.tailBytes)throw std::runtime_error("Streaming resource requires Studio move journal");
            const auto absolute=fs::absolute(edit.path).lexically_normal();
            const auto relative=absolute.lexically_relative(root_).generic_string();
            const auto target=safeTarget(root_,relative,edit.createOnly);
            if(!paths.insert(target).second||
               (edit.createOnly?(journalExists(target)||!edit.before.empty()):readProjectFileBytes(target)!=edit.before))
                throw std::runtime_error("Project document changed or duplicated: "+target.string());
            for(const auto size:{edit.before.size(),edit.after.size()}){
                if(size>MaxBytes-bytes)throw std::runtime_error("Project document journal size limit");
                bytes+=size;
            }
            changed=changed||edit.createOnly||edit.before!=edit.after;
            Json row=Json::object();row.set("path",Json::string(relative));
            row.set("beforeHash",Json::string(digest(edit.before)));row.set("afterHash",Json::string(digest(edit.after)));
            const auto permission=edit.createOnly?(fs::perms::owner_read|fs::perms::owner_write):
                (fs::status(target).permissions()&fs::perms::mask);
            row.set("permissions",Json::number(int64_t(permission)));
            row.set("createOnly",Json::fromBool(edit.createOnly));
            rows.push(std::move(row));
        }
        if(!changed)return false;
        if(!fs::create_directory(folder_))throw std::runtime_error("Cannot reserve project document journal");
        try{
            for(size_t i=0;i<edits.size();++i){
                write(slot(i,"before"),edits[i].before);write(slot(i,"after"),edits[i].after);
            }
            Json state=Json::object();state.set("version",Json::number(int64_t(2)));state.set("files",std::move(rows));
            write(folder_/"manifest.tmp",state.dump(2)+"\n");
            replace(folder_/"manifest.tmp",folder_/"pending.json");
            sync(folder_);sync(cache_);sync(root_);
            return true;
        }catch(...){
            // No live files have changed. Retain a published marker if present.
            if(!journalExists(folder_/"pending.json")){std::error_code ec;fs::remove_all(folder_,ec);}
            throw;
        }
    }
    void publish(size_t i){
        if(!prepared_) {
            prepared_=load();
            for(const auto& s:*prepared_)if(!matchesBefore(s))
                throw std::runtime_error("Project document dependency changed before publication: "+s.path.string());
        }
        const auto& steps=*prepared_;
        if(i>=steps.size())throw std::runtime_error("Invalid project journal publication index");
        const auto& s=steps[i];
        validateTarget(s);
        if(!matchesBefore(s))throw std::runtime_error("Project document changed before publication: "+s.path.string());
        if(s.createOnly||s.before!=s.after)put(s,s.after);
    }
    void commit(){
        namespace fs=std::filesystem;using namespace project_file_detail;
        const auto steps=load(); // independently verify the durable snapshots
        for(const auto& s:steps){validateTarget(s);if(!matchesAfter(s))
            throw std::runtime_error("Project document changed before commit: "+s.path.string());}
        replace(folder_/"pending.json",folder_/"committed.json");
        // Committed marker is durable on supported POSIX filesystems.
        cleanup();
    }
    bool recover(){
        namespace fs=std::filesystem;using namespace project_file_detail;
        if(!journalExists(folder_))return false;
        if(!fs::is_directory(fs::symlink_status(folder_)))throw std::runtime_error("Unsafe project document journal");
        const bool pending=journalExists(folder_/"pending.json"),committed=journalExists(folder_/"committed.json");
        if(pending&&committed)throw std::runtime_error("Ambiguous project document journal markers");
        if(committed){
            if(!fs::is_regular_file(fs::symlink_status(folder_/"committed.json")))throw std::runtime_error("Unsafe committed document marker");
            cleanup();return false;
        }
        if(!pending){cleanup();return false;} // no publication before the pending marker
        const auto steps=load();std::vector<bool> changed;
        // Preflight the complete transaction before restoring even one file.
        for(const auto& s:steps){
            validateTarget(s);
            const bool before=matchesBefore(s);
            if(!before&&!matchesAfter(s))
                throw std::runtime_error("Project recovery conflict; retained journal: "+s.path.string());
            changed.push_back(!before);
        }
        for(size_t i=steps.size();i>0;--i)if(changed[i-1]){
            const auto& s=steps[i-1];
            if(!matchesAfter(s))throw std::runtime_error("Project recovery changed during rollback");
            if(s.createOnly){
                if(!fs::remove(s.path))throw std::runtime_error("Cannot remove created save output");
                sync(s.path.parent_path());
            }else put(s,s.before);
        }
        for(const auto& s:steps)if(!matchesBefore(s))
            throw std::runtime_error("Project recovery baseline changed");
        // Retire the pending marker only after all original bytes are restored.
        replace(folder_/"pending.json",folder_/"rolledback.json");
        cleanup();return true;
    }
private:
    struct Step {std::filesystem::path path;std::string before,after;std::filesystem::perms permissions;bool createOnly=false;};
    static std::filesystem::path lockPath(const std::filesystem::path& cache){
        project_file_detail::directory(cache);return cache/"project-document.lock";
    }
    std::filesystem::path slot(size_t i,const char* side)const{return folder_/(std::to_string(i)+"."+side);}
    std::vector<Step> load()const{
        namespace fs=std::filesystem;using namespace project_file_detail;
        const auto manifest=folder_/"pending.json";
        if(!fs::is_directory(fs::symlink_status(folder_))||
           !fs::is_regular_file(fs::symlink_status(manifest))||fs::file_size(manifest)>16u*1024u*1024u)
            throw std::runtime_error("Invalid pending project document journal");
        const auto state=Json::parse(readProjectFileBytes(manifest));
        const auto version=state.getInt("version");
        if((version!=1&&version!=2)||!state["files"].isArray()||!state["files"].size()||state["files"].size()>MaxFiles)
            throw std::runtime_error("Invalid project document journal manifest");
        std::set<fs::path> paths;std::vector<Step> steps;size_t bytes=0;
        for(const auto& row:state["files"].items()){
            if(!row.isObject()||!row["path"].isString())throw std::runtime_error("Invalid project journal entry");
            if(version==2&&row["createOnly"].type()!=Json::Type::Bool)throw std::runtime_error("Invalid creation marker");
            const bool createOnly=version==2&&row.getBool("createOnly");
            const auto target=safeTarget(root_,row.getStr("path"),createOnly);
            if(!paths.insert(target).second)throw std::runtime_error("Duplicate project recovery target");
            const auto permission=row.getInt("permissions",-1);
            if(permission<0||permission>int64_t(fs::perms::mask))throw std::runtime_error("Invalid project document permissions");
            auto read=[&](const char* side,const char* hash){
                const auto p=slot(steps.size(),side);
                if(!fs::is_regular_file(fs::symlink_status(p))||fs::file_size(p)>MaxBytes-bytes)
                    throw std::runtime_error("Invalid project recovery snapshot");
                auto text=readProjectFileBytes(p);
                if(text.size()>MaxBytes-bytes||digest(text)!=row.getStr(hash))
                    throw std::runtime_error("Project recovery snapshot hash mismatch");
                bytes+=text.size();return text;
            };
            auto before=read("before","beforeHash"),after=read("after","afterHash");
            if(createOnly&&!before.empty())throw std::runtime_error("Invalid absent-file snapshot");
            steps.push_back({target,std::move(before),std::move(after),fs::perms(permission),createOnly});
        }
        return steps;
    }
    void validateTarget(const Step& s)const{
        const auto path=project_file_detail::safeTarget(root_,s.path.lexically_relative(root_).generic_string(),s.createOnly);
        if(path!=s.path||(project_file_detail::journalExists(path)&&
           (std::filesystem::status(path).permissions()&std::filesystem::perms::mask)!=s.permissions))
            throw std::runtime_error("Project document path or permissions changed: "+s.path.string());
    }
    bool matchesBefore(const Step& s)const{
        validateTarget(s);
        return s.createOnly?!project_file_detail::journalExists(s.path):readProjectFileBytes(s.path)==s.before;
    }
    bool matchesAfter(const Step& s)const{
        validateTarget(s);
        return project_file_detail::journalExists(s.path)&&readProjectFileBytes(s.path)==s.after;
    }
    void put(const Step& s,const std::string& bytes){
        namespace fs=std::filesystem;using namespace project_file_detail;
        validateTarget(s);
        const auto staged=folder_/"publish.tmp";
        if(journalExists(staged)){
            if(!fs::is_regular_file(fs::symlink_status(staged)))throw std::runtime_error("Unsafe project publication staging file");
            fs::remove(staged);
        }
        write(staged,bytes);
        fs::permissions(staged,s.permissions,fs::perm_options::replace);
#if !defined(_WIN32)
        sync(staged);
#endif
        if(s.createOnly){
            // Atomic no-clobber publish. Never replace a file that appeared
            // after preflight, even if a noncooperating writer created it.
#if defined(_WIN32)
            if(!MoveFileExW(staged.c_str(),s.path.c_str(),MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Cannot create save output");
#else
            fs::create_hard_link(staged,s.path);
            sync(s.path.parent_path());
            fs::remove(staged);
#endif
            sync(s.path.parent_path());sync(folder_);
        }else replace(staged,s.path);
    }
    void cleanup()noexcept{
        std::error_code ec;std::filesystem::remove_all(folder_,ec);
        try{project_file_detail::sync(cache_);}catch(...){}
    }
    std::filesystem::path root_,cache_,folder_;
    project_file_detail::Lock lock_;
    std::optional<std::vector<Step>> prepared_;
};
} // namespace aiverse::editor
