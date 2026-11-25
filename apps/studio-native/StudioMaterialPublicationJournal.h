#pragma once
#include "apps/editor/core/MaterialOps.h"
// Create-only building material publication intent. Recovery is deliberately
// read-only: a second process may still own the publication, and other assets
// may reference the generated files. Never infer permission to delete them.
#include "apps/studio-native/StudioContentDirectory.h"
#include "apps/studio-native/StudioAssetCatalog.h"
#include "apps/studio-native/StudioAssetReferences.h"
#include "apps/studio-native/StudioAssetUsage.h"
#include "apps/studio-native/StudioMaterialValidation.h"
#include "engine/data/Json.h"
#include "apps/editor/core/PublicationIntegrity.h"
#include "apps/editor/core/PublicationArchive.h"
#include <set>
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
    static void requireIsolatedVacant(const std::filesystem::path& project,const std::string& name,const std::filesystem::path& active){
        namespace fs=std::filesystem;
        if(!validStudioMaterialName(name))throw std::runtime_error("Invalid material publication identity");
        const auto intent=active/"intent.json",exposure=active/"exposure.json";
        if(fs::is_symlink(active)||!fs::is_directory(active)||fs::is_symlink(intent)||!fs::is_regular_file(intent)||fs::file_size(intent)>4096)
            throw std::runtime_error("Invalid material publication journal");
        const auto doc=Json::parseFile(intent.string());
        if(doc.getInt("version")!=2||doc.getStr("kind")!="building-material"||fs::exists(exposure)||fs::is_symlink(exposure))
            throw std::runtime_error("Publication may have exposed assets; manual review required");
        const auto content=project/"content",parent=content/"GeneratedBuildingMaterials",dest=parent/name;
        if(fs::is_symlink(content)||fs::is_symlink(parent)||fs::exists(dest)||fs::is_symlink(dest)||fs::exists(studioMetaPath(dest))||fs::is_symlink(studioMetaPath(dest)))
            throw std::runtime_error("Material content destination already exists");
        const auto library=project/"materials.json";
        if(fs::is_symlink(library))throw std::runtime_error("Unsafe material library");
        if(fs::exists(library)){
            if(!fs::is_regular_file(library)||fs::file_size(library)>16*1024*1024)throw std::runtime_error("Invalid material library");
            const auto materials=Json::parseFile(library.string())["materials"];
            if(!materials.isObject()||materials.has(name))throw std::runtime_error("Material already exists or library invalid");
        }
    }
    static std::string slotName(const std::string& slot){
        const auto pos=slot.rfind('-');
        if(pos==std::string::npos||pos==0||slot.size()-pos<2||slot.size()-pos>5||slot.find_first_not_of("0123456789",pos+1)!=std::string::npos)
            throw std::runtime_error("Invalid material quarantine slot");
        const auto name=slot.substr(0,pos);
        if(!validStudioMaterialName(name))throw std::runtime_error("Invalid material quarantine name");
        return name;
    }
    static std::filesystem::path quarantineRoot(const std::filesystem::path& project){
        const auto archive=root(project).parent_path()/"material-publications-quarantine";
        if(std::filesystem::is_symlink(archive))throw std::runtime_error("Unsafe material quarantine");
        return archive;
    }
    static Json readRecipe(const std::filesystem::path& active,const std::string& name){
        namespace fs=std::filesystem;
        const auto file=active/"recipe.json";
        if(fs::is_symlink(file)||!fs::is_regular_file(file)||fs::file_size(file)>4096)throw std::runtime_error("Original publication recipe unavailable");
        const auto recipe=Json::parseFile(file.string());
        if(recipe.getStr("name")!=name||!validStudioMaterialName(recipe.getStr("style"))||!validStudioMaterialName(recipe.getStr("slot")))
            throw std::runtime_error("Invalid publication recipe");
        const auto resolution=recipe.getInt("resolution");
        if(resolution!=128&&resolution!=256&&resolution!=512)throw std::runtime_error("Invalid publication resolution");
        return recipe;
    }
public:
    void recipe(const Json& value){write("recipe.json",value);}
    static Json retryRecipe(const std::filesystem::path& project,const std::string& name){
        editor::ProjectAssetWriteLock lock(project);
        const auto active=root(project)/name;
        requireIsolatedVacant(project,name,active);
        return readRecipe(active,name);
    }
    static Json inspectQuarantine(const std::filesystem::path& project){
        namespace fs=std::filesystem;
        Json rows=Json::array();const auto archive=quarantineRoot(project);
        if(!fs::exists(archive))return rows;
        for(const auto& slot:fs::directory_iterator(archive)){
            if(rows.size()>=256)break;
            if(slot.is_symlink()||!slot.is_directory())continue;
            const auto id=slot.path().filename().string();
            const auto journal=slot.path()/"journal";
            if(!fs::exists(journal))continue;
            Json row=Json::object();row.set("id",Json::string(id));row.set("restorable",Json::fromBool(false));
            try{
                const auto name=slotName(id);row.set("name",Json::string(name));
                requireIsolatedVacant(project,name,journal);
                const auto active=root(project)/name;
                if(fs::exists(active)||fs::is_symlink(active))throw std::runtime_error("Active publication already exists");
                row.set("restorable",Json::fromBool(true));
            }catch(const std::exception& e){row.set("reason",Json::string(e.what()));}
            rows.push(std::move(row));
        }
        return rows;
    }
    static void restoreQuarantine(const std::filesystem::path& project,const std::string& id){
        namespace fs=std::filesystem;
        editor::ProjectAssetWriteLock lock(project);
        const auto name=slotName(id);const auto slot=quarantineRoot(project)/id;
        if(fs::is_symlink(slot)||!fs::is_directory(slot))throw std::runtime_error("Invalid material quarantine slot");
        const auto journal=slot/"journal";requireIsolatedVacant(project,name,journal);
        const auto active=root(project)/name;
        if(fs::exists(active)||fs::is_symlink(active))throw std::runtime_error("Active publication already exists");
        fs::create_directories(active.parent_path());fs::rename(journal,active);
        std::error_code ec;fs::remove(slot,ec);
    }
    StudioMaterialPublicationJournal(const std::filesystem::path& project,const std::string& name,bool isolated=false) {
        namespace fs=std::filesystem;
        if(!validStudioMaterialName(name))throw std::runtime_error("Invalid publication journal name");
        const auto parent=root(project);fs::create_directories(parent);dir_=parent/name;
        if(!fs::create_directory(dir_))throw std::runtime_error("Publication already pending; inspect recovery journal");
        try { Json intent=Json::parse(R"({"version":1,"kind":"building-material"})");if(isolated)intent.set("version",Json::number(int64_t(2)));write("intent.json",intent); }
        catch(...) { finish();throw; }
    }
    std::filesystem::path stage() const {return dir_/"stage";}
    void beginExposure(){write("exposure.json",Json::parse(R"({"started":true})"));}
    static std::string quarantineIsolated(const std::filesystem::path& project,const std::string& name){
        namespace fs=std::filesystem;
        editor::ProjectAssetWriteLock lock(project);
        if(!validStudioMaterialName(name))throw std::runtime_error("Invalid material publication identity");
        const auto active=root(project)/name;
        requireIsolatedVacant(project,name,active);
        const auto archive=root(project).parent_path()/"material-publications-quarantine";
        if(fs::is_symlink(archive))throw std::runtime_error("Unsafe material quarantine");
        fs::create_directories(archive);
        for(unsigned i=0;i<10000;++i){
            const auto slot=archive/(name+"-"+std::to_string(i));
            if(!fs::create_directory(slot))continue;
            try{fs::rename(active,slot/"journal");return slot.filename().string();}
            catch(...){std::error_code ec;fs::remove(slot,ec);throw;}
        }
        throw std::runtime_error("Material quarantine exhausted");
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
        const auto library=editor::resolveMaterialLibraryIndex(project,read(project/"materials.json",16*1024*1024),16*1024*1024);
        if(!candidate.isObject()||!library[name].isObject()||!(library[name]==candidate))
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
    static Json rollbackPreview(const std::filesystem::path& project,const std::string& name,const Json& rules,const Json& map){
        namespace fs=std::filesystem;
        editor::ProjectAssetWriteLock lock(project);
        if(!validStudioMaterialName(name))throw std::runtime_error("Invalid material publication identity");
        const auto dir=root(project)/name;
        if(fs::is_symlink(dir)||!fs::is_directory(dir))throw std::runtime_error("Unknown material publication");
        const auto materialFile=project/"materials.json";
        Json library=Json::object();
        if(fs::is_symlink(materialFile))throw std::runtime_error("Unsafe material library");
        if(fs::exists(materialFile)){
            if(!fs::is_regular_file(materialFile)||fs::file_size(materialFile)>16*1024*1024)throw std::runtime_error("Invalid material library");
            library=editor::resolveMaterialLibraryIndex(project,Json::parseFile(materialFile.string()),16*1024*1024);
            if(!library.isObject())throw std::runtime_error("Invalid material library");
        }
        const std::string prefix="GeneratedBuildingMaterials/"+name+"/";
        Json blockers=Json::array();
        auto add=[&](const std::string& kind,const std::string& owner){
            if(blockers.size()>=200)return;
            Json item=Json::object();item.set("kind",Json::string(kind));item.set("owner",Json::string(owner));blockers.push(std::move(item));
        };
        if(library.has(name))add("material_exists",name);
        for(const auto& ref:studioCollectAssetReferences(library,rules,{}))
            if(ref.path.starts_with("content/"+prefix)||ref.path.starts_with(prefix))add("known_asset_reference",ref.document+":"+ref.owner+":"+ref.field);
        const auto needle=Json::string(name).dump(-1);
        std::vector<std::string> needles{prefix,needle};
        const auto assetsFile=dir/"assets.json";
        if(!fs::is_symlink(assetsFile)&&fs::is_regular_file(assetsFile)&&fs::file_size(assetsFile)<=65536){
            const auto evidence=Json::parseFile(assetsFile.string());
            if(evidence.isArray()&&evidence.size()<=4)for(const auto& item:evidence.items()){
                const auto guid=item.getStr("guid");
                if(studioValidGuid(guid))needles.push_back(guid);
            }
        }
        const auto scan=studioRecoveryTextScan(project,needles);
        for(const auto& hit:scan.mentions.items())add("text_reference",hit.getStr("file")+": "+hit.getStr("reference"));
        const auto mapText=map.dump(-1),rulesText=rules.dump(-1);
        if(mapText.find(prefix)!=std::string::npos||mapText.find(needle)!=std::string::npos)add("open_map_reference","current map");
        if(rulesText.find(prefix)!=std::string::npos||rulesText.find(needle)!=std::string::npos)add("merged_rules_reference","current rules");
        const auto content=project/"content",parent=content/"GeneratedBuildingMaterials",folder=parent/name;
        const auto sidecar=studioMetaPath(folder);
        const bool unsafe=fs::is_symlink(content)||fs::is_symlink(parent)||fs::is_symlink(folder)||fs::is_symlink(sidecar);
        const bool remains=unsafe||fs::exists(folder)||fs::exists(sidecar);
        if(remains)add("content_requires_manual_review",prefix);
        Json result=Json::object();result.set("name",Json::string(name));result.set("blockers",std::move(blockers));
        // Only journal retirement is permitted, and only when no material or content remains.
        // Text scans are advisory and cannot prove absence of dynamic references.
        result.set("journalOnlyRollbackAllowed",Json::fromBool(!library.has(name)&&!remains));
        result.set("contentRollbackAllowed",Json::fromBool(false));
        Json coverage=Json::object();
        coverage.set("visited",Json::number(int64_t(scan.visited)));
        coverage.set("scanned",Json::number(int64_t(scan.scanned)));
        coverage.set("skipped",Json::number(int64_t(scan.skipped)));
        coverage.set("truncated",Json::fromBool(scan.truncated));
        coverage.set("dynamicReferencesVerified",Json::fromBool(false));
        result.set("coverage",std::move(coverage));
        return result;
    }
    static void rollbackEmpty(const std::filesystem::path& project,const std::string& name,const Json& rules,const Json& map){
        editor::ProjectAssetWriteLock lock(project);
        if(!rollbackPreview(project,name,rules,map).getBool("journalOnlyRollbackAllowed"))
            throw std::runtime_error("Rollback would affect material or content; manual review required");
        editor::archivePublicationJournal(root(project)/name);
    }
    static void finalize(const std::filesystem::path& project,const std::string& name){
        editor::ProjectAssetWriteLock lock(project);
        if(!validStudioMaterialName(name))throw std::runtime_error("Invalid publication name");
        const auto publications=inspect(project);
        for(const auto& row:publications.items()){
            if(row.getStr("name")!=name)continue;
            if(!row.getBool("finalizable"))throw std::runtime_error("Material publication evidence incomplete or mismatched");
            editor::archivePublicationJournal(root(project)/name);return;
        }
        throw std::runtime_error("Unknown material publication");
    }
    static Json inspect(const std::filesystem::path& project) {
        namespace fs=std::filesystem;
        Json rows=Json::array();const auto parent=root(project);
        if(!fs::exists(parent))return rows;
        const auto libraryFile=project/"materials.json";
        Json library=Json::object();
        if(fs::exists(libraryFile)) {
            if(fs::is_symlink(libraryFile)||!fs::is_regular_file(libraryFile)||fs::file_size(libraryFile)>16*1024*1024)
                throw std::runtime_error("Unsafe material library for publication inspection");
            library=editor::resolveMaterialLibraryIndex(project,Json::parseFile(libraryFile.string()),16*1024*1024);
        }
        for(const auto& entry:fs::directory_iterator(parent)) {
            const auto name=entry.path().filename().string();
            if(entry.is_symlink()||!entry.is_directory()||!validStudioMaterialName(name))continue;
            Json row=Json::object();row.set("name",Json::string(name));
            const auto file=entry.path()/"candidate.json";
            std::string status="incomplete_or_active";
            Json candidate;
            if(fs::is_regular_file(file)&&!fs::is_symlink(file)&&fs::file_size(file)<=65536){
                candidate=Json::parseFile(file.string());
                if(library[name].isObject())status=library[name]==candidate?"material_committed":"material_conflict";
            }
            row.set("status",Json::string(status));
            try{requireIsolatedVacant(project,name,entry.path());row.set("quarantinable",Json::fromBool(true));try{(void)readRecipe(entry.path(),name);row.set("retryable",Json::fromBool(true));}catch(...){} }catch(...){}
            // Only inspect fixed, transaction-owned output names. Never follow links.
            const auto folder=project/"content"/"GeneratedBuildingMaterials"/name;
            const auto assetsFile=entry.path()/"assets.json";
            Json checks=Json::array();
            bool complete=false;std::set<std::string> recorded;
            if(fs::is_regular_file(assetsFile)&&!fs::is_symlink(assetsFile)&&fs::file_size(assetsFile)<=65536){
                const auto entries=Json::parseFile(assetsFile.string());
                complete=entries.isArray()&&entries.size()<=4;
                if(complete)for(const auto& asset:entries.items()){
                    const auto rel=asset.getStr("path");
                    const auto filename=fs::path(rel).filename().string();
                    if(filename!="base-color.png"&&filename!="metallic-roughness.png"&&filename!="normal.png"&&filename!="occlusion.png"){complete=false;continue;}
                    if(rel!="GeneratedBuildingMaterials/"+name+"/"+filename){complete=false;continue;}
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
                    bool exactMeta=false;
                    if(metaPresent&&fs::file_size(meta)<=65536&&asset["metadata"].isObject())try{exactMeta=Json::parseFile(meta.string())==asset["metadata"];}catch(...){}
                    complete=complete&&recorded.insert(rel).second&&check.getBool("hashMatches")&&guidMatches&&exactMeta&&studioValidGuid(asset.getStr("guid"))&&studioMaterialTextureKey(asset.getStr("channel"))&&candidate.getStr(asset.getStr("channel"))=="content/"+rel;
                    check.set("recoverable",Json::fromBool(status=="material_committed"&&present&&!metaPresent&&check.getBool("hashMatches")&&asset["metadata"].isObject()));
                    checks.push(std::move(check));
                }
            }
            if(candidate.isObject())for(const auto& [key,value]:candidate.members()){
                const std::string prefix="content/GeneratedBuildingMaterials/"+name+"/";
                if(studioMaterialTextureKey(key)&&value.isString()&&value.str().starts_with(prefix)&&!recorded.count(value.str().substr(8)))complete=false;
            }
            // Do not finalize empty or filtered/malformed evidence.
            row.set("finalizable",Json::fromBool(status=="material_committed"&&complete&&!recorded.empty()&&recorded.size()==checks.size()));
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
