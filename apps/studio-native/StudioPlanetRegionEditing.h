// Spherical EVOLVE authoring: read-only chart and drag calculations.
#pragma once
#include "apps/studio-native/StudioPlanetProcessOverlays.h"

namespace aiverse::studio {
enum class PlanetRegionHandle { West, East, North, South, Center };
struct PlanetRegionChart {
    std::string owner,body;
    Vec3 up{},east{},south{};
    Fixed radius{};
    evolve::Area area{};
};
inline std::optional<PlanetRegionChart> planetRegionChart(
        const editor::MapDoc& doc,const StudioPlanetPreview& preview,const std::string& id){
    if(id.rfind("entity:@",0)!=0)return {};
    const int index=doc.findEntity(id.substr(8)),bi=doc.data.findBody(preview.bodyId);
    if(index<0||bi<0||!preview.placements.contains(id))return {};
    const auto& entity=doc.data.entities[size_t(index)];
    const auto& body=doc.data.bodies[size_t(bi)];
    const auto& components=entity.extra["components"];
    const auto& a=components["EvolveArea"];
    if(!components["EvolveRegion"].isObject()||!a.isObject()||
       !a.getBool("enabled",true)||entity.at.getStr("body")!=body.id)return {};
    PlanetRegionChart chart;chart.owner=id;chart.body=body.id;
    chart.radius=StudioPlanetPreview::fixed(preview.radius);
    chart.area={a.getInt("offsetXcm",0),a.getInt("offsetZcm",0),
        a.getInt("halfWidthCm",1000),a.getInt("halfDepthCm",1000),true};
    if(!evolve::valid(chart.area)||chart.radius.raw<=0)return {};
    game::CubeSphere cube(body.faceSize);
    chart.up=cube.dirFromFace(int(entity.at.getInt("face")),entity.at["x"].fx(),entity.at["y"].fx());
    int face;Fixed x,z;cube.faceFromDir(chart.up,face,x,z);
    game::CubeSphere::faceTangents(face,chart.up,chart.east,chart.south);
    return chart;
}
inline std::optional<Vec2> planetRegionCoordinates(const PlanetRegionChart& chart,Vec3 direction){
    direction=fxNormalize(direction);
    if(direction.lengthSq().raw<=0)return {};
    const auto turns=game::sphereArcTurns(chart.up,direction);
    if(turns>=Fixed::fromRatio(499,1000))return {};
    if(turns.raw==0)return Vec2{};
    const auto tangent=game::sphereTangentToward(chart.up,direction);
    const auto length=turns*game::kFxTwoPi*chart.radius;
    return Vec2{tangent.dot(chart.east)*length,tangent.dot(chart.south)*length};
}
inline std::pair<int64_t,int64_t> planetRegionHandleCoordinates(
        const evolve::Area& a,PlanetRegionHandle handle){
    auto x=a.centerXcm,z=a.centerZcm;
    if(handle==PlanetRegionHandle::West)x-=a.halfWidthCm;
    if(handle==PlanetRegionHandle::East)x+=a.halfWidthCm;
    if(handle==PlanetRegionHandle::North)z-=a.halfDepthCm;
    if(handle==PlanetRegionHandle::South)z+=a.halfDepthCm;
    return {x,z};
}
inline std::optional<render::float3> planetRegionHandlePosition(
        const editor::DocBody& body,const PlanetRegionChart& chart,int64_t x,int64_t z,float lift){
    const auto direction=planet_overlay_detail::regionDirection(chart.up,chart.east,chart.south,
        chart.radius,Fixed::fromRatio(x,100),Fixed::fromRatio(z,100));
    if(!direction)return {};
    const game::CubeSphere cube(body.faceSize);
    return planet_overlay_detail::surfacePoint(body,cube,*direction,float(chart.radius.toDouble()),lift);
}
// One pure edit computation shared by preview and final RPC payload.
// Offset is in authored local centimetres, never global X/Z.
inline std::optional<evolve::Area> planetRegionDragArea(const PlanetRegionChart& chart,
        PlanetRegionHandle handle,Vec2 coordinates,bool symmetric,bool snap){
    auto next=chart.area;
    const auto quantize=[&](Fixed v){
        const double cm=v.toDouble()*100.;
        return int64_t(std::llround(snap?std::round(cm/100.)*100.:cm));
    };
    const int64_t x=quantize(coordinates.x),z=quantize(coordinates.y);
    if(handle==PlanetRegionHandle::Center){next.centerXcm=x;next.centerZcm=z;}
    else{
        const bool xAxis=handle==PlanetRegionHandle::West||handle==PlanetRegionHandle::East;
        const bool positive=handle==PlanetRegionHandle::East||handle==PlanetRegionHandle::South;
        auto& center=xAxis?next.centerXcm:next.centerZcm;
        auto& half=xAxis?next.halfWidthCm:next.halfDepthCm;
        const int64_t value=xAxis?x:z;
        if(symmetric){
            const int64_t extent=positive?value-center:center-value;
            half=std::clamp<int64_t>(extent,50,evolve::kAreaMaxHalfExtentCm);
        }else{
            const int64_t opposite=positive?center-half:center+half;
            int64_t span=std::clamp<int64_t>(positive?value-opposite:opposite-value,100,2*evolve::kAreaMaxHalfExtentCm);
            span-=span%2;half=span/2;center=opposite+(positive?half:-half);
        }
    }
    if(!evolve::valid(next))return {};
    // Entire rectangle must stay in the invertible portion of the chart.
    for(int sx:{-1,1})for(int sz:{-1,1})
        if(!planet_overlay_detail::regionDirection(chart.up,chart.east,chart.south,chart.radius,
            Fixed::fromRatio(next.centerXcm+sx*next.halfWidthCm,100),
            Fixed::fromRatio(next.centerZcm+sz*next.halfDepthCm,100)))return {};
    return next;
}
inline Json planetRegionEditPayload(const PlanetRegionChart& chart,const evolve::Area& next,uint64_t revision){
    Json p=Json::object();p.set("entityId",Json::string(chart.owner));
    p.set("componentId",Json::string("EvolveArea"));p.set("key",Json::string("bounds"));
    p.set("expectedRevision",Json::number(int64_t(revision)));
    Json v=Json::object();
    v.set("offsetXcm",Json::number(next.centerXcm));v.set("offsetZcm",Json::number(next.centerZcm));
    v.set("halfWidthCm",Json::number(next.halfWidthCm));v.set("halfDepthCm",Json::number(next.halfDepthCm));
    p.set("value",std::move(v));return p;
}
} // namespace aiverse::studio
