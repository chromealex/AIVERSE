#pragma once
#include "engine/render/Animation.h"
#include "engine/render/Renderer3D.h"
#include "engine/data/Json.h"
#include "engine/render/graph/ShaderGraph.h"
#include <map>
#include <filesystem>
#include <stdexcept>

namespace aiverse::bench {
using namespace render;
struct SceneAsset {
    std::unique_ptr<Model> model;
    Pose pose;
    std::vector<mat4> world;
    mat4 transform;
    bool animated=false;
};
struct SceneAssets {
    std::vector<SceneAsset> assets;
    std::vector<PointLight> lights;
    std::vector<size_t> lightOrder;
    Json audit=Json::array();
    static float3 vector3(const Json& j,float3 fallback={}) {
        return j.isArray()&&j.size()==3?float3{float(j[0].toDouble()),float(j[1].toDouble()),float(j[2].toDouble())}:fallback;
    }
    static bool secondaryUv(const Json& j) {
        if(j.isObject())for(const auto& [key,value]:j.members()){
            if(key=="texCoord"&&value.integer()!=0)return true;
            if(secondaryUv(value))return true;
        }
        if(j.isArray())for(const auto& value:j.items())if(secondaryUv(value))return true;
        return false;
    }
    std::map<std::string,std::shared_ptr<const graph::Program>> materialGraphs;
    void applyGraphs(Model& model,const Json& bindings) {
        for(const auto& [name,file]:bindings.members()) {
            auto it=std::find_if(model.materials.begin(),model.materials.end(),[&](const Material& m){return m.name==name;});
            if(it==model.materials.end())throw std::runtime_error("unknown graph material: "+name);
            const auto path=file.str();
            auto& program=materialGraphs[path];
            if(!program){
                std::string bytes;
                if(!platform::readContentFile(path,bytes))throw std::runtime_error("cannot read material graph: "+path);
                program=std::make_shared<graph::Program>(graph::compile(Json::parse(bytes),"material"));
            }
            it->shaderGraph=program;
            std::fprintf(stderr,"material graph: %s <- %s\n",name.c_str(),path.c_str());
        }
    }
    void load(gfx::Device& d,const Json& config,Json& warnings,Json& notes) {
        for(const auto& entry:config["models"].items()) {
            const std::string path=entry.getStr("path");
            std::vector<std::string> problems;
            // FBX archives keep textures in a separate directory. A declared search root is
            // portable; never read the author's absolute DCC paths from inside an asset.
            auto read=[&](const std::string& file,std::string& bytes){
                if(platform::readContentFile(file,bytes))return true;
                const std::string root=entry.getStr("textureRoot");
                return !root.empty()&&platform::readContentFile(root+"/"+std::filesystem::path(file).filename().string(),bytes);
            };
            SceneAsset a;a.model=loadModel(path,read,&problems);
            a.transform=mat4::translate(vector3(entry["translation"]));
            std::string source;Json gltf;
            if(std::filesystem::path(path).extension()==".gltf"&&read(path,source))gltf=Json::parse(source);
            for(const auto& problem:problems) {
                bool replaced=(!gltf.isNull()&&!secondaryUv(gltf["materials"])&&problem.ends_with("TEXCOORD_1 is ignored"))||problem.ends_with("cameras are ignored")||problem.ends_with("cameras and lights are ignored")||problem.ends_with("extension KHR_lights_punctual is ignored");
                (replaced?notes:warnings).push(Json::string(problem));
                std::fprintf(stderr,"asset %s: %s\n",replaced?"note":"warning",problem.c_str());
            }
            for(const auto& [name, override] : entry["materials"].members()) {
                auto it=std::find_if(a.model->materials.begin(),a.model->materials.end(),[&](const Material& m){return m.name==name;});
                if(it==a.model->materials.end())throw std::runtime_error("unknown material override: "+name);
                auto& m=*it;auto c=vector3(override["baseColor"],{m.baseColor.x,m.baseColor.y,m.baseColor.z});m.baseColor={c.x,c.y,c.z,m.baseColor.w};
                m.emissive=vector3(override["emissive"],m.emissive);
                m.emissiveStrength=float(override["emissiveStrength"].toDouble(m.emissiveStrength));
                m.vertexColors=override.getBool("vertexColors",m.vertexColors);
                m.metallic=float(override["metallic"].toDouble(m.metallic));m.roughness=float(override["roughness"].toDouble(m.roughness));
                auto image=[&](const char* key,bool roughness){
                    std::string file=override.getStr(key);if(file.empty())return -1;
                    std::string bytes;Image img;img.name=file;
                    if(!read(file,bytes)||!decodeImage(bytes,img))throw std::runtime_error("invalid material image: "+file);
                    if(roughness)for(size_t i=0;i<img.rgba.size();i+=4){img.rgba[i+1]=img.rgba[i];img.rgba[i]=255;img.rgba[i+2]=255;img.rgba[i+3]=255;}
                    int index=int(a.model->images.size());a.model->images.push_back(std::move(img));return index;
                };
                if(override.has("baseTexture"))m.baseTex=image("baseTexture",false);
                if(override.has("normalTexture"))m.normalTex=image("normalTexture",false);
                if(override.has("roughnessTexture")){m.mrTex=image("roughnessTexture",true);m.roughness=1;}
            }
            applyGraphs(*a.model,entry["materialGraphs"]);
            std::string animation=entry.getStr("animation");
            if(!animation.empty()) {
                std::vector<std::string> animProblems;
                auto anim=loadModel(animation,read,&animProblems);
                for(const auto& problem:animProblems)warnings.push(Json::string(problem));
                size_t channels=0;
                for(auto clip:anim->clips){
                    std::vector<AnimChannel> mapped;
                    for(auto channel:clip.channels) {
                        int node=a.model->findNode(anim->nodes.at(size_t(channel.node)).name);
                        if(node>=0){channel.node=node;mapped.push_back(std::move(channel));++channels;}
                    }
                    clip.channels=std::move(mapped);if(!clip.channels.empty())a.model->clips.push_back(std::move(clip));
                }
                if(!channels)throw std::runtime_error("animation has no matching channels: "+animation);
                a.animated=true;bindPose(*a.model,a.pose);
            }
            if(entry["lights"].boolean()) {
                for(size_t i=0;i<a.model->nodes.size();++i) {
                    int li=a.model->nodes[i].light;if(li<0)continue;
                    const auto& definition=a.model->lights.at(size_t(li));
                    if(definition.type!=ModelLight::Type::Point)throw std::runtime_error("benchmark only supports point lights");
                    PointLight l;auto p=a.transform*a.model->bindWorld.at(i)*float4{0,0,0,1};l.pos={p.x,p.y,p.z};
                    // The RTS light system uses display RGB and finite game-unit attenuation.
                    auto srgb=[](float x){return x<=0.0031308f?12.92f*x:1.055f*std::pow(x,1.0f/2.4f)-0.055f;};
                    auto color=vector3(entry["lightColor"],definition.color);
                    l.color={srgb(color.x),srgb(color.y),srgb(color.z)};l.intensity=definition.intensity*float(entry["lightIntensityScale"].toDouble(1));
                    l.radius=definition.range>0?definition.range:float(entry["lightRadius"].toDouble(0.75));l.key=uint32_t(lights.size()+1);
                    lights.push_back(l);
                }
            }
            Json item=Json::object();item.set("path",Json::string(path));item.set("animated",Json::fromBool(a.animated));
            uint64_t triangles=0;for(const auto& n:a.model->nodes)if(n.mesh>=0)for(const auto& p:a.model->meshes[size_t(n.mesh)].primitives)triangles+=p.indexCount/3;
            item.set("triangles",Json::number(int64_t(triangles)));item.set("clips",Json::number(int64_t(a.model->clips.size())));
            if(a.animated){
                const auto& clip=a.model->clips.back();Pose start,end;std::vector<mat4> wa,wb;
                bindPose(*a.model,start);bindPose(*a.model,end);sampleClip(clip,0,start);sampleClip(clip,std::min(0.5f,clip.duration),end);
                worldMatrices(*a.model,start,wa);worldMatrices(*a.model,end,wb);int moving=0;
                for(size_t n=0;n<wa.size();++n){bool changed=false;for(int k=0;k<16;++k)changed|=std::abs(wa[n].m[k]-wb[n].m[k])>1e-5f;if(changed)++moving;}
                item.set("animationDurationSeconds",Json::number(double(clip.duration)));item.set("animatedNodes",Json::number(int64_t(moving)));
                if(!moving)throw std::runtime_error("animation pose does not change: "+path);
            }
            auto xyz=[](float3 v){Json j=Json::array();for(float x:{v.x,v.y,v.z})j.push(Json::number(double(x)));return j;};
            item.set("boundsMin",xyz(a.model->boundsMin));item.set("boundsMax",xyz(a.model->boundsMax));
            std::fprintf(stderr,"loaded %s: %llu triangles, %zu clips, bounds %s .. %s\n",path.c_str(),(unsigned long long)triangles,a.model->clips.size(),item["boundsMin"].dump().c_str(),item["boundsMax"].dump().c_str());
            if(!uploadModel(d,*a.model))throw std::runtime_error("addon upload failed: "+d.lastError());
            audit.push(item);assets.push_back(std::move(a));
        }
        // Recipe lights use linear RGB, like glTF punctual lights.
        for(const auto& entry:config["pointLights"].items()) {
            if(!entry["position"].isArray()||entry["position"].size()!=3||!entry["color"].isArray()||entry["color"].size()!=3)
                throw std::runtime_error("point light requires position and color xyz");
            PointLight light;light.pos=vector3(entry["position"]);auto color=vector3(entry["color"]);
            light.radius=float(entry["radius"].toDouble(0));light.intensity=float(entry["intensity"].toDouble(0));
            for(float value:{light.pos.x,light.pos.y,light.pos.z,color.x,color.y,color.z,light.radius,light.intensity})
                if(!std::isfinite(value))throw std::runtime_error("nonfinite recipe point light");
            if(light.radius<=0||light.intensity<0||light.intensity>1e6f||color.x<0||color.y<0||color.z<0||color.x>1||color.y>1||color.z>1)
                throw std::runtime_error("recipe point light out of range");
            auto srgb=[](float value){return value<=.0031308f?12.92f*value:1.055f*std::pow(value,1.0f/2.4f)-.055f;};
            light.color={srgb(color.x),srgb(color.y),srgb(color.z)};light.key=uint32_t(lights.size()+1);
            lights.push_back(light);
        }
        lightOrder.resize(lights.size());for(size_t i=0;i<lights.size();++i)lightOrder[i]=i;
    }
    void selectLights(float3 eye) {
        std::stable_sort(lightOrder.begin(),lightOrder.end(),[&](size_t a,size_t b){
            auto da=lights[a].pos-eye,db=lights[b].pos-eye;
            float aa=dot(da,da),bb=dot(db,db);return aa==bb?a<b:aa<bb;
        });
    }
    void draw(Renderer3D& r,float seconds,bool lightEnabled) {
        for(auto& a:assets){
            ModelDraw md;md.model=a.model.get();md.world=a.transform;
            if(a.animated){bindPose(*a.model,a.pose);const auto& clip=a.model->clips.back();sampleClip(clip,clip.duration>0?std::fmod(seconds,clip.duration):0,a.pose);worldMatrices(*a.model,a.pose,a.world);md.nodeWorld=&a.world;md.pose=&a.pose;}
            r.drawModel(md);
        }
        if(lightEnabled)for(size_t i=0;i<std::min(lightOrder.size(),LightSystem::kMaxLights);++i)r.lights.add(lights[lightOrder[i]]);
    }
    void release(gfx::Device& d){for(auto& a:assets)releaseModel(d,*a.model);assets.clear();}
};
}
