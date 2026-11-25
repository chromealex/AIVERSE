// Native, read-only local wiring candidates. Explicit Wire entities remain
// the authority; proximity never silently adds or deletes a connection.
#pragma once
#include "CircuitEcs.h"
#include "WorldProcessSurfaceFrame.h"
#include "engine/sim/CommonComponents.h"

namespace aiverse::circuit {
struct LocalWireCandidate {Entity node=kNullEntity;Fixed distance{};};
inline std::optional<std::vector<LocalWireCandidate>> localWireCandidates(
        const World& world,const game::Surface* surface,Entity source,
        Fixed radius,size_t maximum=64){
    namespace wp=lib::worldprocess;
    constexpr auto positionLimit=Fixed::fromInt(1000000);
    if(radius<Fixed{}||radius>Fixed::fromInt(10000)||maximum>kMaxNodes||
       world.countExisting<Node>()>kMaxNodes)return {};
    const auto locate=[&](Entity e)->std::optional<wp::SurfaceFrame>{
        const auto* node=world.tryGetConst<Node>(e);
        const auto* tr=world.tryGetConst<Transform>(e);
        if(!world.alive(e)||!node||!node->enabled||!valid(*node)||!tr)return {};
        for(auto v:{tr->pos.x,tr->pos.y,tr->pos.z})
            if(v< -positionLimit||v>positionLimit)return {};
        return wp::surfaceFrame(surface,tr->pos);
    };
    const auto origin=locate(source);
    if(!origin)return {};
    std::vector<LocalWireCandidate> result;
    if(!maximum)return result;
    world.eachExistingConst<Node>([&](Entity e,const Node&){
        if(e==source)return;
        const auto target=locate(e);
        if(!target||!wp::sameSurfaceBody(*origin,*target))return;
        // Actual 3D chord includes altitude, unlike surface ground distance.
        // Position bounds keep the squared Fixed distance within range.
        const auto d=(target->origin-origin->origin).length();
        if(d<=radius)result.push_back({e,d});
    });
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){
        return a.distance!=b.distance?a.distance<b.distance:a.node.packed()<b.node.packed();
    });
    if(result.size()>maximum)result.resize(maximum);
    return result;
}
} // namespace aiverse::circuit
