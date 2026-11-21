// NEREID — bounded presentation-only expanding ripple rings.
// Consumes deterministic ECS interaction snapshots, but owns no simulation
// state, GPU buffer, or genre-specific VFX implementation.
#pragma once
#include "engine/render/Renderer3D.h"
#include "engine/world/NereidWindPresentation.h"
#include <aiverselib/water/NereidInteractionPresentation.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace aiverse::world {

struct NereidRipple {
    uint64_t waterId=0;      // packed generation-safe entity from WaterInteractionOutput
    uint64_t sourceId=0;
    lib::nereid::WaterInteractionKind kind=lib::nereid::WaterInteractionKind::None;
    double x=0,y=0,z=0;
    float age=0, lifetime=3.f, speed=3.f;
    float initialRadius=1, amplitude=1, width=0.2f;
    float directionX=0,directionZ=0; // normalized movement for directional wakes
};

struct NereidRippleShaderPacket {
    std::array<render::float4,16> rings{};
    std::array<render::float4,16> shapes{};
};

// Draws camera-origin-relative geometry using the existing overlay triangle
// queue. Not a physical displacement or a readback from deterministic water.
class NereidRippleField {
public:
    static constexpr size_t kMaxRipples=256;
    void clear() {ripples_.clear();lastTick_=0;hasTick_=false;}
    void setLimit(size_t max) {limit_=std::clamp(max,size_t(1),kMaxRipples);
        if(ripples_.size()>limit_)
            ripples_.erase(ripples_.begin(),ripples_.begin()+std::ptrdiff_t(ripples_.size()-limit_));
    }
    size_t size()const{return ripples_.size();}
    const std::vector<NereidRipple>& ripples()const{return ripples_;}
    // Select at most sixteen nearest ripple rings for one water body.
    // A body snapshot uses Entity::packed()+1 as its ID; events contain
    // Entity::packed() directly. Render coordinates match WorldScene origin.
    NereidRippleShaderPacket shaderPacket(
            uint64_t waterId,double originX,double originZ,
            double targetX,double targetZ) const {
        NereidRippleShaderPacket result{};
        struct Candidate {double distanceSquared=0;size_t index=0;};
        std::vector<Candidate> candidates;
        candidates.reserve(ripples_.size());
        for(size_t i=0;i<ripples_.size();++i) {
            const auto& r=ripples_[i];
            if(waterId==0 || r.waterId==UINT64_MAX ||
               waterId!=r.waterId+1)continue;
            const double dx=r.x-targetX,dz=r.z-targetZ;
            const double d2=dx*dx+dz*dz;
            // Do not clip by distance from the mesh origin: a polygon or
            // a wide lake can span kilometres. The fragment shader gates
            // each ring spatially against the actual pixel position.
            candidates.push_back({d2,i});
        }
        std::sort(candidates.begin(),candidates.end(),[](auto a,auto b) {
            if(a.distanceSquared==b.distanceSquared)return a.index<b.index;
            return a.distanceSquared<b.distanceSquared;
        });
        for(size_t i=0;i<std::min(size_t(16),candidates.size());++i) {
            const auto& r=ripples_[candidates[i].index];
            const float fade=std::clamp(1.f-r.age/r.lifetime,0.f,1.f);
            const float radius=r.initialRadius+r.speed*r.age;
            result.rings[i]={float(r.x-originX),float(r.z-originZ),radius,
                              std::min(r.amplitude*fade*0.12f,2.f)};
            if(r.kind==lib::nereid::WaterInteractionKind::SurfaceWake &&
               (r.directionX!=0 || r.directionZ!=0))
                result.shapes[i]={r.directionX,r.directionZ,float(kWakeTransverse),1.f};
        }
        return result;
    }
    std::array<render::float4,16> shaderRipples(
            uint64_t waterId,double originX,double originZ,
            double targetX,double targetZ) const {
        return shaderPacket(waterId,originX,originZ,targetX,targetZ).rings;
    }
    // Localized ocean/box tile selection. Excludes impulses whose current
    // annular wavefront cannot touch this tile; scores overlapping waves by
    // proximity to the tile centre. Work per tile is bounded by 256 pulses.
    NereidRippleShaderPacket shaderPacketForTile(
            uint64_t waterId,double originX,double originZ,
            double minX,double maxX,double minZ,double maxZ) const {
        NereidRippleShaderPacket result{};
        if(waterId==0 || !(minX<=maxX && minZ<=maxZ))return result;
        struct Candidate {double distance=0;size_t index=0;};
        std::vector<Candidate> candidates;
        candidates.reserve(ripples_.size());
        const double centerX=(minX+maxX)*0.5,centerZ=(minZ+maxZ)*0.5;
        for(size_t i=0;i<ripples_.size();++i) {
            const auto& r=ripples_[i];
            if(r.waterId==UINT64_MAX || waterId!=r.waterId+1)continue;
            const double dx=std::max({minX-r.x,0.0,r.x-maxX});
            const double dz=std::max({minZ-r.z,0.0,r.z-maxZ});
            const double nearest=std::hypot(dx,dz);
            const double farthest=std::hypot(
                std::max(std::abs(minX-r.x),std::abs(maxX-r.x)),
                std::max(std::abs(minZ-r.z),std::abs(maxZ-r.z)));
            const double ring=double(r.initialRadius)+double(r.speed)*double(r.age);
            constexpr double band=2.5; // Gaussian support in metres
            // A directional SurfaceWake is an ellipse in the water shader:
            // |(along*kWakeAlong, across*kWakeTransverse)| = ring, so its
            // wavefront reaches ring/kWakeAlong along the heading and only
            // ring/kWakeTransverse sideways. Cull against those bounds.
            double outer=ring+band,inner=ring-band;
            if(isDirectionalWake(r)){
                outer=(ring+band)/kWakeAlong;
                inner=(ring-band)/kWakeTransverse;
            }
            if(nearest>outer || farthest<inner)continue;
            const double dist=std::abs(std::hypot(r.x-centerX,r.z-centerZ)-ring);
            candidates.push_back({dist,i});
        }
        std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b) {
            return a.distance==b.distance?a.index<b.index:a.distance<b.distance;
        });
        for(size_t i=0;i<std::min(size_t(16),candidates.size());++i) {
            const auto& r=ripples_[candidates[i].index];
            const float fade=std::clamp(1.f-r.age/r.lifetime,0.f,1.f);
            result.rings[i]={float(r.x-originX),float(r.z-originZ),
                             r.initialRadius+r.speed*r.age,
                             std::min(r.amplitude*fade*0.12f,2.f)};
            if(r.kind==lib::nereid::WaterInteractionKind::SurfaceWake &&
               (r.directionX!=0 || r.directionZ!=0))
                result.shapes[i]={r.directionX,r.directionZ,float(kWakeTransverse),1.f};
        }
        return result;
    }
    std::array<render::float4,16> shaderRipplesForTile(
            uint64_t waterId,double originX,double originZ,
            double minX,double maxX,double minZ,double maxZ) const {
        return shaderPacketForTile(waterId,originX,originZ,
                                   minX,maxX,minZ,maxZ).rings;
    }
    // Supplying the same simulated tick twice must not duplicate VFX events.
    // Call in ascending tick order; a rollback starts a new visual timeline.
    template<class EventCallback>
    void ingest(const std::vector<lib::nereid::WaterPresentationEvent>& events,
                uint32_t completedTick,EventCallback&& onEvent) {
        if(hasTick_ && completedTick==lastTick_)return;
        if(hasTick_ && completedTick<lastTick_)ripples_.clear();
        lastTick_=completedTick;hasTick_=true;
        for(const auto& item:events) {
            const auto& e=item.event;
            if(e.tick!=completedTick || e.kind==lib::nereid::WaterInteractionKind::None)
                continue;
            // Callback is a host SpectraFX/audio hook; its implementation
            // must not modify authoritative ECS while the read phase runs.
            onEvent(item);
            if(e.kind!=lib::nereid::WaterInteractionKind::Enter &&
               e.kind!=lib::nereid::WaterInteractionKind::Exit &&
               e.kind!=lib::nereid::WaterInteractionKind::SurfaceWake &&
               e.kind!=lib::nereid::WaterInteractionKind::SwitchBody)
                continue;
            NereidRipple r;
            r.waterId=uint64_t(e.water.packed());
            r.sourceId=uint64_t(item.source.packed());
            r.kind=e.kind;
            r.x=e.position.x.toDouble();
            r.y=e.position.y.toDouble();
            r.z=e.position.z.toDouble();
            r.initialRadius=std::clamp(float(e.radius.toDouble()),0.05f,100.f);
            r.amplitude=std::clamp(float(e.strength.toDouble()),0.f,16.f);
            if(e.kind==lib::nereid::WaterInteractionKind::SurfaceWake) {
                r.directionX=float(item.movementDirection.x.toDouble());
                r.directionZ=float(item.movementDirection.y.toDouble());
            }
            if(e.kind==lib::nereid::WaterInteractionKind::SurfaceWake) {
                r.lifetime=2.2f;r.speed=2.5f;r.width=0.11f;
            }else{
                r.lifetime=3.5f;r.speed=4.5f;r.width=0.22f;
            }
            if(ripples_.size()>=limit_)ripples_.erase(ripples_.begin());
            ripples_.push_back(r);
        }
    }
    void ingest(const std::vector<lib::nereid::WaterPresentationEvent>& events,
                uint32_t completedTick) {
        ingest(events,completedTick,[](const auto&){});
    }
    // Independent render-time ageing: never feeds a simulation checksum.
    template<class ResolveCurrent>
    void advance(float deltaSeconds,ResolveCurrent&& current) {
        if(!std::isfinite(deltaSeconds) || deltaSeconds<=0)return;
        const double dt=double(std::min(deltaSeconds,0.25f));
        for(auto& r:ripples_) {
            r.age+=float(dt);
            const Vec3 flow=current(r.waterId,r.x,r.y,r.z);
            // Use flow from the copied ECS water snapshot only; never
            // integrate physical actor velocity or modify simulation.
            r.x+=std::clamp(flow.x.toDouble(),-100.0,100.0)*dt;
            r.z+=std::clamp(flow.z.toDouble(),-100.0,100.0)*dt;
        }
        ripples_.erase(std::remove_if(ripples_.begin(),ripples_.end(),
            [](const NereidRipple& r){return r.age>=r.lifetime;}),ripples_.end());
    }
    void advance(float deltaSeconds) {
        advance(deltaSeconds,[](uint64_t,double,double,double){return Vec3{};});
    }
    // Environmental wind drift is visual only and independent from
    // WaterBody.flow (which remains authoritative for gameplay forces).
    template<class SampleWind>
    void applyVisualWind(float deltaSeconds,SampleWind&& sample) {
        if(!std::isfinite(deltaSeconds) || deltaSeconds<=0)return;
        const double dt=std::min(double(deltaSeconds),0.25);
        for(auto& r:ripples_) {
            const auto wind=sample(r.x,r.z);
            if(!std::isfinite(wind.east) || !std::isfinite(wind.north) ||
               !std::isfinite(wind.gust) || !std::isfinite(wind.coupling))
                continue;
            const double weight=0.07*
                std::clamp(double(wind.coupling),0.0,1.0)*
                (0.35+0.65*std::clamp(double(wind.gust),0.0,1.0));
            r.x+=std::clamp(double(wind.east),-100.0,100.0)*dt*weight;
            r.z+=std::clamp(double(wind.north),-100.0,100.0)*dt*weight;
        }
    }
    void applyVisualWind(float deltaSeconds,float east,float north,
                         float gust,float coupling) {
        applyVisualWind(deltaSeconds,[=](double,double) {
            return NereidVisualWind{east,north,gust,coupling};
        });
    }
    // Alpha-blended annular quads, generated only for currently live ripples.
    // World origin uses double subtraction before conversion to float,
    // avoiding far-world precision loss. Y is lifted slightly to avoid z-fight.
    void draw(render::Renderer3D& renderer,double originX,double originY,
              double originZ)const {
        constexpr int segments=24;
        constexpr float tau=6.28318530717958647692f;
        for(const auto& r:ripples_) {
            const float fade=std::clamp(1.f-r.age/r.lifetime,0.f,1.f);
            const uint8_t alpha=uint8_t(std::clamp(r.amplitude*fade*42.f,0.f,160.f));
            if(alpha==0)continue;
            const float radius=r.initialRadius+r.speed*r.age;
            const float halfWidth=r.width*(1.f+r.age*0.2f);
            const float inner=std::max(0.f,radius-halfWidth);
            const float outer=radius+halfWidth;
            const float x=float(r.x-originX),y=float(r.y-originY)+0.035f,
                        z=float(r.z-originZ);
            const uint32_t color=render::rgba(190,220,230,alpha);
            for(int j=0;j<segments;++j) {
                const float t0=tau*float(j)/segments;
                const float t1=tau*float(j+1)/segments;
                const render::float3 a{x+std::cos(t0)*inner,y,z+std::sin(t0)*inner};
                const render::float3 b{x+std::cos(t1)*inner,y,z+std::sin(t1)*inner};
                const render::float3 c{x+std::cos(t1)*outer,y,z+std::sin(t1)*outer};
                const render::float3 d{x+std::cos(t0)*outer,y,z+std::sin(t0)*outer};
                renderer.triangle(a,b,c,color);
                renderer.triangle(a,c,d,color);
            }
        }
    }
private:
    // Must match the SurfaceWake ellipse in engine/render/Shaders.h (GLSL
    // and MSL: `along*0.75`, `across*max(shape.z,1.0)`) and shapes[i].z.
    static constexpr double kWakeAlong=0.75;
    static constexpr double kWakeTransverse=1.35;
    static bool isDirectionalWake(const NereidRipple& r){
        return r.kind==lib::nereid::WaterInteractionKind::SurfaceWake &&
               (r.directionX!=0 || r.directionZ!=0);
    }
    std::vector<NereidRipple> ripples_;
    size_t limit_=kMaxRipples;
    uint32_t lastTick_=0;
    bool hasTick_=false;
};

} // namespace aiverse::world
