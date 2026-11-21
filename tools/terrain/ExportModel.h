#pragma once
#include "tools/modeling/Modeling.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
namespace aiverse::terrain_export {
namespace fs=std::filesystem;
static uint32_t u32(const std::string& s,size_t p){return uint32_t(uint8_t(s[p]))|uint32_t(uint8_t(s[p+1]))<<8|uint32_t(uint8_t(s[p+2]))<<16|uint32_t(uint8_t(s[p+3]))<<24;}
static void write(const fs::path& p,const std::string& s){std::ofstream f(p,std::ios::binary);f.write(s.data(),std::streamsize(s.size()));if(!f)throw std::runtime_error("write failed: "+p.string());}
static void save(const fs::path& out,const std::string& name,const aiverse::render::Model& model,const std::vector<std::string>& materials={},bool clamp=false){
    const auto glb=aiverse::modeling::exportGlb(model);const auto n=u32(glb,12);auto doc=Json::parse(glb.substr(20,n));const size_t bh=20+n;
    doc.member("buffers").at(0).set("uri",Json::string(name+".bin"));
    if(!materials.empty()){
        auto images=Json::array(),textures=Json::array();int index=0;
        for(size_t m=0;m<materials.size();++m){
            auto binding=[](int i){auto t=Json::object();t.set("index",Json::number(int64_t(i)));return t;};
            auto& mat=doc.member("materials").at(m);auto& pbr=mat.member("pbrMetallicRoughness");
            pbr.set("baseColorTexture",binding(index));pbr.set("metallicRoughnessTexture",binding(index+2));
            // Json object members live in a vector: adding siblings can invalidate pbr.
            mat.set("normalTexture",binding(index+1));mat.set("occlusionTexture",binding(index+3));
            for(auto suffix:{"basecolor","normal","orm","ao"}){auto im=Json::object(),tx=Json::object();im.set("uri",Json::string("textures/"+materials[m]+"_"+suffix+".png"));tx.set("source",Json::number(int64_t(index++)));images.push(im);textures.push(tx);}
        }
        doc.set("images",images);
        {auto samplers=Json::array(),sampler=Json::object();sampler.set("magFilter",Json::number(int64_t(9729)));sampler.set("minFilter",Json::number(int64_t(9987)));sampler.set("wrapS",Json::number(int64_t(clamp?33071:10497)));sampler.set("wrapT",Json::number(int64_t(clamp?33071:10497)));samplers.push(sampler);doc.set("samplers",samplers);for(size_t i=0;i<textures.size();++i)textures.at(i).set("sampler",Json::number(int64_t(0)));}
        doc.set("textures",textures);
    }
    auto policy=Json::object();policy.set("autoLod",Json::fromBool(false));
    const bool authored=std::any_of(model.meshes.begin(),model.meshes.end(),[](const auto& mesh){return !mesh.lods.empty();});
    policy.set("independentShadowLod",Json::fromBool(authored));
    write(out/(name+".gltf.batching.json"),policy.dump());
    write(out/(name+".gltf"),doc.dump());write(out/(name+".bin"),glb.substr(bh+8,u32(glb,bh)));
}
}
