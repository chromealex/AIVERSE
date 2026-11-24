// Exact editor history snapshot codec; runtime map JSON is intentionally
// not used as the history format (it quantizes positions and omits layers).
#include "MapDoc.h"
#include "ProjectDocumentJournal.h"
#include "engine/game/MapGrid.h"
#include <climits>
#include <cmath>
#include <bit>
#include <limits>
#include <type_traits>
#include <atomic>
#include <chrono>
#include <random>
#include <algorithm>

namespace aiverse::editor {
namespace {
namespace fs=std::filesystem;
constexpr size_t MaxBytes=256u*1024u*1024u;
constexpr char Hex[]="0123456789abcdef";
void budget(size_t n){if(n>MaxBytes)throw std::runtime_error("Map history exceeds disk budget");}
uint8_t unhex(char c){
    if(c>='0'&&c<='9')return uint8_t(c-'0');
    if(c>='a'&&c<='f')return uint8_t(c-'a'+10);
    throw std::runtime_error("Invalid map history binary data");
}
template<class T> Json binary(const std::vector<T>& values){
    static_assert(sizeof(T)<=2 && std::is_integral_v<T>);
    if(values.size()>MaxBytes/(2*sizeof(T)))throw std::runtime_error("Map history layer exceeds disk budget");
    std::string text; text.reserve(values.size()*2*sizeof(T));
    for(T value:values){
        using U=std::make_unsigned_t<T>;
        const auto bits=std::bit_cast<U>(value);
        for(size_t k=0;k<sizeof(T);++k){
            const auto byte=uint8_t(bits>>(8*k));text+=Hex[byte>>4];text+=Hex[byte&15];
        }
    }
    return Json::string(std::move(text));
}
template<class T> std::vector<T> layer(const Json& value){
    if(!value.isString())throw std::runtime_error("Invalid map history layer");
    const auto text=value.str();
    if(text.size()>MaxBytes||text.size()%(2*sizeof(T)))throw std::runtime_error("Invalid map history layer length");
    std::vector<T> result;result.reserve(text.size()/(2*sizeof(T)));
    for(size_t i=0;i<text.size();i+=2*sizeof(T)){
        using U=std::make_unsigned_t<T>;U bits=0;
        for(size_t k=0;k<sizeof(T);++k){
            const auto byte=uint8_t((unhex(text[i+2*k])<<4)|unhex(text[i+2*k+1]));
            bits=U(bits|U(U(byte)<<(8*k)));
        }
        result.push_back(std::bit_cast<T>(bits));
    }
    return result;
}
Json integer(int value){return Json::number(int64_t(value));}
int getInt(const Json& j,const char* key){
    const auto& v=j[key];const auto n=v.integer(INT64_MIN);
    if(!v.isNumber()||n<INT_MIN||n>INT_MAX)throw std::runtime_error("Invalid map history integer");
    return int(n);
}
Json fp(float value){return Json::number(int64_t(std::bit_cast<uint32_t>(value)));}
float getFloat(const Json& j,const char* key){
    const auto& v=j[key];const auto n=v.integer(-1);
    if(!v.isNumber()||n<0||n>UINT32_MAX)throw std::runtime_error("Invalid map history float");
    const float value=std::bit_cast<float>(uint32_t(n));
    if(!std::isfinite(value))throw std::runtime_error("Non-finite map history value");
    return value;
}
void object(const Json& j){if(!j.isObject())throw std::runtime_error("Invalid map history object");}
void array(const Json& j,size_t max=1000000){if(!j.isArray()||j.size()>max)throw std::runtime_error("Invalid map history array");}
std::string string(const Json& j,const char* key){if(!j[key].isString())throw std::runtime_error("Invalid map history string");return j.getStr(key);}
Json encode(const MapData& d){
    Json j=Json::object();
    j.set("worldSpace",Json::fromBool(d.worldSpace));j.set("name",Json::string(d.name));
    j.set("w",integer(d.w));j.set("h",integer(d.h));j.set("roadsOnly",Json::fromBool(d.roadsOnly));
    j.set("cells",binary(d.cells));j.set("levels",binary(d.levels));j.set("heights",binary(d.heights));
    j.set("corners",binary(d.corners));j.set("cornersFile",Json::string(d.cornersFile));
    j.set("detailScale",integer(d.detailScale));j.set("detail",binary(d.detail));j.set("detailSplat",binary(d.detailSplat));
    j.set("detailFile",Json::string(d.detailFile));j.set("fieldRes",binary(d.fieldRes));j.set("fieldAmt",binary(d.fieldAmt));
    j.set("regions",binary(d.regions));j.set("extra",d.extra);j.set("worldExtra",d.worldExtra);
    Json resources=Json::array(),names=Json::array(),players=Json::array(),entities=Json::array(),roads=Json::array(),bodies=Json::array();
    for(const auto& r:d.fieldResources)resources.push(Json::string(r));
    for(const auto& [k,v]:d.regionNames){Json row=Json::array();row.push(integer(uint8_t(k)));row.push(Json::string(v));names.push(std::move(row));}
    for(const auto& p:d.players){
        Json row=Json::object(),res=Json::array();
        row.set("name",Json::string(p.name));row.set("team",integer(p.team));row.set("faction",Json::string(p.faction));
        row.set("hasStart",Json::fromBool(p.hasStart));row.set("startX",fp(p.startX));row.set("startZ",fp(p.startZ));
        row.set("waves",Json::fromBool(p.waves));row.set("startAt",p.startAt);row.set("extra",p.extra);
        for(const auto& [key,value]:p.resources){Json r=Json::object();r.set("key",Json::string(key));r.set("value",fp(value));res.push(std::move(r));}
        row.set("resources",std::move(res));players.push(std::move(row));
    }
    for(const auto& e:d.entities){
        Json row=Json::object();row.set("id",Json::string(e.id));row.set("type",Json::string(e.type));
        row.set("x",fp(e.x));row.set("z",fp(e.z));row.set("owner",integer(e.owner));row.set("garrison",integer(e.garrison));
        row.set("amount",fp(e.amount));row.set("at",e.at);row.set("extra",e.extra);entities.push(std::move(row));
    }
    for(const auto& [a,b]:d.roads){Json row=Json::object();row.set("a",integer(a));row.set("b",integer(b));roads.push(std::move(row));}
    for(const auto& b:d.bodies){
        Json row=Json::object(),faces=Json::array();
        row.set("id",Json::string(b.id));row.set("faceSize",integer(b.faceSize));
        row.set("cells",binary(b.cells));row.set("levels",binary(b.levels));row.set("corners",binary(b.corners));
        row.set("cornersFile",Json::string(b.cornersFile));row.set("meta",b.meta);row.set("terrainExtra",b.terrainExtra);
        for(const auto& f:b.faceExtra)faces.push(f);
        row.set("faces",std::move(faces));row.set("detailScale",integer(b.detailScale));
        row.set("detail",binary(b.detail));row.set("detailFile",Json::string(b.detailFile));bodies.push(std::move(row));
    }
    j.set("fieldResources",std::move(resources));j.set("regionNames",std::move(names));j.set("players",std::move(players));
    j.set("entities",std::move(entities));j.set("roads",std::move(roads));j.set("bodies",std::move(bodies));
    return j;
}
void validateSidecarName(const std::string& value){
    if(value.empty())return;
    const fs::path path(value);
    if(value.size()>1024||path.is_absolute()||path.has_root_name()||value.find('\\')!=std::string::npos||value.find(':')!=std::string::npos)
        throw std::runtime_error("Unsafe map history sidecar path");
    for(const auto& segment:path)if(segment=="."||segment==".."||segment.empty())
        throw std::runtime_error("Unsafe map history sidecar segment");
}
MapData decode(const Json& j){
    object(j);MapData d;
    d.worldSpace=j.getBool("worldSpace");d.name=string(j,"name");d.w=getInt(j,"w");d.h=getInt(j,"h");d.roadsOnly=j.getBool("roadsOnly");
    if(d.w<1||d.h<1||d.w>game::kMaxMapSize||d.h>game::kMaxMapSize)throw std::runtime_error("Invalid map history dimensions");
    d.cells=layer<char>(j["cells"]);d.levels=layer<int8_t>(j["levels"]);d.heights=layer<int16_t>(j["heights"]);
    d.corners=layer<int16_t>(j["corners"]);d.cornersFile=string(j,"cornersFile");
    d.detailScale=getInt(j,"detailScale");d.detail=layer<int16_t>(j["detail"]);d.detailSplat=layer<uint8_t>(j["detailSplat"]);
    d.detailFile=string(j,"detailFile");d.fieldRes=layer<int8_t>(j["fieldRes"]);d.fieldAmt=layer<uint8_t>(j["fieldAmt"]);
    d.regions=layer<char>(j["regions"]);d.extra=j["extra"];d.worldExtra=j["worldExtra"];object(d.extra);object(d.worldExtra);
    validateSidecarName(d.cornersFile);validateSidecarName(d.detailFile);
    const size_t n=size_t(d.w)*size_t(d.h);
    auto flat=[&](size_t size){return size==n||(d.worldSpace&&size==0);};
    if(!flat(d.cells.size())||!flat(d.levels.size())||!flat(d.heights.size())||!flat(d.fieldRes.size())||
       !flat(d.fieldAmt.size())||!flat(d.regions.size())||(!d.corners.empty()&&d.corners.size()!=size_t(d.w+1)*size_t(d.h+1)))
        throw std::runtime_error("Invalid map history grid shape");
    const auto detailShape=[](int w,int h,int scale,size_t count,size_t splat,size_t faces){
        if(scale<0||scale>16)throw std::runtime_error("Invalid map history detail scale");
        if(count==0){if(splat)throw std::runtime_error("Invalid map history splat");return;}
        const size_t expected=faces*size_t(w*scale+1)*size_t(h*scale+1);
        if(scale==0||count!=expected||(splat&&splat!=expected*4))throw std::runtime_error("Invalid map history detail shape");
    };
    detailShape(d.w,d.h,d.detailScale,d.detail.size(),d.detailSplat.size(),1);
    array(j["fieldResources"],128);
    for(const auto& r:j["fieldResources"].items()){if(!r.isString())throw std::runtime_error("Invalid resource");d.fieldResources.push_back(r.str());}
    array(j["regionNames"],256);
    for(const auto& row:j["regionNames"].items()){
        if(!row.isArray()||row.size()!=2||!row[1].isString()||row[0].integer(-1)<0||row[0].integer(-1)>255)
            throw std::runtime_error("Invalid region name");
        if(!d.regionNames.emplace(char(row[0].integer()),row[1].str()).second)throw std::runtime_error("Duplicate region");
    }
    d.players.clear();array(j["players"],4096);
    for(const auto& row:j["players"].items()){
        object(row);DocPlayer p;p.name=string(row,"name");p.team=getInt(row,"team");p.faction=string(row,"faction");
        p.hasStart=row.getBool("hasStart");p.startX=getFloat(row,"startX");p.startZ=getFloat(row,"startZ");
        p.waves=row.getBool("waves");p.startAt=row["startAt"];p.extra=row["extra"];object(p.extra);
        array(row["resources"],4096);
        for(const auto& r:row["resources"].items())p.resources.emplace_back(string(r,"key"),getFloat(r,"value"));
        d.players.push_back(std::move(p));
    }
    array(j["entities"]);
    for(const auto& row:j["entities"].items()){
        object(row);DocEntity e;e.id=string(row,"id");e.type=string(row,"type");e.x=getFloat(row,"x");e.z=getFloat(row,"z");
        e.owner=getInt(row,"owner");e.garrison=getInt(row,"garrison");e.amount=getFloat(row,"amount");e.at=row["at"];e.extra=row["extra"];object(e.extra);
        d.entities.push_back(std::move(e));
    }
    array(j["roads"]);
    for(const auto& row:j["roads"].items()){
        const int a=getInt(row,"a"),b=getInt(row,"b");
        if(a<0||b<0||size_t(a)>=d.entities.size()||size_t(b)>=d.entities.size()||a==b)throw std::runtime_error("Invalid map history road");
        d.roads.emplace_back(a,b);
    }
    array(j["bodies"],128);
    for(const auto& row:j["bodies"].items()){
        object(row);DocBody b;b.id=string(row,"id");b.faceSize=getInt(row,"faceSize");
        if(b.faceSize<0||b.faceSize>512)throw std::runtime_error("Invalid map history body size");
        b.cells=layer<char>(row["cells"]);b.levels=layer<int8_t>(row["levels"]);b.corners=layer<int16_t>(row["corners"]);
        b.cornersFile=string(row,"cornersFile");b.meta=row["meta"];b.terrainExtra=row["terrainExtra"];object(b.meta);object(b.terrainExtra);
        array(row["faces"],6);for(const auto& f:row["faces"].items()){object(f);b.faceExtra.push_back(f);}
        b.detailScale=getInt(row,"detailScale");b.detail=layer<int16_t>(row["detail"]);b.detailFile=string(row,"detailFile");
        validateSidecarName(b.cornersFile);validateSidecarName(b.detailFile);
        const size_t side=size_t(b.faceSize),cells=6*side*side;
        if(b.cells.size()!=cells||b.levels.size()!=cells||
           (side&&(b.corners.size()!=6*(side+1)*(side+1)||b.faceExtra.size()!=6))||
           (!side&&!b.corners.empty()))throw std::runtime_error("Invalid map history body layers");
        detailShape(b.faceSize,b.faceSize,b.detailScale,b.detail.size(),0,6);
        d.bodies.push_back(std::move(b));
    }
    return d;
}
struct HistoryLocation {fs::path cache,file,lock,dirty;};
HistoryLocation location(const std::string& path,bool create){
    using namespace project_file_detail;
    const auto input=fs::absolute(path).lexically_normal();
    if(journalExists(input)&&!fs::is_regular_file(fs::symlink_status(input)))
        throw std::runtime_error("Unsafe map history source");
    const auto map=fs::canonical(input.parent_path())/input.filename();
    const auto cache=map.parent_path()/".aiverse-cache";
    if(create)directory(cache);
    else if(journalExists(cache)&&!fs::is_directory(fs::symlink_status(cache)))throw std::runtime_error("Unsafe map history cache");
    const auto key=digest(map.filename().generic_string());
    return {cache,cache/("map-history-"+key+".json"),cache/("map-history-"+key+".lock"),cache/("map-history-"+key+".dirty")};
}
std::string fingerprint(const std::string& path,const MapData& loaded){
    using namespace project_file_detail;
    const auto p=fs::path(path);
    if(fs::file_size(p)>MaxBytes)throw std::runtime_error("Map file exceeds history disk budget");
    const auto raw=readProjectFileBytes(p);
    const auto state=encode(loaded).dump(-1);budget(state.size());
    return digest(digest(raw)+digest(state));
}
}
uint64_t MapDoc::nextHistoryId(){
    static std::atomic<uint64_t> next{1};
    return next.fetch_add(1,std::memory_order_relaxed);
}
std::string MapDoc::newHistoryEpoch(){
    static std::atomic<uint64_t> serial{0};std::random_device random;
    return std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+":"+
        std::to_string(++serial)+":"+std::to_string(random());
}
void MapDoc::setHistoryLabel(uint64_t id,const std::string& label){
    for(auto* stack:{&undo_,&redo_})for(auto& e:*stack)if(e.id==id)e.label=label.substr(0,256);
}
std::string MapDoc::historyLabel(uint64_t id)const{
    for(const auto* stack:{&undo_,&redo_})for(const auto& e:*stack)if(e.id==id)return e.label;
    return {};
}
std::shared_ptr<void> MapDoc::beginHistorySave(const std::string& file){
    using namespace project_file_detail;
    if(compoundBefore_)throw std::runtime_error("Cannot save an active compound edit");
    const auto loc=location(file,true);
    auto lock=std::make_shared<Lock>(loc.lock);
    if(journalExists(loc.dirty)&&!fs::is_regular_file(fs::symlink_status(loc.dirty)))throw std::runtime_error("Unsafe map history marker");
    write(loc.dirty,"Map save in progress\\n");sync(loc.cache);sync(loc.cache.parent_path());
    return lock;
}
void MapDoc::saveHistoryCheckpoint(){
    using namespace project_file_detail;
    historyPersistenceWarning_.clear();
    try{
        if(compoundBefore_)throw std::runtime_error("Cannot persist active compound edit");
        const auto loc=location(path,true);
        if(!fs::is_regular_file(fs::symlink_status(loc.dirty)))throw std::runtime_error("Missing map save marker");
        if(journalExists(loc.file)&&!fs::is_regular_file(fs::symlink_status(loc.file)))throw std::runtime_error("Unsafe map history checkpoint");
        // Read the actual published map and its sidecars, not rounded live state.
        MapDoc saved;std::string error;
        if(!saved.loadImpl(path,&error,false)||!error.empty())throw std::runtime_error("Cannot verify saved map: "+error);
        Json body=Json::object(),undo=Json::array(),redo=Json::array();
        body.set("baseline",Json::string(fingerprint(path,saved.data)));
        size_t bytes=0;
        const auto append=[&](Json& rows,const std::vector<HistoryEntry>& stack){
            for(const auto& e:stack){
                auto snapshot=encode(e.data).dump(-1);
                if(snapshot.size()>MaxBytes-bytes)throw std::runtime_error("Map history snapshot budget exceeded");
                bytes+=snapshot.size();
                Json row=Json::object();row.set("label",Json::string(e.label));row.set("snapshot",Json::string(std::move(snapshot)));rows.push(std::move(row));
            }
        };
        append(undo,undo_);append(redo,redo_);
        body.set("undo",std::move(undo));body.set("redo",std::move(redo));
        const auto payload=body.dump(-1);budget(payload.size());
        Json envelope=Json::object();envelope.set("version",Json::number(int64_t(1)));envelope.set("sha256",Json::string(digest(payload)));
        envelope.set("payload",Json::string(payload));const auto text=envelope.dump(-1);budget(text.size());
        const auto tmp=fs::path(loc.file.string()+".tmp");
        if(journalExists(tmp)){if(!fs::is_regular_file(fs::symlink_status(tmp)))throw std::runtime_error("Unsafe map history staging file");fs::remove(tmp);}
        write(tmp,text);replace(tmp,loc.file);
        fs::remove(loc.dirty);sync(loc.cache);
    }catch(const std::exception& e){historyPersistenceWarning_=e.what();}
}
void MapDoc::restoreHistoryCheckpoint(){
    using namespace project_file_detail;
    historyPersistenceWarning_.clear();
    try{
        const auto loc=location(path,false);
        if(!journalExists(loc.cache))return;
        Lock lock(loc.lock);
        if(journalExists(loc.dirty))throw std::runtime_error("Interrupted map save; previous history fenced");
        if(!journalExists(loc.file))return;
        if(!fs::is_regular_file(fs::symlink_status(loc.file))||fs::file_size(loc.file)>MaxBytes)
            throw std::runtime_error("Unsafe or oversized map history checkpoint");
        const auto envelope=Json::parse(readProjectFileBytes(loc.file));
        const auto payload=envelope.getStr("payload");
        if(envelope.getInt("version")!=1||digest(payload)!=envelope.getStr("sha256"))throw std::runtime_error("Map history checksum mismatch");
        const auto body=Json::parse(payload);
        if(body.getStr("baseline")!=fingerprint(path,data))throw std::runtime_error("Saved map or terrain sidecars changed; history unavailable");
        array(body["undo"],200);array(body["redo"],200);
        if(body["undo"].size()+body["redo"].size()>200)throw std::runtime_error("Map history step limit");
        std::vector<HistoryEntry> undo,redo;size_t bytes=0;
        const auto read=[&](const Json& rows,std::vector<HistoryEntry>& out){
            for(const auto& row:rows.items()){
                const auto text=string(row,"snapshot"),label=string(row,"label");
                if(text.size()>MaxBytes-bytes||label.size()>256)throw std::runtime_error("Map history snapshot budget exceeded");
                bytes+=text.size();auto snapshot=decode(Json::parse(text));
                out.push_back({std::move(snapshot),nextHistoryId(),label});
            }
        };
        read(body["undo"],undo);read(body["redo"],redo);
        undo_=std::move(undo);redo_=std::move(redo);
        for(const auto* stack:{&undo_,&redo_})for(const auto& e:*stack)lastHistoryId_=std::max(lastHistoryId_,e.id);
    }catch(const std::exception& e){historyPersistenceWarning_=e.what();undo_.clear();redo_.clear();}
}
}
