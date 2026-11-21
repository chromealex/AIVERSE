// VIVID bounded segment-to-segment groom collision.
// Cross-guide capsules, pinned roots, deterministic Jacobi projections.
// Presentation-only CPU reference; no gameplay or RHI dependencies.
#pragma once
#include "HumanGroom.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace aiverse::vivid {
class GroomSegmentCollision final {
    using V=render::float3;
    using Cell=GroomSegmentWorkspace::Cell;
public:
    using Workspace=GroomSegmentWorkspace;
private:
    static bool finite(V p) noexcept {
        return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
    }
    static std::pair<float,float> nearest(V p0,V p1,V q0,V q1) noexcept {
        const auto u=p1-p0,v=q1-q0,w=p0-q0;
        const float a=render::dot(u,u),b=render::dot(u,v);
        const float c=render::dot(v,v),d=render::dot(u,w);
        const float e=render::dot(v,w),det=a*c-b*b;
        float s=0.f,t=0.f;
        if(a<=1.e-12f && c<=1.e-12f)return {s,t};
        if(a<=1.e-12f) return {0.f,std::clamp(e/c,0.f,1.f)};
        if(c<=1.e-12f) return {std::clamp(-d/a,0.f,1.f),0.f};
        if(det>1.e-12f)
            s=std::clamp((b*e-c*d)/det,0.f,1.f);
        t=std::clamp((b*s+e)/c,0.f,1.f);
        s=std::clamp((b*t-d)/a,0.f,1.f);
        t=std::clamp((b*s+e)/c,0.f,1.f);
        return {s,t};
    }
public:
    static bool resolve(std::span<GroomGuide> guides,
                        const GroomSelfCollisionConfig& cfg,
                        GroomSelfCollisionStats& stats,
                        Workspace& workspace) {
        if(!cfg.segmentEnabled || !cfg.enabled || cfg.response==0 ||
           guides.size()<2)return true;
        if(!std::isfinite(cfg.friction)||cfg.friction<0||cfg.friction>1 ||
           !std::isfinite(cfg.separation)||cfg.separation<=0 ||
           !std::isfinite(cfg.response)||cfg.response<0||cfg.response>1)
            return false;
        // Indexed flat scratch avoids a heap allocation per guide.
        // Offsets are stable for this solve and map guide points to scratch.
        workspace.reset();
        workspace.offsets.reserve(guides.size()+1);
        workspace.offsets.push_back(0);
        auto& segments=workspace.segments;
        // Reserve once per groom; later substeps reuse the same allocation.
        segments.reserve(std::min<std::size_t>(16384, guides.size()*8));
        for(std::size_t g=0;g<guides.size();++g) {
            const auto& guide=guides[g];
            if(guide.position.size()!=guide.previous.size() ||
               guide.position.size()!=guide.rest.size())return false;
            if(guide.position.size()>131072-workspace.offsets.back())
                return false;
            workspace.offsets.push_back(
                workspace.offsets.back()+guide.position.size());
            for(std::size_t j=1;j<guide.position.size();++j) {
                const auto a=guide.position[j-1],b=guide.position[j];
                if(!finite(a)||!finite(b))return false;
                if(segments.size()>=131072)return false;
                segments.push_back({g,j-1,a,b});
            }
        }
        workspace.displacements.assign(workspace.offsets.back(),V{});
        workspace.counts.assign(workspace.offsets.back(),0.f);
        // A dense groom may contain far more segments than this CPU
        // reference can hash economically. Skip *this optional pass*
        // rather than failing the parent Verlet step or exhausting RAM.
        if(segments.size()>16384) {
            stats.budgetHit=true;
            return true;
        }
        const float cellSize=cfg.separation;
        std::size_t entries=0;
        constexpr std::size_t maxEntries=524288;
        workspace.cells.reserve(std::min<std::size_t>(
            maxEntries,std::max<std::size_t>(segments.size()*4,1024)));
        auto gridCoord=[&](float p,int& coord) {
            const auto n=std::floor(p/cellSize);
            if(!std::isfinite(n)||n< -1000000.f||n>1000000.f)
                return false;
            coord=int(n);
            return true;
        };
        for(std::size_t i=0;i<segments.size();++i) {
            const auto& seg=segments[i];
            const V lo{std::min(seg.a.x,seg.b.x)-cellSize,
                       std::min(seg.a.y,seg.b.y)-cellSize,
                       std::min(seg.a.z,seg.b.z)-cellSize};
            const V hi{std::max(seg.a.x,seg.b.x)+cellSize,
                       std::max(seg.a.y,seg.b.y)+cellSize,
                       std::max(seg.a.z,seg.b.z)+cellSize};
            Cell a,b;
            if(!gridCoord(lo.x,a.x)||!gridCoord(lo.y,a.y)||
               !gridCoord(lo.z,a.z)||!gridCoord(hi.x,b.x)||
               !gridCoord(hi.y,b.y)||!gridCoord(hi.z,b.z))return false;
            const auto x=std::uint64_t(b.x-a.x+1),y=std::uint64_t(b.y-a.y+1),
                z=std::uint64_t(b.z-a.z+1);
            if(x*y*z>maxEntries-entries) {
                stats.budgetHit=true;
                return true; // no positions changed; optional pass skipped
            }
            for(int ix=a.x;ix<=b.x;++ix)
                for(int iy=a.y;iy<=b.y;++iy)
                    for(int iz=a.z;iz<=b.z;++iz) {
                        workspace.cells.push_back({{ix,iy,iz},i});
                        ++entries;
                    }
        }
        // Sort cell membership once; unlike std::map<Cell,vector> this
        // retains capacity for the next substep. Pair IDs are sorted and
        // unique globally so a pair spanning many cells is solved once.
        std::sort(workspace.cells.begin(),workspace.cells.end());
        constexpr std::size_t maxRawPairs=1000000;
        for(std::size_t begin=0;begin<workspace.cells.size();) {
            std::size_t finish=begin+1;
            while(finish<workspace.cells.size() &&
                  !(workspace.cells[begin].cell<workspace.cells[finish].cell) &&
                  !(workspace.cells[finish].cell<workspace.cells[begin].cell))
                ++finish;
            const auto n=finish-begin;
            // Cap quadratic occupancy BEFORE enumerating it. In an
            // overloaded cell skip optional segment narrowphase safely.
            if(n>1 && n*(n-1)/2>maxRawPairs-workspace.pairs.size()) {
                stats.budgetHit=true;
                return true;
            }
            for(std::size_t i=begin;i<finish;++i)
                for(std::size_t j=i+1;j<finish;++j) {
                    auto a=workspace.cells[i].segment;
                    auto b=workspace.cells[j].segment;
                    if(segments[a].guide==segments[b].guide)continue;
                    if(a>b)std::swap(a,b);
                    workspace.pairs.push_back({a,b});
                }
            begin=finish;
        }
        std::sort(workspace.pairs.begin(),workspace.pairs.end());
        workspace.pairs.erase(
            std::unique(workspace.pairs.begin(),workspace.pairs.end()),
            workspace.pairs.end());
        const float r2=cfg.separation*cfg.separation;
        for(const auto [a,b]:workspace.pairs) {
            if(stats.pairChecks>=cfg.maxPairChecks) {
                stats.budgetHit=true;
                break;
            }
            ++stats.pairChecks;
            const auto& p=segments[a];
            const auto& q=segments[b];
            const auto [s,t]=nearest(p.a,p.b,q.a,q.b);
            const auto pa=p.a+(p.b-p.a)*s;
            const auto pb=q.a+(q.b-q.a)*t;
            const auto diff=pb-pa;
            const float squared=render::dot(diff,diff);
            if(!std::isfinite(squared)||squared>=r2)continue;
            const float distance=std::sqrt(std::max(0.f,squared));
            const auto axis=((a+b)&1u)?V{1,0,0}:V{0,0,1};
            const auto normal=distance>1.e-7f?diff*(1.f/distance):axis;
            const float penetration=(cfg.separation-distance)*cfg.response;
            const float aw0=p.first==0?0.f:1.f-s;
            const float aw1=s;
            const float bw0=q.first==0?0.f:1.f-t;
            const float bw1=t;
            const float normalization=aw0*aw0+aw1*aw1+
                bw0*bw0+bw1*bw1;
            if(normalization<=1.e-8f)continue;
            const auto shift=normal*(penetration/normalization);
            const auto add=[&](std::size_t g,std::size_t point,V amount,float w) {
                if(point==0||w==0)return;
                const auto id=workspace.offsets[g]+point;
                workspace.displacements[id]+=amount*w;
                workspace.counts[id]+=w;
            };
            add(p.guide,p.first,shift*(-aw0),aw0);
            add(p.guide,p.first+1,shift*(-aw1),aw1);
            add(q.guide,q.first,shift*bw0,bw0);
            add(q.guide,q.first+1,shift*bw1,bw1);
            ++stats.contacts;
        }
        // Validate every correction before touching live guide arrays.
        for(std::size_t g=0;g<guides.size();++g)
            for(std::size_t j=1;j<guides[g].position.size();++j) {
                const auto id=workspace.offsets[g]+j;
                if(workspace.counts[id]<=0)continue;
                auto shift=workspace.displacements[id]*
                    (1.f/workspace.counts[id]);
                const auto length=render::length(shift);
                if(length>cfg.separation)shift=shift*(cfg.separation/length);
                if(!finite(guides[g].position[j]+shift))return false;
            }
        for(std::size_t g=0;g<guides.size();++g)
            for(std::size_t j=1;j<guides[g].position.size();++j) {
                const auto id=workspace.offsets[g]+j;
                if(workspace.counts[id]<=0)continue;
                auto shift=workspace.displacements[id]*
                    (1.f/workspace.counts[id]);
                const auto length=render::length(shift);
                if(length>cfg.separation)shift=shift*(cfg.separation/length);
                auto& guide=guides[g];
                // Normal collision response is inelastic, while friction
                // damps residual tangential velocity in the Verlet state.
                const auto velocity=guide.position[j]-guide.previous[j];
                guide.position[j]+=shift;
                guide.previous[j]=guide.position[j]-
                    velocity*(1.f-cfg.friction);
            }
        return true;
    }
    // Convenience entry point for tools/tests that do not retain a
    // Workspace. Production groom runtime passes its persistent scratch.
    static bool resolve(std::span<GroomGuide> guides,
                        const GroomSelfCollisionConfig& cfg,
                        GroomSelfCollisionStats& stats) {
        Workspace temporary;
        return resolve(guides,cfg,stats,temporary);
    }
};
} // namespace aiverse::vivid
