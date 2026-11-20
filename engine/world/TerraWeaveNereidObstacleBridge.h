// TerraWeave Stage62: publish authoritative Box3D tree/debris blockage
// into NEREID's existing WaterFlowObstructionComponent ECS inputs.
// The server captures this AFTER physics::Scene::step(), at an ECS
// structural write barrier BEFORE the next NEREID water sampling tick.
// No water solver duplication, no float simulation on lockstep clients.
#pragma once
#include "engine/world/TerraWeaveFallenPhysics.h"
#include "engine/world/TerraWeaveSlopeDebrisPhysics.h"
#include "engine/world/TerraWeaveFlowObstructionEcs.h"
#include "engine/world/WorldScene.h"
#include <aiverselib/water/NereidWaterSystems.h>
#include <aiverselib/water/NereidFlowObstacles.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct FlowObstructionCapturePolicy {
    uint32_t maxFallenTrees=128; // <=128; rest remain physical/nav obstacles
    uint32_t maxSlopeDebris=128;  // <=128
    uint32_t treePermeabilityPermille=250;
    uint32_t debrisPermeabilityPermille=650;
    uint32_t deflectionPermille=400;
};
inline void validateFlowObstructionCapture(
        const FlowObstructionCapturePolicy& c){
    if(c.maxFallenTrees>128 || c.maxSlopeDebris>128 ||
       (!c.maxFallenTrees && !c.maxSlopeDebris) ||
       c.treePermeabilityPermille>1000 ||
       c.debrisPermeabilityPermille>1000 ||
       c.deflectionPermille>1000)
        throw std::invalid_argument("TerraWeave water obstruction capture policy");
}

namespace obstruction_detail {
// Float Box3D is the SERVER presentation/physics layer. Quantize at its
// owner-thread barrier before updating authoritative Fixed ECS obstructions.
// A network client does NOT capture locally simulated physics.
inline int64_t cm(float metres){
    if(!std::isfinite(metres) || std::abs(metres)>90000.f)
        throw std::out_of_range("TerraWeave invalid Box3D obstacle position");
    return std::llround(double(metres)*100.0);
}
inline uint64_t treeKey(AnchoredPlantId id){
    uint64_t key=detail::mix64(id.key ^
        (uint64_t(id.speciesId)<<32) ^ 0x74726565776f6465ULL);
    return key?key:1;
}
inline uint64_t slideKey(SlopeDebrisId id){
    uint64_t key=detail::mix64(
        id.tick ^ detail::mix64(uint64_t(id.from.x)) ^
        (detail::mix64(uint64_t(id.from.z))<<1) ^
        detail::mix64(uint64_t(id.to.x)) ^
        (detail::mix64(uint64_t(id.to.z))>>1) ^
        0x736c6f7065646562ULL);
    return key?key:1;
}
} // namespace obstruction_detail

inline std::vector<lib::nereid::WaterFlowObstructionComponent>
captureNereidFlowObstructions(
        const World& waterWorld,const physics::Scene& physicsScene,
        const FallenPlantPhysics* trees,const SlopeDebrisPhysics* debris,
        FlowObstructionCapturePolicy policy={}) {
    validateFlowObstructionCapture(policy);
    using Obstacle=lib::nereid::WaterFlowObstructionComponent;
    std::map<uint64_t,Obstacle> sorted;
    const auto add=[&](uint64_t key,int64_t x,int64_t y,int64_t z,
                       int64_t radiusCm,int64_t heightCm,
                       uint32_t permeability) {
        if(!key || radiusCm<5 || radiusCm>10000 ||
           heightCm<5 || heightCm>30000)
            return;
        const int64_t radius=radiusCm;
        if(x<-10000000+radius || x>10000000-radius ||
           z<-10000000+radius || z>10000000-radius)
            return;
        const int64_t bottom=y-heightCm/2;
        const int64_t top=y+(heightCm+1)/2;
        if(bottom<-10000000 || top>10000000)return;
        const Vec3 sampleAt{
            Fixed::fromRatio(x,100),Fixed::fromRatio(y,100),
            Fixed::fromRatio(z,100)};
        // The existing NEREID sample chooses the authoritative WaterBody
        // polygon/spline/priority. Obstruction does not create water itself.
        const auto water=lib::nereid::sampleWater(waterWorld,sampleAt);
        if(!water.found || Fixed::fromRatio(top,100)<=water.bottomY ||
           Fixed::fromRatio(bottom,100)>=water.surfaceY)
            return;
        Obstacle obstacle;
        obstacle.waterBody=water.body;
        obstacle.center={sampleAt.x,sampleAt.z};
        obstacle.radius=Fixed::fromRatio(radius,100);
        obstacle.bottomY=Fixed::fromRatio(bottom,100);
        obstacle.topY=Fixed::fromRatio(top,100);
        obstacle.sourceKey=key;
        obstacle.permeabilityPermille=permeability;
        obstacle.deflectionPermille=policy.deflectionPermille;
        if(!lib::nereid::validWaterFlowObstruction(obstacle) ||
           !sorted.emplace(key,obstacle).second)
            throw std::invalid_argument(
                "TerraWeave duplicated or invalid water obstruction source");
    };
    if(trees){
        uint32_t processed=0;
        for(const auto& item:trees->snapshot()){
            if(processed>=policy.maxFallenTrees)break;
            if(!physicsScene.valid(item.body))continue;
            const b3Vec3 p=physicsScene.position(item.body);
            const b3Quat q=physicsScene.rotation(item.body);
            const double qx=q.v.x,qy=q.v.y,qz=q.v.z,qw=q.s;
            if(!std::isfinite(qx) || !std::isfinite(qy) ||
               !std::isfinite(qz) || !std::isfinite(qw))
                continue;
            // A fallen trunk is a dynamic capsule. Its rotated local +Y
            // axis bounds the horizontal footprint; keep radius <=100m.
            const double ax=2.0*(qx*qy-qw*qz);
            const double az=2.0*(qy*qz+qw*qx);
            const double ay=1.0-2.0*(qx*qx+qz*qz);
            const double halfCm=
                double(item.heightCm)*item.modelScalePermille/2000.0;
            const double horizontal=std::sqrt(ax*ax+az*az);
            const int64_t radius=std::clamp<int64_t>(
                int64_t(std::ceil(halfCm*horizontal+40)),40,10000);
            const int64_t vertical=std::clamp<int64_t>(
                int64_t(std::ceil(halfCm*std::abs(ay)*2.0+80)),20,30000);
            add(obstruction_detail::treeKey(item.id),
                obstruction_detail::cm(p.x),
                obstruction_detail::cm(p.y),
                obstruction_detail::cm(p.z),
                radius,vertical,policy.treePermeabilityPermille);
            ++processed;
        }
    }
    if(debris){
        uint32_t processed=0;
        for(const auto& item:debris->snapshot()){
            if(processed>=policy.maxSlopeDebris)break;
            if(!physicsScene.valid(item.body))continue;
            const b3Vec3 p=physicsScene.position(item.body);
            add(obstruction_detail::slideKey(item.id),
                obstruction_detail::cm(p.x),
                obstruction_detail::cm(p.y),
                obstruction_detail::cm(p.z),
                std::max<int64_t>(5,item.radiusCm),
                std::max<int64_t>(10,int64_t(item.radiusCm)*2),
                policy.debrisPermeabilityPermille);
            ++processed;
        }
    }
    std::vector<Obstacle> out;
    out.reserve(sorted.size());
    for(const auto& [id,entry]:sorted){
        (void)id;out.push_back(entry);
    }
    return out;
}

// Scene stable water IDs are NOT ECS body Entity indices. The host supplies
// this exact mapping (from its water manifest/scene adapter); missing bodies
// intentionally produce no visual blockage, while ECS flow remains valid.
using NereidSceneWaterId=
    std::function<std::optional<uint64_t>(Entity)>;

inline std::vector<world::NereidSceneFlowObstruction>
makeNereidSceneObstructionSnapshot(
        const std::vector<lib::nereid::WaterFlowObstructionComponent>& raw,
        const NereidSceneWaterId& sceneIdForBody) {
    if(!sceneIdForBody ||
       raw.size()>lib::nereid::kMaxWaterFlowObstructions)
        throw std::invalid_argument("TerraWeave missing scene-water ID mapper");
    std::vector<world::NereidSceneFlowObstruction> output;
    output.reserve(raw.size());
    for(const auto& component:raw) {
        if(!lib::nereid::validWaterFlowObstruction(component) ||
           !component.sourceKey)
            throw std::invalid_argument(
                "TerraWeave invalid visual flow obstruction source");
        const auto stableId=sceneIdForBody(component.waterBody);
        if(!stableId)continue;
        if(!*stableId)
            throw std::invalid_argument(
                "TerraWeave scene water IDs must be nonzero");
        output.push_back({*stableId,component});
    }
    // WorldScene further sorts/validates the list, this normalization lets
    // native hosts compare stable snapshots for dirties/replay diagnostics.
    std::sort(output.begin(),output.end(),
        [](const auto& a,const auto& b){
            return a.sceneWaterId==b.sceneWaterId ?
                a.effect.sourceKey<b.effect.sourceKey :
                a.sceneWaterId<b.sceneWaterId;
        });
    return output;
}

inline FlowObstructionPublishResult synchronizeNereidPhysicalObstacles(
        World& authoritativeEcs,world::WorldScene& scene,
        const physics::Scene& physicsScene,
        const FallenPlantPhysics* trees,const SlopeDebrisPhysics* debris,
        const NereidSceneWaterId& sceneIdForBody,
        FlowObstructionCapturePolicy policy={}) {
    // Run on native structural/game scene owner barrier AFTER the Box3D step
    // and BEFORE NEREID samples water next tick. Visuals cannot inspect
    // mutable Box3D live pointers or recalculate hydraulic flow themselves.
    const auto physical=captureNereidFlowObstructions(
        authoritativeEcs,physicsScene,trees,debris,policy);
    auto visual=makeNereidSceneObstructionSnapshot(
        physical,sceneIdForBody);
    auto changed=publishNereidFlowObstructions(
        authoritativeEcs,physical);
    scene.setNereidFlowObstructions(std::move(visual));
    return changed;
}

} // namespace aiverse::terraweave
