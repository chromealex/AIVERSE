#include "apps/studio-native/StudioAssetCatalog.h"
#include "apps/studio-native/StudioContentDirectory.h"
#include <sqlite3.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <climits>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <random>
#include <regex>
#include <stdexcept>
#include <system_error>
namespace fs=std::filesystem;
namespace aiverse::studio {
namespace {
void sqlCheck(int status,sqlite3* db,const char* context) {
    if(status!=SQLITE_OK && status!=SQLITE_DONE && status!=SQLITE_ROW)
        throw std::runtime_error(std::string(context)+": "+sqlite3_errmsg(db));
}
struct Statement {
    sqlite3* db;
    sqlite3_stmt* stmt=nullptr;
    Statement(sqlite3* handle,const char* sql):db(handle){
        sqlCheck(sqlite3_prepare_v2(db,sql,-1,&stmt,nullptr),db,"prepare AssetIndex");
    }
    ~Statement(){if(stmt)sqlite3_finalize(stmt);}
};
std::string generateGuid() {
    std::array<unsigned char,16> bytes{};
    std::random_device rd;
    for(unsigned char& byte:bytes)byte=static_cast<unsigned char>(rd()&255u);
    bytes[6]=(bytes[6]&0x0fu)|0x40u; // UUID v4
    bytes[8]=(bytes[8]&0x3fu)|0x80u;
    const char* hex="0123456789abcdef";
    std::string out;
    out.reserve(32);
    for(unsigned char b:bytes){out+=hex[b>>4];out+=hex[b&15];}
    return out;
}
bool isSidecar(const fs::path& p){
    const auto name=p.filename().string();
    return p.extension()==".meta" ||
       name.find(".aiverse-meta-update")!=std::string::npos ||
       name.find(".aiverse-tmp")!=std::string::npos ||
       name.find(".aiverse-backup-")!=std::string::npos;
}
void writeMeta(const fs::path& path,const Json& doc) {
    const auto tmp=fs::path(path.string()+".aiverse-tmp");
    if(fs::exists(path))throw std::runtime_error("Metadata already exists: "+path.filename().string());
    {
        std::ofstream output(tmp,std::ios::binary|std::ios::trunc);
        if(!output)throw std::runtime_error("Cannot create metadata sidecar");
        output<<doc.dump(2)<<"\n";
        if(!output){fs::remove(tmp);throw std::runtime_error("Cannot write metadata sidecar");}
    }
    // Never overwrite an existing sidecar; retain original on a collision.
    if(fs::exists(path)){fs::remove(tmp);throw std::runtime_error("Metadata collision");}
    std::error_code ec;
    fs::rename(tmp,path,ec);
    if(ec){fs::remove(tmp);throw std::runtime_error("Cannot finalize metadata: "+ec.message());}
}
StudioAssetEntry row(sqlite3_stmt* st) {
    auto str=[&](int i)->std::string{
        const auto* value=sqlite3_column_text(st,i);
        return value?reinterpret_cast<const char*>(value):"";
    };
    StudioAssetEntry e;
    e.guid=str(0);e.path=str(1);e.importer=str(2);
    e.directory=sqlite3_column_int(st,3)!=0;
    e.bytes=sqlite3_column_int64(st,4);
    e.settings=Json::parse(str(5));
    return e;
}
}
bool studioValidGuid(const std::string& guid) {
    if(guid.size()!=32)return false;
    return std::all_of(guid.begin(),guid.end(),[](unsigned char c){
        return (c>='0'&&c<='9')||(c>='a'&&c<='f');
    });
}
std::filesystem::path studioMetaPath(const fs::path& source) {
    return fs::path(source.string()+".meta"); // sibling Folder.meta or Ship.glb.meta
}
std::string studioImporterForPath(const fs::path& path,bool directory) {
    if(directory)return "folder";
    auto ext=path.extension().string();
    std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(ext==".png"||ext==".jpg"||ext==".jpeg"||ext==".tga"||
       ext==".dds"||ext==".ktx"||ext==".ktx2"||ext==".webp"||
       ext==".hdr"||ext==".exr")return "texture";
    if(ext==".glb"||ext==".gltf"||ext==".fbx"||ext==".obj"||ext==".usd")return "model";
    if(ext==".wav"||ext==".ogg"||ext==".mp3"||ext==".flac")return "audio";
    return "data";
}
Json studioDefaultSettings(const std::string& importer) {
    Json settings=Json::object();
    if(importer=="texture") {
        settings.set("compression",Json::string("auto"));
        settings.set("mipmaps",Json::fromBool(true));
        settings.set("sRGB",Json::fromBool(true));
        settings.set("maxSize",Json::number(int64_t(4096)));
        Json perPlatform=Json::object();
        for(const char* name:{"metal","vulkan","gles"})
            perPlatform.set(name,Json::string("auto"));
        settings.set("platformFormats",std::move(perPlatform));
    } else if(importer=="model") {
        settings.set("meshCompression",Json::string("medium"));
        settings.set("generateLODs",Json::fromBool(false));
    } else if(importer=="audio") {
        settings.set("compression",Json::string("auto"));
        settings.set("quality",Json::number(0.8,2));
    }
    return settings;
}
StudioAssetCatalog::StudioAssetCatalog(const fs::path& project):project_(project) {
    (void)studioContentRoot(project_);
    const fs::path cache=project_/".aiverse-cache";
    std::error_code ec;
    const auto cacheStatus=fs::symlink_status(cache,ec);
    if(!ec && (fs::is_symlink(cacheStatus) ||
       (fs::exists(cacheStatus)&&!fs::is_directory(cacheStatus))))
        throw std::runtime_error("AssetIndex cache path must be a real directory");
    if(ec && ec!=std::errc::no_such_file_or_directory)
        throw std::runtime_error("Cannot inspect AssetIndex cache: "+ec.message());
    ec.clear();
    fs::create_directories(cache,ec);
    if(ec)throw std::runtime_error("Cannot create AssetIndex cache: "+ec.message());
    // SQLite is regenerable: keep DB and WAL files out of version control.
    // This local ignore rule doesn't modify the game's root .gitignore.
    const fs::path ignored=cache/".gitignore";
    if(!fs::exists(ignored)){
        std::ofstream ignore(ignored,std::ios::binary);
        if(ignore)ignore<<"*\n!.gitignore\n";
    }
    const std::string path=(cache/"asset-index.sqlite3").string();
    const int rc=sqlite3_open_v2(path.c_str(),&db_,
        SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,nullptr);
    if(rc!=SQLITE_OK){
        std::string reason=db_?sqlite3_errmsg(db_):"SQLite initialization failed";
        if(db_)sqlite3_close(db_);
        db_=nullptr;
        throw std::runtime_error("Cannot open AssetIndex: "+reason);
    }
    sqlite3_busy_timeout(db_,3000);
    exec("PRAGMA journal_mode=WAL");
    exec("CREATE TABLE IF NOT EXISTS assets("
         "guid TEXT PRIMARY KEY,path TEXT NOT NULL UNIQUE,importer TEXT NOT NULL,"
         "directory INTEGER NOT NULL,bytes INTEGER NOT NULL,settings TEXT NOT NULL)");
    exec("CREATE INDEX IF NOT EXISTS idx_asset_importer ON assets(importer)");
}
StudioAssetCatalog::~StudioAssetCatalog(){if(db_)sqlite3_close(db_);}
void StudioAssetCatalog::exec(const char* sql) const {
    char* error=nullptr;
    if(sqlite3_exec(db_,sql,nullptr,nullptr,&error)!=SQLITE_OK) {
        std::string message=error?error:"SQLite error";
        if(error)sqlite3_free(error);
        throw std::runtime_error(message);
    }
}
StudioAssetEntry StudioAssetCatalog::inspect(const fs::path& source,bool directory) const {
    StudioAssetEntry e;
    e.path=source.lexically_relative(project_/"content").generic_string();
    e.directory=directory;
    e.importer=studioImporterForPath(source,directory);
    const fs::path meta=studioMetaPath(source);
    std::error_code ec;
    const fs::file_status status=fs::symlink_status(meta,ec);
    if(ec && ec!=std::errc::no_such_file_or_directory)
        throw std::runtime_error("Cannot inspect .meta: "+ec.message());
    if(fs::is_symlink(status))
        throw std::runtime_error("Metadata symlink forbidden: "+e.path);
    if(fs::exists(status)) {
        if(!fs::is_regular_file(status))throw std::runtime_error("Invalid .meta entry: "+e.path);
        const Json json=Json::parseFile(meta.string());
        const auto guid=json.getStr("guid");
        if(!studioValidGuid(guid))throw std::runtime_error("Invalid GUID in "+meta.string());
        if(json.getStr("importer")!=e.importer)
            throw std::runtime_error("Importer type mismatch in "+meta.string());
        if(!json["settings"].isObject())throw std::runtime_error("Invalid importer settings: "+e.path);
        e.guid=guid;e.settings=json["settings"];
    } else {
        e.guid=generateGuid();e.settings=studioDefaultSettings(e.importer);
        Json json=Json::object();
        json.set("schemaVersion",Json::number(int64_t(1)));
        json.set("guid",Json::string(e.guid));
        json.set("importer",Json::string(e.importer));
        json.set("settings",e.settings);
        writeMeta(meta,json);
    }
    if(!directory) {
        e.bytes=static_cast<std::int64_t>(
            std::min<std::uintmax_t>(fs::file_size(source),
              static_cast<std::uintmax_t>(INT64_MAX)));
    }
    return e;
}
void StudioAssetCatalog::rebuild() {
    const fs::path root=studioContentRoot(project_);
    std::vector<StudioAssetEntry> scanned;
    std::error_code ec;
    fs::recursive_directory_iterator it(root,fs::directory_options::skip_permission_denied,ec);
    if(ec)throw std::runtime_error("Cannot scan content: "+ec.message());
    const fs::recursive_directory_iterator end;
    for(;it!=end;it.increment(ec)) {
        if(ec)throw std::runtime_error("Cannot iterate content: "+ec.message());
        const auto stat=it->symlink_status(ec);
        if(ec)throw std::runtime_error("Cannot stat asset: "+ec.message());
        if(fs::is_symlink(stat)){it.disable_recursion_pending();continue;}
        if(isSidecar(it->path())){it.disable_recursion_pending();continue;}
        if(!fs::is_regular_file(stat) && !fs::is_directory(stat))continue;
        if(scanned.size()>=20000)throw std::runtime_error("Too many indexed content assets");
        scanned.push_back(inspect(it->path(),fs::is_directory(stat)));
    }
    if(ec)throw std::runtime_error("Cannot finish content scan: "+ec.message());
    // Detect duplicated GUIDs before touching the old index.
    std::sort(scanned.begin(),scanned.end(),[](const auto& a,const auto& b){return a.guid<b.guid;});
    for(std::size_t i=1;i<scanned.size();++i)
        if(scanned[i-1].guid==scanned[i].guid)
            throw std::runtime_error("GUID conflict between content/"+scanned[i-1].path+
                                     " and content/"+scanned[i].path+
                                     ". Resolve the .meta collision before rebuilding.");
    exec("BEGIN IMMEDIATE");
    try {
        exec("DELETE FROM assets");
        Statement insert(db_,"INSERT INTO assets(guid,path,importer,directory,bytes,settings)"
                              "VALUES(?1,?2,?3,?4,?5,?6)");
        for(const auto& e:scanned){
            const std::string settings=e.settings.dump(-1);
            sqlite3_reset(insert.stmt);sqlite3_clear_bindings(insert.stmt);
            sqlite3_bind_text(insert.stmt,1,e.guid.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(insert.stmt,2,e.path.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(insert.stmt,3,e.importer.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_bind_int(insert.stmt,4,e.directory?1:0);
            sqlite3_bind_int64(insert.stmt,5,e.bytes);
            sqlite3_bind_text(insert.stmt,6,settings.c_str(),-1,SQLITE_TRANSIENT);
            sqlCheck(sqlite3_step(insert.stmt),db_,"Insert AssetIndex entry");
        }
        exec("COMMIT");
    } catch(...) {try{exec("ROLLBACK");}catch(...){}throw;}
}
std::vector<StudioAssetEntry> StudioAssetCatalog::all() const {
    Statement query(db_,"SELECT guid,path,importer,directory,bytes,settings FROM assets ORDER BY path");
    std::vector<StudioAssetEntry> result;
    int rc;
    while((rc=sqlite3_step(query.stmt))==SQLITE_ROW)result.push_back(row(query.stmt));
    if(rc!=SQLITE_DONE)sqlCheck(rc,db_,"Enumerate AssetIndex");
    return result;
}
std::optional<StudioAssetEntry> StudioAssetCatalog::byGuid(const std::string& guid) const {
    if(!studioValidGuid(guid))return std::nullopt;
    Statement query(db_,"SELECT guid,path,importer,directory,bytes,settings FROM assets WHERE guid=?1");
    sqlite3_bind_text(query.stmt,1,guid.c_str(),-1,SQLITE_TRANSIENT);
    const int rc=sqlite3_step(query.stmt);
    if(rc==SQLITE_ROW)return row(query.stmt);
    if(rc!=SQLITE_DONE)sqlCheck(rc,db_,"Asset GUID lookup");
    return std::nullopt;
}
std::optional<StudioAssetEntry> StudioAssetCatalog::byPath(const std::string& path) const {
    Statement query(db_,"SELECT guid,path,importer,directory,bytes,settings FROM assets WHERE path=?1");
    sqlite3_bind_text(query.stmt,1,path.c_str(),-1,SQLITE_TRANSIENT);
    const int rc=sqlite3_step(query.stmt);
    if(rc==SQLITE_ROW)return row(query.stmt);
    if(rc!=SQLITE_DONE)sqlCheck(rc,db_,"Asset path lookup");
    return std::nullopt;
}
} // namespace aiverse::studio

namespace aiverse::studio {
namespace {
bool option(const std::string& candidate,std::initializer_list<const char*> valid) {
    for(const char* allowed:valid)if(candidate==allowed)return true;
    return false;
}
void validateMetaSetting(const std::string& importer,
                         const std::string& key,const Json& value){
    auto text=[&](std::initializer_list<const char*> allowed){
        if(!value.isString()||!option(value.str(),allowed))
            throw std::runtime_error("Unsupported importer option value: "+key);
    };
    auto boolean=[&]{
        if(value.type()!=Json::Type::Bool)
            throw std::runtime_error("Expected boolean importer setting: "+key);
    };
    if(importer=="texture"){
        if(key=="compression")text({"auto","none","fast","high","lossless"});
        else if(key=="mipmaps"||key=="sRGB")boolean();
        else if(key=="maxSize"){
            if(!value.isNumber()||value.toDouble()<64||value.toDouble()>16384||
               std::floor(value.toDouble())!=value.toDouble())
                throw std::runtime_error("maxSize must be an integer from 64 to 16384");
            const auto n=std::uint64_t(value.toDouble());
            if((n&(n-1))!=0)throw std::runtime_error("maxSize must be a power of two");
        }else if(key=="metalFormat")text({"auto","astc","rgba8"});
        else if(key=="vulkanFormat")text({"auto","astc","bc7","etc2","rgba8"});
        else if(key=="glesFormat")text({"auto","etc2","rgba8"});
        else throw std::runtime_error("Unknown texture importer setting: "+key);
    }else if(importer=="model"){
        if(key=="meshCompression")text({"off","low","medium","high"});
        else if(key=="generateLODs")boolean();
        else throw std::runtime_error("Unknown model importer setting: "+key);
    }else if(importer=="audio"){
        if(key=="compression")text({"auto","lossless","vorbis","aac"});
        else if(key=="quality"){
            if(!value.isNumber()||!std::isfinite(value.toDouble())||
               value.toDouble()<0||value.toDouble()>1)
                throw std::runtime_error("Audio quality must be between 0 and 1");
        }else throw std::runtime_error("Unknown audio importer setting: "+key);
    }else throw std::runtime_error("This asset type has no editable import settings");
}
}
StudioAssetEntry StudioAssetCatalog::patchSettings(const std::string& guid,
              const std::string& key,const Json& value){
    if(!studioValidGuid(guid))throw std::runtime_error("Invalid GUID");
    const auto existing=byGuid(guid);
    if(!existing)throw std::runtime_error("Asset was not found; refresh Assets");
    validateMetaSetting(existing->importer,key,value);
    const fs::path physical=studioContentRoot(project_)/fs::path(existing->path);
    // Guard against the asset or its parents changing to symlinks since indexing.
    fs::path current=studioContentRoot(project_);
    for(const auto& part:fs::path(existing->path)){
        current/=part;
        const auto status=fs::symlink_status(current);
        if(fs::is_symlink(status))throw std::runtime_error("Asset path has a symlink");
    }
    const auto meta=studioMetaPath(physical);
    const auto status=fs::symlink_status(meta);
    if(!fs::is_regular_file(status)||fs::is_symlink(status))
        throw std::runtime_error("Refusing to edit invalid asset .meta");
    Json document=Json::parseFile(meta.string());
    if(document.getStr("guid")!=guid||document.getStr("importer")!=existing->importer)
        throw std::runtime_error("Asset metadata changed since the last index refresh");
    Json settings=document["settings"];
    if(!settings.isObject())throw std::runtime_error("Invalid importer settings");
    if(key=="metalFormat"||key=="vulkanFormat"||key=="glesFormat"){
        Json platforms=settings["platformFormats"];
        if(!platforms.isObject())platforms=Json::object();
        const char* name=key=="metalFormat"?"metal":key=="vulkanFormat"?"vulkan":"gles";
        platforms.set(name,value);
        settings.set("platformFormats",std::move(platforms));
    }else settings.set(key,value);
    document.set("settings",std::move(settings));
    const auto tmp=fs::path(meta.string()+".aiverse-meta-update");
    {
        std::ofstream stream(tmp,std::ios::binary|std::ios::trunc);
        if(!stream)throw std::runtime_error("Cannot write new metadata");
        stream<<document.dump(2)<<"\n";
        if(!stream){fs::remove(tmp);throw std::runtime_error("Cannot complete metadata write");}
    }
    std::error_code ec;
    fs::rename(tmp,meta,ec); // atomic replacement on supported host filesystems
    if(ec){fs::remove(tmp);throw std::runtime_error("Cannot update .meta: "+ec.message());}
    rebuild();
    const auto updated=byGuid(guid);
    if(!updated)throw std::runtime_error("Metadata saved but asset index is stale");
    return *updated;
}
} // namespace aiverse::studio
