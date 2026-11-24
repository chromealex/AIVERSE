#pragma once
// Create-only building material publication intent. Recovery is deliberately
// read-only: a second process may still own the publication, and other assets
// may reference the generated files. Never infer permission to delete them.
#include "apps/studio-native/StudioContentDirectory.h"
#include "apps/studio-native/StudioAssetCatalog.h"
#include "apps/studio-native/StudioMaterialValidation.h"
#include "engine/data/Json.h"
#include "apps/editor/core/PublicationIntegrity.h"
#include "apps/editor/core/ProjectAssetWriteLock.h"
#include <fstream>
#include <filesystem>
#include <stdexcept>

namespace aiverse::studio {
class StudioMaterialPublicationJournal {
    std::filesystem::path dir_;
    static std::filesystem::path root(const std::filesystem::path& project) {
        namespace fs=std::filesystem;
        const auto cache=project/".aiverse-cache", journals=cache/"material-publications";
        if(fs::is_symlink(cache)||fs::is_symlink(journals))throw std::runtime_error("Unsafe publication journal path");
        return journals;
    }
public:
    StudioMaterialPublicationJournal(const std::filesystem::path& project,const std::string& name) {
        namespace fs=std::filesystem;
        if(!validStudioMaterialName(name))throw std::runtime_error("Invalid publication journal name");
        const auto parent=root(project);fs::create_directories(parent);dir_=parent/name;
        if(!fs::create_directory(dir_))throw std::runtime_error("Publication already pending; inspect recovery journal");
        try { write("intent.json",Json::parse(R"({"version":1,"kind":"building-material"})")); }
        catch(...) { finish();throw; }
    }
    void candidate(const Json& material) { write("candidate.json",material); }
    void assets(const Json& entries) { write("assets.json",entries); }
    // Explicit repair of one missing sidecar, before catalog rebuild. Caller
    // must have exclusive project access; no replacement of existing metadata.
    static void restoreMissingMetadata(const std::filesystem::path& project,const std::string& name,const std::string& filename){
        namespace fs=std::filesystem;
        editor::ProjectAssetWriteLock projectLock(project);
        if(!validStudioMaterialName(name)||(filename!="base-color.png"&&filename!="metallic-roughness.png"&&filename!="normal.png"&&filename!="occlusion.png"))
            throw std::runtime_error("Invalid publication recovery identity");
        const auto dir=root(project)/name;
        if(fs::is_symlink(dir)||!fs::is_directory(dir))throw std::runtime_error("Missing publication journal");
        auto read=[](const fs::path& file,size_t max){
            if(fs::is_symlink(file)||!fs::is_regular_file(file)||fs::file_size(file)>max)throw std::runtime_error("Invalid recovery document");
            return Json::parseFile(file.string());
        };
        const auto candidate=read(dir/"candidate.json",65536);
        const auto library=read(project/"materials.json",16*1024*1024);
        if(!candidate.isObject()||!library["materials"][name].isObject()||!(library["materials"][name]==candidate))
            throw std::runtime_error("Recovery requires matching committed material");
        const auto records=read(dir/"assets.json",65536);
        if(!records.isArray()||records.size()>4)throw std::runtime_error("Invalid asset evidence");
        const std::string rel="GeneratedBuildingMaterials/"+name+"/"+filename;
        Json record;size_t matches=0;
        for(const auto& asset:records.items())if(asset.getStr("path")==rel){record=asset;++matches;}
        const auto guid=record.getStr("guid");const auto& meta=record["metadata"];
        if(matches!=1||!studioValidGuid(guid)||!meta.isObject()||meta.getStr("guid")!=guid||
           meta.getInt("schemaVersion")!=1||meta.getStr("importer")!="texture"||!meta["settings"].isObject()||
           !studioMaterialTextureKey(record.getStr("channel"))||candidate.getStr(record.getStr("channel"))!="content/"+rel)
            throw std::runtime_error("Missing or invalid recovery metadata evidence");
        const auto content=studioContentRoot(project),parent=content/"GeneratedBuildingMaterials",folder=parent/name;
        if(fs::is_symlink(parent)||fs::is_symlink(folder)||!fs::is_directory(folder))throw std::runtime_error("Unsafe generated asset folder");
        const auto file=folder/filename,target=studioMetaPath(file);
        if(fs::exists(target)||fs::is_symlink(target)||record.getStr("sha256").size()!=64||
           editor::publicationFileSha256(file)!=record.getStr("sha256"))
            throw std::runtime_error("Existing metadata or texture integrity mismatch");
        // Sidecars are authoritative; inspect disk, not the potentially stale SQLite index.
        size_t scanned=0;
        for(const auto& entry:fs::recursive_directory_iterator(content)){
            if(++scanned>40000)throw std::runtime_error("Recovery scan limit exceeded");
            if(entry.is_symlink())continue;
            if(entry.path().extension()==".meta"&&entry.is_regular_file()){
                if(read(entry.path(),65536).getStr("guid")==guid)
                    throw std::runtime_error("Recovery GUID already exists");
            }
        }
        const auto staging=fs::path(target.string()+".aiverse-tmp");
        if(!fs::create_directory(staging))throw std::runtime_error("Metadata recovery staging busy");
        struct Cleanup{fs::path path;~Cleanup(){std::error_code ec;fs::remove_all(path,ec);}}cleanup{staging};
        const auto payload=staging/"payload";
        {std::ofstream out(payload,std::ios::binary);out<<meta.dump(2)<<"\n";out.close();if(!out)throw std::runtime_error("Cannot stage recovery metadata");}
        fs::create_hard_link(payload,target); // exclusive create, never overwrite
    }
    void finish() noexcept { std::error_code ec;std::filesystem::remove_all(dir_,ec); }
    static Json inspect(const std::filesystem::path& project) {
        namespace fs=std::filesystem;
        Json rows=Json::array();const auto parent=root(project);
        if(!fs::exists(parent))return rows;
        const auto libraryFile=project/"materials.json";
        Json library=Json::object();
        if(fs::exists(libraryFile)) {
            if(fs::is_symlink(libraryFile)||!fs::is_regular_file(libraryFile)||fs::file_size(libraryFile)>16*1024*1024)
                throw std::runtime_error("Unsafe material library for publication inspection");
            library=Json::parseFile(libraryFile.string())["materials"];
        }
        for(const auto& entry:fs::directory_iterator(parent)) {
            const auto name=entry.path().filename().string();
            if(entry.is_symlink()||!entry.is_directory()||!validStudioMaterialName(name))continue;
            Json row=Json::object();row.set("name",Json::string(name));
            const auto file=entry.path()/"candidate.json";
            std::string status="incomplete_or_active";
            if(fs::is_regular_file(file)&&!fs::is_symlink(file)&&fs::file_size(file)<=65536){
                const auto candidate=Json::parseFile(file.string());
                if(library[name].isObject())status=library[name]==candidate?"material_committed":"material_conflict";
            }
            row.set("status",Json::string(status));
            // Only inspect fixed, transaction-owned output names. Never follow links.
            const auto folder=project/"content"/"GeneratedBuildingMaterials"/name;
            const auto assetsFile=entry.path()/"assets.json";
            Json checks=Json::array();
            if(fs::is_regular_file(assetsFile)&&!fs::is_symlink(assetsFile)&&fs::file_size(assetsFile)<=65536){
                const auto entries=Json::parseFile(assetsFile.string());
                if(entries.isArray()&&entries.size()<=4)for(const auto& asset:entries.items()){
                    const auto rel=asset.getStr("path");
                    const auto filename=fs::path(rel).filename().string();
                    if(filename!="base-color.png"&&filename!="metallic-roughness.png"&&filename!="normal.png"&&filename!="occlusion.png")continue;
                    if(rel!="GeneratedBuildingMaterials/"+name+"/"+filename)continue;
                    const auto physical=folder/filename,meta=fs::path(physical.string()+".meta");
                    const bool safe=!fs::is_symlink(project/"content")&&!fs::is_symlink(folder.parent_path())&&!fs::is_symlink(folder);
                    const bool present=safe&&!fs::is_symlink(physical)&&fs::is_regular_file(physical);
                    const bool metaPresent=present&&!fs::is_symlink(meta)&&fs::is_regular_file(meta);
                    bool guidMatches=false;
                    if(metaPresent&&fs::file_size(meta)<=65536)try{
                        guidMatches=!asset.getStr("guid").empty()&&Json::parseFile(meta.string()).getStr("guid")==asset.getStr("guid");
                    }catch(...){}
                    Json check=Json::object();check.set("path",Json::string(rel));
                    check.set("present",Json::fromBool(present));check.set("metadataPresent",Json::fromBool(metaPresent));
                    check.set("guidMatches",Json::fromBool(guidMatches));
                    const auto digest=asset.getStr("sha256");
                    if(digest.size()==64){
                        bool matches=false;if(present)try{matches=editor::publicationFileSha256(physical)==digest;}catch(...){}
                        check.set("hashMatches",Json::fromBool(matches));
                    }
                    checks.push(std::move(check));
                }
            }
            row.set("assets",std::move(checks));rows.push(std::move(row));
        }
        return rows;
    }
private:
    void write(const char* name,const Json& value) {
        namespace fs=std::filesystem;
        const auto temp=dir_/"pending.tmp";
        {std::ofstream stream(temp,std::ios::binary|std::ios::trunc);stream<<value.dump(2)<<"\n";stream.close();
         if(!stream)throw std::runtime_error("Cannot write publication journal");}
        fs::rename(temp,dir_/name);
    }
};
} // namespace aiverse::studio
