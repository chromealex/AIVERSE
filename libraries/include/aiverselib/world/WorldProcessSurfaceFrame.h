// Native coordinate adapter: reuse the canonical GameCore Surface, not a
// separate planet topology. Body-local simulation positions only, never
// render/orbit world positions. Flat hosts need not link Surface unless used.
#pragma once
#include "engine/game/Surface.h"
#include <optional>

namespace aiverse::lib::worldprocess {
struct SurfaceFrame {
    int body=-1; // -1: legacy plane
    int cell=-1; // canonical Surface cell, including face/body
    Vec3 origin{};
    Vec3 east{Fixed::fromInt(1),Fixed{},Fixed{}};
    Vec3 up{Fixed{},Fixed::fromInt(1),Fixed{}};
    Vec3 south{Fixed{},Fixed{},Fixed::fromInt(1)};
    Vec3 localVector(Vec3 v)const{return {v.dot(east),v.dot(up),v.dot(south)};}
    Vec3 simulationVector(Vec3 v)const{return east*v.x+up*v.y+south*v.z;}
};
inline std::optional<SurfaceFrame> surfaceFrame(const game::Surface* surface,Vec3 p){
    SurfaceFrame frame;
    frame.origin=p;
    if(!surface)return frame;
    if(!surface->sphere()||!surface->tab||surface->bodies.empty()||
       surface->bodies.size()>size_t(game::Surface::kMaxBodies)||
       !game::Surface::onSphere(p))return {};
    // Surface::bodyAt clamps out-of-range IDs; reject such inputs instead
    // of accidentally treating them as the last planet.
    constexpr auto spacing=int64_t(game::Surface::kBodySpacing)*Fixed::kOne;
    const auto bodyIndex=(p.x.raw-game::Surface::kSphereMinRaw)/spacing;
    if(bodyIndex<0||size_t(bodyIndex)>=surface->bodies.size())return {};
    frame.body=int(bodyIndex);
    if(!surface->bodies[size_t(frame.body)].hasSurface())return {};
    constexpr auto maxLocal=Fixed::fromInt(2048);
    if(p.y< -maxLocal||p.y>maxLocal||p.z< -maxLocal||p.z>maxLocal)return {};
    const auto local=p-game::Surface::anchor(frame.body);
    if(local.x< -maxLocal||local.x>maxLocal||local.lengthSq().raw<=0)return {};
    frame.cell=surface->cellAt(p);
    if(frame.cell<0)return {};
    frame.up=surface->up(p);
    surface->frame(p,frame.east,frame.south);
    return frame;
}
// Do not join independent bodies merely because their projected XZ values
// overlap. A deliberate authored connection is a separate gameplay policy.
inline bool sameSurfaceBody(const SurfaceFrame& a,const SurfaceFrame& b){
    return a.body==b.body;
}
inline std::optional<Fixed> surfaceGroundDistance(const game::Surface* surface,
        const SurfaceFrame& a,const SurfaceFrame& b){
    if(!sameSurfaceBody(a,b))return {};
    if(surface){
        if(a.body<0||size_t(a.body)>=surface->bodies.size())return {};
        return surface->groundDist(a.origin,b.origin);
    }
    if(a.body!=-1)return {};
    return (a.origin-b.origin).xz().length();
}
} // namespace aiverse::lib::worldprocess
