#pragma once
// Caverns Stage19: stable saved authored inputs. No GPU mesh bytes in saves.
// JSON is a map-document property, versioned independently of mesh cache.
#include "engine/world/TerraWeaveCavernsVolumeSnapshot.h"
#include "engine/data/Json.h"
#include <cstdint>
#include <limits>
#include <stdexcept>
namespace aiverse::terraweave::caverns {
inline Json caveSnapshotToJson(CaveVolumeSnapshot snapshot) {
    canonicalizeCaveSnapshot(snapshot);
    if(snapshot.sourceRevision>uint64_t(INT64_MAX)||
       snapshot.generatorVersion>uint64_t(INT64_MAX)||
       snapshot.settings.seed>uint64_t(INT64_MAX))
        throw std::out_of_range("Caverns persisted integer overflow");
    Json out=Json::object();
    out.set("version",Json::number(int64_t(1)));
    out.set("generatorVersion",Json::number(int64_t(snapshot.generatorVersion)));
    out.set("sourceRevision",Json::number(int64_t(snapshot.sourceRevision)));
    Json settings=Json::object();
    settings.set("seed",Json::number(int64_t(snapshot.settings.seed)));
    settings.set("cellCm",Json::number(int64_t(snapshot.settings.cellCm)));
    settings.set("tunnelRadiusCm",Json::number(int64_t(snapshot.settings.tunnelRadiusCm)));
    settings.set("chamberRadiusCm",Json::number(int64_t(snapshot.settings.chamberRadiusCm)));
    settings.set("tunnelChance",Json::number(int64_t(snapshot.settings.tunnelChance)));
    settings.set("chamberChance",Json::number(int64_t(snapshot.settings.chamberChance)));
    settings.set("minYcm",Json::number(snapshot.settings.minYcm));
    settings.set("maxYcm",Json::number(snapshot.settings.maxYcm));
    out.set("settings",std::move(settings));
    Json stamps=Json::array();
    for(const auto& s:snapshot.stamps){
        if(s.id>uint64_t(INT64_MAX))
            throw std::out_of_range("Caverns stamp ID overflow");
        Json j=Json::object();
        j.set("id",Json::number(int64_t(s.id)));
        j.set("xCm",Json::number(s.center.x));
        j.set("yCm",Json::number(s.center.y));
        j.set("zCm",Json::number(s.center.z));
        j.set("radiusCm",Json::number(int64_t(s.radiusCm)));
        j.set("mode",Json::string(s.mode==CaveStampMode::Carve?"carve":"fill"));
        stamps.push(std::move(j));
    }
    out.set("stamps",std::move(stamps));
    Json entrances=Json::array();
    for(const auto& e:snapshot.entrances){
        if(e.id>uint64_t(INT64_MAX))
            throw std::out_of_range("Caverns entrance ID overflow");
        Json j=Json::object();
        j.set("id",Json::number(int64_t(e.id)));
        j.set("xCm",Json::number(e.xCm));
        j.set("zCm",Json::number(e.zCm));
        j.set("surfaceYcm",Json::number(e.surfaceYcm));
        j.set("caveYcm",Json::number(e.caveYcm));
        j.set("radiusCm",Json::number(int64_t(e.radiusCm)));
        entrances.push(std::move(j));
    }
    out.set("entrances",std::move(entrances));
    return out;
}
inline CaveVolumeSnapshot caveSnapshotFromJson(const Json& src) {
    if(src.isNull())return {};
    if(!src.isObject()||src.getInt("version",-1)!=1||
       !src["settings"].isObject()||!src["stamps"].isArray()||
       !src["entrances"].isArray())
        throw std::invalid_argument("Caverns document format");
    CaveVolumeSnapshot s;
    const auto positive=[](int64_t v)->uint64_t {
        if(v<=0)throw std::invalid_argument("Caverns unsigned ID/revision");
        return uint64_t(v);
    };
    const auto checked32=[](int64_t v)->int32_t {
        if(v<INT32_MIN||v>INT32_MAX)throw std::out_of_range("Caverns document int32");
        return int32_t(v);
    };
    s.generatorVersion=positive(src.getInt("generatorVersion",0));
    s.sourceRevision=positive(src.getInt("sourceRevision",0));
    const Json& cfg=src["settings"];
    if(cfg.getInt("seed",-1)<0)throw std::invalid_argument("Caverns seed");
    s.settings.seed=uint64_t(cfg.getInt("seed",0));
    s.settings.cellCm=checked32(cfg.getInt("cellCm",4800));
    s.settings.tunnelRadiusCm=checked32(cfg.getInt("tunnelRadiusCm",450));
    s.settings.chamberRadiusCm=checked32(cfg.getInt("chamberRadiusCm",1200));
    const auto chance=[&](const char* name,int64_t fallback)->uint32_t {
        const auto v=cfg.getInt(name,fallback);
        if(v<0||v>1000)throw std::invalid_argument("Caverns probability");
        return uint32_t(v);
    };
    s.settings.tunnelChance=chance("tunnelChance",550);
    s.settings.chamberChance=chance("chamberChance",180);
    s.settings.minYcm=cfg.getInt("minYcm",-200000);
    s.settings.maxYcm=cfg.getInt("maxYcm",-300);
    if(src["stamps"].size()>16384||src["entrances"].size()>4096)
        throw std::length_error("Caverns document cap");
    for(const auto& j:src["stamps"].items()){
        if(!j.isObject())throw std::invalid_argument("Caverns stamp object");
        CaveSphereStamp stamp;
        stamp.id=positive(j.getInt("id",0));
        stamp.center={j.getInt("xCm"),j.getInt("yCm"),j.getInt("zCm")};
        stamp.radiusCm=checked32(j.getInt("radiusCm"));
        const auto mode=j.getStr("mode");
        if(mode!="carve"&&mode!="fill")throw std::invalid_argument("Caverns stamp mode");
        stamp.mode=mode=="carve"?CaveStampMode::Carve:CaveStampMode::Fill;
        s.stamps.push_back(stamp);
    }
    for(const auto& j:src["entrances"].items()){
        if(!j.isObject())throw std::invalid_argument("Caverns entrance object");
        Entrance e;
        e.id=positive(j.getInt("id",0));
        e.xCm=j.getInt("xCm");e.zCm=j.getInt("zCm");
        e.surfaceYcm=j.getInt("surfaceYcm");e.caveYcm=j.getInt("caveYcm");
        e.radiusCm=checked32(j.getInt("radiusCm"));
        s.entrances.push_back(e);
    }
    canonicalizeCaveSnapshot(s);
    return s;
}
} // namespace aiverse::terraweave::caverns
