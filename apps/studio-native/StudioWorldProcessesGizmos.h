// World Processes authoring gizmos through Studio's existing OverlayShape API.
// Pure read-only MapDoc projection: no ECS, no duplicate renderer/physics,
// no geometry mutation, and no separate Studio plugin/window.
#pragma once
#include "apps/studio-native/StudioScene.h"
#include "apps/editor/core/MapDoc.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aiverse::studio::worldprocess_gizmos {

using Shape=StudioScene::OverlayShape;
inline constexpr std::size_t kShapeBudget=1200;
inline Shape makePoint(float x,float y,float z,
                       std::uint8_t r,std::uint8_t g,std::uint8_t b,
                       float radius=5.f){
    Shape o;o.kind=3;o.a={x,y,z};o.radius=radius;
    o.r=r;o.g=g;o.bl=b;return o;
}
inline Shape makeLine(render::float3 a,render::float3 b,
                      std::uint8_t r,std::uint8_t g,std::uint8_t blue,
                      float thickness=2.f){
    Shape o;o.kind=1;o.a=a;o.b=b;o.thickness=thickness;
    o.r=r;o.g=g;o.bl=blue;return o;
}
inline Shape makeCircle(render::float3 a,float radius,
                        std::uint8_t r,std::uint8_t g,std::uint8_t blue){
    Shape o=makePoint(a.x,a.y,a.z,r,g,blue);
    o.kind=2;o.radius=radius;return o;
}
inline Shape makeLabel(render::float3 a,std::string name,
                       std::uint8_t r,std::uint8_t g,std::uint8_t blue){
    Shape o=makePoint(a.x,a.y,a.z,r,g,blue);
    o.kind=4;o.text=std::move(name);return o;
}
inline void add(std::vector<Shape>& result,Shape shape){
    if(result.size()<kShapeBudget)result.push_back(std::move(shape));
}
inline float capped(const Json& j,float fallback,float low,float high){
    const double value=j.toDouble(double(fallback));
    return std::isfinite(value)?std::clamp(float(value),low,high):fallback;
}
inline render::float3 location(const editor::DocEntity& e,float y=.18f){
    return {std::clamp(e.x,-100000.f,100000.f),y,
            std::clamp(e.z,-100000.f,100000.f)};
}
inline void rectangle(std::vector<Shape>& out,render::float3 center,
                      float width,float depth,
                      std::uint8_t r,std::uint8_t g,std::uint8_t b){
    const float w=std::clamp(width,.05f,1000.f)*.5f;
    const float h=std::clamp(depth,.05f,1000.f)*.5f;
    const render::float3 a{center.x-w,center.y,center.z-h};
    const render::float3 c{center.x+w,center.y,center.z-h};
    const render::float3 d{center.x+w,center.y,center.z+h};
    const render::float3 e{center.x-w,center.y,center.z+h};
    add(out,makeLine(a,c,r,g,b));add(out,makeLine(c,d,r,g,b));
    add(out,makeLine(d,e,r,g,b));add(out,makeLine(e,a,r,g,b));
}

// Unlike StudioScene's built-in circle (screen-space pixel radius), this
// ring is a true world-space contour so it matches the editable radius handle.
inline void worldRing(std::vector<Shape>& shapes,render::float3 center,
                      float radius,std::uint8_t r,std::uint8_t g,
                      std::uint8_t b){
    constexpr int sides=24;
    radius=std::clamp(radius,.5f,500.f);
    constexpr float tau=6.28318530718f;
    for(int i=0;i<sides;++i){
        const float angle=tau*float(i)/float(sides);
        const float next=tau*float(i+1)/float(sides);
        const render::float3 a{center.x+radius*std::cos(angle),
                               center.y,center.z+radius*std::sin(angle)};
        const render::float3 z{center.x+radius*std::cos(next),
                               center.y,center.z+radius*std::sin(next)};
        add(shapes,makeLine(a,z,r,g,b,1.5f));
    }
}
inline std::vector<Shape> collect(const editor::MapDoc& document,
                                  std::string_view selectedId={}){
    std::vector<Shape> out;
    const auto& entities=document.data.entities;
    if(entities.size()>65536)return out;
    std::map<std::string,const editor::DocEntity*> ids;
    for(const auto& e:entities)
        if(!e.id.empty())ids.try_emplace(e.id,&e);
    const auto resolve=[&](const Json& doc,const char* key)
        ->const editor::DocEntity* {
        const auto id=doc.getStr(key);
        const auto it=ids.find(id);
        return it==ids.end()?nullptr:it->second;
    };
    const auto link=[&](const Json& doc,const char* a,const char* b,
                        std::uint8_t r,std::uint8_t g,std::uint8_t blue){
        if(const auto* ea=resolve(doc,a))
            if(const auto* eb=resolve(doc,b)){
                add(out,makeLine(location(*ea,.32f),location(*eb,.32f),
                                 r,g,blue,2.f));
                return;
            }
    };
    const auto single=[&](const editor::DocEntity& e,const Json& doc,
                          const char* target,std::uint8_t r,
                          std::uint8_t g,std::uint8_t blue){
        if(const auto* t=resolve(doc,target))
            add(out,makeLine(location(e),location(*t),r,g,blue,1.5f));
    };
    for(const auto& e:entities){
        if(out.size()+24>=kShapeBudget)break;
        if(!selectedId.empty()&&selectedId!="*"&&selectedId!=e.id)
            continue;
        const auto& components=e.extra["components"];
        if(!components.isObject())continue;
        const auto pos=location(e);
        if(const auto& c=components["MorphNode"];c.isObject()){
            add(out,makePoint(pos.x,.45f,pos.z,90,220,255,6));
            if(c.getBool("anchored"))add(out,makeCircle(pos,9,110,220,255));
        }
        if(const auto& c=components["MorphSpring"];c.isObject())
            link(c,"a","b",60,210,245);
        if(const auto& c=components["MorphForce"];c.isObject())
            single(e,c,"node",60,210,245);
        if(const auto& c=components["CrowdAgent"];c.isObject()){
            const auto goal=render::float3{
                capped(c["goalX"],0,-100000,100000)/100.f,.35f,
                capped(c["goalZ"],0,-100000,100000)/100.f
            };
            add(out,makeLine(pos,goal,100,240,155));
            add(out,makeCircle(goal,7,100,240,155));
        }
        if(const auto& c=components["CrowdSlot"];c.isObject())
            add(out,makePoint(pos.x,.45f,pos.z,100,240,155,5));
        if(const auto& c=components["CrowdPreference"];c.isObject()){
            single(e,c,"agent",100,240,155);
            for(const auto* key:{"slot0","slot1","slot2","slot3"})
                single(e,c,key,100,200,155);
        }
        if(const auto& c=components["ImprintCell"];c.isObject()){
            const float amount=capped(c["footprint"],0,0,1000000)+
                               capped(c["scent"],0,0,1000000);
            add(out,makeCircle(pos,std::clamp(amount/10000.f+5.f,5.f,35.f),
                               255,174,82));
        }
        if(const auto& c=components["ImprintLink"];c.isObject())
            link(c,"a","b",255,174,82);
        if(const auto& c=components["ImprintDeposit"];c.isObject())
            single(e,c,"cell",255,174,82);
        if(const auto& c=components["ImprintTracker"];c.isObject()){
            single(e,c,"source",255,190,95);
            single(e,c,"cell",255,174,82);
        }
        if(const auto& c=components["CircuitNode"];c.isObject())
            add(out,makePoint(pos.x,.55f,pos.z,255,222,75,6));
        if(const auto& c=components["CircuitWire"];c.isObject())
            link(c,"from","to",255,222,75);
        if(const auto& c=components["CircuitGate"];c.isObject()){
            single(e,c,"inputA",255,222,75);
            single(e,c,"inputB",255,222,75);
            single(e,c,"output",255,246,140);
        }
        if(const auto& c=components["CircuitSwitch"];c.isObject())
            single(e,c,"node",255,222,75);
        if(const auto& c=components["CircuitDevice"];c.isObject()){
            single(e,c,"node",255,222,75);
            add(out,makeCircle(pos,8,255,222,75));
        }
        if(const auto& c=components["GranularField"];c.isObject()){
            const float gridX=capped(c["width"],1,1,128);
            const float gridZ=capped(c["height"],1,1,128);
            const float cell=capped(e.extra["worldProcessGizmo"]["cellSizeMetres"],
                                    1,.05f,32.f);
            rectangle(out,pos,gridX*cell,gridZ*cell,220,164,96);
            add(out,makeLabel({pos.x,.65f,pos.z},"GRANULAR",220,164,96));
        }
        if(const auto& c=components["GranularSource"];c.isObject())
            single(e,c,"field",220,164,96);
        if(const auto& c=components["EvolveArea"];c.isObject()){
            const float halfWidth=capped(c["halfWidthCm"],1000,50,50000)/100.f;
            const float halfDepth=capped(c["halfDepthCm"],1000,50,50000)/100.f;
            const render::float3 areaCenter={pos.x+float(c.getInt("offsetXcm",0))/100.f,
                                               pos.y,pos.z+float(c.getInt("offsetZcm",0))/100.f};
            rectangle(out,areaCenter,halfWidth*2.f,halfDepth*2.f,138,190,255);
            add(out,makeLabel({pos.x,.65f,pos.z},"EVOLVE AREA",138,190,255));
        }else if(const auto& c=components["EvolveRegion"];c.isObject()){
            // Back-compat visualization only; add EvolveArea for actual gameplay
            // TerraWeave forest climate bounds.
            const float radius=capped(
                e.extra["worldProcessGizmo"]["radiusMetres"],10,.5f,500.f);
            worldRing(out,pos,radius,138,190,255);
            add(out,makeLabel({pos.x,.65f,pos.z},"EVOLVE PREVIEW",138,190,255));
        }
        if(const auto& c=components["EvolvePressure"];c.isObject())
            single(e,c,"region",138,190,255);
        if(const auto& c=components["SocietyPerson"];c.isObject())
            add(out,makePoint(pos.x,.45f,pos.z,220,130,245,5));
        if(const auto& c=components["SocietyContact"];c.isObject())
            link(c,"source","target",220,130,245);
        if(const auto& c=components["SocietyMessage"];c.isObject())
            single(e,c,"source",220,130,245);
        if(const auto& c=components["SocietyNpcTrigger"];c.isObject())
            single(e,c,"person",220,130,245);
    }
    return out;
}
} // namespace aiverse::studio::worldprocess_gizmos
