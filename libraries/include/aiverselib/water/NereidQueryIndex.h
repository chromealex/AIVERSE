// AIVERSE NEREID stage 4 -- transient, deterministic X/Z water broadphase.
// Construct per read phase / simulation tick. Never persist this derived index.
// This header depends on the generic NEREID ECS policy, not on any game genre.
#pragma once
#include <aiverselib/water/NereidWaterSystems.h>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::lib::nereid {

struct WaterBoundsXZ {
    Fixed minX{},maxX{},minZ{},maxZ{};
    bool contains(Vec3 position) const {
        return position.x>=minX && position.x<=maxX &&
               position.z>=minZ && position.z<=maxZ;
    }
};

// Signed saturation avoids undefined overflow while building an AABB for
// extremely large footprints. All math stays deterministic integer/Fixed.
namespace water_index_detail {
inline Fixed plusPositive(Fixed value,Fixed positive) {
    if(value.raw>std::numeric_limits<int64_t>::max()-positive.raw)
        return Fixed::fromRaw(std::numeric_limits<int64_t>::max());
    return Fixed::fromRaw(value.raw+positive.raw);
}
inline Fixed minusPositive(Fixed value,Fixed positive) {
    if(value.raw<std::numeric_limits<int64_t>::min()+positive.raw)
        return Fixed::fromRaw(std::numeric_limits<int64_t>::min());
    return Fixed::fromRaw(value.raw-positive.raw);
}
inline WaterBoundsXZ combine(WaterBoundsXZ a,WaterBoundsXZ b) {
    return {fxMin(a.minX,b.minX),fxMax(a.maxX,b.maxX),
            fxMin(a.minZ,b.minZ),fxMax(a.maxZ,b.maxZ)};
}
inline WaterBoundsXZ boundsOf(const WaterBodyComponent& body,
                              const WaterGeometryComponent* geometry) {
    if(geometry && geometry->enabled) {
        // Caller validates geometry; these finite AABBs include polygon
        // edges and variable-width spline endcaps. Fixed arithmetic in the
        // exact policy may round by a few raw units; widen conservatively.
        const Fixed margin=Fixed::fromRaw(8);
        bool first=true;
        WaterBoundsXZ bounds{};
        for(const auto& point:geometry->points) {
            const Fixed width=geometry->kind==WaterGeometryKind::RiverSpline
                ?point.halfWidth:Fixed{};
            WaterBoundsXZ p{
                minusPositive(point.x,width),plusPositive(point.x,width),
                minusPositive(point.z,width),plusPositive(point.z,width)};
            if(first){bounds=p;first=false;}
            else bounds=combine(bounds,p);
        }
        bounds.minX=minusPositive(bounds.minX,margin);
        bounds.minZ=minusPositive(bounds.minZ,margin);
        bounds.maxX=plusPositive(bounds.maxX,margin);
        bounds.maxZ=plusPositive(bounds.maxZ,margin);
        return bounds;
    }
    const Fixed hx=body.footprint==WaterFootprint::Box
        ?body.halfExtents.x:body.radius;
    const Fixed hz=body.footprint==WaterFootprint::Box
        ?body.halfExtents.y:body.radius;
    return {minusPositive(body.center.x,hx),plusPositive(body.center.x,hx),
            minusPositive(body.center.z,hz),plusPositive(body.center.z,hz)};
}
} // namespace water_index_detail

// Immutable *during its read phase*, not a second ECS water source of truth.
// A balanced AABB hierarchy rejects far-away rivers/lakes before the costly
// exact polygon or polyline test. Infinite oceans are evaluated separately.
// Building O(B log^2 B) (stable total-order partitions), querying typically
// O(log B + nearby B), worst case O(B) for overlapping huge waters.
class WaterQueryIndex {
public:
    explicit WaterQueryIndex(const World& world):world_(world) {
        world_.eachExistingConst<WaterBodyComponent>(
            [&](Entity entity,const WaterBodyComponent& body) {
                if(!world_.alive(entity) || !body.enabled ||
                   body.surfaceY<=body.bottomY)return;
                const auto* geometry=world_.tryGetConst<WaterGeometryComponent>(entity);
                if(geometry && geometry->enabled) {
                    if(!validWaterGeometry(*geometry))return;
                }else if(!validWaterBody(body))return;
                if((!geometry || !geometry->enabled) &&
                   body.footprint==WaterFootprint::Infinite) {
                    infinite_.push_back(entity);
                    return;
                }
                finite_.push_back({entity,water_index_detail::boundsOf(body,geometry)});
            });
        nodes_.reserve(finite_.size()*2);
        if(!finite_.empty())build(0,int(finite_.size()),0);
    }

    WaterSample sample(Vec3 position) const {
        WaterSample out;
        for(Entity entity:infinite_)consider(entity,position,out);
        if(!nodes_.empty())visit(0,position,out);
        applyWaterConfluenceFlow(world_,position,out);
        applyWaterCascadeFlow(world_,position,out);
        applyNereidFlowObstructions(world_,position,out);
        return applyNereidFloodSample(
            world_,world_.resourceConst<WaterFloodResource>(),position,out);
    }
    size_t finiteBodyCount() const {return finite_.size();}
    size_t infiniteBodyCount() const {return infinite_.size();}

private:
    struct BodyRef { Entity entity;WaterBoundsXZ bounds; };
    struct Node {
        WaterBoundsXZ bounds{};
        int left=-1,right=-1;
        int first=0,count=0;
    };
    const World& world_;
    std::vector<BodyRef> finite_;
    std::vector<Entity> infinite_;
    std::vector<Node> nodes_;

    int build(int begin,int end,int depth) {
        const int node=int(nodes_.size());
        nodes_.emplace_back();
        if(end-begin<=4) {
            auto bounds=finite_[size_t(begin)].bounds;
            for(int i=begin+1;i<end;++i)
                bounds=water_index_detail::combine(bounds,finite_[size_t(i)].bounds);
            nodes_[size_t(node)].bounds=bounds;
            nodes_[size_t(node)].first=begin;
            nodes_[size_t(node)].count=end-begin;
            return node;
        }
        const bool xAxis=(depth%2)==0;
        const int mid=begin+(end-begin)/2;
        // Tie break by the full generation-safe Entity ID: construction
        // is independent of source pool storage order and platform.
        auto less=[xAxis](const BodyRef& lhs,const BodyRef& rhs) {
            const Fixed a=xAxis?lhs.bounds.minX:lhs.bounds.minZ;
            const Fixed b=xAxis?rhs.bounds.minX:rhs.bounds.minZ;
            if(a!=b)return a<b;
            return lhs.entity.packed()<rhs.entity.packed();
        };
        std::nth_element(finite_.begin()+begin,finite_.begin()+mid,
                         finite_.begin()+end,less);
        const int left=build(begin,mid,depth+1);
        const int right=build(mid,end,depth+1);
        nodes_[size_t(node)].left=left;
        nodes_[size_t(node)].right=right;
        nodes_[size_t(node)].bounds=water_index_detail::combine(
            nodes_[size_t(left)].bounds,nodes_[size_t(right)].bounds);
        return node;
    }

    void consider(Entity entity,Vec3 position,WaterSample& out) const {
        if(!world_.alive(entity))return;
        const auto* body=world_.tryGetConst<WaterBodyComponent>(entity);
        if(!body)return;
        const auto* geometry=world_.tryGetConst<WaterGeometryComponent>(entity);
        if(!coversWaterBody(*body,geometry,position))return;
        const auto* field=world_.tryGetConst<WaterCurrentFieldComponent>(entity);
        const auto* vortices=world_.tryGetConst<WaterVortexFieldComponent>(entity);
        considerWaterCandidate(out,entity,*body,position,geometry,field,vortices);
    }
    void visit(int index,Vec3 position,WaterSample& out) const {
        const auto& node=nodes_[size_t(index)];
        if(!node.bounds.contains(position))return;
        if(node.count>0) {
            for(int i=node.first;i<node.first+node.count;++i) {
                const auto& body=finite_[size_t(i)];
                if(body.bounds.contains(position))
                    consider(body.entity,position,out);
            }
        }else {
            visit(node.left,position,out);
            visit(node.right,position,out);
        }
    }
};

// Alternative to addWaterSamplingSystem; register exactly one sampling stage.
// Rebuild from authoritative ECS bodies every tick, so no stale pointers or
// cache invalidation after rollback, movement, edits or deserialization.
// Updates only WaterContact; never touches source water bodies or the renderer.
inline void addIndexedWaterSamplingSystem(Simulation& sim,std::string name) {
    sim.addSystem(std::move(name),[](Simulation& current) {
        World& world=current.world();
        if(!world.countExisting<WaterContactComponent>())return;
        auto* contacts=&world.pool<WaterContactComponent>();
        const World& read=world;
        const WaterQueryIndex index(read);
        for(size_t i=0;i<contacts->size();++i) {
            const Entity entity=contacts->entityAt(i);
            if(!read.alive(entity))continue;
            const auto* probe=read.tryGetConst<WaterProbeComponent>(entity);
            auto& contact=contacts->dataAt(i);
            contact.sample=probe?index.sample(probe->position):WaterSample{};
            contact.sampledTick=current.tick();
        }
    });
}

} // namespace aiverse::lib::nereid
