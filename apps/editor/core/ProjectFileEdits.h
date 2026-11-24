// Owner-side reversible file plans for existing project documents.
// No arbitrary paths from the UI; callers supply the exact loaded file set.
// Atomic replacement per file, in-process rollback on failure. This is not a
// durable multi-file crash journal or a lock against concurrent external writers.
#pragma once
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <set>
#include <utility>
#include <chrono>

namespace aiverse::editor {
struct ProjectFileEdit {
    std::filesystem::path path;
    std::string before, after;
};
inline std::vector<ProjectFileEdit> reverseProjectFileEdits(std::vector<ProjectFileEdit> edits) {
    for(auto& e:edits) std::swap(e.before,e.after);
    return edits;
}
inline std::string readProjectFileBytes(const std::filesystem::path& p) {
    namespace fs=std::filesystem;
    if(!fs::is_regular_file(fs::symlink_status(p)))
        throw std::runtime_error("Not a regular project document: "+p.string());
    std::ifstream in(p,std::ios::binary);
    if(!in) throw std::runtime_error("Cannot read project document: "+p.string());
    std::string result;char buf[16384];
    while(in.read(buf,sizeof(buf)) || in.gcount())
        result.append(buf,static_cast<size_t>(in.gcount()));
    if(in.bad()||!in.eof()) throw std::runtime_error("Cannot read project document: "+p.string());
    return result;
}
inline void applyProjectFileEdits(const std::filesystem::path& project,
                                  const std::vector<ProjectFileEdit>& edits) {
    namespace fs=std::filesystem;
    const auto root=fs::canonical(project);
    struct Step { fs::path path,stage; const ProjectFileEdit* edit; bool published=false,keep=false; };
    std::vector<Step> steps;
    std::set<fs::path> unique;
    auto validate=[&](const Step& s,const std::string& expected) {
        if(fs::canonical(s.path)!=s.path || readProjectFileBytes(s.path)!=expected)
            throw std::runtime_error("Project document changed; reopen before editing: "+s.path.string());
    };
    auto cleanup=[&] {
        for(const auto& s:steps) if(!s.keep&&!s.stage.empty()){
            std::error_code ec;fs::remove_all(s.stage,ec);
        }
    };
    auto write=[](const fs::path& path,const std::string& data,fs::perms permissions) {
        std::ofstream out(path,std::ios::binary|std::ios::trunc);
        if(!out || !(out<<data) || !(out.flush()))
            throw std::runtime_error("Cannot stage project document: "+path.string());
        out.close();
        if(!out) throw std::runtime_error("Cannot close staged project document: "+path.string());
        fs::permissions(path,permissions,fs::perm_options::replace);
    };
    for(const auto& e:edits) {
        const auto path=fs::canonical(e.path);
        const auto rel=path.lexically_relative(root);
        if(rel.empty()||rel.is_absolute()||*rel.begin()==".."||
           !fs::is_regular_file(fs::symlink_status(e.path))||!unique.insert(path).second)
            throw std::runtime_error("Unsafe or duplicate project document: "+e.path.string());
        steps.push_back({path,{},&e});
        validate(steps.back(),e.before);
    }
    try {
        for(auto& s:steps) {
            if(s.edit->before==s.edit->after) continue;
            const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
            for(int i=0;i<128;++i) {
                const auto candidate=s.path.parent_path()/(".aiverse-save-"+std::to_string(stamp)+"-"+std::to_string(i));
                if(fs::create_directory(candidate)){s.stage=candidate;break;}
            }
            if(s.stage.empty()) throw std::runtime_error("Cannot reserve project save staging directory");
            const auto permissions=fs::status(s.path).permissions();
            write(s.stage/"before",s.edit->before,permissions);
            write(s.stage/"after",s.edit->after,permissions);
        }
        // Validate all dependencies again after staging, including unchanged files.
        for(const auto& s:steps) validate(s,s.edit->before);
        for(auto& s:steps) {
            if(s.stage.empty()) continue;
            validate(s,s.edit->before);
            fs::rename(s.stage/"after",s.path);
            s.published=true;
        }
        for(const auto& s:steps) validate(s,s.edit->after);
    } catch(const std::exception& failure) {
        std::string error=failure.what();
        for(auto it=steps.rbegin();it!=steps.rend();++it) if(it->published) {
            try {
                validate(*it,it->edit->after);
                fs::rename(it->stage/"before",it->path);
            } catch(const std::exception& rollback) {
                it->keep=true;
                error+="; rollback failed: "+std::string(rollback.what())+"; backup: "+it->stage.string();
            }
        }
        cleanup();
        throw std::runtime_error(error);
    }
    cleanup();
}
} // namespace aiverse::editor
