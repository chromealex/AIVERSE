// Project-scoped botanical texture discovery and bounded PNG reads.
// A missing/deleted texture is also a change; no arbitrary filesystem access.
#pragma once
#include "engine/render/TerraWeaveBotanicalForest.h"
#include "engine/world/TerraWeaveForestSpeciesManifest.h"
#include "engine/world/WorldAssets.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>

namespace aiverse::studio {
inline std::optional<std::filesystem::path> studioForestTexturePath(
        const std::filesystem::path& root,const std::string& name){
    namespace fs=std::filesystem;
    if(!world::safeAssetPath(name)||name.size()>512)return {};
    std::error_code ec;
    const auto rootStatus=fs::symlink_status(root,ec);
    if(ec||fs::is_symlink(rootStatus)||!fs::is_directory(rootStatus))return {};
    fs::path file=root;
    for(const auto& part:fs::path(name)){
        if(part.empty()||part=="."||part=="..")return {};
        file/=part;const auto st=fs::symlink_status(file,ec);
        if(ec||fs::is_symlink(st))return {};
    }
    if(!fs::is_regular_file(file,ec)||ec)return {};
    return file;
}
using StudioForestTextureStamp=std::map<std::string,std::pair<std::uintmax_t,std::filesystem::file_time_type>>;
inline StudioForestTextureStamp studioForestTextureStamp(const std::string& project,const Json& catalog){
    namespace fs=std::filesystem;
    StudioForestTextureStamp out;
    const auto dir=catalog.getStr("botanicalTextures");
    if(project.empty()||dir.empty()||!world::safeAssetPath(dir)||
       !catalog["species"].isArray()||catalog["species"].size()>64)return out;
    std::set<std::string> names;
    for(const auto& row:catalog["species"].items()){
        const auto* p=terraweave::findBotanicalPreset(row.getStr("preset"));
        if(!p)continue;
        for(const auto stem:{p->barkMaterial,p->leafMaterial})
            for(const char* suffix:{"basecolor","normal","orm"})
                names.insert(dir+"/"+std::string(stem)+"_"+suffix+".png");
    }
    const auto root=fs::path(project)/"content";
    for(const auto& name:names){
        const auto file=studioForestTexturePath(root,name);if(!file)continue;
        std::error_code ec;
        const auto bytes=fs::file_size(*file,ec);if(ec)continue;
        const auto stamp=fs::last_write_time(*file,ec);if(ec)continue;
        out.emplace(name,std::make_pair(bytes,stamp));
    }
    return out;
}
inline terraweave::BotanicalTextureReader studioForestTextureReader(const std::string& project){
    namespace fs=std::filesystem;
    return [root=fs::path(project)/"content"](const std::string& name,std::string& out){
        const auto file=studioForestTexturePath(root,name);if(!file)return false;
        std::error_code ec;const auto n=fs::file_size(*file,ec);
        if(ec||n<24||n>16u*1024u*1024u)return false;
        const auto stamp=fs::last_write_time(*file,ec);if(ec)return false;
        std::ifstream input(*file,std::ios::binary);if(!input)return false;
        unsigned char header[24]{};
        if(!input.read(reinterpret_cast<char*>(header),24))return false;
        const unsigned char sig[8]={137,80,78,71,13,10,26,10};
        if(!std::equal(sig,sig+8,header)||header[12]!='I'||header[13]!='H'||header[14]!='D'||header[15]!='R')return false;
        const auto be=[&](int i){return (uint32_t(header[i])<<24)|(uint32_t(header[i+1])<<16)|(uint32_t(header[i+2])<<8)|header[i+3];};
        if(!be(16)||!be(20)||be(16)>2048||be(20)>2048)return false;
        input.seekg(0);out.resize(size_t(n));if(!input.read(out.data(),std::streamsize(n)))return false;
        const auto after=fs::file_size(*file,ec);if(ec||after!=n)return false;
        const auto afterStamp=fs::last_write_time(*file,ec);return !ec&&afterStamp==stamp;
    };
}
} // namespace aiverse::studio
