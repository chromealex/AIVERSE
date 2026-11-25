// Studio reference graph: typed material/rules slots and conservative exact
// map path candidates, independent of UI and genre runtime.
// materials.json stores project-relative paths; GUID is resolved via .meta index.
#pragma once
#include "apps/studio-native/StudioAssetCatalog.h"
#include "engine/data/Json.h"
#include "engine/render/Xml.h"
#include "engine/pack/Sha256.h"
#include "apps/editor/core/ProjectFileEdits.h"
#include "apps/studio-native/StudioModelRepairBytes.h"
#include "apps/studio-native/StudioTextResourceRepair.h"
#include <iterator>
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
    std::string editDocument{}; // physical text owner for OBJ/MTL repair
    size_t editLine=0;
    std::string editSlot{};
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
        if(material.isString()){
            const auto match=relocated.find(material.str());
            if(match!=relocated.end()){
                edits.emplace_back(name,Json::string(match->second));
                ++updated;
            }
            continue;
        }
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
    return ref.document.starts_with("content/")&&(ref.owner=="glTF resource"||ref.owner=="OBJ resource"||ref.owner=="DAE resource");
}
struct StudioReferenceRelocation {std::string from,to;bool directory=false;};
inline std::string studioGltfResourcePath(const std::string& source,const std::string& uri);
inline std::string studioObjResourcePath(const std::string& source,const std::string& name);
inline std::string studioDaeResourcePath(const std::string& source,const std::string& uri);
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
    const auto resolved=ref.owner=="OBJ resource"?studioObjResourcePath(owner,ref.uri):
        ref.owner=="DAE resource"?studioDaeResourcePath(owner,ref.uri):studioGltfResourcePath(owner,ref.uri);
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
                row.set("repairCandidate",Json::fromBool(
                    studioRepairModelExtension(std::filesystem::path(ref.editDocument.empty()?ref.document:ref.editDocument))&&
                    (studioRelocatedReferencePath(ref.document,defaults)!=ref.document||
                     studioRelocatedReferencePath(ref.path,defaults)!=ref.path)));
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
        size_t probe=0;
        while(input.get(first)&&std::isspace(static_cast<unsigned char>(first))){
            if(++probe>4096)throw std::runtime_error("GLB text probe budget exceeded");
        }
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
    // The runtime accepts multiple chunks. Do not permit a second JSON chunk
    // to shadow the dependency graph. Inspect headers only, seek past payloads.
    uint64_t offset=20ull+length;
    size_t chunks=0;
    while(offset<size){
        if(++chunks>4096||size-offset<8)throw std::runtime_error("Invalid GLB dependency chunk table");
        input.read(reinterpret_cast<char*>(chunk),8);
        if(input.gcount()!=8)throw std::runtime_error("Truncated GLB dependency chunk header");
        const auto next=le(chunk);
        if(le(chunk+4)==0x4e4f534a||next%4||uint64_t(next)>size-offset-8)
            throw std::runtime_error("Invalid or duplicate GLB dependency chunk");
        offset+=8ull+next;
        input.seekg(static_cast<std::streamoff>(offset),std::ios::beg);
        if(!input)throw std::runtime_error("Cannot seek GLB dependency chunk");
    }
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
inline std::string studioDaeResourcePath(const std::string& source,const std::string& uri){
    const auto first=uri.find_first_not_of(" \t\r\n"),last=uri.find_last_not_of(" \t\r\n");
    if(first==std::string::npos)return {};
    auto name=uri.substr(first,last-first+1);
    // The renderer strips file URI prefixes before reading. Never resolve
    // machine-specific absolute paths or remote resources in the editor.
    if(name.starts_with("file:///"))name.erase(0,8);
    else if(name.starts_with("file://"))name.erase(0,7);
    else if(name.starts_with("file:"))return {};
    return studioGltfResourcePath(source,name);
}
struct StudioDependencyCoverage{
    size_t gltf=0,glb=0,obj=0,dae=0,opaqueGlb=0,unsupportedModels=0,symlinks=0,unsupportedUris=0,missingLibraries=0,embedded=0;
    Json json()const{
        Json out=Json::object();
        const auto count=[&](const char* name,size_t value){out.set(name,Json::number(int64_t(value)));};
        count("gltf",gltf);count("glb",glb);count("obj",obj);count("dae",dae);count("opaqueGlb",opaqueGlb);
        count("unsupportedModels",unsupportedModels);count("symlinks",symlinks);count("unsupportedUris",unsupportedUris);
        count("missingLibraries",missingLibraries);count("embedded",embedded);
        out.set("complete",Json::fromBool(false));
        out.set("note",Json::string("Known typed edges and conservative map candidates only. DAE includes declared images; dynamic references and unhandled formats are not resolved."));
        return out;
    }
};
inline std::vector<StudioAssetReference> studioCollectGltfReferences(
    const std::filesystem::path& project,const std::map<std::string,std::string>& guids,StudioDependencyCoverage* coverage=nullptr,bool includeUnsupportedWavefront=false){
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
        if(fs::is_symlink(status)){if(coverage)++coverage->symlinks;it.disable_recursion_pending();continue;}
        const auto name=it->path().filename().string();
        if(name==".aiverse-cache"||name.starts_with(".aiverse-save-")){it.disable_recursion_pending();continue;}
        if(!fs::is_regular_file(status))continue;
        auto ext=it->path().extension().string();
        for(auto& c:ext)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if(ext!=".gltf"&&ext!=".glb"&&ext!=".obj"&&ext!=".dae"){
            if(coverage&&(ext==".fbx"||ext==".3ds"||ext==".3do"||ext==".mdl"||ext==".mdx"||ext==".w3d"||ext==".ply"||ext==".vox"))++coverage->unsupportedModels;
            continue;
        }
        if(models.size()>=2048)throw std::runtime_error("glTF dependency file budget exceeded");
        models.push_back(it->path());
    }
    std::sort(models.begin(),models.end());
    std::uintmax_t bytes=0;
    for(const auto& model:models){
        auto ext=model.extension().string();
        for(auto& c:ext)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const auto source=model.lexically_relative(root).generic_string();
        const auto append=[&](const std::string& owner,const std::string& field,const std::string& name,const std::string& target,
            const std::string& editDocument=std::string{},size_t editLine=0,const std::string& editSlot=std::string{}){
            if(target.empty()){
                if(coverage&&!name.empty()){
                    if(name.starts_with("data:"))++coverage->embedded;else ++coverage->unsupportedUris;
                }
                if(!includeUnsupportedWavefront||owner!="OBJ resource"||editDocument.empty())return;
            }
            const auto found=guids.find(target);
            if(refs.size()>=32768)throw std::runtime_error("Model dependency result budget exceeded");
            refs.push_back({source,owner,field,target,found==guids.end()?std::string{}:found->second,false,name,editDocument,editLine,editSlot});
        };
        if(ext==".dae"){
            if(coverage)++coverage->dae;
            studioAccountModelJson(fs::file_size(model),bytes);
            std::ifstream input(model,std::ios::binary);
            if(!input)throw std::runtime_error("Cannot read DAE dependency");
            const std::string data{std::istreambuf_iterator<char>(input),{}};
            const auto doc=render::xml::parse(render::xml::toUtf8(data));
            if(doc.name!="COLLADA")throw std::runtime_error("Invalid DAE dependency root");
            // Conservative declared image candidates (even unused effects);
            // do not confuse surface init_from IDs with image file paths.
            size_t index=0;
            doc.each("library_images",[&](const render::xml::Element& library){
                library.each("image",[&](const render::xml::Element& image){
                    const auto field="/library_images/image/"+std::to_string(index++)+"/init_from";
                    const auto* init=image.child("init_from");
                    if(!init){if(coverage)++coverage->embedded;return;}
                    const auto* ref=init->child("ref");
                    const auto name=ref?ref->text:init->text;
                    append("DAE resource",field,name,studioDaeResourcePath(source,name));
                });
            });
            continue;
        }
        if(ext==".obj"){
            if(coverage)++coverage->obj;
            studioVisitWavefront(model,bytes,[&](const std::string& key,const std::string& name,size_t line){
                if(key!="mtllib")return;
                const auto mtl=studioObjResourcePath(source,name);
                append("OBJ resource","line "+std::to_string(line)+" / mtllib",name,mtl,source,line,"mtllib");
                if(mtl.empty())return;
                if(!studioSafeModelResourceFile(root,mtl)){if(coverage)++coverage->missingLibraries;return;}
                bool material=false;
                studioVisitWavefront(root/mtl,bytes,[&](const std::string& slot,const std::string& rest,size_t row){
                    if(slot=="newmtl"){material=true;return;}
                    if(!material||(slot!="map_Kd"&&slot!="map_Bump"&&slot!="map_bump"&&slot!="bump"&&slot!="norm"&&slot!="map_Ke"))return;
                    const auto split=rest.find_last_of(" \t");
                    const auto texture=split==std::string::npos?rest:rest.substr(split+1);
                    append("OBJ resource",mtl+":"+std::to_string(row)+" / "+slot,texture,studioObjResourcePath(source,texture),mtl,row,slot);
                });
            });
            continue;
        }
        const auto parsed=studioReadModelJson(model,bytes,ext==".glb");
        if(!parsed){if(coverage)++coverage->opaqueGlb;continue;}
        if(coverage){if(ext==".glb")++coverage->glb;else ++coverage->gltf;}
        const auto& doc=*parsed;
        if(!doc.isObject())throw std::runtime_error("Invalid glTF dependency document");
        for(const char* group:{"buffers","images"}){
            if(doc.has(group)&&!doc[group].isArray())throw std::runtime_error("Invalid glTF resource array");
            size_t index=0;
            for(const auto& item:doc[group].items()){
                const auto field=std::string("/")+group+"/"+std::to_string(index++)+"/uri";
                if(!item.has("uri")){if(coverage)++coverage->embedded;continue;}
                if(!item["uri"].isString())throw std::runtime_error("Invalid glTF resource URI");
                const auto path=studioGltfResourcePath(source,item.getStr("uri"));
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
// Repair typed textual glTF slots against their projected owner and target.
// The original and new complete bytes are retained verbatim.
inline std::string studioRelativeGltfUri(const std::string& owner,const std::string& target){
    const auto rel=std::filesystem::path(target).lexically_relative(std::filesystem::path(owner).parent_path()).generic_string();
    static constexpr char hex[]="0123456789ABCDEF";
    std::string uri;
    for(unsigned char c:rel){
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~'||c=='/')uri+=char(c);
        else{uri+='%';uri+=hex[c>>4];uri+=hex[c&15];}
    }
    if(uri.empty()||studioGltfResourcePath(owner,uri)!=target)throw std::runtime_error("Cannot encode safe relative glTF URI");
    return uri;
}
inline std::vector<editor::ProjectFileEdit> studioPlanGltfRepairs(const std::filesystem::path& project,
    const std::vector<StudioReferenceRelocation>& moves,const std::vector<editor::ProjectFileEdit>* replay=nullptr){
    namespace fs=std::filesystem;
    struct Pending{std::string before;StudioModelRepairDocument doc;std::string tailDigest{};uint64_t tailBytes=0;};
    std::map<std::string,Pending> pending;
    struct TextDemand{std::string original,desired;bool changed=false;};
    std::map<std::string,std::map<std::string,TextDemand>> demands;
    std::set<std::string> changedText;
    size_t bytes=0;
    const auto root=fs::canonical(project);
    if(moves.empty())return {};
    for(const auto& ref:studioCollectGltfReferences(project,{},nullptr,true)){
        const bool safe=ref.path.empty()||studioGltfRelocationSafe(ref,moves);
        const auto owner=studioRelocatedReferencePath(ref.document,moves),target=studioRelocatedReferencePath(ref.path,moves);
        const auto physical=ref.editDocument.empty()?ref.document:ref.editDocument;
        // An inverse move may keep the repaired URI valid while restoring its
        // original equivalent spelling. Recheck the retained document's slots.
        const bool replayMovedOwner=safe&&!ref.path.empty()&&owner!=ref.document&&replay&&
            std::any_of(replay->begin(),replay->end(),[&](const auto& edit){return edit.path==physical;});
        if(ref.owner=="OBJ resource"||ref.owner=="DAE resource"){
            const auto key=ref.owner=="DAE resource"?ref.field:std::to_string(ref.editLine)+"/"+ref.editSlot;
            std::string desired=ref.uri;
            if(!safe||replayMovedOwner){
                for(const auto& move:moves)
                    if(target==ref.path&&studioReferenceMatchesPath(ref.path,move.to,move.directory))
                        throw std::runtime_error("Move would adopt an existing destination reference");
                if(ref.owner=="OBJ resource"){
                    desired=fs::path(target).lexically_relative(fs::path(owner).parent_path()).generic_string();
                    if(desired.empty()||studioObjResourcePath(owner,desired)!=target||
                       (ref.editSlot!="mtllib"&&desired.find_first_of(" \t\r\n")!=std::string::npos))
                        throw std::runtime_error("Wavefront loader cannot represent repaired resource name");
                }else desired=studioRelativeGltfUri(owner,target);
                changedText.insert(physical);
            }
            // Shared MTL slots are resolved relative to each consuming OBJ.
            // Keep every consumer's demand, including unchanged/unsupported slots.
            auto& slots=demands[physical];
            const auto it=slots.find(key);
            if(it==slots.end())slots.emplace(key,TextDemand{ref.uri,desired,desired!=ref.uri});
            else if(it->second.original!=ref.uri||it->second.desired!=desired)
                throw std::runtime_error("Shared MTL requires incompatible resource paths: "+physical);
            continue;
        }
        if(safe&&!replayMovedOwner)continue;
        for(const auto& move:moves)
            if(target==ref.path&&studioReferenceMatchesPath(ref.path,move.to,move.directory))
                throw std::runtime_error("Move would adopt an existing destination reference");
        if(ref.owner!="glTF resource"||!studioRepairModelExtension(fs::path(ref.document))||(owner==ref.document&&target==ref.path)||
           !studioSafeModelResourceFile(root,ref.document))
            throw std::runtime_error("Model resource cannot be repaired safely: "+ref.document+" "+ref.field);
        auto it=pending.find(ref.document);
        if(it==pending.end()){
            if(pending.size()>=32)throw std::runtime_error("glTF repair document count exceeded");
            const auto path=root/ref.document;
            bool compact=fs::file_size(path)>1024u*1024u&&fs::path(ref.document).extension()==".glb"&&studioGlbMagicFile(path);
            if(replay)for(const auto& saved:*replay)if(saved.path.generic_string()==ref.document)
                compact=studioStreamingResource(saved);
            Pending value;
            if(compact){
                auto snapshot=studioReadGlbPrefix(path);
                value.before=std::move(snapshot.prefix);value.tailDigest=std::move(snapshot.digest);value.tailBytes=snapshot.tailBytes;
                value.doc.json=studioDecodeGlbPrefix(value.before,value.tailBytes);
            }else{
                if(fs::file_size(path)>StudioResourceEditBudget/2-bytes)throw std::runtime_error("glTF repair history budget exceeded");
                value.before=editor::readProjectFileBytes(path);
                value.doc=studioDecodeModelRepair(fs::path(ref.document),value.before);
            }
            if(value.before.size()>StudioResourceEditBudget/2-bytes)throw std::runtime_error("glTF repair history budget exceeded");
            bytes+=value.before.size();it=pending.emplace(ref.document,std::move(value)).first;
        }
        // Use typed slots only, and validate the exact original URI.
        bool found=false;
        for(const char* group:{"buffers","images"}){
            const auto prefix=std::string("/")+group+"/";
            if(!ref.field.starts_with(prefix))continue;
            const auto& entries=it->second.doc.json[group];
            for(size_t i=0;i<entries.size();++i){
                if(ref.field!=prefix+std::to_string(i)+"/uri")continue;
                auto item=entries[i];
                if(item.getStr("uri")!=ref.uri)throw std::runtime_error("glTF URI changed during repair planning");
                item.set("uri",Json::string(studioRelativeGltfUri(owner,target)));
                auto array=entries;array.at(i)=std::move(item);it->second.doc.json.set(group,std::move(array));
                found=true;break;
            }
            if(found)break;
        }
        if(!found)throw std::runtime_error("Unknown glTF repair slot");
    }
    std::vector<editor::ProjectFileEdit> result;
    size_t afterBytes=0;
    for(const auto& path:changedText){
        if(pending.size()+result.size()>=32||!studioRepairModelExtension(fs::path(path))||
           !studioSafeModelResourceFile(root,path)||fs::file_size(root/path)>StudioResourceEditBudget/2-bytes)
            throw std::runtime_error("Text resource repair history budget or file type");
        auto before=editor::readProjectFileBytes(root/path);
        if(before.size()>StudioResourceEditBudget/2-bytes)throw std::runtime_error("Text resource repair budget exceeded");
        bytes+=before.size();
        const auto slots=studioTextSlots(fs::path(path),before);
        std::map<std::string,std::string> replacements;
        for(const auto& [key,demand]:demands.at(path)){
            if(!demand.changed)continue;
            const auto slot=std::find_if(slots.begin(),slots.end(),[&](const auto& value){return value.key==key;});
            if(slot==slots.end()||slot->value!=demand.original)
                throw std::runtime_error("Text resource changed during repair planning");
            replacements.emplace(key,fs::path(path).extension()==".dae"?studioXmlResourceEscape(demand.desired):demand.desired);
        }
        auto after=studioPatchResourceText(fs::path(path),before,slots,replacements);
        if(after.size()>StudioResourceEditBudget/2-afterBytes)throw std::runtime_error("Text resource repair budget exceeded");
        afterBytes+=after.size();
        result.push_back({fs::path(path),std::move(before),std::move(after)});
    }
    bytes+=afterBytes;
    for(auto& [path,edit]:pending){
        auto after=edit.tailDigest.empty()?studioEncodeModelRepair(edit.doc):studioEncodeGlbPrefix(edit.doc.json,edit.tailBytes);
        // The same planner runs in both replay directions. Bound each side
        // independently so a larger JSON chunk never makes undo impossible.
        if(after.size()>StudioResourceEditBudget/2-afterBytes||
           after.size()>StudioResourceEditBudget-bytes)
            throw std::runtime_error("glTF repair history budget exceeded");
        afterBytes+=after.size();bytes+=after.size();
        result.push_back({fs::path(path),std::move(edit.before),std::move(after),std::move(edit.tailDigest),edit.tailBytes});
    }
    const auto indexPath=root/"materials.json";
    if(fs::is_regular_file(fs::symlink_status(indexPath))){
        const auto index=Json::parseFile(indexPath.string());
        std::set<std::string> visited;
        for(const auto& [name,entry]:index["materials"].members()){
            if(!entry.isString())continue;
            const auto path=entry.str();
            if(!visited.insert(path).second)continue;
            if(!view::materialAssetPath(path)||!studioSafeModelResourceFile(root,path))
                throw std::runtime_error("Invalid indexed material: "+name);
            auto before=editor::readProjectFileBytes(root/path);
            auto material=Json::parse(before);
            if(!material.isObject())throw std::runtime_error("Invalid material: "+name);
            bool changed=false;
            for(const auto* key:kStudioTextureFields){
                const auto source=material.getStr(key);
                if(source.empty())continue;
                const auto target=studioRelocatedReferencePath(source,moves);
                if(target==source)continue;
                material.set(key,Json::string(target));
                changed=true;
            }
            if(!changed)continue;
            auto after=material.dump(2)+"\n";
            if(result.size()>=32||before.size()>StudioResourceEditBudget-bytes||
               after.size()>StudioResourceEditBudget-bytes-before.size())
                throw std::runtime_error("Material repair history budget exceeded");
            bytes+=before.size()+after.size();
            result.push_back({fs::path(path),std::move(before),std::move(after)});
        }
    }
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.path.generic_string()<b.path.generic_string();});
    return result;
}
// Replay may restore an equivalent noncanonical URI spelling and original
// formatting. Compare normalized typed resource resolution, all other JSON
// fields unchanged; exact source bytes are separately verified.
inline std::string studioCanonicalGltfRepair(const std::string& path,const std::string& bytes,const std::string& tailDigest={},uint64_t tailBytes=0){
    if(view::materialAssetPath(path)){
        const auto document=Json::parse(bytes);
        if(!document.isObject()||!tailDigest.empty()||tailBytes)throw std::runtime_error("Invalid material repair");
        return document.dump(-1);
    }
    if(!studioRepairGltfExtension(std::filesystem::path(path))){
        const auto slots=studioTextSlots(std::filesystem::path(path),bytes);
        std::map<std::string,std::string> normalized;
        for(const auto& slot:slots){
            if(std::filesystem::path(path).extension()==".dae"){
                const auto resolved=studioDaeResourcePath(path,slot.value);
                if(!resolved.empty())normalized.emplace(slot.key,studioXmlResourceEscape(resolved));
            }else{
                const auto& value=slot.value;
                if(!value.empty()&&value.front()!='/'&&value.find_first_of(":\\")==std::string::npos)
                    normalized.emplace(slot.key,std::filesystem::path(value).lexically_normal().generic_string());
            }
        }
        return studioPatchResourceText(std::filesystem::path(path),bytes,slots,normalized);
    }
    StudioModelRepairDocument model;
    if(tailDigest.empty())model=studioDecodeModelRepair(std::filesystem::path(path),bytes);
    else{model.binary=true;model.json=studioDecodeGlbPrefix(bytes,tailBytes);model.tail=tailDigest;}
    auto& doc=model.json;
    for(const char* group:{"buffers","images"}){
        auto array=doc[group];
        if(!array.isArray())continue;
        for(size_t i=0;i<array.size();++i){
            auto item=array[i];const auto uri=item.getStr("uri");
            const auto resolved=studioGltfResourcePath(path,uri);
            if(!resolved.empty()){item.set("uri",Json::string(resolved));array.at(i)=std::move(item);}
        }
        doc.set(group,std::move(array));
    }
    // Include binary identity, with explicit framing to avoid collisions.
    const auto normalized=doc.dump(-1);
    return std::string(model.binary?"glb:":"text:")+std::to_string(normalized.size())+":"+normalized+model.tail;
}
inline std::vector<editor::ProjectFileEdit> studioAbsoluteResourceEdits(const std::filesystem::path& project,
    const std::vector<editor::ProjectFileEdit>& edits,const std::vector<StudioReferenceRelocation>& moves={}){
    auto result=edits;
    const auto root=std::filesystem::canonical(project);
    for(auto& edit:result)edit.path=root/studioRelocatedReferencePath(edit.path.generic_string(),moves);
    return result;
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
    const Json& currentMap,const Json& manifest,const std::vector<StudioAssetEntry>& assets,
    StudioDependencyCoverage* coverage=nullptr){
    auto refs=studioCollectAssetReferences(materials,rules,assets);
    if(refs.size()>32768)throw std::runtime_error("Asset reference result budget exceeded");
    const auto guids=studioReferenceGuids(assets);
    size_t visited=0;
    studioAppendMapReferences(currentMap,"current map","",guids,refs,visited);
    studioVisitSavedMaps(project,manifest,[&](const Json& doc,const std::string& name){
        studioAppendMapReferences(doc,name,"",guids,refs,visited);
    });
    auto gltf=studioCollectGltfReferences(project,guids,coverage);
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

// Page only the selected direction while hashing the complete selected graph
// and coverage. A subsequent page cannot silently mix two graph snapshots.
inline Json studioDependencyPage(const std::vector<StudioAssetReference>& refs,
    const std::string& path,bool directory,size_t depth,const std::string& direction,
    size_t offset,size_t limit,const Json& coverage,const std::string& expectedToken){
    if(depth>16||limit<1||limit>100||offset>65536||
       (direction!="all"&&direction!="incoming"&&direction!="outgoing")||
       (depth==0&&direction!="all"))throw std::runtime_error("Invalid dependency page");
    pack::Sha256 hash;
    const auto add=[&](const std::string& value){
        const auto len=std::to_string(value.size())+":";
        hash.update(len.data(),len.size());hash.update(value.data(),value.size());
    };
    add(path);add(std::to_string(depth));add(directory?"folder":"file");add(coverage.dump(-1));
    Json rows=Json::array();
    size_t total=0,incoming=0,outgoing=0,unresolved=0;
    const auto emit=[&](const StudioAssetReference& ref,const char* side,size_t hop,bool cycle){
        auto row=studioReferenceGraphJson({ref})[0];
        if(hop){
            row.set("direction",Json::string(side));row.set("depth",Json::number(int64_t(hop)));
            row.set("cycle",Json::fromBool(cycle));
        }
        add(row.dump(-1));add(ref.uri);
        if(ref.guid.empty())++unresolved;
        if(std::string(side)=="incoming")++incoming;
        if(std::string(side)=="outgoing")++outgoing;
        if(direction=="all"||direction==side){
            if(total>=offset&&rows.size()<limit)rows.push(std::move(row));
            ++total;
        }
    };
    if(depth==0){
        for(const auto& ref:studioAssetDependencyEdges(refs,path,directory))emit(ref,"all",0,false);
    }else{
        for(bool reverse:{true,false})
            for(const auto& visit:studioTraverseAssetDependencies(refs,path,directory,reverse,depth))
                emit(refs[visit.edge],reverse?"incoming":"outgoing",visit.depth,visit.cycle);
    }
    const auto digest=hash.finish();const auto token=pack::toHex(digest.data(),digest.size());
    if(!expectedToken.empty()&&expectedToken!=token)throw std::runtime_error("Dependency graph changed; refresh from page one");
    Json out=Json::object();out.set("items",std::move(rows));out.set("token",Json::string(token));
    out.set("total",Json::number(int64_t(total)));out.set("incoming",Json::number(int64_t(incoming)));
    out.set("outgoing",Json::number(int64_t(outgoing)));out.set("unresolved",Json::number(int64_t(unresolved)));
    out.set("coverage",coverage);out.set("depth",Json::number(int64_t(depth)));return out;
}

} // namespace aiverse::studio
