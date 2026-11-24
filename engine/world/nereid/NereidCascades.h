// NEREID: universal ECS cascade/waterfall geometry and pure CPU presentation mesh.
// Fixed-point source is genre-neutral; floating vertex generation never
// writes to authoritative water bodies, physics or network checksums.
#pragma once
#include "NereidWater.h"
#include "engine/sim/Simulation.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace aiverse::lib::nereid {

struct WaterCascadePoint {
    Vec3 position{};
    Fixed halfWidth=1_fx;
};
struct WaterCascadeComponent {
    // Each pair forms a descending ribbon; horizontal shelves are allowed.
    std::vector<WaterCascadePoint> points;
    Vec2 widthAxis{1_fx,0_fx}; // world X/Z ribbon width direction
    Fixed foam=Fixed::fromRatio(3,5); // 0..1
    Fixed speed=6_fx;                  // optical flow hint, metres/second
    bool enabled=true;
};
// Optional authoritative linkage from a cascade Entity to upstream and
// downstream water-body Entities. Rendering resolves their current surface
// levels, but never changes the ECS cascade geometry or water body.
struct WaterCascadeConnectionsComponent {
    Entity inlet=kNullEntity;
    Entity outlet=kNullEntity;
    Fixed transitionLength=2_fx;
    bool enabled=true;
};
// An optional, separately versioned join policy. Keeps existing v1
// WaterCascadeConnectionsComponent archives compatible.
struct WaterCascadeJoinPolicyComponent {
    Fixed widthBlend=1_fx; // blend authored cascade width to linked bank
    bool requireFootprint=true;
};
// The source of truth for velocities stays in WaterBody + the linked
// water cascade. This optional policy only changes WaterSample.flow near
// a cascade mouth, NOT WaterBody.flow, velocity integration or water mass.
struct WaterCascadeFlowPolicyComponent {
    Fixed radius=8_fx;                 // mouth influence X/Z metres
    Fixed blend=Fixed::fromRatio(3,4); // 0..1 local flow matching
    Fixed speedLimit=30_fx;            // max resulting outflow m/s
    bool enabled=true;
};
inline void io(Archive& a,WaterCascadeFlowPolicyComponent& p) {
    a(p.radius,p.blend,p.speedLimit,p.enabled);
}

inline void io(Archive& a,WaterCascadeJoinPolicyComponent& p) {
    a(p.widthBlend,p.requireFootprint);
}
inline void io(Archive& a,WaterCascadeConnectionsComponent& c) {
    a(c.inlet,c.outlet,c.transitionLength,c.enabled);
}
inline constexpr size_t kMaxWaterCascadePoints=128;
inline bool validWaterCascade(const WaterCascadeComponent& c) {
    if(!c.enabled || c.points.size()<2 || c.points.size()>kMaxWaterCascadePoints ||
       (c.widthAxis.x==Fixed{} && c.widthAxis.y==Fixed{}) ||
       c.foam<0_fx || c.foam>1_fx || c.speed<0_fx || c.speed>1000_fx)
        return false;
    for(size_t i=0;i<c.points.size();++i) {
        if(c.points[i].halfWidth<=0_fx || c.points[i].halfWidth>1000_fx)
            return false;
        if(i>0 && (c.points[i].position==c.points[i-1].position ||
                  c.points[i].position.y>c.points[i-1].position.y))return false;
    }
    return c.points.front().position.y>c.points.back().position.y;
}

// Strictly presentation-side vertex layout. A separate RHI upload layer
// converts this to MeshBuilder without adding any genre dependency.
struct WaterCascadeVertex {
    float x=0,y=0,z=0,nx=0,ny=0,nz=1,foam=0,u=0,v=0;
};
struct WaterCascadeMesh {
    std::vector<WaterCascadeVertex> vertices;
    std::vector<uint32_t> indices;
    bool empty()const{return indices.empty();}
};
struct WaterCascadeMeshOptions {
    double originX=0,originY=0,originZ=0;
    uint8_t stepsPerSegment=4; // 1..16
    float uvScale=0.1f;
    uint32_t maxVertices=8192;
};

inline WaterCascadeMesh buildWaterCascadeMesh(
        const WaterCascadeComponent& cascade,
        WaterCascadeMeshOptions options={}) {
    WaterCascadeMesh out;
    if(!validWaterCascade(cascade) ||
       options.stepsPerSegment<1 || options.stepsPerSegment>16 ||
       options.maxVertices<8 || options.maxVertices>65536 ||
       !std::isfinite(options.uvScale) || options.uvScale<=0 ||
       !std::isfinite(options.originX) ||
       !std::isfinite(options.originY) ||
       !std::isfinite(options.originZ))return out;
    const double ax=cascade.widthAxis.x.toDouble();
    const double az=cascade.widthAxis.y.toDouble();
    const double axisLength=std::hypot(ax,az);
    if(!std::isfinite(axisLength) || axisLength<1e-9)return out;
    const double rx=ax/axisLength,rz=az/axisLength;
    const size_t subdivisions=size_t(options.stepsPerSegment);
    const size_t faces=(cascade.points.size()-1)*subdivisions;
    if(faces>options.maxVertices/8)return out;
    out.vertices.reserve(faces*8);
    out.indices.reserve(faces*12);
    double along=0.0;
    for(size_t i=1;i<cascade.points.size();++i) {
        const auto& a=cascade.points[i-1];
        const auto& b=cascade.points[i];
        const double dx=b.position.x.toDouble()-a.position.x.toDouble();
        const double dy=b.position.y.toDouble()-a.position.y.toDouble();
        const double dz=b.position.z.toDouble()-a.position.z.toDouble();
        const double segmentLen=std::sqrt(dx*dx+dy*dy+dz*dz);
        if(!std::isfinite(segmentLen) || segmentLen<1e-8)return {};
        // Per-segment face normal; duplicate back-facing vertices so the
        // sheet is visible from either side under backface culling.
        const double nx=-rz*dy,ny=rz*dx-rx*dz,nz=rx*dy;
        const double nl=std::sqrt(nx*nx+ny*ny+nz*nz);
        if(nl<1e-9 || !std::isfinite(nl))return {};
        for(size_t j=0;j<subdivisions;++j) {
            const double t0=double(j)/subdivisions,t1=double(j+1)/subdivisions;
            auto interpolate=[&](double t,bool right) {
                const double width=(a.halfWidth.toDouble()+
                    (b.halfWidth.toDouble()-a.halfWidth.toDouble())*t);
                const double side=right?1.0:-1.0;
                return WaterCascadeVertex{
                    float(a.position.x.toDouble()+dx*t+rx*width*side-options.originX),
                    float(a.position.y.toDouble()+dy*t-options.originY),
                    float(a.position.z.toDouble()+dz*t+rz*width*side-options.originZ),
                    float(nx/nl),float(ny/nl),float(nz/nl),0.f,
                    right?1.f:0.f,
                    float((along+segmentLen*t)*options.uvScale)};
            };
            auto l0=interpolate(t0,false),r0=interpolate(t0,true);
            auto l1=interpolate(t1,false),r1=interpolate(t1,true);
            // Foam rises around kinks, edges and near the falling foot.
            const float edgeFoam=float(cascade.foam.toDouble());
            const float toe=std::clamp(float((double(i-1)+t1)/
                           double(cascade.points.size()-1)),0.f,1.f);
            const float foam=std::clamp(edgeFoam*(0.2f+0.8f*toe*toe),0.f,1.f);
            l0.foam=r0.foam=l1.foam=r1.foam=foam;
            const uint32_t base=uint32_t(out.vertices.size());
            out.vertices.push_back(l0);out.vertices.push_back(r0);
            out.vertices.push_back(l1);out.vertices.push_back(r1);
            // Front/back sides of the same physical sheet.
            auto back0=l0,back1=r0,back2=l1,back3=r1;
            for(auto* v:{&back0,&back1,&back2,&back3}) {
                v->nx=-v->nx;v->ny=-v->ny;v->nz=-v->nz;
            }
            out.vertices.push_back(back0);out.vertices.push_back(back1);
            out.vertices.push_back(back2);out.vertices.push_back(back3);
            out.indices.insert(out.indices.end(),{
                base,base+1,base+2,base+1,base+3,base+2,
                base+4,base+6,base+5,base+5,base+6,base+7});
        }
        along+=segmentLen;
    }
    return out;
}
inline void io(Archive& a,WaterCascadePoint& p) {
    a(p.position,p.halfWidth);
}
inline void io(Archive& a,WaterCascadeComponent& c) {
    a(c.widthAxis.x,c.widthAxis.y,c.foam,c.speed,c.enabled);
    const uint32_t count=a.count(c.points.size());
    if(count>kMaxWaterCascadePoints){a.fail();return;}
    if(!a.writing())c.points.assign(count,{});
    for(uint32_t i=0;i<count && a.ok();++i)a(c.points[i]);
}
inline void registerNereidCascadePersistence(Simulation& sim) {
    sim.registerPersistentComponent<WaterCascadeComponent>("nereid.cascade",1);
    sim.registerPersistentComponent<WaterCascadeConnectionsComponent>(
        "nereid.cascade_connections",1);
    sim.registerPersistentComponent<WaterCascadeJoinPolicyComponent>(
        "nereid.cascade_join_policy",1);
    sim.registerPersistentComponent<WaterCascadeFlowPolicyComponent>(
        "nereid.cascade_flow_policy",1);
}
} // namespace aiverse::lib::nereid
