#pragma once
#include "RuntimeBuildingGenerator.h"
#include "BuildingCpuCache.h"
namespace aiverse::lib::systems {
// Full binary key (not hash-only). Includes the transform so cached
// physics boxes cannot be returned at a different world location.
// For reusable instance geometry request origin={} and yaw=0.
inline std::string buildingGeometryCacheKey(const RuntimeBuildingRequest& r){
    BuildingCacheKey k;k.text("building-geometry-v1");k.text(r.style);k.text(r.function);k.number(r.seed);
    for(float v:{r.size.width,r.size.height,r.size.depth})k.real(v);
    k.number(uint64_t(r.origin.x.raw));k.number(uint64_t(r.origin.y.raw));k.number(uint64_t(r.origin.z.raw));k.number(r.yaw);
    k.number(r.composeFeatures);k.number(r.features.size());
    for(const auto& f:r.features){
        k.number(uint8_t(f.kind));k.number(uint8_t(f.side));k.number(f.count);
        for(float v:{f.width,f.height,f.depth,f.open,f.position,f.positionZ,f.spacing,f.sill,f.balconyDepth})k.real(v);
        k.number(f.floor);k.number(uint64_t(int64_t(f.wallLine)));k.number(uint8_t(f.balconySupport));
    }
    k.number(uint8_t(r.roofProfile));k.real(r.roofRise);k.number(r.floors);k.number(r.roomsX);k.number(r.roomsZ);
    auto mask=[&](const auto& m){k.number(m.size());for(const auto& row:m)k.text(row);};
    mask(r.footprint);k.number(r.floorFootprints.size());for(const auto& m:r.floorFootprints)mask(m);
    k.number(r.walkableSetbacks);for(float v:r.cornerChamfers)k.real(v);
    k.number(uint8_t(r.interiorProfile));k.number(r.furnitureDensity);k.number(r.interiorLighting);k.number(r.interiorDecor);
    k.number(r.roomUses.size());for(const auto& u:r.roomUses){k.number(u.floor);k.number(u.x);k.number(u.z);k.number(uint8_t(u.use));}
    k.number(r.damageZones.size());for(const auto& d:r.damageZones){k.number(uint8_t(d.kind));for(float v:d.minimum)k.real(v);for(float v:d.maximum)k.real(v);}
    k.number(r.generateVisualLods);return k.take();
}
inline size_t buildingGeometryEstimatedBytes(const RuntimeBuildingResult& r){
    size_t bytes=sizeof(r)+r.message.capacity()+r.building.fitError.capacity()+r.structureGraph.error.capacity();
    auto vector=[&](const auto& v){bytes+=v.capacity()*sizeof(typename std::decay_t<decltype(v)>::value_type);};
    auto string=[&](const std::string& s){bytes+=s.capacity()+1;};
    auto mesh=[&](const StyledMesh& m){
        vector(m.vertices);vector(m.parts);vector(m.materials);
        for(const auto& p:m.parts)vector(p.indices);
        for(const auto& t:m.materials){string(t.id);string(t.surfacePattern);string(t.textures.baseColor);string(t.textures.metallicRoughness);string(t.textures.normal);string(t.textures.occlusion);string(t.textures.emissive);}
    };
    mesh(r.building.mesh);vector(r.building.visualLods);
    for(const auto& l:r.building.visualLods)mesh(l.mesh);
    vector(r.building.boxes);vector(r.building.gates);vector(r.building.lights);
    for(const auto& l:r.building.lights)string(l.id);
    vector(r.featureAnchors);for(const auto& a:r.featureAnchors)string(a.socketId);
    const auto& g=r.structureGraph;
    vector(g.solids);vector(g.sockets);for(const auto& x:g.sockets)string(x.id);
    vector(g.rooms);for(const auto& x:g.rooms)string(x.id);
    vector(g.connections);for(const auto& x:g.connections){string(x.id);string(x.from);string(x.to);}
    vector(g.exteriorApertures);for(const auto& x:g.exteriorApertures){string(x.socketId);string(x.roomId);}
    vector(g.terraces);vector(g.terraceAccess);for(const auto& x:g.terraceAccess){string(x.socketId);string(x.roomId);}
    vector(g.balconies);for(const auto& x:g.balconies)string(x.socketId);
    vector(g.roofSections);vector(g.visualPrisms);for(const auto& x:g.visualPrisms)vector(x.polygon);
    vector(g.footprintPolygon);vector(g.roomUses);for(const auto& x:g.roomUses)string(x.roomId);
    vector(g.furniture);for(const auto& x:g.furniture){string(x.id);string(x.roomId);}
    vector(g.furnitureSockets);for(const auto& x:g.furnitureSockets){string(x.id);string(x.roomId);string(x.furnitureId);}
    vector(g.interiorLights);for(const auto& x:g.interiorLights){string(x.id);string(x.roomId);}
    vector(g.interiorDecor);for(const auto& x:g.interiorDecor){string(x.id);string(x.roomId);}
    vector(g.removedDamage);
    return bytes;
}
// Owns its style registry: callers cannot change styles behind cached keys.
// All public operations serialize against register/clear. No GPU or file IO.
class CachedRuntimeBuildingGenerator {
public:
    using Lease=BuildingCpuCache<RuntimeBuildingResult>::Lease;
    explicit CachedRuntimeBuildingGenerator(size_t budget=128u*1024u*1024u,size_t entries=128):cache_(budget,entries){}
    bool registerStyleJson(const std::string& json,std::string* error=nullptr){
        std::lock_guard lock(registryMutex_);return generator_.registerStyleJson(json,error);
    }
    bool registerStyle(BuildingStyle style,std::string* error=nullptr){
        std::lock_guard lock(registryMutex_);return generator_.registerStyle(std::move(style),error);
    }
    void clear(){std::lock_guard lock(registryMutex_);cache_.clear();generator_.clear();}
    void clearCache(){std::lock_guard lock(registryMutex_);cache_.clear();}
    Lease generate(const RuntimeBuildingRequest& request){
        std::lock_guard lock(registryMutex_);
        // Bound key allocation even for malformed native inputs.
        bool oversized=request.style.size()>64||request.function.size()>64||request.features.size()>24||
            request.footprint.size()>6||request.floorFootprints.size()>8||request.roomUses.size()>128||request.damageZones.size()>16;
        for(const auto& row:request.footprint)oversized|=row.size()>6;
        for(const auto& m:request.floorFootprints){oversized|=m.size()>6;for(const auto& row:m)oversized|=row.size()>6;}
        if(oversized){RuntimeBuildingResult r;r.error=RuntimeBuildingError::InvalidInput;r.message="Building cache request exceeds key budget";return std::make_shared<const RuntimeBuildingResult>(std::move(r));}
        return cache_.get(buildingGeometryCacheKey(request),[&]{return generator_.generate(request);},
            [](const auto& r){return bool(r);},buildingGeometryEstimatedBytes);
    }
    BuildingCacheStats stats()const{return cache_.stats();}
private:
    std::mutex registryMutex_;RuntimeBuildingGenerator generator_;BuildingCpuCache<RuntimeBuildingResult> cache_;
};
}
