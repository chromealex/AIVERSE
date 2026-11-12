#include "engine/render/Lights.h"
#include "engine/render/StaticPointBounce.h"
#include "engine/render/ProbeEnvironment.h"
#include "engine/render/LightGridCache.h"
#include "engine/render/MeshOcclusionCache.h"
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
    auto cache=encodeLightGrid(g,123);auto cached=decodeLightGrid(cache,123);
    assert(cached.texels.size()==g.texels.size());
    for(size_t i=0;i<g.texels.size();++i){auto a=g.texels[i],b=cached.texels[i];assert(a.x==b.x&&a.y==b.y&&a.z==b.z&&a.w==b.w);}
    rejected=false;try{decodeLightGrid(cache,124);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    cache.back()^=1;rejected=false;try{decodeLightGrid(cache,123);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    rejected=false;try{decodeLightGrid(cache.substr(0,60),123);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    room.albedos={{.5f,.5f,.5f}};
    auto sealed=makeDiffuseVolume(room,{-.5f,-.5f,-.5f},{-.5f,-.5f,-.5f},1);
    bakeProbeEnvironment(room,sealed,{0,1,0},64,20);
    for(const auto& probe:sealed.probes)for(int c=0;c<3;++c)assert(probe.sun[size_t(c)].x==0&&probe.sky[size_t(c)].x==0&&probe.skyVisibility.x==0);
    bakeProbeEnvironment(room,sealed,{0,1,0},64,20,3);
    for(const auto& probe:sealed.probes)for(int c=0;c<3;++c)assert(probe.sun[size_t(c)].x==0&&probe.sky[size_t(c)].x==0);
    rejected=false;try{bakeProbeEnvironment(room,sealed,{0,1,0},64,20,5);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    StaticShadowGeometry floor;floor.albedos={{.8f,0,0}};
    floor.triangles={{{-20,0,-20},{20,0,-20},{20,0,20}},{{-20,0,-20},{20,0,20},{-20,0,20}}};floor.build();
    auto sunny=makeDiffuseVolume(floor,{-.5f,.5f,-.5f},{-.5f,.5f,-.5f},1);
    bakeProbeEnvironment(floor,sunny,{0,1,0},64,20);
    assert(sunny.probes[0].sun[0].x>0&&sunny.probes[0].sky[0].x>0&&sunny.probes[0].sun[1].x==0);
    assert(sunny.occludeAmbient&&sunny.probes[0].skyVisibility.x>0);
    auto open=makeDiffuseVolume(empty,{0,0,0},{0,0,0},1);bakeProbeEnvironment(empty,open,{0,1,0},128,20);
    auto visibility=open.probes[0].skyVisibility;
    assert(std::abs(visibility.x-.5f)<.001f&&std::abs(visibility.z-.5f)<.001f);
    g.build(std::vector<PointLight>{},1,nullptr,&sunny);assert(!g.texels.empty());
    std::cout<<"environment probes: open sun, colored bounce, sealed room and cache validation PASS\n";
    field.probes[0].visibility.distances.clear();rejected=false;
    try{g.build(std::vector<PointLight>{white},2,nullptr,&field);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    std::cout<<"one-bounce color transfer, orientation, determinism and black/empty controls PASS\n";
    std::cout<<"static point shadow BVH, cube faces, atlas layout and validation PASS\n";
    std::cout<<"static light grid conservative coverage and validation PASS\n";
    // Transport must move energy into an initially dark receiver without recounting old orders.
    StaticDiffuseField transportField;transportField.nx=2;transportField.ny=transportField.nz=1;transportField.spacing=1;transportField.probes.resize(2);
    for(auto& c:transportField.probes[0].sun)c={1,0,0,0};
    ProbeTransportLink forward;forward.receiver=1;forward.normal={0,1,0};forward.direction={0,0,1};forward.albedo={.5f,.25f,.1f};forward.count=1;forward.sources[0]=0;forward.weights[0]=1;
    std::vector<ProbeTransportLink> links(16,forward);
    auto once=transportField;propagateProbeTransport(once,links,16,1);assert(once.probes[1].sun[0].x==0);
    auto bounced=transportField;propagateProbeTransport(bounced,links,16,3);
    assert(std::abs(bounced.probes[1].sun[0].x-.5f)<1e-6f&&std::abs(bounced.probes[1].sun[1].x-.25f)<1e-6f);
    assert(bounced.probes[0].sun[0].x==1&&bounced.probes[1].sky[0].x==0);
    for(auto& link:links)link.albedo={};
    auto blackTransport=transportField;propagateProbeTransport(blackTransport,links,16,4);assert(blackTransport.probes[1].sun[0].x==0);
    StaticDiffuseField linkField;linkField.spacing=1;linkField.nx=linkField.ny=linkField.nz=1;
    StaticShadowGeometry barrier;barrier.build();links.clear();
    appendProbeTransport(links,barrier,linkField,0,{.25f,.5f,.5f},{1,0,0},{1,0,0},{1,1,1});assert(links.size()==1);
    barrier.triangles={{{.4f,-2,-2},{.4f,2,-2},{.4f,2,2}},{{.4f,-2,-2},{.4f,2,2},{.4f,-2,2}}};barrier.build();links.clear();
    appendProbeTransport(links,barrier,linkField,0,{.25f,.5f,.5f},{1,0,0},{1,0,0},{1,1,1});assert(links.empty());
    // Combining fixed sky/sun must preserve directional RGB and point-light data.
    StaticLightGrid separate;separate.build(std::vector<PointLight>{},1,nullptr,&sunny);
    separate.texels[size_t(separate.texels[0].z)]={.2f,.1f,.3f,.4f};
    auto combined=separate;combined.combineEnvironment({2,3,4},{5,6,7});
    size_t combinedOffset=size_t(combined.texels[0].z);
    for(size_t c=0;c<3;++c){auto a=separate.texels[combinedOffset+100+c],b=separate.texels[combinedOffset+103+c],v=combined.texels[combinedOffset+100+c];
        float x=float(c+2),y=float(c+5);assert(v.x==a.x*x+b.x*y&&v.y==a.y*x+b.y*y&&v.z==a.z*x+b.z*y&&v.w==a.w*x+b.w*y);
    }
    assert((int(combined.texels[0].w)&6)==2);
    rejected=false;try{combined.combineEnvironment({-1,0,0},{});}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    assert(combined.texels[combinedOffset].x==separate.texels[combinedOffset].x);
    std::cout<<"multibounce transport: dark receiver, albedo, bounce orders and wall visibility PASS\n";

    StaticShadowGeometry openAo;
    assert(localDiffuseVisibility(openAo,{0,0,0},{0,1,0},1,64)==1);
    StaticShadowGeometry lidAo;
    lidAo.triangles={{{-10,.1f,-10},{10,.1f,10},{10,.1f,-10}},{{-10,.1f,-10},{-10,.1f,10},{10,.1f,10}}};lidAo.build();
    float covered=localDiffuseVisibility(lidAo,{0,0,0},{0,1,0},1,64);
    assert(covered<.15f&&covered>=0);
    assert(localDiffuseVisibility(lidAo,{0,0,0},{0,-1,0},1,64)==1);
    assert(localDiffuseVisibility(lidAo,{0,0,0},{0,1,0},.05f,64)==1);
    assert(covered==localDiffuseVisibility(lidAo,{0,0,0},{0,1,0},1,64));
    std::cout<<"local AO: open, covered, opposite hemisphere, radius and repeatability PASS\n";

    Model aoModel;aoModel.vertices.resize(3);aoModel.vertices[0].pos={0,0,0};aoModel.vertices[1].pos={1,0,0};aoModel.vertices[2].pos={0,0,1};
    for(auto& v:aoModel.vertices)v.nrm={0,1,0};aoModel.indices={0,1,2};aoModel.materials.resize(1);aoModel.meshes.resize(1);
    Primitive aoPrimitive;aoPrimitive.indexCount=3;aoModel.meshes[0].primitives.push_back(aoPrimitive);
    auto sharedAo=aoModel;sharedAo.meshes.push_back(sharedAo.meshes[0]);
    rejected=false;try{bakeMeshOcclusion(sharedAo,1,64);}catch(const std::runtime_error&){rejected=true;}assert(rejected&&sharedAo.vertices[0].color==0xffffffffu);
    auto untouched=aoModel;auto aoKey=meshOcclusionKey(aoModel,1,64);
    assert(bakeMeshOcclusion(aoModel,1,64)==3&&aoModel.materials[0].vertexOcclusion);
    auto aoBytes=encodeMeshOcclusion(aoModel,aoKey);decodeMeshOcclusion(untouched,aoBytes,aoKey);
    assert(untouched.materials[0].vertexOcclusion&&untouched.vertices[0].color==aoModel.vertices[0].color);
    rejected=false;try{decodeMeshOcclusion(untouched,aoBytes,aoKey+1);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    aoBytes[28]^=1;rejected=false;try{decodeMeshOcclusion(untouched,aoBytes,aoKey);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    auto invalidAo=aoModel;invalidAo.materials[0].vertexOcclusion=false;invalidAo.vertices[0].color=0x80ffffff;
    rejected=false;try{bakeMeshOcclusion(invalidAo,1,64);}catch(const std::runtime_error&){rejected=true;}assert(rejected&&invalidAo.vertices[0].color==0x80ffffff);
    std::cout<<"mesh AO bake/cache: roundtrip, stale/corrupt rejection and alpha preservation PASS\n";

}
