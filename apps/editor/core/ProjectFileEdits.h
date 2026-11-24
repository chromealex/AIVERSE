// Owner-generated reversible plans for existing project documents.
#pragma once
#include <filesystem>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>
namespace aiverse::editor {
struct ProjectFileEdit {
    std::filesystem::path path;
    std::string before, after;
    // Studio-only GLB prefix edit. Generic project document journal rejects
    // this mode; the Studio move journal owns streaming publication.
    std::string tailDigest{};
    uint64_t tailBytes=0;
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
// Shared filesystem publication primitives used by the Studio asset journal.
void syncProjectFilePath(const std::filesystem::path& path);
void renameProjectFilePath(const std::filesystem::path& source,const std::filesystem::path& target,bool replaceExisting=false);
bool recoverProjectFileEdits(const std::filesystem::path& project);
void applyProjectFileEdits(const std::filesystem::path& project,const std::vector<ProjectFileEdit>& edits);
} // namespace aiverse::editor
