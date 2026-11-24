#pragma once
#include "apps/editor/core/MapDoc.h"
#include "engine/world/TerraWeaveCavernsPlanetAuthority.h"
#include "engine/world/TerraWeaveCavernsPlanetCut.h"
#include "engine/world/TerraWeaveCavernsArchive.h"
#include "engine/world/TerraWeaveCavernsRaycast.h"
#include <array>
#include <charconv>
#include <map>
#include <limits>
#include <cmath>

namespace aiverse::editor {
namespace planet_caves {
using namespace terraweave::caverns;
inline constexpr const char* key="terraweavePlanetCaverns";
// First authoring tier: one selected immutable chart per document.
// Store it separately from the flat-world key to prevent legacy consumers
// from interpreting local centimetres as absolute world coordinates.
inline void validateAnchor(const MapData& map,const CavePlanetAnchor& a){
    const int body=map.findBody(a.bodyId);
    if(!map.planet()||body<0)throw std::invalid_argument("Caverns planetary body missing");
    const auto& b=map.bodies[size_t(body)];
    const auto kind=b.meta.getStr("kind","planet");
    const auto radius=b.meta.getFx("radius",Fixed::fromInt(32));
    if((kind!="planet"&&kind!="moon")||b.faceSize<2||b.faceSize>512||
       radius<Fixed::fromInt(2)||radius>Fixed::fromInt(1200)||
       a.face<0||a.face>=6||a.a<Fixed{}||a.b<Fixed{}||
       a.a>Fixed::fromInt(b.faceSize)||a.b>Fixed::fromInt(b.faceSize)||
       a.halfExtentCm<100||Fixed::fromRatio(a.halfExtentCm,100)>radius/8)
        throw std::invalid_argument("Caverns planetary chart outside supported body bounds");
}
struct Region {CavePlanetAnchor anchor;CaveVolumeSnapshot volume;};
inline constexpr size_t maxRegions=32;
inline uint64_t regionId(const Json& v){
    if(!v.isString())throw std::invalid_argument("Caverns region ID must be decimal text");
    const auto text=v.str();uint64_t n=0;
    const auto p=std::from_chars(text.data(),text.data()+text.size(),n);
    if(p.ec!=std::errc{}||p.ptr!=text.data()+text.size()||!n||n>uint64_t(INT64_MAX)||
       std::to_string(n)!=text)throw std::invalid_argument("Caverns invalid region ID");
    return n;
}
inline Json regionIdJson(uint64_t id){return Json::string(std::to_string(id));}
inline Region decodeRegion(const MapData& map,const Json& j){
    Region r{cavePlanetAnchorFromJson(j["anchor"]),caveSnapshotFromJson(j["volume"])};
    validateAnchor(map,r.anchor);validateCavePlanetBounds(r.anchor,r.volume);return r;
}
inline Json encode(const MapData& map,const Region& r){
    validateAnchor(map,r.anchor);validateCavePlanetBounds(r.anchor,r.volume);
    Json j=Json::object();j.set("version",Json::number(int64_t(1)));
    j.set("anchor",cavePlanetAnchorToJson(r.anchor));j.set("volume",caveSnapshotToJson(r.volume));return j;
}
struct RegionSet {
    uint64_t active=0,next=1;
    std::map<uint64_t,Region> regions;
};
inline std::string activeRegionId(const MapData& map){
    const auto& j=map.extra[key];
    if(j.isNull())return {};
    if(!j.isObject())throw std::invalid_argument("Caverns planetary document");
    if(j.getInt("version",0)==1)return "1";
    if(j.getInt("version",0)!=2)throw std::invalid_argument("Caverns planetary document version");
    const auto id=j.getStr("activeRegionId");
    if(!id.empty())(void)regionId(j["activeRegionId"]);
    return id;
}
inline RegionSet readRegions(const MapData& map){
    const auto& j=map.extra[key];RegionSet out;
    if(j.isNull())return out;
    if(j.isObject()&&j.getInt("version",0)==1){
        out.active=1;out.next=2;out.regions.emplace(1,decodeRegion(map,j));return out;
    }
    if(!j.isObject()||j.getInt("version",0)!=2||!j["regions"].isArray()||
       j["regions"].size()>maxRegions||!j["activeRegionId"].isString())
        throw std::invalid_argument("Caverns planetary region set");
    out.next=regionId(j["nextRegionId"]);
    if(!j.getStr("activeRegionId").empty())out.active=regionId(j["activeRegionId"]);
    for(size_t i=0;i<j["regions"].size();++i){
        const auto& item=j["regions"][i];const auto id=regionId(item["id"]);
        if(id>=out.next||!out.regions.emplace(id,decodeRegion(map,item)).second)
            throw std::invalid_argument("Caverns duplicate or future region ID");
    }
    if((out.regions.empty()&&out.active)||(!out.regions.empty()&&!out.regions.contains(out.active)))
        throw std::invalid_argument("Caverns selected region missing");
    return out;
}
inline Json encodeRegions(const MapData& map,const RegionSet& set){
    if(set.regions.size()>maxRegions||!set.next||set.next>uint64_t(INT64_MAX)||
       (set.regions.empty()?set.active!=0:!set.regions.contains(set.active)))
        throw std::invalid_argument("Caverns planetary region set identity");
    Json j=Json::object(),items=Json::array();j.set("version",Json::number(int64_t(2)));
    j.set("activeRegionId",Json::string(set.active?std::to_string(set.active):std::string{}));
    j.set("nextRegionId",regionIdJson(set.next));
    for(const auto& [id,r]:set.regions){
        if(!id||id>=set.next)throw std::invalid_argument("Caverns region ID range");
        auto item=encode(map,r);item.set("id",regionIdJson(id));items.push(std::move(item));
    }
    j.set("regions",std::move(items));return j;
}
inline Region read(const MapData& map){
    const auto id=activeRegionId(map);
    if(id.empty())throw std::invalid_argument("Create a planetary Caverns region first");
    const auto& j=map.extra[key];
    if(j.getInt("version",0)==1)return decodeRegion(map,j);
    // Fast selected-chart path for scene picking; mutations validate full set.
    if(!j["regions"].isArray()||j["regions"].size()>maxRegions)
        throw std::invalid_argument("Caverns planetary region list");
    const Json* selected=nullptr;
    for(size_t i=0;i<j["regions"].size();++i){
        const auto& item=j["regions"][i];
        if(item.getStr("id")==id){
            if(selected)throw std::invalid_argument("Caverns duplicate selected region");
            selected=&item;
        }
    }
    if(!selected)throw std::invalid_argument("Caverns selected region missing");
    return decodeRegion(map,*selected);
}
inline Json describeRegions(const RegionSet& set){
    Json list=Json::array();
    for(const auto& [id,r]:set.regions){
        Json j=Json::object();j.set("id",regionIdJson(id));
        j.set("anchor",cavePlanetAnchorToJson(r.anchor));
        j.set("revisionText",Json::string(std::to_string(r.volume.sourceRevision)));
        list.push(std::move(j));
    }
    return list;
}
inline Region create(const MapData& map,CavePlanetAnchor anchor){
    validateAnchor(map,anchor);
    Region r;r.anchor=std::move(anchor);
    auto& s=r.volume.settings;const int32_t cap=r.anchor.halfExtentCm;
    s.cellCm=std::clamp(cap/2,100,4800);
    s.tunnelRadiusCm=std::max(1,s.cellCm/10);
    s.chamberRadiusCm=std::max(1,s.cellCm/4);
    s.minYcm=-cap*2/3;s.maxYcm=-std::max(1,cap/10);
    canonicalizeCaveSnapshot(r.volume);validateCavePlanetBounds(r.anchor,r.volume);return r;
}
// Editor-only body-local frame. Use actual authored coarse corners and the
// existing CubeSphere direction math. No simulation body-index claims or
// orbit state; never hand this temporary Surface to a gameplay owner.
inline CavePlanetFrame frameForDocument(const MapData& map,const CavePlanetAnchor& a){
    validateAnchor(map,a);
    const auto& doc=map.bodies[size_t(map.findBody(a.bodyId))];
    auto surface=std::make_shared<game::Surface>();
    game::BodyDef b;b.id=doc.id;b.kind=doc.meta.getStr("kind","planet")=="moon"?game::BodyKind::Moon:game::BodyKind::Planet;
    b.radius=doc.meta.getFx("radius",Fixed::fromInt(32));b.faceSize=doc.faceSize;
    const size_t count=6ull*size_t(b.faceSize+1)*size_t(b.faceSize+1);
    if(!doc.corners.empty()&&doc.corners.size()!=count)throw std::invalid_argument("Caverns malformed planet corners");
    surface->bodies.push_back(b);surface->corners=doc.corners;
    if(surface->corners.empty())surface->corners.resize(count);
    // Only facePoint/cubeOf are used in the editor chart: do not allocate
    // the full gameplay adjacency tables for every brush preview.
    auto tables=std::make_shared<game::SphereTables>();tables->cs.emplace_back(b.faceSize);
    surface->tab=std::move(tables);
    return CavePlanetFrame::make(std::move(surface),a);
}
// Reject even potentially overlapping rotated chart boxes across cube faces.
// Circumscribed spheres are deliberately conservative, with a fixed-point
// tolerance. Independent body IDs never share chart coordinates.
inline bool regionsMayOverlap(const MapData& map,const CavePlanetAnchor& a,const CavePlanetAnchor& b){
    if(a.bodyId!=b.bodyId)return false;
    const auto pa=frameForDocument(map,a).originBody(),pb=frameForDocument(map,b).originBody();
    const double x=pa.x.toDouble()-pb.x.toDouble(),y=pa.y.toDouble()-pb.y.toDouble(),z=pa.z.toDouble()-pb.z.toDouble();
    const double radius=1.733*(double(a.halfExtentCm)+b.halfExtentCm)/100.+.05;
    return x*x+y*y+z*z<=radius*radius;
}
inline uint64_t addRegion(const MapData& map,RegionSet& set,Region region){
    if(set.regions.size()>=maxRegions||set.next>=uint64_t(INT64_MAX))
        throw std::length_error("Caverns planetary region capacity");
    for(const auto& [id,old]:set.regions){
        (void)id;if(regionsMayOverlap(map,old.anchor,region.anchor))
            throw std::invalid_argument("Caverns planetary region overlap or insufficient separation");
    }
    const auto id=set.next++;set.regions.emplace(id,std::move(region));set.active=id;return id;
}
inline CavePlanetSurfacePatch captureLocalSurface(const MapData& map,const CavePlanetAnchor& a,uint64_t revision){
    auto frame=frameForDocument(map,a);
    const auto n=frame.surface().bodies[0].faceSize;
    std::vector<CavePlanetSurfaceCell> cells;
    const auto cap=Fixed::fromRatio(a.halfExtentCm,100);
    // All faces are considered, including cube edges. The bounded
    // selected cells retain the original surface diagonal.
    for(int f=0;f<6;++f)for(int z=0;z<n;++z)for(int x=0;x<n;++x){
        bool inside=true;
        for(int dz=0;dz<2&&inside;++dz)for(int dx=0;dx<2;++dx){
            const auto p=frame.surface().facePoint(0,f,Fixed::fromInt(x+dx),Fixed::fromInt(z+dz));
            const auto q=frame.vectorFromBody(p-frame.originBody());
            if(fxAbs(q.x)>cap||fxAbs(q.y)>cap||fxAbs(q.z)>cap){inside=false;break;}
        }
        if(inside){if(cells.size()>=8192)throw std::length_error("Caverns planetary chart surface cell budget");cells.push_back({f,x,z});}
    }
    return captureCavePlanetSurface(frame,cells,revision?revision:1);
}
// Editor-only coarse cut preflight. Group the exact captured cells by face;
 // these are chart inspector groups, NOT native PlanetRenderer leaf leases.
 // The returned plan is not permission to expose a hole before volume pairing.
inline CavePlanetCutPlan planLocalSurfaceCut(const CavePlanetFrame& frame,
        const CavePlanetSurfacePatch& patch,const CaveVolumeSnapshot& volume,uint64_t epoch){
    if(!sameCavePlanetAnchor(frame.anchor(),patch.anchor)||patch.cells.empty()||
       patch.cells.size()>8192||patch.bodyTriangles.size()!=patch.cells.size()*2||
       patch.chartTriangles.size()!=patch.bodyTriangles.size())
        throw std::invalid_argument("Caverns chart cut source identity");
    std::map<int,CavePlanetSurfaceSlot> faces;
    const auto n=frame.surface().bodies[size_t(frame.body())].faceSize;
    for(size_t i=0;i<patch.cells.size();++i){
        const auto cell=patch.cells[i];
        if(cell.face<0||cell.face>=6||cell.x<0||cell.x>=n||cell.z<0||cell.z>=n)
            throw std::invalid_argument("Caverns chart cut source cell");
        auto& slot=faces[cell.face];
        slot.key={cell.face,0,0,n,0};
        for(size_t j=0;j<2;++j){
            const auto& body=patch.bodyTriangles[i*2+j];
            const auto chart=projectPlanetSurfaceTriangle(frame,body);
            const auto& source=patch.chartTriangles[i*2+j];
            if(chart.a!=source.a||chart.b!=source.b||chart.c!=source.c)
                throw std::invalid_argument("Caverns chart cut source projection mismatch");
            slot.bodyTriangles.push_back(body);
        }
    }
    std::vector<CavePlanetSurfaceSlot> slots;
    for(auto& [face,slot]:faces){(void)face;slots.push_back(std::move(slot));}
    return planCavePlanetCut(frame,volume,slots,patch.surfaceRevision,epoch);
}
// Presentation-only picking against the exact same coarse triangle source
// as the chart viewport. The authoritative edit re-samples the surface
// height at the quantized centimetre point before committing the shaft.
inline std::optional<PointCm> pickLocalSurface(const CavePlanetSurfacePatch& patch,CaveRayCm ray){
    using D=std::array<double,3>;
    const auto sub=[](D a,D b){return D{a[0]-b[0],a[1]-b[1],a[2]-b[2]};};
    const auto cross=[](D a,D b){return D{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};};
    const auto dot=[](D a,D b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
    const auto point=[](Vec3 v){return D{v.x.toDouble()*100.,v.y.toDouble()*100.,v.z.toDouble()*100.};};
    const D origin{double(ray.origin.x),double(ray.origin.y),double(ray.origin.z)};
    const D direction{ray.dxQ20/double(1<<20),ray.dyQ20/double(1<<20),ray.dzQ20/double(1<<20)};
    if(std::abs(dot(direction,direction)-1.)>.008)throw std::invalid_argument("Caverns surface ray direction");
    double best=50000.;bool hit=false;
    for(const auto& tri:patch.chartTriangles){
        const auto a=point(tri.a),u=sub(point(tri.b),a),v=sub(point(tri.c),a);
        const auto h=cross(direction,v);const auto det=dot(u,h);
        if(std::abs(det)<1e-8)continue;
        const auto s=sub(origin,a);const auto b=dot(s,h)/det;
        if(b<0||b>1)continue;
        const auto q=cross(s,u);const auto c=dot(direction,q)/det;
        if(c<0||b+c>1)continue;
        const auto t=dot(v,q)/det;
        if(t>=0&&t<best){best=t;hit=true;}
    }
    if(!hit)return {};
    return PointCm{int64_t(std::llround(origin[0]+direction[0]*best)),int64_t(std::llround(origin[1]+direction[1]*best)),int64_t(std::llround(origin[2]+direction[2]*best))};
}
inline Json describeBodies(const MapData& map){
    Json out=Json::array();
    for(const auto& b:map.bodies){
        const auto kind=b.meta.getStr("kind","planet");
        if(!b.surface()||(kind!="planet"&&kind!="moon"))continue;
        Json j=Json::object();j.set("id",Json::string(b.id));j.set("faceSize",Json::number(int64_t(b.faceSize)));
        j.set("maxExtentCm",Json::number(int64_t((b.meta.getFx("radius",Fixed::fromInt(32))*Fixed::fromRatio(100,8)).floorInt())));
        out.push(std::move(j));
    }
    return out;
}
} // namespace planet_caves
} // namespace aiverse::editor
