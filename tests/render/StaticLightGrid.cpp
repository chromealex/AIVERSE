#include "engine/render/Lights.h"
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
    std::cout<<"static light grid conservative coverage and validation PASS\n";
}
