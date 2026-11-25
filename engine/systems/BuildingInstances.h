#pragma once
// Shared immutable building template. Instance state/physics is owned by
// the game, not this resource. Only shell geometry is shared: moving doors,
// lifts and damage retain the existing independent authoritative owners.
#include "CachedRuntimeBuildingGenerator.h"
#include <limits>

namespace aiverse::lib::systems {
struct BuildingInstanceTemplate {
    CachedRuntimeBuildingGenerator::Lease local;
    std::string modelKey;
    explicit operator bool()const{return local&&bool(*local)&&!modelKey.empty();}
};
inline std::string buildingInstanceModelKey(const RuntimeBuildingRequest& local,
    uint64_t contentNamespace,uint64_t styleRevision,uint64_t materialRevision){
    if(!contentNamespace||!styleRevision||!materialRevision)return {};
    BuildingCacheKey key;key.text("building-instance-v1");key.number(contentNamespace);
    key.number(styleRevision);key.number(materialRevision);key.text(buildingGeometryCacheKey(local));
    const auto raw=key.take();if(raw.size()>32768)return {};
    // Full encoded identity, not a collision-prone short hash. Safe cache
    // path characters; no embedded NULs or accidental filesystem traversal.
    constexpr char hex[]="0123456789abcdef";
    std::string result="generated/buildings/";
    result.reserve(result.size()+raw.size()*2);
    for(unsigned char c:raw){result.push_back(hex[c>>4]);result.push_back(hex[c&15]);}
    return result;
}
inline BuildingInstanceTemplate acquireBuildingInstanceTemplate(CachedRuntimeBuildingGenerator& generator,
    RuntimeBuildingRequest request,uint64_t contentNamespace,uint64_t styleRevision,uint64_t materialRevision){
    // Existing cache validates request dimensions and all authored features.
    // Do not silently normalize an invalid quarter-turn.
    if(request.yaw>3||!contentNamespace||!styleRevision||!materialRevision)return {};
    request.origin={};request.yaw=0;
    auto local=generator.generate(request);
    if(!local||!*local)return {};
    auto key=buildingInstanceModelKey(request,contentNamespace,styleRevision,materialRevision);
    if(key.empty())return {};
    return {std::move(local),std::move(key)};
}
struct BuildingInstancePlacement {
    uint64_t id=0;Vec3 origin{};uint32_t yaw=0;
};
inline bool validBuildingInstancePlacement(const BuildingInstancePlacement& p){
    const auto bound=Fixed::fromInt(999000);
    return p.id&&p.yaw<4&&fxAbs(p.origin.x)<=bound&&fxAbs(p.origin.y)<=bound&&fxAbs(p.origin.z)<=bound;
}
// Offset needed by a rotation about local zero to preserve the generator's
// minimum-corner convention: yaw 1 => (D-z,x), yaw 2 => (W-x,D-z).
inline Vec3 buildingInstanceRotationOffset(Fixed w,Fixed d,uint32_t yaw){
    switch(yaw){
        case 1:return {d,{},{}};
        case 2:return {w,{},d};
        case 3:return {{},{},w};
        default:return {};
    }
}
inline Fixed buildingInstanceDimension(float n){return Fixed::fromRatio(int64_t(std::llround(double(n)*1000)),1000);}
inline Vec3 buildingInstancePoint(Vec3 local,Fixed w,Fixed d,const BuildingInstancePlacement& p){
    switch(p.yaw){
        case 1:return p.origin+Vec3{d-local.z,local.y,local.x};
        case 2:return p.origin+Vec3{w-local.x,local.y,d-local.z};
        case 3:return p.origin+Vec3{local.z,local.y,w-local.x};
        default:return p.origin+local;
    }
}
struct BuildingPlacedInstance {
    BuildingInstanceTemplate resource;
    BuildingInstancePlacement placement;
    std::vector<BuildingBox> boxes;
    std::vector<Vec3> gates;
    std::vector<BuildingFeatureAnchor> anchors;
};
inline bool placeBuildingInstance(const BuildingInstanceTemplate& resource,const BuildingInstancePlacement& p,
    BuildingPlacedInstance& out){
    if(!resource||!validBuildingInstancePlacement(p))return false;
    const auto& b=resource.local->building;
    const auto w=buildingInstanceDimension(b.constructionSize.width),d=buildingInstanceDimension(b.constructionSize.depth);
    if(w<=Fixed{}||d<=Fixed{})return false;
    BuildingPlacedInstance next;next.resource=resource;next.placement=p;
    next.boxes.reserve(b.boxes.size());
    for(const auto& box:b.boxes){
        const auto a=buildingInstancePoint(box.min,w,d,p),c=buildingInstancePoint(box.max,w,d,p);
        next.boxes.push_back({{fxMin(a.x,c.x),fxMin(a.y,c.y),fxMin(a.z,c.z)},
            {fxMax(a.x,c.x),fxMax(a.y,c.y),fxMax(a.z,c.z)}});
    }
    for(auto g:b.gates)next.gates.push_back(buildingInstancePoint(g,w,d,p));
    next.anchors=resource.local->featureAnchors;
    for(auto& a:next.anchors)a.outside=buildingInstancePoint(a.outside,w,d,p);
    out=std::move(next);return true;
}
} // namespace aiverse::lib::systems
