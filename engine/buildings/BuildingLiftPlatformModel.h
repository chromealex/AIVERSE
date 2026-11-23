#pragma once
// CPU model for ONE movable lift platform. The static building generator
// omits that platform in runtime mode; the renderer owner creates this
// dedicated model once and moves its instance transform with platformPose().
#include "engine/buildings/RuntimeBuildingModel.h"
#include <aiverselib/systems/BuildingCirculationRuntime.h>
#include <memory>
#include <stdexcept>
#include <string>

namespace aiverse::building {
// Geometry is authored at (0,0,0); use buildingLiftPlatformWorldBounds or
// buildingLocalToWorld(pose.x0, yMm/1000, pose.z0, ...) to place its instance.
// Never regenerate/upload the platform per tick. Keep this model resident
// and advance only the game-owned instance transform.
inline std::unique_ptr<render::Model> runtimeLiftPlatformModel(
    const lib::systems::BuildingLiftPlatformPose& pose,
    const lib::systems::BuildingStyle& style,
    const std::string& name="procedural-lift-platform"){
    using namespace lib::systems;
    const float w=pose.x1-pose.x0,d=pose.z1-pose.z0;
    if(!(w>0.1f)||!(d>0.1f)||!(pose.thickness>0.05f))
        throw std::invalid_argument("Invalid lift platform extent");
    const int material=style.structure.plate>=0?
        style.structure.plate:style.shell;
    if(material<0||size_t(material)>=style.materials.size())
        throw std::invalid_argument("Lift platform material unavailable");
    buildingStyleDetail::Mesher mesh(1);
    mesh.box(0,0.f,0.f,0.f,w,pose.thickness,d);
    StyledStructure geometry;
    geometry.mesh.materials={style.materials[size_t(material)]};
    geometry.mesh.vertices=std::move(mesh.v);
    geometry.mesh.parts.push_back({0,std::move(mesh.perMaterial[0])});
    geometry.mesh.boundsMin[0]=0;geometry.mesh.boundsMin[1]=0;
    geometry.mesh.boundsMin[2]=0;
    geometry.mesh.boundsMax[0]=w;geometry.mesh.boundsMax[1]=pose.thickness;
    geometry.mesh.boundsMax[2]=d;
    auto model=runtimeBuildingModel(geometry,name);
    // Dynamic platform is never a candidate for merged rigid static
    // geometry: the owning game will move its render instance each tick.
    model->batching.mergeRigid=false;
    model->batching.geometryPoolPolicy=
        render::Model::Batching::GeometryPoolPolicy::Dedicated;
    model->batching.autoLod=false;
    if(!model->nodes.empty())model->nodes[0].runtimeMutable=true;
    return model;
}
} // namespace aiverse::building
