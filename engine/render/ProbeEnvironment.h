#pragma once
#include "StaticDiffuseField.h"
#include "ProbeTransport.h"
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
        StaticDiffuseProbe p;p.visibility=geometry.bakeMoments(lo+float3{x+.5f,y+.5f,z+.5f}*spacing,spacing*2.5f,4);f.probes.push_back(std::move(p));
    }
    return f;
}
// Sky irradiance of a surface exactly as the lit shaders show it, for one unit of sky radiance:
// visibility-tested probe sky visibility (clamped first-order SH / hemisphere normalization),
// times the sky half of the shader's hemisphere mix. The ground half is not transported.
inline float runtimeSkyIrradiance(const StaticShadowGeometry& geometry,const StaticDiffuseField& f,float3 position,float3 n) {
    // Outside the probe volume there is no shader visibility to mirror: trace the sky directly.
    const auto location=(position-f.origin)*(1/f.spacing);
    if(location.x<0||location.y<0||location.z<0||location.x>=f.nx||location.y>=f.ny||location.z>=f.nz){
        float sky=0;constexpr int kRays=32;
        for(int j=0;j<kRays;++j){float yy=(j+.5f)/kRays,angle=j*2.39996323f,rr=std::sqrt(1-yy*yy);float3 d{rr*std::cos(angle),yy,rr*std::sin(angle)};
            if(geometry.distance(position,d,1000)>=1000)sky+=std::max(0.0f,dot(n,d))*(2.0f/kRays);}
        return sky;
    }
    auto coordinate=(position-f.origin)*(1/f.spacing)-float3{.5f,.5f,.5f};
    const int bx=int(std::floor(coordinate.x)),by=int(std::floor(coordinate.y)),bz=int(std::floor(coordinate.z));
    const auto fraction=coordinate-float3{float(bx),float(by),float(bz)};
    float total=0,visible=0;
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x){
        int px=bx+x,py=by+y,pz=bz+z;
        if(px<0||py<0||pz<0||px>=f.nx||py>=f.ny||pz>=f.nz)continue;
        float weight=(x?fraction.x:1-fraction.x)*(y?fraction.y:1-fraction.y)*(z?fraction.z:1-fraction.z);
        if(weight<1e-6f)continue;
        auto probe=f.origin+float3{px+.5f,py+.5f,pz+.5f}*f.spacing;auto delta=probe-position;float d=length(delta);
        if(d>.001f&&geometry.distance(position,delta*(1/d),d)<d-.03f)continue;
        const auto sv=f.probes[size_t(px+f.nx*(py+f.ny*pz))].skyVisibility;
        visible+=weight*std::clamp((sv.x+sv.y*n.x+sv.z*n.y+sv.w*n.z)/std::max(.1f,.5f+.5f*n.y),0.0f,1.0f);total+=weight;
    }
    return total>1e-6f?visible/total*(.5f+.5f*n.y):0.0f;
}
// skyBounce=runtime: bounced sky uses runtimeSkyIrradiance at each hit, in the same no-1/pi display
// convention as the sun term, so runtime probe sky radiance equals the linear sky colour. The legacy
// estimator traces sky rays from each hit (physically stricter than the shaders' probe visibility).
inline void bakeProbeEnvironment(const StaticShadowGeometry& geometry,StaticDiffuseField& f,float3 sunDirection,int samples=32,float distance=50,int bounces=1,bool runtimeSky=false) {
    if(bounces<1||bounces>4||samples<16||samples>128||!std::isfinite(distance)||distance<=0||distance>1000)throw std::runtime_error("invalid environment probe settings");
    float length2=dot(sunDirection,sunDirection);
    if(!std::isfinite(length2)||length2<1e-6f)throw std::runtime_error("invalid probe sun direction");
    sunDirection=normalize(sunDirection);f.occludeAmbient=true;
    std::vector<ProbeTransportLink> transport;
    auto direction=[&](int i){float sy=1-2*(i+.5f)/samples,phi=i*2.39996323f,r=std::sqrt(std::max(0.0f,1-sy*sy));return float3{r*std::cos(phi),sy,r*std::sin(phi)};};
    if(runtimeSky)for(int z=0;z<f.nz;++z)for(int y=0;y<f.ny;++y)for(int x=0;x<f.nx;++x){ // sky visibility first: hits read it
        auto& sh=f.probes.at(size_t(x+f.nx*(y+f.ny*z))).skyVisibility;sh={};
        const auto origin=f.origin+float3{x+.5f,y+.5f,z+.5f}*f.spacing;
        for(int i=0;i<samples;++i){auto dir=direction(i);
            if(dir.y>0&&geometry.distance(origin,dir,distance)>=distance){float energy=1.0f/samples;sh.x+=energy;sh.y+=2*energy*dir.x;sh.z+=2*energy*dir.y;sh.w+=2*energy*dir.z;}}
    }
    for(int z=0;z<f.nz;++z)for(int y=0;y<f.ny;++y)for(int x=0;x<f.nx;++x){
        auto& probe=f.probes.at(size_t(x+f.nx*(y+f.ny*z)));probe.sun={};probe.sky={};if(!runtimeSky)probe.skyVisibility={};
        const auto origin=f.origin+float3{x+.5f,y+.5f,z+.5f}*f.spacing;
        for(int i=0;i<samples;++i){
            float sy=1-2*(i+.5f)/samples,phi=i*2.39996323f,r=std::sqrt(std::max(0.0f,1-sy*sy));
            float3 dir{r*std::cos(phi),sy,r*std::sin(phi)},n,albedo;
            float hit=geometry.distance(origin,dir,distance,&n,&albedo);
            if(hit>=distance){
                if(dir.y>0&&!runtimeSky){auto& sh=probe.skyVisibility;float energy=1.0f/samples;sh.x+=energy;sh.y+=2*energy*dir.x;sh.z+=2*energy*dir.y;sh.w+=2*energy*dir.z;}
                continue;
            }
            if(hit<.05f||dot(n,n)<.5f)continue;
            if(dot(n,dir)>0)n=n*-1.0f;
            auto surface=origin+dir*hit+n*.04f;
            if(bounces>1)appendProbeTransport(transport,geometry,f,uint32_t(x+f.nx*(y+f.ny*z)),surface,n,dir,albedo);
            float sun=std::max(0.0f,dot(n,sunDirection));
            if(sun>0&&geometry.distance(surface,sunDirection,distance)<distance)sun=0;
            // Resolve narrow sky openings before propagating energy into covered areas.
            float sky=0;
            const int skySamples=runtimeSky?0:bounces>1?32:4;
            if(runtimeSky)sky=runtimeSkyIrradiance(geometry,f,surface,n);
            for(int j=0;j<skySamples;++j){float yy=(j+.5f)/skySamples,angle=j*2.39996323f,rr=std::sqrt(1-yy*yy);float3 d{rr*std::cos(angle),yy,rr*std::sin(angle)};
                if(geometry.distance(surface,d,distance)>=distance)sky+=std::max(0.0f,dot(n,d))*(2.0f/skySamples);
            }
            float colors[3]={albedo.x,albedo.y,albedo.z};
            for(int c=0;c<3;++c){auto add=[&](float4& sh,float energy){sh.x+=energy;sh.y+=2*energy*dir.x;sh.z+=2*energy*dir.y;sh.w+=2*energy*dir.z;};
                add(probe.sun[size_t(c)],colors[c]*sun/samples);add(probe.sky[size_t(c)],colors[c]*sky*(runtimeSky?1.0f:3.14159265f)/samples);
            }
        }
    }
    if(bounces>1)propagateProbeTransport(f,transport,samples,bounces);
}
}
