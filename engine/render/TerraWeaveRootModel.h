// TerraWeave conditional exposed-root bark prototype.
// Root geometry is built only after a soil support snapshot reports exposed
// tree/shrub roots; it is NEVER built for Grass. 3 coarse shared depth tiers
// per species allow normal Renderer3D mesh batching and RHI upload lifecycle.
#pragma once
#include "engine/world/TerraWeaveVegetationSoil.h"
#include "engine/world/TerraWeaveArborMesh.h"
#include "engine/render/Model.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <string>

namespace aiverse::terraweave {

inline std::unique_ptr<render::Model> makeExposedRootModel(
        uint32_t speciesId,uint64_t seed,RootProfile profile,
        uint32_t exposureTier) {
    validateRootProfile(profile);
    if(!speciesId || profile.habit==PlantHabit::Grass ||
       exposureTier<1 || exposureTier>3)
        throw std::invalid_argument("TerraWeave root prototype tier/species");
    const uint32_t depth=std::max<uint32_t>(
        1,profile.depthCm*exposureTier/3u);
    const RootAnchor anchor{
        seed,speciesId,0,0,0,profile};
    RootState state;
    state.plantKey=seed;
    state.speciesId=speciesId;
    state.habit=profile.habit;
    state.exposedCm=depth;
    state.hasVisibleRoots=true;
    const auto segments=generateExposedRoots(anchor,state);
    TreeSkeleton skeleton;
    skeleton.speciesId=speciesId;
    skeleton.seed=seed;
    for(const auto& root:segments)
        skeleton.branches.push_back({
            root.startXcm,root.startYcm,root.startZcm,
            root.endXcm,root.endYcm,root.endZcm,
            int32_t(root.radiusCm),
            std::max<int32_t>(1,int32_t(root.radiusCm)/5),
            -1});
    ArborMeshSettings meshSettings;
    meshSettings.radialSegments=5;
    meshSettings.doubleSidedLeaves=false;
    meshSettings.leafPlanes=1;
    const auto geometry=buildArborMesh(skeleton,meshSettings);
    if(geometry.vertices.empty() || geometry.indices.empty())
        throw std::logic_error("TerraWeave empty exposed roots");
    auto model=std::make_unique<render::Model>();
    model->path="terraweave/roots/"+std::to_string(speciesId)+"/"+
                std::to_string(exposureTier);
    model->batching.autoLod=false;
    render::Material bark;
    bark.name="TerraWeave Exposed Roots";
    bark.baseColor={0.25f,0.15f,0.09f,1.0f};
    bark.roughness=0.95f;
    model->materials.push_back(std::move(bark));
    model->vertices.reserve(geometry.vertices.size());
    for(const auto& v:geometry.vertices) {
        render::ModelVertex vertex;
        vertex.pos={v.x*0.01f,v.y*0.01f,v.z*0.01f};
        vertex.nrm={v.nx,v.ny,v.nz};
        vertex.uv={v.u,v.v};
        model->vertices.push_back(vertex);
    }
    model->indices=geometry.indices;
    render::Primitive primitive;
    primitive.material=0;
    primitive.firstVertex=0;
    primitive.vertexCount=uint32_t(model->vertices.size());
    primitive.firstIndex=0;
    primitive.indexCount=uint32_t(model->indices.size());
    render::MeshDef mesh;
    mesh.name="Exposed roots";
    mesh.primitives.push_back(primitive);
    model->meshes.push_back(std::move(mesh));
    render::Node node;
    node.name="Root crown";
    node.mesh=0;
    model->nodes.push_back(std::move(node));
    model->finalize();
    return model;
}

} // namespace aiverse::terraweave
