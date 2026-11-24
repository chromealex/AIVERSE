// NEREID waterfalls and stepped cascades, presentation-only GPU residency.
// ECS owns WaterCascadeComponent; this view owns uploaded meshes. No FPS/RTS
// coupling, no independent fluid simulation and no new RHI backend.
#pragma once
#include "engine/render/NereidWaterRenderer.h"
#include "engine/world/WorldAssets.h"
#include "engine/world/NereidWaterScene.h"
#include "engine/world/nereid/NereidCascades.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>
#include <utility>

namespace aiverse::world {

struct NereidCascadeSource {
    uint64_t id=0; // stable positive ID; 0 gets a legacy index ID
    uint64_t inletWaterId=0,outletWaterId=0; // optional stable NEREID IDs
    float joinLength=2.f; // X/Z overlap extension in metres (0.1..50)
    float joinWidthBlend=1.f; // 0..1 cascade width -> linked bank width
    bool requireFootprint=true; // reject joins outside source water geometry
    lib::nereid::WaterCascadeFlowPolicyComponent flowPolicy{};
    bool useFlowPolicy=false; // visual-only transfer follows ECS opt-in
    lib::nereid::WaterCascadeComponent cascade;
    lib::nereid::WaterSurfaceStyle style{};
    lib::nereid::WaterCascadeMeshOptions options{};
};

inline bool sameNereidCascadeSource(const NereidCascadeSource& a,
                                   const NereidCascadeSource& b) {
    const auto& x=a.cascade;const auto& y=b.cascade;
    const auto& s=a.style;const auto& t=b.style;
    if(a.inletWaterId!=b.inletWaterId ||
       a.outletWaterId!=b.outletWaterId ||
       a.joinLength!=b.joinLength ||
       a.joinWidthBlend!=b.joinWidthBlend ||
       a.requireFootprint!=b.requireFootprint ||
       x.enabled!=y.enabled || x.widthAxis!=y.widthAxis ||
       x.foam!=y.foam || x.speed!=y.speed ||
       x.points.size()!=y.points.size() ||
       a.options.uvScale!=b.options.uvScale ||
       a.options.stepsPerSegment!=b.options.stepsPerSegment ||
       a.options.maxVertices!=b.options.maxVertices ||
       s.deepR!=t.deepR || s.deepG!=t.deepG || s.deepB!=t.deepB ||
       s.foamR!=t.foamR || s.foamG!=t.foamG || s.foamB!=t.foamB ||
       s.foamStrength!=t.foamStrength || s.opacity!=t.opacity ||
       s.specular!=t.specular || s.refractionStrength!=t.refractionStrength ||
       s.absorption!=t.absorption || s.shoreFoamDepth!=t.shoreFoamDepth ||
       s.depthTint!=t.depthTint || s.underwaterDistortion!=t.underwaterDistortion ||
       s.underwaterScattering!=t.underwaterScattering ||
       s.turbidity!=t.turbidity || s.siltR!=t.siltR || s.siltG!=t.siltG || s.siltB!=t.siltB)return false;
    for(size_t i=0;i<x.points.size();++i)
        if(x.points[i].position!=y.points[i].position ||
           x.points[i].halfWidth!=y.points[i].halfWidth)return false;
    return true;
}

// Build transition geometry ONLY on a presentation-owned copy. When a
// linked water body exists, extend the top or foot ribbon into its horizontal
// surface at the body's current elevation. No changes to ECS source points.
inline lib::nereid::WaterCascadeComponent connectedNereidCascade(
        const NereidCascadeSource& source,
        std::optional<Fixed> inletLevel,
        std::optional<Fixed> outletLevel) {
    auto result=source.cascade;
    if(result.points.size()<2 || result.points.size()>126 ||
       !std::isfinite(source.joinLength) ||
       source.joinLength<=0 || source.joinLength>50.f)
        return result;
    const auto dir=[&](size_t i,size_t j) {
        const auto& a=result.points[i].position;
        const auto& b=result.points[j].position;
        Vec2 d{b.x-a.x,b.z-a.z};
        const Fixed len=d.length();
        if(len>Fixed{})return d/len;
        // Purely vertical drop: pick the forward direction perpendicular
        // to the cascade's authored horizontal ribbon-width axis.
        d={-result.widthAxis.y,result.widthAxis.x};
        const Fixed fallback=d.length();
        return fallback>Fixed{} ? d/fallback : Vec2{1_fx,0_fx};
    };
    const Fixed extension=Fixed::fromRaw(
        int64_t(std::llround(double(source.joinLength)*Fixed::kOne)));
    // Compute directions before changing the source-point indices.
    const Vec2 firstDir=dir(0,1);
    const Vec2 lastDir=dir(result.points.size()-2,result.points.size()-1);
    if(source.inletWaterId && inletLevel &&
       *inletLevel>=result.points.front().position.y) {
        auto first=result.points.front();
        first.position.x-=firstDir.x*extension;
        first.position.z-=firstDir.y*extension;
        first.position.y=*inletLevel;
        result.points.insert(result.points.begin(),first);
    }
    if(source.outletWaterId && outletLevel &&
       *outletLevel<=result.points.back().position.y) {
        auto last=result.points.back();
        last.position.x+=lastDir.x*extension;
        last.position.z+=lastDir.y*extension;
        last.position.y=*outletLevel;
        result.points.push_back(last);
    }
    return result;
}

// Presentation-only shoreline-aware extension policy. The outer join
// end must lie within the linked water footprint, and both edges are
// clipped inward to stay inside the water. Rivers also interpolate their
// authored bank half-width along the closest spline segment.
inline double nereidLinkedRiverHalfWidth(const NereidSceneSurface& water,
                                         Vec3 sample,double fallback) {
    if(!water.useGeometry || !water.geometry.enabled ||
       water.geometry.kind!=lib::nereid::WaterGeometryKind::RiverSpline ||
       !lib::nereid::validWaterGeometry(water.geometry))return fallback;
    const auto& points=water.geometry.points;
    double best=std::numeric_limits<double>::infinity();
    double halfWidth=fallback;
    const double x=sample.x.toDouble(),z=sample.z.toDouble();
    for(size_t i=1;i<points.size();++i) {
        const double ax=points[i-1].x.toDouble(),az=points[i-1].z.toDouble();
        const double dx=points[i].x.toDouble()-ax;
        const double dz=points[i].z.toDouble()-az;
        const double lenSq=dx*dx+dz*dz;
        if(lenSq<=1e-12)continue;
        const double t=std::clamp(((x-ax)*dx+(z-az)*dz)/lenSq,0.0,1.0);
        const double ddx=x-(ax+dx*t),ddz=z-(az+dz*t);
        const double dist=ddx*ddx+ddz*ddz;
        if(dist<best) {
            best=dist;
            halfWidth=points[i-1].halfWidth.toDouble()*(1.0-t)+
                      points[i].halfWidth.toDouble()*t;
        }
    }
    return halfWidth;
}
inline lib::nereid::WaterCascadeComponent connectedNereidCascade(
        const NereidCascadeSource& source,
        const NereidSceneSurface* inlet,
        const NereidSceneSurface* outlet) {
    auto result=source.cascade;
    if(result.points.size()<2 || result.points.size()>126 ||
       !std::isfinite(source.joinLength) ||
       source.joinLength<0.1f || source.joinLength>50.f)
        return result;
    const auto direction=[&](size_t a,size_t b) {
        const Vec3 from=result.points[a].position;
        const Vec3 to=result.points[b].position;
        Vec2 d{to.x-from.x,to.z-from.z};
        if(d.length()<=Fixed{})d={
            -result.widthAxis.y,result.widthAxis.x};
        const Fixed len=d.length();
        return len>Fixed{}?d/len:Vec2{1_fx,0_fx};
    };
    const Vec2 startDir=direction(0,1);
    const Vec2 endDir=direction(result.points.size()-2,result.points.size()-1);
    const Vec2 widthAxis=result.widthAxis/result.widthAxis.length();
    const Fixed extension=Fixed::fromRaw(int64_t(std::llround(
        double(source.joinLength)*Fixed::kOne)));
    const auto fitJoin=[&](const NereidSceneSurface& water,
                           lib::nereid::WaterCascadePoint p) -> std::optional<
                           lib::nereid::WaterCascadePoint> {
        const auto* geometry=water.useGeometry?&water.geometry:nullptr;
        p.position.y=water.body.surfaceY;
        const auto covers=[&](Vec3 position) {
            return lib::nereid::coversWaterBody(water.body,geometry,position);
        };
        if(source.requireFootprint && !covers(p.position))
            return std::nullopt;
        const double authored=p.halfWidth.toDouble();
        const double desired=nereidLinkedRiverHalfWidth(water,p.position,authored);
        const double blended=authored+(desired-authored)*
                             double(source.joinWidthBlend);
        const double width=std::clamp(blended,0.10,1000.0);
        auto asFixed=[](double v) {
            return Fixed::fromRaw(int64_t(std::llround(v*Fixed::kOne)));
        };
        if(!source.requireFootprint) {
            p.halfWidth=asFixed(width);
            return p;
        }
        const auto fits=[&](Fixed half) {
            const Vec3 left{
                p.position.x-widthAxis.x*half,
                p.position.y,
                p.position.z-widthAxis.y*half};
            const Vec3 right{
                p.position.x+widthAxis.x*half,
                p.position.y,
                p.position.z+widthAxis.y*half};
            return covers(left)&&covers(right);
        };
        Fixed upper=asFixed(width),lower=Fixed{};
        if(!fits(upper)) {
            // Fixed-point binary search; connected strip cannot protrude
            // outside the bank at the exterior join cross-section.
            for(int i=0;i<18;++i) {
                const Fixed mid=Fixed::fromRaw(lower.raw+
                    (upper.raw-lower.raw)/2);
                if(fits(mid))lower=mid;else upper=mid;
            }
            upper=lower;
        }
        if(upper<Fixed::fromRatio(1,10))return std::nullopt;
        p.halfWidth=upper;
        return p;
    };
    if(source.inletWaterId && inlet && inlet->body.enabled &&
       inlet->body.surfaceY>=result.points.front().position.y) {
        auto candidate=result.points.front();
        candidate.position.x-=startDir.x*extension;
        candidate.position.z-=startDir.y*extension;
        if(const auto matched=fitJoin(*inlet,candidate))
            result.points.insert(result.points.begin(),*matched);
    }
    if(source.outletWaterId && outlet && outlet->body.enabled &&
       outlet->body.surfaceY<=result.points.back().position.y) {
        auto candidate=result.points.back();
        candidate.position.x+=endDir.x*extension;
        candidate.position.z+=endDir.y*extension;
        if(const auto matched=fitJoin(*outlet,candidate))
            result.points.push_back(*matched);
    }
    return result;
}

inline render::Mesh uploadNereidCascade(gfx::Device& device,
        const lib::nereid::WaterCascadeMesh& cpu,
        lib::nereid::WaterSurfaceStyle style) {
    if(cpu.empty() || !render::validNereidWaterStyle(style))return {};
    style=lib::nereid::resolvedWaterOptics(style);
    render::MeshBuilder builder;
    for(const auto& v:cpu.vertices) {
        const float foam=std::clamp(v.foam*style.foamStrength,0.f,1.f);
        const auto channel=[foam](float deep,float rim) {
            return uint8_t(std::clamp(deep+(rim-deep)*foam,0.f,1.f)*255.f+0.5f);
        };
        const uint32_t tint=render::rgba(
            channel(style.deepR,style.foamR),
            channel(style.deepG,style.foamG),
            channel(style.deepB,style.foamB),255);
        builder.addVertex({v.x,v.y,v.z},{v.nx,v.ny,v.nz},tint);
    }
    for(size_t i=0;i+2<cpu.indices.size();i+=3)
        builder.addTriangle(cpu.indices[i],cpu.indices[i+1],cpu.indices[i+2]);
    return builder.upload(device);
}

class NereidCascadeScene final {
    struct Slot {
        NereidCascadeSource source;
        // Heap-owned so its address survives setCascades(): Renderer3D
        // items_ and camera geometry snapshots keep &*mesh until render.
        std::unique_ptr<render::Mesh> mesh=std::make_unique<render::Mesh>();
        bool resident=false;
        bool dirty=true;
        std::optional<Fixed> inletLevel,outletLevel;
        uint64_t inletRevision=0,outletRevision=0;
        double originX=0,originY=0,originZ=0;
    };
public:
    static constexpr size_t kMaxCascades=32;
    void setCascades(std::vector<NereidCascadeSource> incoming) {
        if(incoming.size()>kMaxCascades)
            throw std::invalid_argument("nereid.cascade: too many cascades");
        // Validate and allocate before touching any GPU handles.
        std::map<uint64_t,Slot> next;
        for(size_t i=0;i<incoming.size();++i) {
            auto source=std::move(incoming[i]);
            if(!source.id)source.id=(uint64_t(1)<<62)|(i+1);
            if((source.cascade.enabled &&
                !lib::nereid::validWaterCascade(source.cascade)) ||
               !render::validNereidWaterStyle(source.style) ||
               source.options.stepsPerSegment<1 ||
               source.options.stepsPerSegment>16 ||
               source.options.maxVertices<8 ||
               source.options.maxVertices>65536 ||
               !std::isfinite(source.joinLength) ||
               source.joinLength<0.1f || source.joinLength>50.f ||
               !std::isfinite(source.joinWidthBlend) ||
               source.joinWidthBlend<0.f || source.joinWidthBlend>1.f ||
               !std::isfinite(source.options.uvScale) ||
               source.options.uvScale<=0)
                throw std::invalid_argument("nereid.cascade: invalid source");
            const uint64_t id=source.id;
            // Initialize every presentation cache field (including optional
            // linked levels) before moving the source into its map slot.
            Slot slot{};
            slot.source=std::move(source);
            if(!next.emplace(id,std::move(slot)).second)
                throw std::invalid_argument("nereid.cascade: duplicate id");
        }
        retired_.reserve(retired_.size()+slots_.size());
        for(auto& [id,slot]:next) {
            auto old=slots_.find(id);
            if(old==slots_.end())continue;
            slot.dirty=old->second.dirty ||
                !sameNereidCascadeSource(slot.source,old->second.source);
            slot.mesh=std::move(old->second.mesh);
            slot.resident=old->second.resident;
            slot.inletLevel=old->second.inletLevel;
            slot.inletRevision=old->second.inletRevision;
            slot.outletRevision=old->second.outletRevision;
            slot.outletLevel=old->second.outletLevel;
            slot.originX=old->second.originX;
            slot.originY=old->second.originY;
            slot.originZ=old->second.originZ;
        }
        // Removed slots: the Mesh object stays alive (and its GPU buffers
        // valid) until the next device-thread update()/shutdown().
        for(auto& [id,old]:slots_) {
            (void)id;
            if(old.mesh)retired_.push_back(std::move(old.mesh));
        }
        slots_.swap(next);
    }

    void setMaxUploadsPerFrame(uint32_t count) {
        if(count<1 || count>8)
            throw std::invalid_argument("nereid.cascade: upload count 1..8");
        maxUploadsPerFrame_=count;
    }
    size_t count()const{return slots_.size();}
    // Mesh object submitted by draw() for this cascade id (stable address
    // across setCascades while the id persists), or nullptr.
    const render::Mesh* meshFor(uint64_t id)const{
        const auto it=slots_.find(id);
        return it==slots_.end()?nullptr:it->second.mesh.get();
    }
    // Uses the SAME fixed-point cascade-mouth kernel as NEREID ECS sampling.
    // The water body's ordinary current/vortex sampling is supplied by the
    // caller, and the cascade field merely blends it near linked mouths.
    Vec3 flowAtConnection(uint64_t stableWaterId,Vec3 position,
                          Vec3 base,const NereidWaterScene& water)const {
        std::array<lib::nereid::WaterCascadeFlowContribution,64> entries{};
        size_t count=0;
        for(const auto& [id,slot]:slots_) {
            const auto& source=slot.source;
            if(!source.useFlowPolicy ||
               !lib::nereid::validWaterCascadeFlowPolicy(source.flowPolicy) ||
               !lib::nereid::validWaterCascade(source.cascade))
                continue;
            const bool outlet=stableWaterId==source.outletWaterId &&
                              source.outletWaterId!=0;
            const bool inlet=stableWaterId==source.inletWaterId &&
                             source.inletWaterId!=0 && !outlet;
            if(!outlet && !inlet)continue;
            Fixed upstreamSpeed{};
            if(outlet) {
                if(const auto* upper=water.surfaceForId(source.inletWaterId)) {
                    const auto& body=upper->body;
                    const auto* geometry=upper->useGeometry?&upper->geometry:nullptr;
                    const auto* current=upper->useCurrentField?&upper->currentField:nullptr;
                    const auto* vortices=upper->useVortices?&upper->vortices:nullptr;
                    upstreamSpeed=lib::nereid::waterCurrentWithVortices(
                        body,geometry,current,vortices,
                        source.cascade.points.front().position).xz().length();
                }
            }
            const auto contribution=lib::nereid::waterCascadeMouthContribution(
                id,source.cascade,source.flowPolicy,outlet,position,
                upstreamSpeed);
            if(contribution.weight>Fixed{} && count<entries.size())
                entries[count++]=contribution;
        }
        return lib::nereid::blendWaterCascadeContributions(base,entries,count);
    }

    template<class Fn> void forEachSource(Fn&& fn)const {
        for(const auto& [id,slot]:slots_)
            if(slot.source.cascade.enabled)fn(id,slot.source);
    }
    size_t residentCount()const {
        size_t n=0;for(const auto& [id,s]:slots_) { (void)id;if(s.resident)++n; }
        return n;
    }
    void update(gfx::Device& device,
                const NereidWaterScene* water=nullptr) {
        for(auto& old:retired_)render::releaseNereidWaterSurface(device,*old);
        retired_.clear();
        uint32_t attempts=0;
        for(auto& [id,slot]:slots_) {
            (void)id;
            if(!slot.source.cascade.enabled) {
                render::releaseNereidWaterSurface(device,*slot.mesh);
                slot.resident=false;slot.dirty=true;
                continue;
            }
            const auto* inletSource=water && slot.source.inletWaterId?
                water->surfaceForId(slot.source.inletWaterId):nullptr;
            const auto* outletSource=water && slot.source.outletWaterId?
                water->surfaceForId(slot.source.outletWaterId):nullptr;
            const std::optional<Fixed> inlet=inletSource?
                std::optional<Fixed>(inletSource->body.surfaceY):std::nullopt;
            const std::optional<Fixed> outlet=outletSource?
                std::optional<Fixed>(outletSource->body.surfaceY):std::nullopt;
            const uint64_t inletRevision=water && inletSource?
                water->surfaceRevision(slot.source.inletWaterId):0;
            const uint64_t outletRevision=water && outletSource?
                water->surfaceRevision(slot.source.outletWaterId):0;
            if(inlet!=slot.inletLevel || outlet!=slot.outletLevel ||
               inletRevision!=slot.inletRevision ||
               outletRevision!=slot.outletRevision)
                slot.dirty=true;
            slot.inletLevel=inlet;
            slot.outletLevel=outlet;
            slot.inletRevision=inletRevision;
            slot.outletRevision=outletRevision;
            if(!slot.dirty || attempts>=maxUploadsPerFrame_)continue;
            ++attempts;
            if(!lib::nereid::validWaterCascade(slot.source.cascade))continue;
            auto opts=slot.source.options;
            const auto& first=slot.source.cascade.points.front().position;
            opts.originX=first.x.toDouble();
            opts.originY=first.y.toDouble();
            opts.originZ=first.z.toDouble();
            const auto joined=connectedNereidCascade(
                slot.source,inletSource,outletSource);
            auto cpu=lib::nereid::buildWaterCascadeMesh(joined,opts);
            if(cpu.empty())continue;
            // Seam foam masks the material transition between a horizontal
            // water surface and the descending sheet. This is a visual
            // gradient on the extra connector segments, never ECS geometry.
            const bool inletStitched=joined.points.size()>slot.source.cascade.points.size() &&
                joined.points.front().position!=slot.source.cascade.points.front().position;
            const bool outletStitched=joined.points.size()>slot.source.cascade.points.size() &&
                joined.points.back().position!=slot.source.cascade.points.back().position;
            const size_t steps=opts.stepsPerSegment;
            const size_t count=cpu.vertices.size();
            if(inletStitched && count>=steps*8) {
                for(size_t i=0;i<steps;++i) {
                    const float t=float(i)/float(steps);
                    const float u=float(i+1)/float(steps);
                    const size_t base=i*8;
                    for(size_t k:{size_t(0),size_t(1),size_t(4),size_t(5)})
                        cpu.vertices[base+k].foam=std::max(
                            cpu.vertices[base+k].foam,(1.f-t)*0.9f);
                    for(size_t k:{size_t(2),size_t(3),size_t(6),size_t(7)})
                        cpu.vertices[base+k].foam=std::max(
                            cpu.vertices[base+k].foam,(1.f-u)*0.9f);
                }
            }
            if(outletStitched && count>=steps*8) {
                const size_t first=count-steps*8;
                for(size_t i=0;i<steps;++i) {
                    const float t=float(i)/float(steps);
                    const float u=float(i+1)/float(steps);
                    const size_t base=first+i*8;
                    for(size_t k:{size_t(0),size_t(1),size_t(4),size_t(5)})
                        cpu.vertices[base+k].foam=std::max(
                            cpu.vertices[base+k].foam,t*0.9f);
                    for(size_t k:{size_t(2),size_t(3),size_t(6),size_t(7)})
                        cpu.vertices[base+k].foam=std::max(
                            cpu.vertices[base+k].foam,u*0.9f);
                }
            }
            auto replacement=uploadNereidCascade(device,cpu,slot.source.style);
            if(!replacement.vb.valid() || !replacement.ib.valid() ||
               replacement.indexCount==0) {
                render::releaseNereidWaterSurface(device,replacement);
                continue;
            }
            render::releaseNereidWaterSurface(device,*slot.mesh);
            *slot.mesh=replacement;
            slot.originX=opts.originX;
            slot.originY=opts.originY;
            slot.originZ=opts.originZ;
            slot.resident=true;slot.dirty=false;
        }
    }
    void draw(render::Renderer3D& renderer,Cell origin,int cellSize)const {
        const double x=double(origin.x)*cellSize;
        const double y=double(origin.y)*cellSize;
        const double z=double(origin.z)*cellSize;
        for(const auto& [id,slot]:slots_) {
            (void)id;
            if(!slot.resident || !slot.source.cascade.enabled)continue;
            const auto xf=render::mat4::translate({
                float(slot.originX-x),float(slot.originY-y),
                float(slot.originZ-z)});
            const auto& style=slot.source.style;
            renderer.drawMesh(*slot.mesh,xf,
                {style.absorption,style.shoreFoamDepth,
                 style.depthTint,style.opacity},
                render::kDrawNoCastShadow|render::kDrawWaterSurface|
                    render::kDrawNereidCascade,
                style.specular,style.refractionStrength);
        }
    }
    void shutdown(gfx::Device& device) {
        for(auto& old:retired_)render::releaseNereidWaterSurface(device,*old);
        retired_.clear();
        for(auto& [id,slot]:slots_) {
            (void)id;
            render::releaseNereidWaterSurface(device,*slot.mesh);
            slot.resident=false;slot.dirty=true;
        }
    }
private:
    std::map<uint64_t,Slot> slots_;
    std::vector<std::unique_ptr<render::Mesh>> retired_;
    uint32_t maxUploadsPerFrame_=2;
};

} // namespace aiverse::world
