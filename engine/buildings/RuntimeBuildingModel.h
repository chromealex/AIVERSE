#pragma once
// CPU-only bridge from the portable runtime building generator to Renderer3D.
// The GPU owner uploads the returned Model with render::uploadModel() on its
// device thread, publishes it via the existing Model owner, and releases it
// ONLY through the engine's normal GPU retirement/lifetime protocol.
// Never upload, release or draw GPU resources inside ECS simulation ticks.
#include <aiverselib/systems/RuntimeBuildingGenerator.h>
#include "engine/render/Model.h"
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace aiverse::building {

inline std::unique_ptr<render::Model> runtimeBuildingModel(
    const lib::systems::StyledStructure& building,
    const std::string& name="procedural-building") {
    const auto& mesh=building.mesh;
    if(mesh.vertices.empty()||mesh.parts.empty()||mesh.materials.empty())
        throw std::invalid_argument("Runtime building has no renderable mesh");
    if(mesh.vertices.size()>std::numeric_limits<uint32_t>::max())
        throw std::length_error("Runtime building vertex count overflows 32 bits");
    auto model=std::make_unique<render::Model>();
    model->path=name;
    model->batching.autoLod=false; // caller explicitly opts in after testing generated LODs
    model->vertices.reserve(mesh.vertices.size());
    model->materials.reserve(mesh.materials.size());
    for(const auto& source:mesh.materials) {
        render::Material material;
        material.name=source.id;
        material.baseColor={source.color[0],source.color[1],source.color[2],1.f};
        material.metallic=source.metallic;
        material.roughness=source.roughness;
        material.emissive={source.emissive[0],source.emissive[1],source.emissive[2]};
        material.emissiveStrength=source.emissiveStrength;
        model->materials.push_back(std::move(material));
    }
    for(const auto& source:mesh.vertices) {
        render::ModelVertex vertex;
        vertex.pos={source.pos[0],source.pos[1],source.pos[2]};
        vertex.nrm={source.nrm[0],source.nrm[1],source.nrm[2]};
        vertex.uv={source.uv[0],source.uv[1]};
        model->vertices.push_back(vertex);
    }
    render::MeshDef meshDef;
    meshDef.name=name;
    for(const auto& part:mesh.parts) {
        if(part.material<0||size_t(part.material)>=model->materials.size()||
           part.indices.empty()||part.indices.size()%3!=0||
           model->indices.size()+part.indices.size()>std::numeric_limits<uint32_t>::max())
            throw std::invalid_argument("Runtime building has invalid material or triangle indices");
        render::Primitive primitive;
        primitive.material=part.material;
        primitive.firstIndex=uint32_t(model->indices.size());
        primitive.indexCount=uint32_t(part.indices.size());
        primitive.firstVertex=0;
        primitive.vertexCount=uint32_t(model->vertices.size());
        for(uint32_t index:part.indices) {
            if(index>=model->vertices.size())
                throw std::invalid_argument("Runtime building index exceeds vertex count");
            model->indices.push_back(index);
        }
        meshDef.primitives.push_back(std::move(primitive));
    }
    model->meshes.push_back(std::move(meshDef));
    render::Node node;
    node.name=name;
    node.mesh=0;
    model->nodes.push_back(std::move(node));
    model->finalize(); // calculates primitive/node/model bounds on CPU
    return model;
}

} // namespace aiverse::building
