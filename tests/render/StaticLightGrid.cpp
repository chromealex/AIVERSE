#include "engine/render/Lights.h"
#include "engine/render/StaticPointBounce.h"
#include <cassert>
#include <iostream>
using namespace aiverse::render;
int main(){
    std::vector<PointLight> lights;
    for(int i=0;i<30;++i){PointLight l;l.pos={float(i%5)-2,float((i/5)%3),float(i/15)};l.radius=.7f+float(i%3)*.3f;lights.push_back(l);}
    StaticLightGrid g;g.build(lights,1);
    for(float x=-4;x<4;x+=.17f)for(float y=-2;y<4;y+=.23f)for(float z=-2;z<3;z+=.29f){
        float3 p{x,y,z};int ix=int(std::floor((x-g.origin.x)*g.origin.w)),iy=int(std::floor((y-g.origin.y)*g.origin.w)),iz=int(std::floor((z-g.origin.z)*g.origin.w));
        if(ix<0||iy<0||iz<0||ix>=g.size.x||iy>=g.size.y||iz>=g.size.z)continue;
        auto header=g.texels[size_t(ix+int(g.size.x)*(iy+int(g.size.y)*iz))];
        for(const auto& l:lights)if(dot(p-l.pos,p-l.pos)<l.radius*l.radius){
            bool found=false;for(int i=0;i<int(header.y);++i){auto v=g.texels[size_t(int(header.x)+2*i)];found|=v.x==l.pos.x&&v.y==l.pos.y&&v.z==l.pos.z&&v.w==l.radius;}assert(found);
        }
    }
    bool rejected=false;lights[0].radius=0;try{g.build(lights,1);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    g.build(std::vector<PointLight>{},1);assert(g.texels.empty()&&g.size.x==0);
    StaticShadowGeometry geometry;
    // A finite wall: rays through it are blocked, rays through the surrounding opening pass.
    geometry.triangles={{{-1,-1,2},{1,-1,2},{1,1,2}},{{-1,-1,2},{1,1,2},{-1,1,2}}};
    geometry.build();
    assert(std::abs(geometry.distance({0,0,0},{0,0,1},10)-2)<1e-5f);
    assert(std::abs(geometry.distance({0,0,4},{0,0,-1},10)-2)<1e-5f);
    assert(geometry.distance({2,0,0},{0,0,1},10)==10);
    assert(geometry.distance({0,0,0},{1,0,0},10)==10);
    assert(geometry.distance({0,0,0},{0,0,1},1)==1);
    // Exercise internal BVH nodes with walls at different distances.
    for(int i=0;i<20;++i)geometry.triangles.push_back({{-1,-1,float(3+i)},{1,-1,float(3+i)},{0,1,float(3+i)}});
    geometry.build();assert(std::abs(geometry.distance({0,0,0},{0,0,1},30)-2)<1e-5f);
    auto shadow=geometry.bake({0,0,0},10,16);
    assert(shadow.distances.size()==6*16*16);
    const int center=8*16+8;
    assert(shadow.distances[size_t(4*16*16+center)]>2 && shadow.distances[size_t(4*16*16+center)]<2.02f);
    assert(shadow.distances[size_t(5*16*16+center)]==10);
    std::vector<PointLight> one(1);one[0].radius=10;
    std::vector<StaticPointShadow> maps{shadow};g.build(one,2,&maps);
    int cells=int(g.size.x*g.size.y*g.size.z);bool checked=false;
    for(int cell=0;cell<cells;++cell)if(g.texels[size_t(cell)].y>0){
        int energy=int(g.texels[size_t(cell)].x)+1,offset=int(g.texels[size_t(energy)].w);
        assert(offset>cells&&g.texels[size_t(offset)].x==16);
        assert(g.texels[size_t(offset+1+4*16*16+center)].x==shadow.distances[size_t(4*16*16+center)]);checked=true;
    }
    assert(checked);
    maps[0].distances.pop_back();rejected=false;try{g.build(one,2,&maps);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    StaticShadowGeometry room;
    auto corner=[](int face,float u,float v){auto d=StaticShadowGeometry::cubeDirection(face,u,v);return d*(2/std::max({std::abs(d.x),std::abs(d.y),std::abs(d.z)}));};
    for(int face=0;face<6;++face){auto a=corner(face,-1,-1),b=corner(face,1,-1),c=corner(face,1,1),d=corner(face,-1,1);room.triangles.push_back({a,b,c});room.triangles.push_back({a,c,d});}
    room.build();auto roomMap=room.bake({0,0,0},10,16);
    for(int face=0;face<6;++face)for(int y=0;y<16;++y)for(int x=0;x<16;++x){
        auto ray=StaticShadowGeometry::cubeDirection(face,2*(x+.5f)/16-1,2*(y+.5f)/16-1);
        float expected=2/std::max({std::abs(ray.x),std::abs(ray.y),std::abs(ray.z)});
        assert(std::abs(roomMap.distances[size_t(face*256+y*16+x)]-expected)<1e-4f);
    }
    StaticShadowGeometry redWall;redWall.albedos={{.5f,0,0}};
    redWall.triangles={{{-10,-10,2},{10,-10,2},{10,10,2}},{{-10,-10,2},{10,10,2},{-10,10,2}}};redWall.build();
    PointLight white;white.pos={0,0,0};white.color={1,1,1};white.radius=5;
    auto bounce=bakePointBounce(redWall,{white},16,4,1);assert(!bounce.empty());
    for(const auto& source:bounce){assert(source.light.color.x>.99f&&source.light.color.y==0&&source.light.color.z==0);assert(source.light.pos.z<2&&source.normal.z<-.99f);assert(source.light.intensity>0);}
    auto repeated=bakePointBounce(redWall,{white},16,4,1);assert(repeated.size()==bounce.size());
    for(size_t i=0;i<bounce.size();++i)assert(repeated[i].light.pos.x==bounce[i].light.pos.x&&repeated[i].light.intensity==bounce[i].light.intensity);
    assert(bakePointBounce(redWall,{white},16,4,0).empty());
    StaticShadowGeometry empty;empty.build();assert(bakePointBounce(empty,{white}).empty());
    redWall.albedos[0]={0,0,0};assert(bakePointBounce(redWall,{white}).empty());
    maps={shadow};maps[0].emitterNormal={0,0,-1};g.build(one,2,&maps);
    for(int cell=0;cell<int(g.size.x*g.size.y*g.size.z);++cell)if(g.texels[size_t(cell)].y>0){
        auto meta=g.texels[size_t(int(g.texels[size_t(int(g.texels[size_t(cell)].x)+1)].w))];
        assert(meta.z==1&&g.texels[size_t(int(meta.w))].z==-1);break;
    }
    auto field=bakeDiffuseField(redWall,{white},bounce,2);
    assert(!field.probes.empty()&&field.probes.size()==size_t(field.nx*field.ny*field.nz));
    bool litProbe=false;for(const auto& probe:field.probes)litProbe|=probe.irradiance[0].x>0;
    assert(litProbe);g.build(std::vector<PointLight>{white},2,nullptr,&field);
    assert(g.texels[0].z>0);int probeOffset=int(g.texels[0].z);
    assert(g.texels[size_t(probeOffset+3)].x==4);
    field.probes[0].visibility.distances.clear();rejected=false;
    try{g.build(std::vector<PointLight>{white},2,nullptr,&field);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    std::cout<<"one-bounce color transfer, orientation, determinism and black/empty controls PASS\n";
    std::cout<<"static point shadow BVH, cube faces, atlas layout and validation PASS\n";
    std::cout<<"static light grid conservative coverage and validation PASS\n";
}
