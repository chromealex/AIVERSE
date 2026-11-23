// aiverse_building_export: dress building-gen buildings in a building style and write them as one .glb
// (look at them in aiverse_modelview, hand them to an artist, or check a new style file).
//
//   aiverse_building_export --style engine/assets/building-styles/scifi.json --out house.glb
//                           [--building def.json] [--seed 7] [--yaw 0..3]
//   aiverse_building_export --style ... --out street.glb --street 8 [--seed 3] [--ground]
//   aiverse_building_export --style ... --out barracks.glb --structure barracks [--seed 7] [--ground]
//   --structure KIND: one whole structure of that kind from the style's `structures` block.
//   --street N: N buildings of seeded sizes (1..5 floors) along both sides of a street, each with its own seed.
#include "engine/buildings/BuildingGlbWriter.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace aiverse;using namespace aiverse::lib::systems;
namespace {
std::string readFile(const std::string& p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("cannot read "+p);std::stringstream s;s<<f.rdbuf();return s.str();}
using buildingExport::Glb;
StyledMesh ground(float x0,float z0,float x1,float z1){
    BuildingStyle s;s.materials={{"street",{0.05f,0.055f,0.06f},0.2f,0.75f,{0,0,0},1},{"line",{0.1f,0.2f,0.25f},0,0.4f,{0.1f,0.7f,0.9f},2}};
    buildingStyleDetail::Mesher m(2);m.box(0,x0,-0.2f,z0,x1,0,z1);
    for(float x=x0+2;x+3<x1;x+=6)m.box(1,x,0,-0.08f,x+3,0.01f,0.08f);
    StyledMesh out;out.materials=s.materials;out.vertices=m.v;for(int i=0;i<2;++i)out.parts.push_back({i,m.perMaterial[size_t(i)]});
    out.boundsMin[0]=x0;out.boundsMin[1]=-0.2f;out.boundsMin[2]=z0;out.boundsMax[0]=x1;out.boundsMax[1]=0.01f;out.boundsMax[2]=z1;return out;
}
}
int main(int argc,char** argv){
    try{
        std::string style,out,def,structure;uint64_t seed=1;uint32_t yaw=0,street=0;bool withGround=false,sizeProvided=false;StructureSize size{};
        for(int i=1;i<argc;++i){std::string a=argv[i];auto next=[&]{if(i+1>=argc)throw std::runtime_error(a+": value expected");return std::string(argv[++i]);};
            if(a=="--style")style=next();else if(a=="--out")out=next();else if(a=="--building")def=next();else if(a=="--seed")seed=std::stoull(next());
            else if(a=="--yaw")yaw=uint32_t(std::stoul(next()));else if(a=="--street")street=uint32_t(std::stoul(next()));else if(a=="--ground")withGround=true;else if(a=="--structure")structure=next();
            else if(a=="--size"){if(sizeProvided)throw std::runtime_error("--size specified twice");sizeProvided=true;size.width=std::stof(next());size.height=std::stof(next());size.depth=std::stof(next());}
            else throw std::runtime_error("unknown argument "+a);}
        if(style.empty()||out.empty())throw std::runtime_error("usage: aiverse_building_export --style <style.json> --out <file.glb> [--building def.json] [--seed N] [--yaw 0..3] [--street N] [--ground] [--structure KIND --size W H D]");
        auto st=readBuildingStyle(Json::parse(readFile(style)),style);
        Glb glb;size_t tris=0;
        if(!structure.empty()){
            if(sizeProvided&&(!std::isfinite(size.width)||!std::isfinite(size.height)||!std::isfinite(size.depth)||size.width<=0||size.height<=0||size.depth<=0))throw std::runtime_error("--size expects three positive finite dimensions");
            auto r=dressStructure(st,structure,seed,{},yaw,size);if(r.mesh.vertices.empty())throw std::runtime_error("style '"+st.id+"' has no structure '"+structure+"'");
            tris+=r.mesh.triangles();glb.add(r.mesh,structure);
            if(withGround)glb.add(ground(r.mesh.boundsMin[0]-8,r.mesh.boundsMin[2]-8,r.mesh.boundsMax[0]+8,r.mesh.boundsMax[2]+8),"ground");
        }else if(!street){
            if(sizeProvided)throw std::runtime_error("--size requires --structure; enterable houses use --building");
            BuildingDef d;if(!def.empty())d=readBuildingDef(Json::parse(readFile(def)),def);else applyBuildingStyle(d,st);
            auto m=dressBuilding(d,st,seed,{},yaw);if(m.vertices.empty())throw std::runtime_error("the building does not fit its definition");
            tris+=m.triangles();glb.add(m,"building");
            if(withGround){auto w=float(d.width.toDouble()),dd=float(d.depth.toDouble());glb.add(ground(-6,-6,w+6,dd+6),"ground");}
        }else{
            if(sizeProvided)throw std::runtime_error("--size requires --structure");
            // Two rows facing a 10 m street along +X: the near row (z < -5) and the far row (z > 5).
            Random g(seed,0x57ee7);float cursor[2]={0,0};
            for(uint32_t i=0;i<street;++i){
                uint32_t row=i%2;
                for(int attempt=0;attempt<20;++attempt){
                    BuildingDef d;d.floors=1+g.below(5);d.width=Fixed::fromInt(int64_t(11+g.below(12)));d.depth=Fixed::fromInt(int64_t(10+g.below(7)));d.partition=g.below(3)!=0;applyBuildingStyle(d,st);
                    auto w=float(d.width.toDouble()),dd=float(d.depth.toDouble());uint32_t turn=g.below(2)*2;   // facades along the street: 0 or 2 quarter turns
                    float x=cursor[row],z=row?6.f:-6.f-dd;
                    auto m=dressBuilding(d,st,seed*1000+i,{Fixed::fromRatio(int64_t(std::lround(x*100)),100),Fixed{},Fixed::fromRatio(int64_t(std::lround(z*100)),100)},turn);
                    if(m.vertices.empty())continue;
                    tris+=m.triangles();glb.add(m,"building"+std::to_string(i));cursor[row]+=w+2.5f;break;
                }
            }
            if(withGround)glb.add(ground(-8,-26,std::max(cursor[0],cursor[1])+6,26),"ground");
        }
        glb.write(out);std::printf("%s: style '%s', %zu triangles\n",out.c_str(),st.id.c_str(),tris);
    }catch(const std::exception& e){std::fprintf(stderr,"aiverse_building_export: %s\n",e.what());return 1;}
    return 0;
}
