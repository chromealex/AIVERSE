#pragma once
// Shared CPU GLB serializer for the procedural building asset pipeline.
// No rendering backend, process invocation or Studio dependencies.
#include <aiverselib/systems/BuildingStructure.h>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::lib::systems::buildingExport {
inline std::string num(float v){std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(6)<<double(v);return out.str();}
inline std::string quoted(const std::string& value){return aiverse::Json::string(value).dump(-1);}
struct Glb{
    std::vector<uint8_t> bin;std::string accessors,views,meshes,nodes,materials;int nAcc=0,nView=0,nMesh=0,nNode=0,nMat=0;
    static void sep(std::string& s){if(!s.empty())s+=",";}
    int view(const void* data,size_t bytes,int target){while(bin.size()%4)bin.push_back(0);size_t off=bin.size();auto* p=static_cast<const uint8_t*>(data);bin.insert(bin.end(),p,p+bytes);
        sep(views);views+="{\"buffer\":0,\"byteOffset\":"+std::to_string(off)+",\"byteLength\":"+std::to_string(bytes)+",\"target\":"+std::to_string(target)+"}";return nView++;}
    int accessor(int v,int comp,size_t count,const char* type,const std::string& extra=""){sep(accessors);
        accessors+="{\"bufferView\":"+std::to_string(v)+",\"componentType\":"+std::to_string(comp)+",\"count\":"+std::to_string(count)+",\"type\":\""+type+"\""+extra+"}";return nAcc++;}
    int material(const StyleMaterial& m){sep(materials);
        materials+="{\"name\":"+quoted(m.id)+",\"pbrMetallicRoughness\":{\"baseColorFactor\":["+num(m.color[0])+","+num(m.color[1])+","+num(m.color[2])+",1],\"metallicFactor\":"+num(m.metallic)+",\"roughnessFactor\":"+num(m.roughness)+"}";
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
        sep(meshes);meshes+="{\"name\":"+quoted(name)+",\"primitives\":["+prims+"]}";
        sep(nodes);nodes+="{\"name\":"+quoted(name)+",\"mesh\":"+std::to_string(nMesh++)+"}";nNode++;
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
} // namespace aiverse::lib::systems::buildingExport
