#pragma once
#include "engine/data/Json.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>

namespace aiverse::studio {
inline bool isSpectraPackagePath(const std::filesystem::path& p){
    auto ext=p.extension().string();
    std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
    return ext==".aivfx";
}
// Only called with OS-native chooser/drop paths, never a WebView path.
inline Json stageSpectraPackage(const std::filesystem::path& project,const std::filesystem::path& source){
    namespace fs=std::filesystem;
    constexpr uint64_t maxBytes=130ull*1024*1024;
    if(!isSpectraPackagePath(source)||!fs::is_regular_file(fs::symlink_status(source)))
        throw std::runtime_error("Select one regular .aivfx package");
    const auto bytes=fs::file_size(source);
    if(bytes<4||bytes>maxBytes)throw std::runtime_error("Invalid VFX package size");
    std::ifstream input(source,std::ios::binary);
    char magic[4]{};
    if(!input.read(magic,4)||magic[0]!='P'||magic[1]!='K'||magic[2]!=3||magic[3]!=4)
        throw std::runtime_error("VFX package must be a ZIP container");
    input.close();
    if(!fs::is_directory(project))throw std::runtime_error("Open a project first");
    const auto folder=project/"packages";
    if(fs::is_symlink(fs::symlink_status(folder)))throw std::runtime_error("Package directory cannot be a symlink");
    fs::create_directories(folder);
    if(!fs::is_directory(fs::symlink_status(folder)))throw std::runtime_error("Invalid package directory");
    std::string stem;
    for(unsigned char ch:source.stem().string()){
        if(stem.size()>=40)break;
        const char c=char(std::tolower(ch));
        stem+=(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'?c:'_';
    }
    if(stem.empty()||stem=="fx")stem="imported_fx";
    for(unsigned n=0;n<256;++n){
        const auto name=n?stem+"_"+std::to_string(n):stem;
        const auto dest=folder/(name+".aivfx");
        if(fs::exists(fs::symlink_status(dest)))continue;
        // copy_file without overwrite is exclusive at the destination.
        if(!fs::copy_file(source,dest,fs::copy_options::none))
            throw std::runtime_error("Cannot stage VFX package");
        try{
            if(fs::file_size(dest)!=bytes)throw std::runtime_error("Package changed during staging");
            Json out=Json::object();out.set("name",Json::string(name));
            out.set("projectPath",Json::string(project.string()));
            out.set("path",Json::string("packages/"+name+".aivfx"));
            return out;
        }catch(...){std::error_code ec;fs::remove(dest,ec);throw;}
    }
    throw std::runtime_error("Too many package name collisions");
}
}
