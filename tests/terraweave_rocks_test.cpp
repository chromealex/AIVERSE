#include "engine/render/TerraWeaveRockModel.h"
#include <cassert>
#include <set>
#include <bit>
#include <fstream>
#include <sstream>
#include <algorithm>
using namespace aiverse::terraweave;
int main(int argc,char** argv){
    using namespace rock_detail;
    std::set<uint64_t> signatures;
    for(auto shape:{RockShape::Rounded,RockShape::Boulder,RockShape::Slab,RockShape::Shard})for(uint64_t seed:{1,2,42}){
        RockProfile p;p.shape=shape;auto m=makeRockModel(p,seed),again=makeRockModel(p,seed);
        assert(m->meshes.size()==3 && m->meshes[0].lods.size()==2 && m->nodes.size()==1);
        for(size_t tier=0;tier<3;++tier){const auto& prim=m->meshes[tier].primitives[0];assert(prim.indexCount/3==(tier==0?1280u:tier==1?320u:80u));
            for(uint32_t i=prim.firstIndex;i<prim.firstIndex+prim.indexCount;i+=3){
                const auto a=m->vertices[m->indices[i]],b=m->vertices[m->indices[i+1]],c=m->vertices[m->indices[i+2]];
                const auto n=rcross(b.pos-a.pos,c.pos-a.pos);assert(rdot(n,n)>1e-14f);assert(rdot(n,a.nrm+b.nrm+c.nrm)>0);
                // texel density: UV area per world area stays within the box-mapping range (no pinched poles)
                const float uvArea=std::abs((b.uv.x-a.uv.x)*(c.uv.y-a.uv.y)-(c.uv.x-a.uv.x)*(b.uv.y-a.uv.y));
                const float density=uvArea/std::sqrt(rdot(n,n))*float(p.textureRepeatCm*p.textureRepeatCm)/10000.f;
                assert(density>.55f && density<1.01f);
            }
        }
        for(size_t i=0;i<m->vertices.size();++i){const auto& v=m->vertices[i];assert(std::isfinite(v.uv.x)&&std::isfinite(v.uv.y));assert(std::abs(rdot(v.nrm,v.nrm)-1)<.002f);assert(v.pos.x==again->vertices[i].pos.x&&v.pos.y==again->vertices[i].pos.y);}
        uint64_t signature=0;for(const auto& v:m->vertices)signature=detail::mix64(signature^std::bit_cast<uint32_t>(v.pos.x)^uint64_t(std::bit_cast<uint32_t>(v.pos.y))<<32);signatures.insert(signature);
        RockInstance at;at.radiusCm=160;at.groundCm=125;at.burialPermille=180;auto pose=rockPose(*m,at);
        assert(m->boundsMin.y*pose.scale+pose.position.y<1.25f);
        assert(m->boundsMax.y*pose.scale+pose.position.y>1.25f);
        for(const auto& mesh:m->meshes)assert(mesh.boundsMin.y*pose.scale+pose.position.y<1.25f);
        for(const auto& v:m->vertices){float3 t{v.tangent.x,v.tangent.y,v.tangent.z};assert(std::abs(rdot(t,t)-1)<.002f);assert(std::abs(rdot(t,v.nrm))<.002f);}
    }
    assert(signatures.size()==12);
    RockScatterSettings s;s.spacingCm=500;s.densityPermille=1000;
    auto height=[](int64_t x,int64_t z)->std::optional<int32_t>{return int32_t((x+z)/20);};
    auto all=scatterRocks({-4000,-4000,4000,4000},s,height),left=scatterRocks({-4000,-4000,0,4000},s,height),right=scatterRocks({0,-4000,4000,4000},s,height);
    left.insert(left.end(),right.begin(),right.end());auto order=[](const auto& a,const auto& b){return a.key<b.key;};std::sort(all.begin(),all.end(),order);std::sort(left.begin(),left.end(),order);assert(all==left&&!all.empty());
    for(int z=-5;z<5;++z)for(int x=-5;x<5;++x)for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx)if(dx||dz)assert(rockVariant(x,z,s.seed,8)!=rockVariant(x+dx,z+dz,s.seed,8));
    auto filtered=scatterRocks({-4000,-4000,4000,4000},s,height,[](const auto& at){return at.xCm>0;});assert(filtered.size()==right.size());
    assert(scatterRocks({0,0,2000,2000},s,[](int64_t,int64_t)->std::optional<int32_t>{return {};}).empty());
    assert(scatterRocks({0,0,2000,2000},s,[](int64_t x,int64_t)->std::optional<int32_t>{return int32_t(x*4);}).empty());
    if(argc>1){
        auto reader=[](const std::string& path,std::string& out){std::ifstream f(path,std::ios::binary);if(!f)return false;std::ostringstream b;b<<f.rdbuf();out=b.str();return true;};
        std::vector<std::string> warnings;auto loaded=aiverse::render::loadModel(argv[1],reader,&warnings);
        assert(warnings.empty() && loaded->meshes.size()==3 && loaded->meshes[0].lods.size()==2);
        assert(loaded->images.size()==4 && loaded->materials[0].metallic==0);
        for(auto& im:loaded->images)assert(im.width==1024 && im.height==1024 && !im.rgba.empty());
    }
    bool bad=false;try{s.variants=3;(void)scatterRocks({},s,height);}catch(const std::invalid_argument&){bad=true;}assert(bad);
    bad=false;try{RockProfile p;p.widthCm=-1;(void)makeRockModel(p,1);}catch(const std::invalid_argument&){bad=true;}assert(bad);
    bad=false;try{s.variants=8;s.spacingCm=1;(void)scatterRocks({-100000,-100000,100000,100000},s,height);}catch(const std::length_error&){bad=true;}assert(bad);
}
