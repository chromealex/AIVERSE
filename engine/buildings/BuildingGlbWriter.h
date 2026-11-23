#pragma once
// Shared CPU GLB serializer for the procedural building asset pipeline.
// No rendering backend, process invocation or Studio dependencies.
#include <aiverselib/systems/BuildingStructure.h>
#include "engine/buildings/BuildingPngEncoder.h"
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
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
    std::vector<uint8_t> bin;std::string accessors,views,meshes,nodes,materials,images,textures;int nAcc=0,nView=0,nMesh=0,nNode=0,nMat=0;
    static void sep(std::string& s){if(!s.empty())s+=",";}
    int view(const void* data,size_t bytes,int target){while(bin.size()%4)bin.push_back(0);size_t off=bin.size();auto* p=static_cast<const uint8_t*>(data);bin.insert(bin.end(),p,p+bytes);
        sep(views);views+="{\"buffer\":0,\"byteOffset\":"+std::to_string(off)+",\"byteLength\":"+std::to_string(bytes)+",\"target\":"+std::to_string(target)+"}";return nView++;}
    int accessor(int v,int comp,size_t count,const char* type,const std::string& extra=""){sep(accessors);
        accessors+="{\"bufferView\":"+std::to_string(v)+",\"componentType\":"+std::to_string(comp)+",\"count\":"+std::to_string(count)+",\"type\":\""+type+"\""+extra+"}";return nAcc++;}
    // Existing Studio AssetCatalog resolves GUIDs. Embed PNG/JPEG bytes
    // as GLB buffer views; never put local project filesystem paths in glTF.
    using TextureReader=std::function<bool(
        const std::string&,std::vector<uint8_t>&,std::string&)>;
    TextureReader textureReader;
    std::map<std::string,int> cachedTextures;
    bool usesUvTransform=false;
    int texture(const std::string& guid){
        if(guid.empty())return -1;
        if(auto it=cachedTextures.find(guid);it!=cachedTextures.end())
            return it->second;
        std::vector<uint8_t> bytes;std::string issue;
        if(!textureReader||!textureReader(guid,bytes,issue))
            throw std::runtime_error(issue.empty()?"GLB texture asset unresolved: "+guid:issue);
        const bool png=bytes.size()>=8&&bytes[0]==137&&bytes[1]==80&&
            bytes[2]==78&&bytes[3]==71&&bytes[4]==13&&bytes[5]==10&&
            bytes[6]==26&&bytes[7]==10;
        const bool jpg=bytes.size()>=4&&bytes[0]==255&&bytes[1]==216&&
            bytes[bytes.size()-2]==255&&bytes[bytes.size()-1]==217;
        if((!png&&!jpg)||bytes.size()>20u*1024u*1024u)
            throw std::runtime_error("GLB texture must be valid PNG/JPEG up to 20 MiB");
        while(bin.size()%4)bin.push_back(0);
        const size_t offset=bin.size();
        bin.insert(bin.end(),bytes.begin(),bytes.end());
        sep(views);views+="{\"buffer\":0,\"byteOffset\":"+std::to_string(offset)+
            ",\"byteLength\":"+std::to_string(bytes.size())+"}";
        const int imageView=nView++;
        const int id=int(cachedTextures.size());
        sep(images);images+="{\"bufferView\":"+std::to_string(imageView)+
            ",\"mimeType\":"+quoted(png?"image/png":"image/jpeg")+"}";
        sep(textures);textures+="{\"source\":"+std::to_string(id)+"}";
        cachedTextures.emplace(guid,id);
        return id;
    }
    int generatedTexture(const StyleMaterial& source,int channel){
        if(source.surfacePattern.empty())return -1;
        const std::string key="surface:"+source.id+":"+
            source.surfacePattern+":"+
            std::to_string(source.surfaceSeed)+":"+
            std::to_string(channel);
        if(auto it=cachedTextures.find(key);it!=cachedTextures.end())
            return it->second;
        const auto generated=generateBuildingSurface(
            source.surfacePattern,source.id,source.surfaceSeed,
            static_cast<BuildingSurfaceChannel>(channel));
        if(generated.rgba.empty())
            throw std::runtime_error("Cannot generate authored PBR surface");
        const auto bytes=aiverse::building::encodeBuildingPngRgba(
            generated.rgba,generated.width,generated.height);
        while(bin.size()%4)bin.push_back(0);
        const size_t off=bin.size();
        bin.insert(bin.end(),bytes.begin(),bytes.end());
        sep(views);
        views+="{\"buffer\":0,\"byteOffset\":"+
            std::to_string(off)+",\"byteLength\":"+
            std::to_string(bytes.size())+"}";
        const int viewId=nView++;
        const int id=int(cachedTextures.size());
        sep(images);
        images+="{\"bufferView\":"+std::to_string(viewId)+
                ",\"mimeType\":\"image/png\"}";
        sep(textures);
        textures+="{\"source\":"+std::to_string(id)+"}";
        cachedTextures.emplace(key,id);
        return id;
    }
    int material(const StyleMaterial& m){
        aiverse::Json root=aiverse::Json::object();
        root.set("name",aiverse::Json::string(m.id));
        aiverse::Json pbr=aiverse::Json::object();
        aiverse::Json rgb=aiverse::Json::array();
        for(float n:m.color)rgb.push(aiverse::Json::number(double(n),6));
        rgb.push(aiverse::Json::number(1.0,1));
        pbr.set("baseColorFactor",std::move(rgb));
        pbr.set("metallicFactor",aiverse::Json::number(double(m.metallic),6));
        pbr.set("roughnessFactor",aiverse::Json::number(double(m.roughness),6));
        const auto texInfo=[&](int index){
            aiverse::Json o=aiverse::Json::object();
            o.set("index",aiverse::Json::number(int64_t(index)));
            if(m.uvScale[0]!=1.f||m.uvScale[1]!=1.f||
               m.uvOffset[0]!=0.f||m.uvOffset[1]!=0.f||
               m.uvRotation!=0.f){
                aiverse::Json transform=aiverse::Json::object();
                aiverse::Json scale=aiverse::Json::array();
                scale.push(aiverse::Json::number(double(m.uvScale[0]),6));
                scale.push(aiverse::Json::number(double(m.uvScale[1]),6));
                aiverse::Json offset=aiverse::Json::array();
                offset.push(aiverse::Json::number(double(m.uvOffset[0]),6));
                offset.push(aiverse::Json::number(double(m.uvOffset[1]),6));
                transform.set("scale",std::move(scale));
                transform.set("offset",std::move(offset));
                transform.set("rotation",aiverse::Json::number(double(m.uvRotation),6));
                aiverse::Json ext=aiverse::Json::object();
                ext.set("KHR_texture_transform",std::move(transform));
                o.set("extensions",std::move(ext));
                usesUvTransform=true;
            }
            return o;
        };
        const int base=m.textures.baseColor.empty()?
            generatedTexture(m,0):texture(m.textures.baseColor);
        const int mr=m.textures.metallicRoughness.empty()?
            generatedTexture(m,1):texture(m.textures.metallicRoughness);
        const int normal=m.textures.normal.empty()?
            generatedTexture(m,2):texture(m.textures.normal);
        const int ao=texture(m.textures.occlusion);
        const int emissive=texture(m.textures.emissive);
        if(base>=0)pbr.set("baseColorTexture",texInfo(base));
        if(mr>=0)pbr.set("metallicRoughnessTexture",texInfo(mr));
        root.set("pbrMetallicRoughness",std::move(pbr));
        if(normal>=0){
            auto info=texInfo(normal);
            info.set("scale",aiverse::Json::number(double(m.normalScale),6));
            root.set("normalTexture",std::move(info));
        }
        if(ao>=0){
            auto info=texInfo(ao);
            info.set("strength",aiverse::Json::number(double(m.occlusionStrength),6));
            root.set("occlusionTexture",std::move(info));
        }
        if(emissive>=0)root.set("emissiveTexture",texInfo(emissive));
        if(m.emissive[0]+m.emissive[1]+m.emissive[2]>0||emissive>=0){
            aiverse::Json factor=aiverse::Json::array();
            for(float n:m.emissive)factor.push(aiverse::Json::number(double(n),6));
            root.set("emissiveFactor",std::move(factor));
            if(m.emissiveStrength!=1.f){
                aiverse::Json strength=aiverse::Json::object();
                strength.set("emissiveStrength",aiverse::Json::number(double(m.emissiveStrength),6));
                aiverse::Json ext=aiverse::Json::object();
                ext.set("KHR_materials_emissive_strength",std::move(strength));
                root.set("extensions",std::move(ext));
            }
        }
        sep(materials);materials+=root.dump(-1);
        return nMat++;
    }
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
        std::string json="{\"asset\":{\"version\":\"2.0\",\"generator\":\"aiverse_building_export\"},\"extensionsUsed\":[\"KHR_materials_emissive_strength\""+std::string(usesUvTransform?",\"KHR_texture_transform\"":"")+"],\"scene\":0,\"scenes\":[{\"nodes\":["+scene+"]}],"
            "\"nodes\":["+nodes+"],\"meshes\":["+meshes+"],\"materials\":["+materials+"],\"images\":["+images+"],\"textures\":["+textures+"],\"accessors\":["+accessors+"],\"bufferViews\":["+views+"],\"buffers\":[{\"byteLength\":"+std::to_string(bin.size())+"}]}";
        while(json.size()%4)json+=' ';
        std::ofstream f(path,std::ios::binary);if(!f)throw std::runtime_error("cannot write "+path);
        auto u32=[&](uint32_t v){f.write(reinterpret_cast<const char*>(&v),4);};
        u32(0x46546C67);u32(2);u32(uint32_t(12+8+json.size()+8+bin.size()));
        u32(uint32_t(json.size()));u32(0x4E4F534A);f.write(json.data(),std::streamsize(json.size()));
        u32(uint32_t(bin.size()));u32(0x004E4942);f.write(reinterpret_cast<const char*>(bin.data()),std::streamsize(bin.size()));
    }
};
} // namespace aiverse::lib::systems::buildingExport
