#pragma once
#include "StaticDiffuseField.h"
namespace aiverse::render {
// Offline transport for a fixed sun direction and uniform hemispherical sky.
// Runtime color/intensity changes only modify uniforms; geometry/direction changes need a rebake.
inline StaticDiffuseField makeDiffuseVolume(const StaticShadowGeometry& geometry,float3 lo,float3 hi,float spacing) {
    if(!std::isfinite(spacing)||spacing<.5f||spacing>4)throw std::runtime_error("invalid probe spacing");
    auto e=(hi-lo)*(1/spacing);
    for(float c:{lo.x,lo.y,lo.z,e.x,e.y,e.z})if(!std::isfinite(c))throw std::runtime_error("invalid probe bounds");
    if(e.x<0||e.y<0||e.z<0||e.x>100||e.y>100||e.z>100)throw std::runtime_error("probe extent exceeds budget");
    StaticDiffuseField f;f.origin=lo;f.spacing=spacing;f.nx=int(std::ceil(e.x))+1;f.ny=int(std::ceil(e.y))+1;f.nz=int(std::ceil(e.z))+1;
    if(size_t(f.nx)*f.ny*f.nz>20000)throw std::runtime_error("probe count exceeds budget");
    for(int z=0;z<f.nz;++z)for(int y=0;y<f.ny;++y)for(int x=0;x<f.nx;++x){
        StaticDiffuseProbe p;p.visibility=geometry.bake(lo+float3{x+.5f,y+.5f,z+.5f}*spacing,spacing*2.5f,4);f.probes.push_back(std::move(p));
    }
    return f;
}
inline void bakeProbeEnvironment(const StaticShadowGeometry& geometry,StaticDiffuseField& f,float3 sunDirection,int samples=32,float distance=50) {
    if(samples<16||samples>128||!std::isfinite(distance)||distance<=0||distance>1000)throw std::runtime_error("invalid environment probe settings");
    float length2=dot(sunDirection,sunDirection);
    if(!std::isfinite(length2)||length2<1e-6f)throw std::runtime_error("invalid probe sun direction");
    sunDirection=normalize(sunDirection);f.occludeAmbient=true;
    for(int z=0;z<f.nz;++z)for(int y=0;y<f.ny;++y)for(int x=0;x<f.nx;++x){
        auto& probe=f.probes.at(size_t(x+f.nx*(y+f.ny*z)));probe.sun={};probe.sky={};probe.skyVisibility={};
        const auto origin=f.origin+float3{x+.5f,y+.5f,z+.5f}*f.spacing;
        for(int i=0;i<samples;++i){
            float sy=1-2*(i+.5f)/samples,phi=i*2.39996323f,r=std::sqrt(std::max(0.0f,1-sy*sy));
            float3 dir{r*std::cos(phi),sy,r*std::sin(phi)},n,albedo;
            float hit=geometry.distance(origin,dir,distance,&n,&albedo);
            if(hit>=distance){
                if(dir.y>0){auto& sh=probe.skyVisibility;float energy=1.0f/samples;sh.x+=energy;sh.y+=2*energy*dir.x;sh.z+=2*energy*dir.y;sh.w+=2*energy*dir.z;}
                continue;
            }
            if(hit<.05f||dot(n,n)<.5f)continue;
            if(dot(n,dir)>0)n=n*-1.0f;
            auto surface=origin+dir*hit+n*.04f;
            float sun=std::max(0.0f,dot(n,sunDirection));
            if(sun>0&&geometry.distance(surface,sunDirection,distance)<distance)sun=0;
            // Four deterministic upper-hemisphere visibility rays. Only diffuse low frequencies.
            float sky=0;
            for(int j=0;j<4;++j){float yy=(j+.5f)/4,angle=j*2.39996323f,rr=std::sqrt(1-yy*yy);float3 d{rr*std::cos(angle),yy,rr*std::sin(angle)};
                if(geometry.distance(surface,d,distance)>=distance)sky+=std::max(0.0f,dot(n,d))*.5f;
            }
            float colors[3]={albedo.x,albedo.y,albedo.z};
            for(int c=0;c<3;++c){auto add=[&](float4& sh,float energy){sh.x+=energy;sh.y+=2*energy*dir.x;sh.z+=2*energy*dir.y;sh.w+=2*energy*dir.z;};
                add(probe.sun[size_t(c)],colors[c]*sun/samples);add(probe.sky[size_t(c)],colors[c]*sky*3.14159265f/samples);
            }
        }
    }
}
}
