#pragma once
// Presentation (float OK, never fed back into the sim): ground triangles for a TelegraphView, the way Drone Fall's
// TelegraphProgressCircle/Cone/Line modules draw a background shape plus a fill that grows with the windup
// (circle and ring fill outward from the centre, cone from the apex, line from its start). Vertices are
// x, y, z, rgba (0xRRGGBBAA) in world units: one ViewGroundVertex each for RtsGroundCanvas, or a Lua
// view.draw_ground() vertex list (docs/scripting.md). Own and enemy colours as TelegraphColorConfig.
#include "Telegraphs.h"
#include <cmath>
#include <cstdint>
#include <vector>
namespace aiverse::lib::genre {
struct DecalVertex {float x=0,y=0,z=0;uint32_t rgba=0xffffffffu;};
struct TelegraphColors {uint32_t ownBack=0x3366ff40u,ownFill=0x3366ffa0u,enemyBack=0xff333340u,enemyFill=0xff3333b0u;};
namespace detail {
inline float decalF(Fixed v){return float(v.toDouble());}
inline void decalSector(std::vector<DecalVertex>& out,float cx,float cz,float y,float yaw,float half,float r0,float r1,uint32_t c,int segments){
    // yaw in turns, 0 faces +Z, +X right: direction (sin, cos).
    const float tau=6.28318530718f;int n=std::max(3,int(std::ceil(segments*half*2.f)));
    for(int i=0;i<n;++i){
        float a0=(yaw-half+2*half*float(i)/float(n))*tau,a1=(yaw-half+2*half*float(i+1)/float(n))*tau;
        DecalVertex p00{cx+std::sin(a0)*r0,y,cz+std::cos(a0)*r0,c},p01{cx+std::sin(a1)*r0,y,cz+std::cos(a1)*r0,c};
        DecalVertex p10{cx+std::sin(a0)*r1,y,cz+std::cos(a0)*r1,c},p11{cx+std::sin(a1)*r1,y,cz+std::cos(a1)*r1,c};
        if(r0>0.f){out.insert(out.end(),{p00,p10,p11,p00,p11,p01});}else out.insert(out.end(),{p00,p10,p11});
    }
}
inline void decalRect(std::vector<DecalVertex>& out,float ox,float oz,float y,float yaw,float from,float to,float half,uint32_t c){
    const float tau=6.28318530718f;float fx=std::sin(yaw*tau),fz=std::cos(yaw*tau),rx=fz,rz=-fx;
    auto at=[&](float f,float s){return DecalVertex{ox+fx*f+rx*s,y,oz+fz*f+rz*s,c};};
    auto a=at(from,-half),b=at(from,half),d=at(to,half),e=at(to,-half);out.insert(out.end(),{a,b,d,a,d,e});
}
}
// groundY: height of the decal (a few hundredths above the ground); own: the viewer owns the caster.
inline std::vector<DecalVertex> telegraphDecal(const TelegraphView& v,float groundY,bool own,const TelegraphColors& colors={},int segments=48){
    std::vector<DecalVertex> out;if(!v.live||v.phase==AttackPhase::Idle)return out;
    using namespace detail;auto back=own?colors.ownBack:colors.enemyBack,fill=own?colors.ownFill:colors.enemyFill;
    float x=decalF(v.pose.origin.x),z=decalF(v.pose.origin.y),yaw=decalF(v.pose.yaw),t=float(v.progressMilli)/1000.f;
    float fillY=groundY+0.005f;
    switch(v.shape){
    case AreaShape::Circle:decalSector(out,x,z,groundY,0,.5f,0,decalF(v.radius),back,segments);if(t>0)decalSector(out,x,z,fillY,0,.5f,0,decalF(v.radius)*t,fill,segments);break;
    case AreaShape::Ring:{float r0=decalF(v.inner),r1=decalF(v.radius);decalSector(out,x,z,groundY,0,.5f,r0,r1,back,segments);if(t>0)decalSector(out,x,z,fillY,0,.5f,r0,r0+(r1-r0)*t,fill,segments);break;}
    case AreaShape::Cone:decalSector(out,x,z,groundY,yaw,decalF(v.halfAngle),0,decalF(v.radius),back,segments);if(t>0)decalSector(out,x,z,fillY,yaw,decalF(v.halfAngle),0,decalF(v.radius)*t,fill,segments);break;
    case AreaShape::Line:{float len=decalF(v.length),hw=decalF(v.halfWidth);decalRect(out,x,z,groundY,yaw,0,len,hw,back);if(t>0)decalRect(out,x,z,fillY,yaw,0,len*t,hw,fill);break;}
    }
    return out;
}
}
