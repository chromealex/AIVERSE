// Static model-preview bridge: emit a complete, borrowed View model packet
// without constructing a legacy RtsView or an animation player.
// RTS host selects resource/variant and supplies presentation appearance.
// The renderer owns actual model drawing and GPU state.
#pragma once
#include "engine/view/AppearancePacket.h"
#include <algorithm>
#include <cstdint>

namespace aiverse::game::view_adapter {

struct StaticPreviewModel {
    const render::Model* model=nullptr;
    const std::vector<render::Material>* materials=nullptr;
    const std::vector<int>* nodeMaterial=nullptr;
    render::GeometryPolicy geometry{};
    render::mat4 fix{};
    render::float4 tint{1,1,1,1};
    render::float3 team{1,1,1};
    int variant=-1;
    bool structure=false,shape=false,air=false,transparent=false;
    bool staticObject=false;
    float scale=1.f,sx=1.f,sz=1.f,flyHeight=0.f;
};

inline bool buildStaticPreviewPacket(const StaticPreviewModel& spec,
                                     float yaw,view::Object& packet) {
    if(!spec.model)return false;
    const float k=spec.scale;
    const render::float3 p{0,spec.air?spec.flyHeight:0,0};
    render::mat4 base;
    if(spec.structure && spec.shape)
        base=render::mat4::translate(p)*render::mat4::scale({
            spec.sx*k,k*std::max(spec.sx,spec.sz)*.75f,spec.sz*k});
    else
        base=render::mat4::translate(p)*render::mat4::rotateY(yaw)*
             render::mat4::scale({k,k,k});
    // RTS owns *selection* of appearance and the authored object frame;
    // generic View owns packet field composition shared by all genres.
    view::AppearanceSpec appearance;
    appearance.model=spec.model;
    appearance.geometry=spec.geometry;
    appearance.materials=spec.materials;
    appearance.nodeMaterial=spec.nodeMaterial;
    appearance.variant=spec.variant;
    appearance.tint=spec.tint;
    appearance.team=spec.team;
    if(spec.transparent)appearance.flags|=render::kDrawTransparent;
    if(spec.staticObject && !spec.air)
        appearance.flags|=render::kDrawStatic;
    // Static preview never supplies a graph; View cannot tick it twice.
    return view::buildAppearancePacket(appearance,base*spec.fix,packet);
}

// A flat-ground placement preview keeps the same resolved mesh/material
// packet as a static portrait, but moves it onto the sampled surface and
// applies the host-provided valid/invalid colour. Never flag ghosts static.
inline bool buildStaticGhostPacket(const StaticPreviewModel& spec,
                                   render::float3 position,float ground,
                                   render::float4 ghostTint,
                                   view::Object& packet) {
    if(!buildStaticPreviewPacket(spec,0.f,packet))return false;
    packet.draw.world=render::mat4::translate({
        position.x,ground,position.z})*packet.draw.world;
    packet.draw.tint=ghostTint;
    packet.draw.flags=render::kDrawNoCastShadow;
    packet.draw.flash=0.f;
    packet.draw.dissolve=0.f;
    return true;
}

inline void submitStaticPreview(render::Renderer3D& renderer,
                                const view::Object& packet) {
    view::submitPrecomputedAppearance(renderer,packet);
}

} // namespace aiverse::game::view_adapter
