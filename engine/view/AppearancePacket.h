// Genre-independent authored appearance packet builder.
// Presentation hosts own Model/Materials/AnimationGraph and choose all rules;
// View borrows their immutable outputs only for a synchronous submission.
// No model import, animation clock tick, GPU state or genre concept here.
#pragma once
#include "View.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace aiverse::view {

struct AppearanceSpec {
    const render::Model* model=nullptr;
    const std::vector<render::Material>* materials=nullptr;
    const std::vector<int>* nodeMaterial=nullptr;
    render::GeometryPolicy geometry{};
    render::float4 tint{1,1,1,1};
    render::float3 team{1,1,1};
    int variant=-1;
    uint32_t flags=0;
    // Optional single-writer graph input: host MUST call View::update()
    // once before submitOne/submitPackets, or pass a precomputed pose with
    // graph unset. Never let multiple View owners tick the same graph.
    std::shared_ptr<const render::AnimationGraph> graph;
    render::AnimationParameters parameters;
    uint64_t sourceId=0,generation=0;
    // Optional native Model asset epoch, independent of entity generation.
    // Set from ModelPublicationEpoch::next() at new Model publication;
    // do not bump it merely because an AnimationGraph was recompiled.
    uint64_t modelPublicationGeneration=0;
};

inline bool buildAppearancePacket(const AppearanceSpec& spec,
                                  const render::mat4& world,
                                  Object& packet) {
    if(!spec.model)return false;
    packet={};
    packet.id=spec.sourceId;
    packet.generation=spec.generation;
    packet.draw.model=spec.model;
    packet.draw.geometry=spec.geometry;
    packet.draw.world=world;
    packet.draw.materials=spec.materials;
    packet.draw.nodeMaterial=spec.nodeMaterial;
    packet.draw.variant=spec.variant;
    packet.draw.tint=spec.tint;
    packet.draw.team=spec.team;
    packet.draw.flags=spec.flags;
    packet.graph=spec.graph;
    packet.parameters=spec.parameters;
    packet.animationModelGeneration=spec.modelPublicationGeneration;
    return true;
}

inline void submitPrecomputedAppearance(render::Renderer3D& renderer,
                                        const Object& packet) {
    // Only for static or already-resolved host poses (no graph). A graph
    // requires exactly one View::update() and View::submitOne() instead.
    if(!packet.graph && packet.visible)
        View::submitModel(renderer,packet.draw);
}

} // namespace aiverse::view
