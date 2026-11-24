// Studio reference graph: typed material/rules slots and conservative exact
// map path candidates, independent of UI and genre runtime.
// materials.json stores project-relative paths; GUID is resolved via .meta index.
#pragma once
#include "apps/studio-native/StudioAssetCatalog.h"
#include "engine/data/Json.h"
#include <map>
#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace aiverse::studio {

struct StudioAssetReference {
    std::string document; // project document or current map
    std::string owner;    // material/type name or map scope
    std::string field;    // documented slot or JSON pointer
    std::string path;     // runtime-compatible project-relative path
    std::string guid;     // empty for unresolved/legacy paths
    bool writable = false; // currently only materials.json slots support repair
};

inline constexpr const char* kStudioTextureFields[] = {
    "texture", "normalMap", "emissiveMap", "teamMask",
    "metallicRoughnessMap", "occlusionMap"
};

// Build GUID lookup once; no implicit rebuild or filesystem write in discovery.
inline std::map<std::string, std::string> studioReferenceGuids(
    const std::vector<StudioAssetEntry>& assets) {
    std::map<std::string, std::string> result;
    for (const auto& asset : assets)
        if (!asset.directory && !asset.guid.empty())
            result.emplace("content/" + asset.path, asset.guid);
    return result;
}

inline std::vector<StudioAssetReference> studioCollectAssetReferences(
    const Json& materials, const Json& rules,
    const std::vector<StudioAssetEntry>& assets) {
    const auto guids = studioReferenceGuids(assets);
    std::vector<StudioAssetReference> out;
    auto append = [&](const char* document, const std::string& owner,
                      const std::string& field, const Json& value, bool writable) {
        if (!value.isString() || value.str().empty()) return;
        const auto found = guids.find(value.str());
        StudioAssetReference ref;
        ref.document = document; ref.owner = owner; ref.field = field;
        ref.path = value.str(); ref.writable = writable;
        if (found != guids.end()) ref.guid = found->second;
        out.push_back(std::move(ref));
    };
    for (const auto& [name, material] : materials.members()) {
        if (!material.isObject()) continue;
        for (const char* field : kStudioTextureFields)
            append("materials.json", name, field, material[field], true);
    }
    // Actual rules schema: types.<type>.view.model may be string,
    // string-array, or entries with a "file" string. The runtime uses
    // those paths directly; never rewrite rules/includes here.
    for (const auto& [name, type] : rules["types"].members()) {
        const Json& model = type["view"]["model"];
        if (model.isString()) {
            append("rules.json", name, "view.model", model, false);
        } else if (model.isArray()) {
            std::size_t index = 0;
            for (const auto& entry : model.items()) {
                const std::string key = "view.model[" + std::to_string(index++) + "]";
                if (entry.isString()) append("rules.json", name, key, entry, false);
                else if (entry.isObject())
                    append("rules.json", name, key + ".file", entry["file"], false);
            }
        }
    }
    return out;
}

// Apply only validated old->new mappings to known material slots. Caller
// persists via existing engine material validation + save routine.
inline std::size_t studioRemapMaterialReferences(
    Json& materials, const std::map<std::string, std::string>& relocated) {
    if (relocated.empty()) return 0;
    std::vector<std::pair<std::string, Json>> edits;
    std::size_t updated = 0;
    for (const auto& [name, material] : materials.members()) {
        if (!material.isObject()) continue;
        Json candidate = material;
        bool changed = false;
        for (const char* field : kStudioTextureFields) {
            const Json& current = candidate[field];
            if (!current.isString()) continue;
            const auto match = relocated.find(current.str());
            if (match == relocated.end()) continue;
            candidate.set(field, Json::string(match->second));
            ++updated;
            changed = true;
        }
        if (changed) edits.emplace_back(name, std::move(candidate));
    }
    for (auto& edit : edits) materials.set(edit.first, std::move(edit.second));
    return updated;
}

inline bool studioReferenceMatchesPath(const std::string& value,const std::string& path,bool directory){
    return value==path||(directory&&value.starts_with(path+"/"));
}
inline bool studioIsGltfResourceReference(const StudioAssetReference& ref){
    return ref.document.starts_with("content/")&&ref.owner=="glTF resource";
}
// Resolve the set of known dependencies affected by an internal move.
// This is a preview, not a rename executor. Exact paths and folder descendants
// are matched; an unaffected resource in another folder must not match.
inline Json studioMoveReferenceImpact(const Json& transfer,
    const std::vector<StudioAssetReference>& references) {
    Json result=Json::array();
    for (const auto& item:transfer["items"].items()) {
        if (!item["id"].isNumber() || !item["from"].isString() ||
            !item["to"].isString()) continue;
        const std::string source="content/"+item.getStr("from");
        const std::string destination="content/"+item.getStr("to");
        if (source==destination) continue;
        const bool folder=item.getBool("directory");
        for (const auto& ref:references) {
            const bool targetMoves=studioReferenceMatchesPath(ref.path,source,folder);
            const bool ownerMoves=studioIsGltfResourceReference(ref)&&studioReferenceMatchesPath(ref.document,source,folder);
            if(!targetMoves&&!ownerMoves)continue;
            Json row=Json::object();
            row.set("id",item["id"]);
            row.set("document",Json::string(ref.document));
            row.set("owner",Json::string(ref.owner));
            row.set("field",Json::string(ref.field));
            row.set("from",Json::string(ref.path));
            row.set("to",Json::string(targetMoves?destination+ref.path.substr(source.size()):ref.path));
            row.set("writable",Json::fromBool(ref.writable));
            result.push(std::move(row));
        }
    }
    return result;
}

// Conservative fence for authored map/rules paths, including folder descendants.
// Material slots use the separate writable owner. This is not a dynamic script
// or binary dependency resolver.
inline bool studioDocumentNamesAsset(const Json& document,const std::string& path,bool directory){
    if(document.isString()){
        const auto value=document.str();
        for(const auto& name:{path,"content/"+path})
            if(value==name||(directory&&value.starts_with(name+"/")))return true;
    }else if(document.isArray()){
        for(const auto& value:document.items())if(studioDocumentNamesAsset(value,path,directory))return true;
    }else if(document.isObject()){
        for(const auto& [key,value]:document.members()){
            (void)key;if(studioDocumentNamesAsset(value,path,directory))return true;
        }
    }
    return false;
}
// Shared enumeration and byte budget for read-only discovery and mutation fences.
template<class Visitor>
inline void studioVisitSavedMaps(const std::filesystem::path& project,
    const Json& manifest, Visitor&& visit){
    namespace fs=std::filesystem;
    const auto root=fs::canonical(project);
    std::set<fs::path> maps;
    const auto add=[&](const fs::path& path){
        const auto canonical=fs::canonical(path);
        const auto relative=canonical.lexically_relative(root);
        if(relative.empty()||relative.is_absolute()||*relative.begin()==".."||
           !fs::is_regular_file(fs::symlink_status(path)))
            throw std::runtime_error("Unsafe saved map dependency");
        if(!maps.count(canonical)&&maps.size()>=512)throw std::runtime_error("Too many map documents to validate asset references");
        // Reject symlinked ancestors and paths outside the canonical project.
        const auto lexical=path.lexically_normal().lexically_relative(root);
        if(lexical.empty()||lexical.is_absolute()||*lexical.begin()=="..")
            throw std::runtime_error("Unsafe saved map dependency");
        auto cursor=root;
        for(const auto& part:lexical){cursor/=part;if(fs::is_symlink(fs::symlink_status(cursor)))
            throw std::runtime_error("Symlink in saved map dependency");}
        maps.insert(canonical);
    };
    for(const auto& map:manifest["maps"].items()){
        const auto file=map.getStr("file");
        if(!file.empty())add(root/file);
    }
    const auto folder=root/"maps";
    if(fs::exists(folder)){
        if(!fs::is_directory(fs::symlink_status(folder)))throw std::runtime_error("Unsafe maps folder");
        for(auto it=fs::recursive_directory_iterator(folder);it!=fs::recursive_directory_iterator();++it){
            const auto& entry=*it;
            if(entry.path().filename()==".aiverse-cache"){it.disable_recursion_pending();continue;}
            const auto status=entry.symlink_status();
            if(fs::is_symlink(status))throw std::runtime_error("Symlink in maps folder");
            if(fs::is_regular_file(status)&&entry.path().extension()==".json")add(entry.path());
        }
    }
    std::uintmax_t bytes=0;
    for(const auto& map:maps){
        const auto size=fs::file_size(map);
        if(size>64ull*1024*1024-bytes)throw std::runtime_error("Saved map validation budget exceeded");
        bytes+=size;visit(Json::parseFile(map.string()),map.lexically_relative(root).generic_string());
    }
}
// Resolve only local relative glTF buffer/image URIs. Never open the target,
// follow a remote URI or interpret a URI as an absolute filesystem path.
inline std::string studioGltfResourcePath(const std::string& source,const std::string& uri){
    namespace fs=std::filesystem;
    if(uri.empty()||uri.size()>8192||uri.find_first_of(":?#\\")!=std::string::npos||uri.front()=='/')return {};
    std::string decoded;
    const auto hex=[](char c)->int{
        if(c>='0'&&c<='9')return c-'0';
        if(c>='a'&&c<='f')return c-'a'+10;
        if(c>='A'&&c<='F')return c-'A'+10;
        return -1;
    };
    for(size_t i=0;i<uri.size();++i){
        unsigned char c=static_cast<unsigned char>(uri[i]);
        if(c=='%'){
            if(i+2>=uri.size())return {};
            const int hi=hex(uri[i+1]),lo=hex(uri[i+2]);
            if(hi<0||lo<0)return {};
            c=static_cast<unsigned char>(hi*16+lo);i+=2;
            if(c=='/'||c=='\\'||c==':'||c=='?'||c=='#'||c=='%')return {};
        }
        if(c<32||c==127)return {};
        decoded.push_back(static_cast<char>(c));
    }
    const auto relative=(fs::path(source).parent_path()/fs::path(decoded)).lexically_normal();
    const auto result=relative.generic_string();
    if(!result.starts_with("content/")||result.size()<=8||result.ends_with("/"))return {};
    return result;
}
inline std::vector<StudioAssetReference> studioCollectGltfReferences(
    const std::filesystem::path& project,const std::map<std::string,std::string>& guids){
    namespace fs=std::filesystem;
    const auto root=fs::canonical(project),content=root/"content";
    std::vector<StudioAssetReference> refs;
    if(!fs::exists(fs::symlink_status(content)))return refs;
    if(!fs::is_directory(fs::symlink_status(content)))throw std::runtime_error("Unsafe content dependency root");
    std::vector<fs::path> models;
    size_t entries=0;
    for(auto it=fs::recursive_directory_iterator(content);it!=fs::recursive_directory_iterator();++it){
        if(++entries>100000)throw std::runtime_error("glTF dependency directory budget exceeded");
        const auto status=it->symlink_status();
        if(fs::is_symlink(status)){it.disable_recursion_pending();continue;}
        const auto name=it->path().filename().string();
        if(name==".aiverse-cache"||name.starts_with(".aiverse-save-")){it.disable_recursion_pending();continue;}
        if(!fs::is_regular_file(status))continue;
        auto ext=it->path().extension().string();
        for(auto& c:ext)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if(ext!=".gltf")continue;
        if(models.size()>=2048)throw std::runtime_error("glTF dependency file budget exceeded");
        models.push_back(it->path());
    }
    std::sort(models.begin(),models.end());
    std::uintmax_t bytes=0;
    for(const auto& model:models){
        const auto size=fs::file_size(model);
        if(size>16ull*1024*1024||size>64ull*1024*1024-bytes)
            throw std::runtime_error("glTF dependency byte budget exceeded");
        bytes+=size;
        const auto doc=Json::parseFile(model.string());
        if(!doc.isObject())throw std::runtime_error("Invalid glTF dependency document");
        const auto source=model.lexically_relative(root).generic_string();
        for(const char* group:{"buffers","images"}){
            if(doc.has(group)&&!doc[group].isArray())throw std::runtime_error("Invalid glTF resource array");
            size_t index=0;
            for(const auto& item:doc[group].items()){
                const auto field=std::string("/")+group+"/"+std::to_string(index++)+"/uri";
                if(!item.has("uri"))continue;
                if(!item["uri"].isString())throw std::runtime_error("Invalid glTF resource URI");
                const auto path=studioGltfResourcePath(source,item.getStr("uri"));
                if(path.empty())continue;
                const auto found=guids.find(path);
                if(refs.size()>=32768)throw std::runtime_error("glTF dependency result budget exceeded");
                refs.push_back({source,"glTF resource",field,path,found==guids.end()?std::string{}:found->second,false});
            }
        }
    }
    return refs;
}
// Conservative mutation fence: refuse moving either endpoint of known
// external resource edges. Relative URI rewrite/coordinated folder relocation
// is deliberately not implemented by this read-only graph.
inline void studioCheckGltfAssetPaths(const std::filesystem::path& project,
    const std::vector<std::pair<std::string,bool>>& paths){
    if(paths.empty())return;
    for(const auto& ref:studioCollectGltfReferences(project,{}))
        for(const auto& [path,directory]:paths)
            if(studioReferenceMatchesPath(ref.path,"content/"+path,directory)||
               studioReferenceMatchesPath(ref.document,"content/"+path,directory))
                throw std::runtime_error("Asset operation blocked by glTF resource: "+ref.document+" "+ref.field);
}
inline void studioCheckMapAssetPaths(const std::filesystem::path& project,
    const Json& currentMap,const Json& rules,const Json& manifest,
    const std::vector<std::pair<std::string,bool>>& paths){
    auto check=[&](const Json& doc,const std::string& label){
        for(const auto& [path,directory]:paths)
            if(studioDocumentNamesAsset(doc,path,directory))
                throw std::runtime_error("Asset move blocked by "+label+" reference: "+path);
    };
    if(paths.empty())return;
    check(currentMap,"current map");check(rules,"rules");
    studioVisitSavedMaps(project,manifest,check);
    studioCheckGltfAssetPaths(project,paths);
}
inline std::string studioReferencePointerSegment(const std::string& key){
    std::string result;
    for(char c:key){if(c=='~')result+="~0";else if(c=='/')result+="~1";else result+=c;}
    return result;
}
// Conservative candidates, not schema-validated rewrite locations. Explicit
// content/ paths (including missing assets) or exact existing catalog paths
// are reported; arbitrary substrings and dynamic scripts are not interpreted.
inline void studioAppendMapReferences(const Json& value,const std::string& document,
    const std::string& pointer,const std::map<std::string,std::string>& guids,
    std::vector<StudioAssetReference>& refs,size_t& visited,size_t depth=0){
    if(depth>128||++visited>2000000||pointer.size()>8192)
        throw std::runtime_error("Map reference traversal budget exceeded");
    if(value.isString()){
        const auto& raw=value.str();
        const std::string path=raw.starts_with("content/")?raw:"content/"+raw;
        const auto found=guids.find(path);
        if((raw.starts_with("content/")&&raw.size()>8)||found!=guids.end()){
            if(refs.size()>=32768||path.size()>8192)throw std::runtime_error("Asset reference result budget exceeded");
            refs.push_back({document,"map",pointer.empty()?"/":pointer,path,
                found==guids.end()?std::string{}:found->second,false});
        }
    }else if(value.isArray()){
        size_t index=0;
        for(const auto& child:value.items())
            studioAppendMapReferences(child,document,pointer+"/"+std::to_string(index++),guids,refs,visited,depth+1);
    }else if(value.isObject()){
        for(const auto& [key,child]:value.members())
            studioAppendMapReferences(child,document,pointer+"/"+studioReferencePointerSegment(key),guids,refs,visited,depth+1);
    }
}
inline std::vector<StudioAssetReference> studioCollectProjectAssetReferences(
    const std::filesystem::path& project,const Json& materials,const Json& rules,
    const Json& currentMap,const Json& manifest,const std::vector<StudioAssetEntry>& assets){
    auto refs=studioCollectAssetReferences(materials,rules,assets);
    if(refs.size()>32768)throw std::runtime_error("Asset reference result budget exceeded");
    const auto guids=studioReferenceGuids(assets);
    size_t visited=0;
    studioAppendMapReferences(currentMap,"current map","",guids,refs,visited);
    studioVisitSavedMaps(project,manifest,[&](const Json& doc,const std::string& name){
        studioAppendMapReferences(doc,name,"",guids,refs,visited);
    });
    auto gltf=studioCollectGltfReferences(project,guids);
    if(gltf.size()>32768-refs.size())throw std::runtime_error("Asset reference result budget exceeded");
    refs.insert(refs.end(),gltf.begin(),gltf.end());
    return refs;
}
// Both directions of one selected asset, without substring matches.
inline std::vector<StudioAssetReference> studioAssetDependencyEdges(
    const std::vector<StudioAssetReference>& refs,const std::string& path,bool directory){
    const auto target="content/"+path;
    std::vector<StudioAssetReference> out;
    for(const auto& ref:refs)
        if(studioReferenceMatchesPath(ref.path,target,directory)||
           studioReferenceMatchesPath(ref.document,target,directory))out.push_back(ref);
    return out;
}
inline Json studioReferenceGraphJson(const std::vector<StudioAssetReference>& refs) {
    Json result = Json::array();
    for (const auto& ref : refs) {
        Json row = Json::object();
        row.set("document", Json::string(ref.document));
        row.set("owner", Json::string(ref.owner));
        row.set("field", Json::string(ref.field));
        row.set("path", Json::string(ref.path));
        row.set("guid", Json::string(ref.guid));
        row.set("writable", Json::fromBool(ref.writable));
        result.push(std::move(row));
    }
    return result;
}

} // namespace aiverse::studio
