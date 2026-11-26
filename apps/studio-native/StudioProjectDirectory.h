#pragma once
#include "engine/data/Json.h"
#include <filesystem>
#include <stdexcept>

namespace aiverse::studio {

// One directory of names only. The asset catalog owns imports and content metadata.
inline Json studioProjectDirectory(const std::string& project,const std::string& folder){
    namespace fs=std::filesystem;
    const fs::path root{std::u8string(project.begin(),project.end())};
    const fs::path relative{std::u8string(folder.begin(),folder.end())};
    if(relative.has_root_path() || folder.find('\0')!=std::string::npos)
        throw std::runtime_error("Expected a project-relative folder");
    fs::path current=root;
    for(const auto& part:relative){
        if(part.empty() || part.has_root_path() || part=="." || part=="..")
            throw std::runtime_error("Invalid project folder");
        current/=part;
        if(!fs::is_directory(fs::symlink_status(current)))
            throw std::runtime_error("Project folder is unavailable");
    }
    Json entries=Json::array();
    for(const auto& entry:fs::directory_iterator(current)){
        std::error_code error;
        const auto status=entry.symlink_status(error);
        if(error)continue;
        Json item=Json::object();
        const auto name=entry.path().filename().u8string();
        item.set("name",Json::string(std::string(name.begin(),name.end())));
        item.set("directory",Json::fromBool(fs::is_directory(status)));
        entries.push(std::move(item));
    }
    Json result=Json::object();
    const auto absolute=current.make_preferred().u8string();
    result.set("absolute",Json::string(std::string(absolute.begin(),absolute.end())));
    result.set("entries",std::move(entries));
    return result;
}
} // namespace aiverse::studio
