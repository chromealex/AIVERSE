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
#include <sstream>
#include <optional>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace aiverse::studio {

struct StudioAssetReference {
    std::string document; // project document or current map
    std::string owner;    // material/type name or map scope
    std::string field;    // documented slot or JSON pointer
    std::string path;     // runtime-compatible project-relative path
    std::string guid;     // empty for unresolved/legacy paths
    bool writable = false; // currently only materials.json slots support repair
    std::string uri{}; // original model resource name, native relocation validation only
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
    return ref.document.starts_with("content/")&&(ref.owner=="glTF resource"||ref.owner=="OBJ resource");
}
struct StudioReferenceRelocation {std::string from,to;bool directory=false;};
inline std::string studioGltfResourcePath(const std::string& source,const std::string& uri);
inline std::string studioObjResourcePath(const std::string& source,const std::string& name);
inline std::string studioRelocatedReferencePath(const std::string& path,
    const std::vector<StudioReferenceRelocation>& moves){
    for(const auto& move:moves)
        if(studioReferenceMatchesPath(path,move.from,move.directory))
            return move.to+path.substr(move.from.size());
    return path;
}
// Only allow unchanged local URI resolution. Refuse adopting an unresolved
// destination reference, and do not guess about encoded or parent traversal
// when the owner changes directories.
inline bool studioGltfRelocationSafe(const StudioAssetReference& ref,
    const std::vector<StudioReferenceRelocation>& moves){
    const auto owner=studioRelocatedReferencePath(ref.document,moves);
    const auto target=studioRelocatedReferencePath(ref.path,moves);
    for(const auto& move:moves)
        if(target==ref.path&&studioReferenceMatchesPath(ref.path,move.to,move.directory))
            return false;
    if(owner==ref.document&&target==ref.path)return true;
    if(ref.uri.empty())return false;
    if(std::filesystem::path(owner).parent_path()!=std::filesystem::path(ref.document).parent_path()&&
       (ref.uri.find("..")!=std::string::npos||ref.uri.find('%')!=std::string::npos))return false;
    const auto resolved=ref.owner=="OBJ resource"?studioObjResourcePath(owner,ref.uri):studioGltfResourcePath(owner,ref.uri);
    return resolved==target;
}
// Resolve the set of known dependencies affected by an internal move.
// This is a preview, not a rename executor. Exact paths and folder descendants
// are matched; an unaffected resource in another folder must not match.
inline Json studioMoveReferenceImpact(const Json& transfer,
    const std::vector<StudioAssetReference>& references) {
    Json result=Json::array();
    std::vector<StudioReferenceRelocation> defaults;
    for(const auto& item:transfer["items"].items())
        if(item["from"].isString()&&item["to"].isString()&&item.getStr("conflict").empty())
            defaults.push_back({"content/"+item.getStr("from"),"content/"+item.getStr("to"),item.getBool("directory")});
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
            const bool gltf=studioIsGltfResourceReference(ref);
            const bool destinationReference=gltf&&studioReferenceMatchesPath(ref.path,destination,folder);
            if(!targetMoves&&!ownerMoves&&!destinationReference)continue;
            // Preview only proves a single non-conflicting default relocation;
            // the native transfer owner checks the actual complete plan again.
            if(gltf&&transfer["items"].size()==1&&item.getStr("conflict").empty()&&
               studioGltfRelocationSafe(ref,{{source,destination,folder}}))continue;
            Json row=Json::object();
            row.set("id",item["id"]);
            row.set("document",Json::string(ref.document));
            row.set("owner",Json::string(ref.owner));
            row.set("field",Json::string(ref.field));
            row.set("from",Json::string(ref.path));
            row.set("to",Json::string(targetMoves?destination+ref.path.substr(source.size()):ref.path));
            row.set("writable",Json::fromBool(ref.writable));
            if(gltf){
                // Exact selected paths (including keep-both names) are checked
                // by the native transfer owner. A resource row is never a
                // promise that rewriting the source is supported.
                row.set("validateOnApply",Json::fromBool(true));
                row.set("preservesDefault",Json::fromBool(studioGltfRelocationSafe(ref,defaults)));
            }
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
// The OBJ loader uses the original model directory for both mtllib and
// texture paths, and does not percent-decode names.
inline std::string studioObjResourcePath(const std::string& source,const std::string& name){
    if(name.empty()||name.size()>8192||name.front()=='/'||name.find_first_of(":\\")!=std::string::npos)return {};
    for(unsigned char c:name)if(c<32||c==127)return {};
    const auto path=(std::filesystem::path(source).parent_path()/name).lexically_normal().generic_string();
    if(!path.starts_with("content/")||path.size()<=8||path.ends_with("/"))return {};
    return path;
}
inline bool studioSafeModelResourceFile(const std::filesystem::path& root,const std::string& path){
    namespace fs=std::filesystem;
    auto cursor=root;
    for(const auto& part:fs::path(path)){
        cursor/=part;
        const auto status=fs::symlink_status(cursor);
        if(!fs::exists(status))return false;
        if(fs::is_symlink(status))throw std::runtime_error("Symlink in model dependency path");
    }
    if(!fs::is_regular_file(fs::symlink_status(cursor)))throw std::runtime_error("Invalid model dependency file");
    return true;
}
inline void studioAccountModelJson(std::uintmax_t size,std::uintmax_t& bytes){
    if(size>16ull*1024*1024||size>64ull*1024*1024-bytes)
        throw std::runtime_error("Model dependency byte budget exceeded");
    bytes+=size;
}
// GLB v2: stream the JSON chunk, never load BIN payloads. Non-GLB opaque
// files remain outside coverage (some legacy projects have placeholder models).
inline std::optional<Json> studioReadModelJson(const std::filesystem::path& path,
    std::uintmax_t& bytes,bool binary){
    namespace fs=std::filesystem;
    const auto size=fs::file_size(path);
    if(!binary){studioAccountModelJson(size,bytes);return Json::parseFile(path.string());}
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("Cannot read GLB dependency");
    unsigned char header[12]{};
    input.read(reinterpret_cast<char*>(header),12);
    const auto count=input.gcount();
    if(count<4||std::memcmp(header,"glTF",4)!=0){
        // Runtime also accepts a text JSON model regardless of extension.
        input.clear();input.seekg(0);
        char first=0;
        while(input.get(first)&&std::isspace(static_cast<unsigned char>(first))){}
        if(first!='{')return std::nullopt;
        studioAccountModelJson(size,bytes);return Json::parseFile(path.string());
    }
    const auto le=[](const unsigned char* p){
        return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
    };
    if(count!=12||le(header+4)!=2||uint64_t(le(header+8))!=size||size<20)
        throw std::runtime_error("Invalid GLB dependency header");
    unsigned char chunk[8]{};
    input.read(reinterpret_cast<char*>(chunk),8);
    if(input.gcount()!=8||le(chunk+4)!=0x4e4f534a||!le(chunk)||le(chunk)%4||uint64_t(le(chunk))>size-20)
        throw std::runtime_error("Invalid GLB JSON chunk");
    const auto length=le(chunk);studioAccountModelJson(length,bytes);
    std::string json(length,'\0');
    input.read(json.data(),static_cast<std::streamsize>(length));
    if(input.gcount()!=static_cast<std::streamsize>(length))throw std::runtime_error("Truncated GLB JSON chunk");
    return Json::parse(json);
}
inline std::string studioWavefrontTrim(const std::string& s){
    const auto start=s.find_first_not_of(" \t\r"),end=s.find_last_not_of(" \t\r");
    return start==std::string::npos?std::string{}:s.substr(start,end-start+1);
}
template<class Visitor>
inline void studioVisitWavefront(const std::filesystem::path& path,std::uintmax_t& bytes,Visitor&& visitor){
    studioAccountModelJson(std::filesystem::file_size(path),bytes);
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("Cannot read Wavefront dependency");
    std::string line;size_t index=0;
    while(std::getline(input,line)){
        ++index;if(line.size()>1024*1024)throw std::runtime_error("Wavefront dependency line budget exceeded");
        line=studioWavefrontTrim(line);
        if(line.empty()||line.front()=='#')continue;
        std::istringstream stream(line);std::string key,rest;stream>>key;std::getline(stream,rest);
        visitor(key,studioWavefrontTrim(rest),index);
    }
    if(input.bad())throw std::runtime_error("Cannot finish Wavefront dependency read");
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
        if(ext!=".gltf"&&ext!=".glb"&&ext!=".obj")continue;
        if(models.size()>=2048)throw std::runtime_error("glTF dependency file budget exceeded");
        models.push_back(it->path());
    }
    std::sort(models.begin(),models.end());
    std::uintmax_t bytes=0;
    for(const auto& model:models){
        auto ext=model.extension().string();
        for(auto& c:ext)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const auto source=model.lexically_relative(root).generic_string();
        const auto append=[&](const std::string& owner,const std::string& field,const std::string& name,const std::string& target){
            if(target.empty())return;
            const auto found=guids.find(target);
            if(refs.size()>=32768)throw std::runtime_error("Model dependency result budget exceeded");
            refs.push_back({source,owner,field,target,found==guids.end()?std::string{}:found->second,false,name});
        };
        if(ext==".obj"){
            studioVisitWavefront(model,bytes,[&](const std::string& key,const std::string& name,size_t line){
                if(key!="mtllib")return;
                const auto mtl=studioObjResourcePath(source,name);
                append("OBJ resource","line "+std::to_string(line)+" / mtllib",name,mtl);
                if(mtl.empty()||!studioSafeModelResourceFile(root,mtl))return;
                bool material=false;
                studioVisitWavefront(root/mtl,bytes,[&](const std::string& slot,const std::string& rest,size_t row){
                    if(slot=="newmtl"){material=true;return;}
                    if(!material||(slot!="map_Kd"&&slot!="map_Bump"&&slot!="map_bump"&&slot!="bump"&&slot!="norm"&&slot!="map_Ke"))return;
                    const auto split=rest.find_last_of(" \t");
                    const auto texture=split==std::string::npos?rest:rest.substr(split+1);
                    append("OBJ resource",mtl+":"+std::to_string(row)+" / "+slot,texture,studioObjResourcePath(source,texture));
                });
            });
            continue;
        }
        const auto parsed=studioReadModelJson(model,bytes,ext==".glb");
        if(!parsed)continue;
        const auto& doc=*parsed;
        if(!doc.isObject())throw std::runtime_error("Invalid glTF dependency document");
        for(const char* group:{"buffers","images"}){
            if(doc.has(group)&&!doc[group].isArray())throw std::runtime_error("Invalid glTF resource array");
            size_t index=0;
            for(const auto& item:doc[group].items()){
                const auto field=std::string("/")+group+"/"+std::to_string(index++)+"/uri";
                if(!item.has("uri"))continue;
                if(!item["uri"].isString())throw std::runtime_error("Invalid glTF resource URI");
                const auto path=studioGltfResourcePath(source,item.getStr("uri"));
                if(path.empty())continue;
                append("glTF resource",field,item.getStr("uri"),path);
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
inline void studioCheckGltfRelocations(const std::filesystem::path& project,
    const std::vector<StudioReferenceRelocation>& moves){
    if(moves.empty())return;
    for(const auto& ref:studioCollectGltfReferences(project,{}))
        if(!studioGltfRelocationSafe(ref,moves))
            throw std::runtime_error("Asset move would change model resource resolution: "+ref.document+" "+ref.field);
}
inline void studioCheckMapAssetPaths(const std::filesystem::path& project,
    const Json& currentMap,const Json& rules,const Json& manifest,
    const std::vector<std::pair<std::string,bool>>& paths,bool checkGltf=true){
    auto check=[&](const Json& doc,const std::string& label){
        for(const auto& [path,directory]:paths)
            if(studioDocumentNamesAsset(doc,path,directory))
                throw std::runtime_error("Asset move blocked by "+label+" reference: "+path);
    };
    if(paths.empty())return;
    check(currentMap,"current map");check(rules,"rules");
    studioVisitSavedMaps(project,manifest,check);
    if(checkGltf)studioCheckGltfAssetPaths(project,paths);
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
// Bounded breadth-first traversal yields shortest edge distances. Only
// asset documents are expanded; map/material/rules owners are terminal nodes.
// Iterative DFS marks directed cycle-closing edges (not every edge in an SCC).
struct StudioDependencyVisit {size_t edge=0,depth=0;bool cycle=false;};
inline std::vector<StudioDependencyVisit> studioTraverseAssetDependencies(
    const std::vector<StudioAssetReference>& refs,const std::string& path,bool directory,
    bool reverse,size_t maxDepth){
    if(maxDepth<1||maxDepth>16)throw std::runtime_error("Invalid dependency depth");
    using Index=std::map<std::string,std::vector<size_t>>;
    Index adjacency;
    const auto root="content/"+path;
    const auto source=[&](size_t i)->const std::string&{return reverse?refs[i].path:refs[i].document;};
    const auto target=[&](size_t i)->const std::string&{return reverse?refs[i].document:refs[i].path;};
    for(size_t i=0;i<refs.size();++i)
        if(source(i).starts_with("content/"))adjacency[source(i)].push_back(i);
    std::map<std::string,size_t> distance;
    std::vector<std::string> queue;
    const auto seed=[&](const std::string& node){if(distance.emplace(node,0).second)queue.push_back(node);};
    if(directory){for(const auto& [node,edges]:adjacency){(void)edges;if(studioReferenceMatchesPath(node,root,true))seed(node);}}
    else seed(root);
    std::vector<StudioDependencyVisit> result;
    std::map<size_t,size_t> included;
    for(size_t q=0;q<queue.size();++q){
        const auto node=queue[q];const auto d=distance.at(node);
        if(d>=maxDepth)continue;
        const auto it=adjacency.find(node);if(it==adjacency.end())continue;
        for(size_t e:it->second){
            if(!included.emplace(e,result.size()).second)continue;
            result.push_back({e,d+1,false});
            if(target(e).starts_with("content/")&&distance.emplace(target(e),d+1).second)queue.push_back(target(e));
        }
    }
    std::map<std::string,int> color;
    struct Frame{std::string node;size_t next=0;};
    for(const auto& node:queue){
        if(color[node])continue;
        std::vector<Frame> stack{{node,0}};color[node]=1;
        while(!stack.empty()){
            auto& frame=stack.back();
            const auto it=adjacency.find(frame.node);
            if(it==adjacency.end()||frame.next>=it->second.size()){
                color[frame.node]=2;stack.pop_back();continue;
            }
            const auto e=it->second[frame.next++];
            const auto mark=included.find(e);if(mark==included.end()||!target(e).starts_with("content/"))continue;
            const auto& next=target(e);
            if(color[next]==1)result[mark->second].cycle=true;
            else if(!color[next]){color[next]=1;stack.push_back({next,0});}
        }
    }
    return result;
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

inline Json studioDependencyTraversalJson(const std::vector<StudioAssetReference>& refs,
    const std::string& path,bool directory,size_t depth){
    Json out=Json::array();
    for(bool reverse:{true,false}){
        for(const auto& visit:studioTraverseAssetDependencies(refs,path,directory,reverse,depth)){
            auto rows=studioReferenceGraphJson({refs[visit.edge]});
            auto row=rows[0];
            row.set("direction",Json::string(reverse?"incoming":"outgoing"));
            row.set("depth",Json::number(static_cast<int64_t>(visit.depth)));
            row.set("cycle",Json::fromBool(visit.cycle));
            out.push(std::move(row));
        }
    }
    return out;
}

} // namespace aiverse::studio
