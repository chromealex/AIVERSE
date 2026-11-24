// Owner-generated reversible plans for existing project documents.
#pragma once
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>
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
bool recoverProjectFileEdits(const std::filesystem::path& project);
void applyProjectFileEdits(const std::filesystem::path& project,const std::vector<ProjectFileEdit>& edits);
} // namespace aiverse::editor
