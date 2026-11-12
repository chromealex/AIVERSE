#pragma once
#include "Lights.h"
namespace aiverse::render {
struct StaticBounceSource {PointLight light;float3 normal;};
// One diffuse bounce: deterministic equal-solid-angle rays sample illuminated opaque surfaces.
// The finite-radius engine light convention is retained; this is an approximation, not path tracing.
inline std::vector<StaticBounceSource> bakePointBounce(const StaticShadowGeometry& geometry,
        const std::vector<PointLight>& lights,int samples=16,float radius=5,float strength=1) {
    if(samples<4||samples>32||!std::isfinite(radius)||radius<=0||radius>20||!std::isfinite(strength)||strength<0||strength>2||lights.size()>32)
        throw std::runtime_error("invalid static bounce settings");
    std::vector<StaticBounceSource> result;
    auto decode=[](float c){return c<=.04045f?c/12.92f:std::pow((c+.055f)/1.055f,2.4f);};
    auto encode=[](float c){return c<=.0031308f?12.92f*c:1.055f*std::pow(c,1/2.4f)-.055f;};
    for(const auto& source:lights){
        if(source.spot())throw std::runtime_error("static bounce requires point sources");
        for(int i=0;i<samples;++i){
            float y=1-2*(i+.5f)/samples,phi=i*2.39996323f,r=std::sqrt(std::max(0.0f,1-y*y));
            float3 direction{r*std::cos(phi),y,r*std::sin(phi)},normal,albedo;
            float distance=geometry.distance(source.pos,direction,source.radius,&normal,&albedo);
            if(distance>=source.radius||distance<.05f||dot(normal,normal)<.5f)continue;
            if(dot(normal,direction)>0)normal=normal*-1.0f;
            float falloff=1-distance*distance/(source.radius*source.radius);
            float energy=strength*source.intensity*(4.0f/samples)*falloff*falloff*distance*distance/(1+distance*distance);
            float3 radiance{decode(source.color.x)*albedo.x*energy,decode(source.color.y)*albedo.y*energy,decode(source.color.z)*albedo.z*energy};
            float peak=std::max({radiance.x,radiance.y,radiance.z});if(peak<1e-6f)continue;
            StaticBounceSource bounce;bounce.normal=normal;bounce.light.pos=source.pos+direction*distance+normal*.04f;
            bounce.light.radius=radius;bounce.light.intensity=peak;
            bounce.light.color={encode(radiance.x/peak),encode(radiance.y/peak),encode(radiance.z/peak)};
            result.push_back(bounce);
        }
    }
    return result;
}
inline StaticDiffuseField bakeDiffuseField(const StaticShadowGeometry& geometry,const std::vector<PointLight>& direct,
        const std::vector<StaticBounceSource>& bounce,float spacing=2) {
    if(!std::isfinite(spacing)||spacing<.5f||spacing>4)throw std::runtime_error("invalid diffuse field spacing");
    StaticDiffuseField field;field.spacing=spacing;if(direct.empty()||bounce.empty())return field;
    float3 lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};
    auto bounds=[&](const PointLight& l){lo={std::min(lo.x,l.pos.x-l.radius),std::min(lo.y,l.pos.y-l.radius),std::min(lo.z,l.pos.z-l.radius)};hi={std::max(hi.x,l.pos.x+l.radius),std::max(hi.y,l.pos.y+l.radius),std::max(hi.z,l.pos.z+l.radius)};};
    for(const auto& light:direct)bounds(light);
    for(const auto& source:bounce)bounds(source.light);
    auto extent=(hi-lo)*(1/spacing);
    if(extent.x>100||extent.y>100||extent.z>100)throw std::runtime_error("diffuse field extent exceeds budget");
    field.origin=lo;field.nx=int(std::ceil(extent.x))+1;field.ny=int(std::ceil(extent.y))+1;field.nz=int(std::ceil(extent.z))+1;
    if(size_t(field.nx)*field.ny*field.nz>20000)throw std::runtime_error("diffuse probe budget exceeded");
    field.probes.reserve(size_t(field.nx)*field.ny*field.nz);
    auto decode=[](float c){return c<=.04045f?c/12.92f:std::pow((c+.055f)/1.055f,2.4f);};
    for(int z=0;z<field.nz;++z)for(int y=0;y<field.ny;++y)for(int x=0;x<field.nx;++x){
        auto position=lo+float3{x+.5f,y+.5f,z+.5f}*spacing;StaticDiffuseProbe probe;
        for(const auto& source:bounce){auto delta=source.light.pos-position;float d2=dot(delta,delta);
            if(d2<.01f||d2>=source.light.radius*source.light.radius)continue;
            float distance=std::sqrt(d2);auto direction=delta*(1/distance);
            float emission=std::max(0.0f,dot(source.normal,direction*-1.0f));if(emission<=0)continue;
            if(geometry.distance(position,direction,distance-.06f)<distance-.06f)continue;
            float falloff=1-d2/(source.light.radius*source.light.radius);
            float energy=source.light.intensity*emission*falloff*falloff/(1+d2);
            float rgb[3]={decode(source.light.color.x)*energy,decode(source.light.color.y)*energy,decode(source.light.color.z)*energy};
            for(int c=0;c<3;++c){auto& sh=probe.irradiance[size_t(c)];sh.x+=rgb[c]*.25f;sh.y+=rgb[c]*.5f*direction.x;sh.z+=rgb[c]*.5f*direction.y;sh.w+=rgb[c]*.5f*direction.z;}
        }
        probe.visibility=geometry.bakeMoments(position,spacing*2.5f,4);field.probes.push_back(std::move(probe));
    }
    return field;
}

}
