// Optional Box3D contact -> MORPH ECS impact bridge for authoritative hosts.
// No engine/physics dependency from libraries/. Box3D remains the sole
// rigid-body/contact solver; MORPH only owns quantized deformable offsets.
#pragma once
#include "aiverselib/world/MorphEcs.h"
#include "aiverselib/world/MorphPhysisGravity.h"
#include "engine/physics/PhysisBox3D.h"
#include "engine/physics/Scene.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <tuple>
#include <span>
#include <vector>

namespace aiverse::examples::world_processes_native {

struct MorphContactConfig {
    // Quantized spring/velocity impulse for each m/s of closing speed.
    // This is an art-directed deformation scale, NOT Box3D mass impulse.
    std::int64_t unitsPerMetrePerSecond=1000;
    std::int64_t minimumImpulse=1;
    std::int64_t maximumImpulse=100000;
};
struct MorphContactInput {
    Entity target=kNullEntity;
    physics::BodyHandle body=physics::kNoBody,other=physics::kNoBody;
    std::uint32_t applyTick=0;
    std::int64_t dx=0,dy=0,dz=0;
};

inline bool valid(const MorphContactConfig& config){
    return config.unitsPerMetrePerSecond>=1 &&
           config.unitsPerMetrePerSecond<=morph::kForceLimit &&
           config.minimumImpulse>=0 &&
           config.maximumImpulse>=config.minimumImpulse &&
           config.maximumImpulse<=morph::kForceLimit;
}

// Box3D::Scene is already stepped by its sole authoritative physics owner.
// This read-only projector transforms begin contacts to fully-quantized
// replayable MORPH inputs. A lockstep client MUST consume the server's
// recorded/replicated inputs, not rerun float Box3D independently.
inline bool collectMorphContactInputs(
        const physics::Scene& scene,const World& ecs,
        const std::map<physics::BodyHandle,Entity>& bodyToNode,
        std::uint32_t nextEcsTick,
        std::vector<MorphContactInput>& result,
        MorphContactConfig config={},
        const game::Surface* surface=nullptr,
        const std::map<uint64_t,Entity>* localFrameOwners=nullptr){
    // Opt-in local frames for every mapped node; do not mix old world-space
    // inputs and local inputs in the same collection.
    if(surface&&!localFrameOwners)return false;
    if(!valid(config)||nextEcsTick==UINT32_MAX||
       scene.contactBegins().size()>4096)return false;
    std::vector<MorphContactInput> staged;
    staged.reserve(scene.contactBegins().size()*2);
    for(const auto& contact:scene.contactBegins()){
        if(!scene.valid(contact.a)||!scene.valid(contact.b))continue;
        const auto& va=scene.linearVelocity(contact.a);
        const auto& vb=scene.linearVelocity(contact.b);
        const auto& normal=contact.normal;
        const float length=std::sqrt(normal.x*normal.x+normal.y*normal.y+
                                     normal.z*normal.z);
        if(!std::isfinite(length)||length<1e-6f)continue;
        const float nx=normal.x/length,ny=normal.y/length,nz=normal.z/length;
        const float closing=(va.x-vb.x)*nx+(va.y-vb.y)*ny+(va.z-vb.z)*nz;
        if(!std::isfinite(closing)||closing<=0.f)continue;
        const double scaled=double(closing)*double(config.unitsPerMetrePerSecond);
        if(!std::isfinite(scaled))return false;
        const auto magnitude=std::int64_t(std::llround(
            std::clamp(scaled,0.,double(config.maximumImpulse))));
        if(magnitude<config.minimumImpulse)continue;
        for(int side=0;side<2;++side){
            const auto handle=side?contact.b:contact.a;
            const auto owner=bodyToNode.find(handle);
            if(owner==bodyToNode.end())continue;
            const Entity node=owner->second;
            if(!ecs.alive(node)||!ecs.has<morph::Node>(node))return false;
            const double sign=side?1.:-1.;
            const auto component=[&](float direction){
                return std::int64_t(std::llround(
                    std::clamp(double(magnitude)*sign*double(direction),
                               -double(morph::kForceLimit),
                               double(morph::kForceLimit))));
            };
            float dx=nx,dy=ny,dz=nz;
            if(localFrameOwners){
                const auto binding=localFrameOwners->find(node.packed());
                if(binding==localFrameOwners->end())return false;
                Vec3 direction{};
                if(!physics::physisVec3({nx,ny,nz},direction))return false;
                const auto local=morph::projectMorphContactDirection(
                    ecs,surface,binding->second,direction);
                if(!local)return false;
                dx=float(local->x.toDouble());dy=float(local->y.toDouble());dz=float(local->z.toDouble());
            }
            staged.push_back({
                node,handle,side?contact.a:contact.b,nextEcsTick,
                component(dx),component(dy),component(dz)
            });
        }
    }
    std::sort(staged.begin(),staged.end(),
        [](const auto& a,const auto& b){
            if(a.target.packed()!=b.target.packed())
                return a.target.packed()<b.target.packed();
            if(a.body!=b.body)return a.body<b.body;
            return a.other<b.other;
        });
    result.swap(staged);
    return true;
}

// Bounded conservative debounce for repeated contact-begins, including
// mesh replacement. Identity is the generation-safe target node + the
// opposite mapped node or still-live native handle. This is not contact
// manifold persistence: distinct impacts inside the window coalesce.
class MorphContactDebounce {
public:
    bool filter(std::vector<MorphContactInput>& inputs,
                const std::map<physics::BodyHandle,Entity>& nodes,uint32_t tick,
                uint32_t window=2){
        if(window>32||inputs.size()>8192||tick==UINT32_MAX)return false;
        auto next=history_;
        for(auto it=next.begin();it!=next.end();){
            if(it->second>tick)return false;
            if(tick-it->second>window)it=next.erase(it);else ++it;
        }
        std::vector<MorphContactInput> accepted;accepted.reserve(inputs.size());
        // Stable ordering even for several begin events on a single pair:
        // keep the strongest quantized impact, then lexicographic tie.
        std::sort(inputs.begin(),inputs.end(),[](const auto& a,const auto& b){
            const auto strength=[](const auto& x){return x.dx*x.dx+x.dy*x.dy+x.dz*x.dz;};
            if(strength(a)!=strength(b))return strength(a)>strength(b);
            return std::tuple{a.target.packed(),a.other,a.body,a.dx,a.dy,a.dz}<
                   std::tuple{b.target.packed(),b.other,b.body,b.dx,b.dy,b.dz};
        });
        for(const auto& input:inputs){
            if(input.applyTick!=tick)return false;
            const auto other=nodes.find(input.other);
            const Key key{input.target.packed(),other!=nodes.end(),
                other==nodes.end()?uint64_t(input.other):other->second.packed()};
            if(next.contains(key))continue;
            if(next.size()>=32768)return false;
            next.emplace(key,tick);accepted.push_back(input);
        }
        history_=std::move(next);inputs.swap(accepted);return true;
    }
    void reset(){history_.clear();}
private:
    using Key=std::tuple<uint64_t,bool,uint64_t>;
    std::map<Key,uint32_t> history_;
};

// Commit only on a fixed simulation tick barrier AFTER inputs are recorded.
// Validation preflight makes a rejected batch leave the ECS untouched.
// Multiple begin contacts on one node are combined by morph::enqueueImpact.
inline bool enqueueMorphContactInputs(
        World& world,std::span<const MorphContactInput> inputs,
        std::uint32_t applyTick){
    if(inputs.size()>8192||applyTick==UINT32_MAX)return false;
    for(const auto& e:inputs){
        if(e.applyTick!=applyTick||!world.alive(e.target)||
           !world.has<morph::Node>(e.target)||
           !morph::valid(morph::Impact{e.dx,e.dy,e.dz,applyTick,true}))
            return false;
        const auto* pending=static_cast<const World&>(world)
                                .tryGetConst<morph::Impact>(e.target);
        if(pending&&pending->pending&&pending->applyTick!=applyTick)
            return false;
    }
    for(const auto& input:inputs)
        if(!morph::enqueueImpact(world,input.target,applyTick,
                                 input.dx,input.dy,input.dz))
            return false;
    return true;
}

// Optional simple collider proxy for a MORPH deformable instance. The
// Box3D owner chooses when to create/destroy it; geometry deformation is
// visual and cannot modify its rigid hull without explicit re-publication.
inline physics::BodyDesc makeMorphProxyBox(
        b3Vec3 centre,b3Vec3 fullExtents,std::uint32_t userData=0){
    physics::BodyDesc body;
    body.motion=physics::Motion::Kinematic;
    body.shape=physics::ShapeKind::Box;
    body.position=centre;
    body.size=fullExtents;
    body.userData=userData;
    body.category=physics::kCategoryKinematic;
    return body;
}

} // namespace aiverse::examples::world_processes_native
