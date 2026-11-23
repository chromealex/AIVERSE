#pragma once
// Presentation adapter for authored lift cabins and landing doors.
// Renderer3D owns GPU upload, instance transforms and retirement. This layer
// produces immutable CPU models ONCE plus per-frame transforms; it never
// regenerates geometry or mutates navmesh/SpatialWorld in render callbacks.
#include "engine/buildings/BuildingLiftPlatformModel.h"
#include "engine/buildings/BuildingLiftDoorOwner.h"
#include "engine/buildings/BuildingCirculationHost.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::building {

struct BuildingLiftDoorMesh {
    uint32_t feature=0;
    float leafWidth=0,height=2.3f,thickness=0.12f;
    std::unique_ptr<render::Model> model;
};
struct BuildingLiftVisualPose {
    BuildingLiftDoorKey key;
    bool left=false;
    Vec3 worldPosition{};
    uint32_t yaw=0; // quarter turns, same as building instance
    float openFraction=0.f;
    uint64_t stateRevision=0;
};
struct BuildingLiftPlatformVisualPose {
    uint32_t feature=0;
    Vec3 worldPosition{};
    uint32_t yaw=0;
    uint64_t stateRevision=0;
};
struct BuildingLiftPresentationFrame {
    std::vector<BuildingLiftVisualPose> doorLeaves;
    std::vector<BuildingLiftPlatformVisualPose> platforms;
    bool valid=false;
    std::string error;
};

// Visual interpolation follows the authoritative *door transition phase*.
// A fully open lift is visually open, a traveling lift is fully closed.
// This never determines collision/boarding permission: the physical door
// owner decides that independently from authoritative lift state.
inline float liftDoorVisualOpen(const BuildingLiftSnapshot& state){
    const float t=std::clamp(float(state.phaseElapsedMs)/
        float(BuildingLiftController::kDoorTimeMs),0.f,1.f);
    switch(state.phase){
      case BuildingLiftPhase::Open:return 1.f;
      case BuildingLiftPhase::Closing:return 1.f-t;
      case BuildingLiftPhase::Opening:return t;
      case BuildingLiftPhase::Traveling:
      case BuildingLiftPhase::Offline:return 0.f;
    }
    return 0.f;
}
// A single door-leaf mesh is reused for both sides. Different shaft widths
// receive one mesh per feature, not a fresh mesh every door frame or tick.
inline BuildingLiftDoorMesh runtimeLiftDoorLeafModel(
    uint32_t feature,float shaftWidth,const BuildingStyle& style){
    if(!(std::isfinite(shaftWidth)&&shaftWidth>=1.4f&&shaftWidth<=160.f))
        throw std::invalid_argument("Lift door opening width invalid");
    const int material=style.structure.plate>=0?
        style.structure.plate:style.shell;
    if(material<0||size_t(material)>=style.materials.size())
        throw std::invalid_argument("Lift door has no material");
    const float w=shaftWidth*0.5f;
    buildingStyleDetail::Mesher mesh(1);
    mesh.box(0,0.f,0.f,0.f,w,2.3f,0.12f);
    StyledStructure result;
    result.mesh.materials={style.materials[size_t(material)]};
    result.mesh.vertices=std::move(mesh.v);
    result.mesh.parts.push_back({0,std::move(mesh.perMaterial[0])});
    result.mesh.boundsMin[0]=result.mesh.boundsMin[1]=result.mesh.boundsMin[2]=0.f;
    result.mesh.boundsMax[0]=w;
    result.mesh.boundsMax[1]=2.3f;
    result.mesh.boundsMax[2]=0.12f;
    auto model=runtimeBuildingModel(result,"procedural-lift-door-"+std::to_string(feature));
    model->batching.mergeRigid=false;
    model->batching.geometryPoolPolicy=
        render::Model::Batching::GeometryPoolPolicy::Dedicated;
    model->batching.autoLod=false;
    if(!model->nodes.empty())model->nodes[0].runtimeMutable=true;
    return {feature,w,2.3f,0.12f,std::move(model)};
}

inline BuildingLiftPresentationFrame buildingLiftVisualFrame(
    const BuildingCirculationHost& host,const BuildingLiftDoorOwner& doors,
    float buildingW,float buildingD,Vec3 origin,uint32_t yaw){
    BuildingLiftPresentationFrame frame;
    if(yaw>3||!std::isfinite(buildingW)||!std::isfinite(buildingD)||
       buildingW<=0||buildingD<=0){
        frame.error="Invalid lift presentation transform";
        return frame;
    }
    for(const auto& [feature,controller]:host.lifts()){
        const auto& pose=controller.platformPose();
        // Platform Model uses geometry authored at its own local (0,0,0).
        frame.platforms.push_back({feature,
            buildingLocalToWorld(pose.x0,float(pose.yMm)/1000.f,
                pose.z0,buildingW,buildingD,origin,yaw),
            yaw,controller.snapshot().revision});
    }
    for(const auto& door:doors.poses()){
        auto it=host.lifts().find(door.key.feature);
        if(it==host.lifts().end()){
            frame.error="Missing lift controller for authored landing door";
            frame.doorLeaves.clear();frame.platforms.clear();
            return frame;
        }
        const float opening=(it->second.snapshot().currentFloor==door.key.floor||
            it->second.snapshot().targetFloor==door.key.floor)?
            liftDoorVisualOpen(it->second.snapshot()):0.f;
        const float half=(door.x1-door.x0)*0.5f;
        const float mid=(door.x0+door.x1)*0.5f;
        const float leftX=door.x0-half*opening;
        const float rightX=mid+half*opening;
        // The physical landing leaves are mounted just before the shaft.
        // Both use a door leaf mesh whose geometry begins at x=0/z=0.
        for(int side=0;side<2;++side){
            BuildingLiftVisualPose visual;
            visual.key=door.key;
            visual.left=(side==0);
            visual.worldPosition=buildingLocalToWorld(
                side==0?leftX:rightX,
                float(door.yMm)/1000.f,door.z-0.06f,
                buildingW,buildingD,origin,yaw);
            visual.yaw=yaw;visual.openFraction=opening;
            visual.stateRevision=it->second.snapshot().revision;
            frame.doorLeaves.push_back(visual);
        }
    }
    frame.valid=true;
    return frame;
}
} // namespace aiverse::building
