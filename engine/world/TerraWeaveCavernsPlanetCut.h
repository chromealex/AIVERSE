#pragma once
#include "engine/world/TerraWeaveCavernsPlanetSurface.h"
#include "engine/world/TerraWeaveCavernsSurfaceGroup.h"
#include <compare>
#include <map>
#include <set>
#include <span>

namespace aiverse::terraweave::caverns {
struct CavePlanetSurfaceSlotKey {
    int face=0,x=0,z=0,size=0,level=0;
    auto operator<=>(const CavePlanetSurfaceSlotKey&)const=default;
};
struct CavePlanetSurfaceSlot {
    CavePlanetSurfaceSlotKey key;
    std::vector<world::Triangle> bodyTriangles; // actual selected source, no skirts
};
struct CavePlanetCutSlot {
    CavePlanetSurfaceSlotKey key;
    std::vector<world::Triangle> originalBody;
    std::vector<world::Triangle> chartTriangles;
    std::vector<world::Triangle> bodyTriangles;
};
struct CavePlanetCutPlan {
    CavePlanetAnchor anchor;
    uint64_t surfaceRevision=0,authorityRevision=0,surfaceEpoch=0;
    std::map<CavePlanetSurfaceSlotKey,CavePlanetCutSlot> slots;
    std::vector<CaveSurfaceRim> rims;
    std::vector<uint64_t> accepted,rejected;
};
inline std::set<SurfaceEdgeKey> planetaryOpenEdges(std::span<const world::Triangle> triangles){
    struct Use {unsigned count=0;SurfaceVertexKey from{},to{};};
    std::map<SurfaceEdgeKey,Use> uses;
    for(const auto& t:triangles){
        if(!world::SpatialWorld::validTriangle(t))throw std::invalid_argument("Caverns planetary invalid source triangle");
        const Vec3 v[]={t.a,t.b,t.c};
        for(unsigned i=0;i<3;++i){
            const auto a=surfaceVertexKey(v[i]),b=surfaceVertexKey(v[(i+1)%3]);
            auto& u=uses[surfaceEdgeKey(v[i],v[(i+1)%3])];
            if(!u.count){u.from=a;u.to=b;}
            else if(u.count!=1||u.from!=b||u.to!=a)
                throw std::invalid_argument("Caverns planetary nonmanifold or inconsistent surface");
            ++u.count;
        }
    }
    std::set<SurfaceEdgeKey> out;
    for(const auto& [edge,u]:uses)if(u.count==1)out.insert(edge);
    return out;
}
inline Vec3 planetCutPoint(CutVertexCm p){
    const auto f=[](double x){
        if(!std::isfinite(x)||std::abs(x)>100000.)throw std::out_of_range("Caverns planetary cut extent");
        return Fixed::fromRaw(int64_t(std::llround(x*double(Fixed::kOne)/100.)));
    };
    return {f(p.x),f(p.y),f(p.z)};
}
inline double projectedCutArea(const std::vector<CutVertexCm>& p){
    double area=0;
    for(size_t i=0;i<p.size();++i){const auto& a=p[i];const auto& b=p[(i+1)%p.size()];area+=a.x*b.z-b.x*a.z;}
    return std::abs(area)*.5;
}
inline CavePlanetCutPlan planCavePlanetCut(const CavePlanetFrame& frame,
        const CaveVolumeSnapshot& authored,std::span<const CavePlanetSurfaceSlot> slots,
        uint64_t surfaceRevision,uint64_t epoch){
    if(slots.empty()||slots.size()>64||!surfaceRevision||!epoch)
        throw std::invalid_argument("Caverns planetary surface group budget");
    auto snapshot=authored;canonicalizeCaveSnapshot(snapshot);validateCavePlanetVolume(frame,snapshot);
    CavePlanetCutPlan out;out.anchor=frame.anchor();out.surfaceRevision=surfaceRevision;
    out.authorityRevision=snapshot.sourceRevision;out.surfaceEpoch=epoch;
    std::map<SurfaceVertexKey,Vec3> originalVertices;
    std::vector<world::Triangle> originalChart,originalBody;
    for(const auto& slot:slots){
        const auto& k=slot.key;
        if(k.face<0||k.face>=6||k.x<0||k.z<0||k.size<=0||k.level<0||
           slot.bodyTriangles.empty()||slot.bodyTriangles.size()>65534||
           slot.bodyTriangles.size()>131072-originalChart.size())
            throw std::invalid_argument("Caverns planetary source slot");
        CavePlanetCutSlot item;item.key=k;item.originalBody=slot.bodyTriangles;
        for(const auto& source:slot.bodyTriangles){
            auto chart=projectPlanetSurfaceTriangle(frame,source);
            const Vec3 a[]={chart.a,chart.b,chart.c},b[]={source.a,source.b,source.c};
            for(unsigned i=0;i<3;++i){
                auto [it,added]=originalVertices.emplace(surfaceVertexKey(a[i]),b[i]);
                if(!added&&it->second!=b[i])throw std::invalid_argument("Caverns planetary chart aliases source vertices");
            }
            item.chartTriangles.push_back(chart);originalChart.push_back(chart);originalBody.push_back(source);
        }
        if(!out.slots.emplace(k,std::move(item)).second)throw std::invalid_argument("Caverns duplicate planetary slot");
    }
    const auto outer=planetaryOpenEdges(originalChart);
    const auto outerBody=planetaryOpenEdges(originalBody);
    if(outer.empty()||outerBody.size()!=outer.size())throw std::invalid_argument("Caverns planetary source boundary");
    std::vector<const Entrance*> accepted;
    size_t work=0;constexpr size_t maxWork=4000000;
    for(const auto& e:snapshot.entrances){
        const auto bounds=entranceBounds(e);
        bool safe=true;double covered=0,low=1e30,high=-1e30;
        for(const auto& t:originalChart){
            if(++work>maxWork)throw std::length_error("Caverns planetary coverage work budget");
            const CutTriangleCm tri{toCutVertex(t.a),toCutVertex(t.b),toCutVertex(t.c)};
            if(std::max({tri.a.x,tri.b.x,tri.c.x})<bounds.minX||std::min({tri.a.x,tri.b.x,tri.c.x})>=bounds.maxX||
               std::max({tri.a.z,tri.b.z,tri.c.z})<bounds.minZ||std::min({tri.a.z,tri.b.z,tri.c.z})>=bounds.maxZ)continue;
            std::vector<CutVertexCm> p{tri.a,tri.b,tri.c};
            for(unsigned side=0;side<16&&!p.empty();++side)p=clipCutPolygon(p,e,side,16,false);
            const double area=projectedCutArea(p);
            if(area<=1e-8)continue;
            covered+=area;
            for(const auto& v:p){low=std::min(low,v.y);high=std::max(high,v.y);}
            if(!entranceIntersectsTriangleAtHeight(tri,e))safe=false;
        }
        if(covered<=1e-8)continue;
        constexpr double pi=3.14159265358979323846;
        const double target=8.*double(e.radiusCm)*e.radiusCm*std::sin(2.*pi/16.);
        // Require the complete mouth area on selected triangles. The
        // boundary/manifold certificate below independently rejects gaps.
        if(std::abs(covered-target)>std::max(.01,target*1e-7))safe=false;
        for(const auto& edge:outer){
            if(++work>maxWork)throw std::length_error("Caverns planetary boundary work budget");
            const auto a=toCutVertex({Fixed::fromRaw(edge.first[0]),Fixed::fromRaw(edge.first[1]),Fixed::fromRaw(edge.first[2])});
            const auto b=toCutVertex({Fixed::fromRaw(edge.second[0]),Fixed::fromRaw(edge.second[1]),Fixed::fromRaw(edge.second[2])});
            if(std::max(a.x,b.x)>=bounds.minX&&std::min(a.x,b.x)<bounds.maxX&&
               std::max(a.z,b.z)>=bounds.minZ&&std::min(a.z,b.z)<bounds.maxZ){safe=false;break;}
        }
        if(safe)for(const auto& s:snapshot.stamps){
            if(++work>maxWork)throw std::length_error("Caverns planetary fill work budget");
            if(s.mode!=CaveStampMode::Fill)continue;
            const auto p=s.center;const auto r=s.radiusCm;
            if(p.x+r>=bounds.minX&&p.x-r<=bounds.maxX&&p.z+r>=bounds.minZ&&p.z-r<=bounds.maxZ&&
               p.y+r>=low&&p.y-r<=high){safe=false;break;}
        }
        if(safe)for(const auto* prior:accepted)if(overlaps(bounds,entranceBounds(*prior))){safe=false;break;}
        if(safe){accepted.push_back(&e);out.accepted.push_back(e.id);}
        else out.rejected.push_back(e.id);
    }
    std::vector<world::Triangle> finalChart,finalBody;
    for(auto& [key,slot]:out.slots){
        (void)key;std::vector<CutTriangleCm> fragments;
        for(const auto& t:slot.chartTriangles){
            if(accepted.size()>maxWork-work)throw std::length_error("Caverns planetary clipping work budget");
            work+=accepted.size();
            appendCutTerrainTriangle(fragments,{toCutVertex(t.a),toCutVertex(t.b),toCutVertex(t.c)},accepted,16,65534);
        }
        slot.chartTriangles.clear();
        for(const auto& t:fragments){
            world::Triangle chart{planetCutPoint(t.a),planetCutPoint(t.b),planetCutPoint(t.c)};
            const auto body=[&](Vec3 p){
                const auto it=originalVertices.find(surfaceVertexKey(p));
                return it==originalVertices.end()?frame.toBody(p):it->second;
            };
            world::Triangle native{body(chart.a),body(chart.b),body(chart.c)};
            if(!world::SpatialWorld::validTriangle(chart)||!world::SpatialWorld::validTriangle(native))
                throw std::invalid_argument("Caverns planetary quantized fragment invalid");
            slot.chartTriangles.push_back(chart);slot.bodyTriangles.push_back(native);
        }
        if(slot.chartTriangles.size()>262144-finalChart.size())throw std::length_error("Caverns planetary result budget");
        finalChart.insert(finalChart.end(),slot.chartTriangles.begin(),slot.chartTriangles.end());
        finalBody.insert(finalBody.end(),slot.bodyTriangles.begin(),slot.bodyTriangles.end());
    }
    auto expected=outer;auto expectedBody=outerBody;
    for(const auto* e:accepted){
        auto rim=extractCaveSurfaceRimTriangles(finalChart,*e,work,maxWork);
        for(size_t i=0;i<rim.ring.size();++i){
            const auto a=rim.ring[i],b=rim.ring[(i+1)%rim.ring.size()];
            const auto v=[](const SurfaceVertexKey& k){return Vec3{Fixed::fromRaw(k[0]),Fixed::fromRaw(k[1]),Fixed::fromRaw(k[2])};};
            const auto pa=frame.toBody(v(a)),pb=frame.toBody(v(b));
            // An original vertex on a new rim must round-trip exactly,
            // otherwise the volume and surface use different body points.
            for(const auto& k:{a,b})if(auto it=originalVertices.find(k);it!=originalVertices.end()&&it->second!=frame.toBody(v(k)))
                throw std::invalid_argument("Caverns planetary rim original vertex transform mismatch");
            if(!expected.insert(surfaceEdgeKey(v(a),v(b))).second||
               !expectedBody.insert(surfaceEdgeKey(pa,pb)).second)
                throw std::invalid_argument("Caverns planetary overlapping boundary");
        }
        out.rims.push_back(std::move(rim));
    }
    if(planetaryOpenEdges(finalChart)!=expected||planetaryOpenEdges(finalBody)!=expectedBody)
        throw std::invalid_argument("Caverns planetary cut changed exterior or produced unmatched seams");
    return out;
}
} // namespace aiverse::terraweave::caverns
