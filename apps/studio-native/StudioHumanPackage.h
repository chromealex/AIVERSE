#pragma once
// Character package: one portable file (<project>/packages/<name>.aivchar,
// JSON + base64) with a look, its preset picture and optionally a built
// character (Characters/<name>/model.glb, source.json, appearance.json), for
// moving a character between projects. Import never overwrites: it picks a
// free name.
#include "StudioHumanLookDocument.h"
#include "StudioContentDirectory.h"
#include <filesystem>
#include <fstream>
#include <iterator>
namespace aiverse::studio {
inline std::string studioUnbase64(const std::string& in) {
    auto val=[](char c)->int{if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;if(c>='0'&&c<='9')return c-'0'+52;if(c=='+')return 62;if(c=='/')return 63;return -1;};
    std::string out;out.reserve(in.size()*3/4);unsigned buf=0;int bits=0;
    for(char c:in){if(c=='=')break;const int v=val(c);if(v<0)throw std::runtime_error("Broken package data");buf=(buf<<6)|unsigned(v);bits+=6;if(bits>=8){bits-=8;out.push_back(char((buf>>bits)&255));}}
    return out;
}
inline std::string studioReadSmallFile(const std::filesystem::path& p,std::uintmax_t limit) {
    namespace fs=std::filesystem;std::error_code ec;
    if(fs::is_symlink(fs::symlink_status(p,ec))||!fs::is_regular_file(p,ec)||fs::file_size(p,ec)>limit)throw std::runtime_error("Cannot read "+p.filename().string());
    std::ifstream in(p,std::ios::binary);return std::string((std::istreambuf_iterator<char>(in)),{});
}
inline void studioWriteNew(const std::filesystem::path& p,const std::string& bytes) {
    namespace fs=std::filesystem;
    if(fs::exists(p))throw std::runtime_error(p.filename().string()+" already exists");
    {std::ofstream out(p.string()+".pending",std::ios::binary|std::ios::trunc);out<<bytes;if(!out)throw std::runtime_error("Cannot write "+p.filename().string());}
    fs::rename(p.string()+".pending",p);
}
inline std::filesystem::path studioPackageFolder(const std::string& project,bool create) {
    namespace fs=std::filesystem;const fs::path f=fs::path(project)/"packages";
    if(fs::is_symlink(fs::symlink_status(f)))throw std::runtime_error("packages cannot be a symlink");
    if(create)fs::create_directories(f);return f;
}
inline Json listStudioHumanPackages(const std::string& project) {
    namespace fs=std::filesystem;auto names=Json::array();const auto f=studioPackageFolder(project,false);
    std::vector<std::string> sorted;
    if(fs::is_directory(f))for(const auto& e:fs::directory_iterator(f))if(e.is_regular_file()&&e.path().extension()==".aivchar"&&validStudioHumanLookName(e.path().stem().string()))sorted.push_back(e.path().stem().string());
    std::sort(sorted.begin(),sorted.end());for(const auto& n:sorted)names.push(Json::string(n));
    auto r=Json::object();r.set("packages",names);return r;
}
inline Json exportStudioHumanPackage(const std::string& project,const std::string& look,const std::string& character) {
    namespace fs=std::filesystem;
    auto pkg=Json::object();pkg.set("format",Json::string("aiverse-character"));pkg.set("version",Json::number(int64_t(1)));
    const auto lookFile=studioHumanLookFile(project,look);
    pkg.set("look",Json::string(look));pkg.set("lookJson",Json::parse(studioReadSmallFile(lookFile,256*1024)));
    const auto thumb=studioHumanLookThumbnailFile(project,look);
    if(fs::exists(thumb))pkg.set("thumbnail",Json::string(studioBase64(studioReadSmallFile(thumb,512*1024))));
    if(!character.empty()) {
        if(!validStudioHumanLookName(character))throw std::invalid_argument("Invalid character name");
        const fs::path dir=studioContentRoot(project)/"Characters"/character;
        auto files=Json::object();
        for(const char* f:{"model.glb","source.json","appearance.json"})
            if(fs::exists(dir/f))files.set(f,Json::string(studioBase64(studioReadSmallFile(dir/f,256u*1024u*1024u))));
        if(!files.has("model.glb"))throw std::runtime_error("Characters/"+character+"/model.glb not found");
        pkg.set("character",Json::string(character));pkg.set("files",files);
    }
    const auto out=studioPackageFolder(project,true)/(look+".aivchar");
    std::error_code ec;fs::remove(out,ec); // re-export replaces the package itself
    studioWriteNew(out,pkg.dump());
    auto r=Json::object();r.set("path",Json::string("packages/"+look+".aivchar"));r.set("bytes",Json::number(int64_t(fs::file_size(out))));return r;
}
inline Json importStudioHumanPackage(const std::string& project,const std::string& name) {
    namespace fs=std::filesystem;
    if(!validStudioHumanLookName(name))throw std::invalid_argument("Invalid package name");
    const auto pkg=Json::parse(studioReadSmallFile(studioPackageFolder(project,false)/(name+".aivchar"),400u*1024u*1024u));
    if(pkg.getStr("format")!="aiverse-character"||!pkg["version"].isNumber()||pkg["version"].toDouble()!=1)throw std::runtime_error("Not an AIVERSE character package");
    auto freeName=[&](const std::string& base,auto taken){std::string n=base;for(int i=2;taken(n);++i)n=base.substr(0,60)+"_"+std::to_string(i);return n;};
    std::string look=pkg.getStr("look");if(!validStudioHumanLookName(look))throw std::runtime_error("Invalid look name in package");
    (void)vivid::HumanAppearance::fromJson(pkg["lookJson"]);
    look=freeName(look,[&](const std::string& n){return fs::exists(studioHumanLookFile(project,n,true));});
    saveStudioHumanLook(project,look,pkg["lookJson"]);
    if(pkg.has("thumbnail"))studioWriteNew(studioHumanLookThumbnailFile(project,look),studioUnbase64(pkg.getStr("thumbnail")));
    auto r=Json::object();r.set("look",Json::string(look));
    if(pkg.has("character")) {
        std::string ch=pkg.getStr("character");if(!validStudioHumanLookName(ch))throw std::runtime_error("Invalid character name in package");
        const fs::path root=studioContentRoot(project)/"Characters";fs::create_directories(root);
        ch=freeName(ch,[&](const std::string& n){return fs::exists(root/n);});
        fs::create_directory(root/ch);
        for(const auto& [file,data]:pkg["files"].members()) {
            if(file!="model.glb"&&file!="source.json"&&file!="appearance.json")continue;
            studioWriteNew(root/ch/file,studioUnbase64(data.str()));
        }
        r.set("character",Json::string("Characters/"+ch+"/model.glb"));
    }
    return r;
}
}
