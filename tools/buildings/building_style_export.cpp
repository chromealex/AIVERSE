// aiverse_building_export: dress building-gen buildings in a building style and write them as one .glb
// (look at them in aiverse_modelview, hand them to an artist, or check a new style file).
//
//   aiverse_building_export --style engine/assets/building-styles/scifi.json --out house.glb
//                           [--building def.json] [--seed 7] [--yaw 0..3]
//   aiverse_building_export --style ... --out street.glb --street 8 [--seed 3] [--ground]
//   aiverse_building_export --style ... --out barracks.glb --structure barracks [--seed 7] [--ground]
//   --structure KIND: one whole structure of that kind from the style's `structures` block.
//   --street N: N buildings of seeded sizes (1..5 floors) along both sides of a street, each with its own seed.
#include <aiverselib/systems/BuildingStructure.h>
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
std::string num(float v){char b[32];std::snprintf(b,sizeof b,"%.6g",double(v));return b;}
struct Glb{
    std::vector<uint8_t> bin;std::string accessors,views,meshes,nodes,materials;int nAcc=0,nView=0,nMesh=0,nNode=0,nMat=0;
    static void sep(std::string& s){if(!s.empty())s+=",";}
    int view(const void* data,size_t bytes,int target){while(bin.size()%4)bin.push_back(0);size_t off=bin.size();auto* p=static_cast<const uint8_t*>(data);bin.insert(bin.end(),p,p+bytes);
        sep(views);views+="{\"buffer\":0,\"byteOffset\":"+std::to_string(off)+",\"byteLength\":"+std::to_string(bytes)+",\"target\":"+std::to_string(target)+"}";return nView++;}
    int accessor(int v,int comp,size_t count,const char* type,const std::string& extra=""){sep(accessors);
        accessors+="{\"bufferView\":"+std::to_string(v)+",\"componentType\":"+std::to_string(comp)+",\"count\":"+std::to_string(count)+",\"type\":\""+type+"\""+extra+"}";return nAcc++;}
    int material(const StyleMaterial& m){sep(materials);
        materials+="{\"name\":\""+m.id+"\",\"pbrMetallicRoughness\":{\"baseColorFactor\":["+num(m.color[0])+","+num(m.color[1])+","+num(m.color[2])+",1],\"metallicFactor\":"+num(m.metallic)+",\"roughnessFactor\":"+num(m.roughness)+"}";
        if(m.emissive[0]+m.emissive[1]+m.emissive[2]>0){materials+=",\"emissiveFactor\":["+num(m.emissive[0])+","+num(m.emissive[1])+","+num(m.emissive[2])+"]";
            if(m.emissiveStrength!=1)materials+=",\"extensions\":{\"KHR_materials_emissive_strength\":{\"emissiveStrength\":"+num(m.emissiveStrength)+"}}";}
        materials+="}";return nMat++;}
    void add(const StyledMesh& m,const std::string& name){
        if(m.vertices.empty())return;
        std::vector<float> pos,nrm,uv;for(const auto& v:m.vertices){pos.insert(pos.end(),v.pos,v.pos+3);nrm.insert(nrm.end(),v.nrm,v.nrm+3);uv.insert(uv.end(),v.uv,v.uv+2);}
        auto bounds=",\"min\":["+num(m.boundsMin[0])+","+num(m.boundsMin[1])+","+num(m.boundsMin[2])+"],\"max\":["+num(m.boundsMax[0])+","+num(m.boundsMax[1])+","+num(m.boundsMax[2])+"]";
        int p=accessor(view(pos.data(),pos.size()*4,34962),5126,m.vertices.size(),"VEC3",bounds),n=accessor(view(nrm.data(),nrm.size()*4,34962),5126,m.vertices.size(),"VEC3"),t=accessor(view(uv.data(),uv.size()*4,34962),5126,m.vertices.size(),"VEC2");
        std::vector<int> mats;for(const auto& mt:m.materials)mats.push_back(material(mt));
        std::string prims;
        for(const auto& part:m.parts){int i=accessor(view(part.indices.data(),part.indices.size()*4,34963),5125,part.indices.size(),"SCALAR");sep(prims);
            prims+="{\"attributes\":{\"POSITION\":"+std::to_string(p)+",\"NORMAL\":"+std::to_string(n)+",\"TEXCOORD_0\":"+std::to_string(t)+"},\"indices\":"+std::to_string(i)+",\"material\":"+std::to_string(mats[size_t(part.material)])+"}";}
        sep(meshes);meshes+="{\"name\":\""+name+"\",\"primitives\":["+prims+"]}";
        sep(nodes);nodes+="{\"name\":\""+name+"\",\"mesh\":"+std::to_string(nMesh++)+"}";nNode++;
    }
    void write(const std::string& path){
        while(bin.size()%4)bin.push_back(0);
        std::string scene;for(int i=0;i<nNode;++i){if(i)scene+=",";scene+=std::to_string(i);}
        std::string json="{\"asset\":{\"version\":\"2.0\",\"generator\":\"aiverse_building_export\"},\"extensionsUsed\":[\"KHR_materials_emissive_strength\"],\"scene\":0,\"scenes\":[{\"nodes\":["+scene+"]}],"
            "\"nodes\":["+nodes+"],\"meshes\":["+meshes+"],\"materials\":["+materials+"],\"accessors\":["+accessors+"],\"bufferViews\":["+views+"],\"buffers\":[{\"byteLength\":"+std::to_string(bin.size())+"}]}";
        while(json.size()%4)json+=' ';
        std::ofstream f(path,std::ios::binary);if(!f)throw std::runtime_error("cannot write "+path);
        auto u32=[&](uint32_t v){f.write(reinterpret_cast<const char*>(&v),4);};
        u32(0x46546C67);u32(2);u32(uint32_t(12+8+json.size()+8+bin.size()));
        u32(uint32_t(json.size()));u32(0x4E4F534A);f.write(json.data(),std::streamsize(json.size()));
        u32(uint32_t(bin.size()));u32(0x004E4942);f.write(reinterpret_cast<const char*>(bin.data()),std::streamsize(bin.size()));
    }
};
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
