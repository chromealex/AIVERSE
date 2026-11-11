#include "SurfaceFixture.h"
#include "tools/modeling/SurfaceProxy.h"
#include "apps/editor/core/Workspace.h"
#include "engine/render/GeometryScene.h"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace aiverse;
using namespace aiverse::modeling;
namespace aiverse::platform { bool readContentFile(const std::string&,std::string&){return false;} }
int checks=0;
void check(bool v,const char* msg){++checks;if(!v)throw std::runtime_error(msg);}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught,"expected rejection");}
int main(int argc,char** argv){try{
    auto source=build(proxytest::wallDocument());
    auto options=Json::parse(R"({"mode":"plane","axis":"z","resolution":64})");
    auto result=bakeSurfaceProxy(*source.model,options);auto& m=*result.model;
    check(m.indices.size()==12,"two opposing quads");check(source.model->findNode(m.nodes[0].name)<0,"baked root cannot inherit source node transforms through LOD mapping");check(source.model->indices.size()>1000,"detailed source");
    check(m.materials[0].normalTex==1&&m.materials[0].mrTex==2,"normal and MR binding");
    const auto bytes=exportGlb(m);auto loaded=loadModel("proxy.glb",[&](const std::string&,std::string& s){s=bytes;return true;});
    check(loaded->images.size()==3,"embedded images loaded");
    check(loaded->materials[0].alpha==AlphaMode::Mask,"mask roundtrip");
    for(size_t i=0;i<3;++i)check(loaded->images[i].rgba==m.images[i].rgba,"texture lossless roundtrip");
    check(loaded->materials[0].normalTex>=0&&loaded->materials[0].mrTex>=0&&loaded->materials[0].occlusionTex>=0,"texture links roundtrip");
    bool curved=false;for(size_t i=0;i<m.images[1].rgba.size();i+=4)if(m.images[0].rgba[i+3]&&std::abs(int(m.images[1].rgba[i+1])-128)>30)curved=true;
    check(curved,"cylinder normals survive flattened geometry");
    auto hole=build(proxytest::wallDocument(true));auto gap=bakeSurfaceProxy(*hole.model,options);
    check(gap.report["coverage"].toDouble()<result.report["coverage"].toDouble()*.8,"opening remains uncovered");
    // Z face uses U=X, V=Y: the centre of both charts must remain transparent.
    const int tile=72;for(int f=0;f<2;++f){const auto& im=gap.model->images[0];size_t at=(size_t(4+32)*im.width+f*tile+4+32)*4;check(im.rgba[at+3]==0,"hole alpha survives dilation");}
    options.set("mode",Json::string("box"));auto box=bakeSurfaceProxy(*source.model,options);check(box.model->indices.size()==36,"six box faces");
    check(length(box.model->boundsMin-source.model->boundsMin)<1e-4f&&length(box.model->boundsMax-source.model->boundsMax)<1e-4f,"bounds preserved");
    // Mask holes and normal textures must contribute to the bake, not just geometry normals.
    auto texturedPlane=build(Json::parse(R"({"version":1,"nodes":[{"id":"plane","geometry":{"kind":"box","size":[4,4,0.2]}}]})"));
    Image alpha;alpha.width=alpha.height=1;alpha.rgba={255,255,255,0};texturedPlane.model->images.push_back(alpha);
    auto& pm=texturedPlane.model->materials[0];pm.baseTex=0;pm.alpha=AlphaMode::Mask;
    rejects([&]{bakeSurfaceProxy(*texturedPlane.model,options);});
    pm.alpha=AlphaMode::Opaque; // opaque alpha must not punch holes
    auto opaque=bakeSurfaceProxy(*texturedPlane.model,options);check(!opaque.model->indices.empty(),"opaque ignores texture alpha");
    pm.baseTex=-1;pm.normalTex=0;texturedPlane.model->images[0].rgba={220,128,220,255};
    auto mapped=bakeSurfaceProxy(*texturedPlane.model,options);bool changed=false;
    for(size_t i=0;i<mapped.model->images[1].rgba.size();i+=4)if(mapped.model->images[0].rgba[i+3]&&std::abs(int(mapped.model->images[1].rgba[i])-128)>30)changed=true;
    check(changed,"source tangent normal map contributes");
    auto solid=build(Json::parse(R"({"version":1,"nodes":[{"id":"box","geometry":{"kind":"box"}}]})"));
    auto planar=Json::parse(R"({"mode":"plane"})");rejects([&]{bakeSurfaceProxy(*solid.model,planar);});
    options.set("resolution",Json::number(int64_t(2048)));rejects([&]{bakeSurfaceProxy(*source.model,options);});
    options.set("resolution",Json::number(int64_t(32)));
    source.model->clips.emplace_back();rejects([&]{bakeSurfaceProxy(*source.model,options);});source.model->clips.clear();
    source.model->materials[0].alpha=AlphaMode::Blend;rejects([&]{bakeSurfaceProxy(*source.model,options);});source.model->materials[0].alpha=AlphaMode::Opaque;
    // Texture colours must be decoded to linear before multiplying material factors.
    Image tex;tex.width=tex.height=1;tex.rgba={128,64,32,255};source.model->images.push_back(tex);source.model->materials[0].baseTex=0;
    auto textured=bakeSurfaceProxy(*source.model,options);bool sampled=false;
    for(size_t i=0;i<textured.model->images[0].rgba.size();i+=4)if(textured.model->images[0].rgba[i+3]){
        check(textured.model->images[0].rgba[i]<128&&textured.model->images[0].rgba[i]>30,"linear texture modulation");sampled=true;break;}
    check(sampled,"textured surface exists");source.model->images.clear();source.model->materials[0].baseTex=-1;
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("aiverse-surface-proxy-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));fs::create_directories(root);
    editor::Workspace ws;ws.project.dir=root.string();ws.project.rulesJson=Json::parse(R"({"types":{"wall":{"view":{"model":"wall.glb","scale":2,"geometry":{"occlusion":true}}}}})");
    {std::ofstream f(root/"wall.glb",std::ios::binary);f<<exportGlb(*source.model);}
    auto request=Json::parse(R"({"model":"wall.glb","target":"models/wall_proxy.glb","mode":"plane","resolution":32})");
    auto baked=ws.call("geometry_proxy_bake",request);if(baked.has("error"))throw std::runtime_error(baked.dump());
    check(fs::exists(root/"models/wall_proxy.glb"),"MCP publishes GLB");check(ws.call("geometry_proxy_bake",request).has("error"),"no overwrite");
    request.set("target",Json::string("../escaped.glb"));check(ws.call("geometry_proxy_bake",request).has("error"),"path confinement");
    auto bind=Json::parse(R"({"type":"wall","source":"wall.glb","model":"models/wall_proxy.glb","distance":50})");
    auto applied=ws.call("geometry_proxy_lod",bind);if(applied.has("error"))throw std::runtime_error(applied.dump());
    check(ws.project.rulesJson["types"]["wall"]["view"]["lod"].size()==1,"LOD binding");
    check(ws.project.rulesJson["types"]["wall"]["view"].getInt("scale")==2,"view preserved");
    check(ws.call("geometry_proxy_lod",bind).has("error"),"duplicate LOD rejected");
    ws.project.rulesJson.member("types").member("wall").member("view").set("animationGraph",Json::object());
    check(ws.call("geometry_proxy_lod",bind).has("error"),"procedural graph view rejected");
    auto scene=Json::parse(R"({"version":1,"hlod":{"enabled":true},"instances":[{"id":"wall","model":"wall.glb","proxy":{"model":"models/wall_proxy.glb","maxPixels":96}}]})");render::GeometryScene::validate(scene);++checks;
    scene.member("instances").at(0).member("proxy").set("maxPixels",Json::number(int64_t(-1)));rejects([&]{render::GeometryScene::validate(scene);});
    if(argc>1){fs::create_directories(argv[1]);for(auto pair:{std::pair{"source.glb",source.model.get()},std::pair{"plane.glb",result.model.get()},std::pair{"box.glb",box.model.get()},std::pair{"hole.glb",gap.model.get()}}){std::ofstream f(fs::path(argv[1])/pair.first,std::ios::binary);f<<exportGlb(*pair.second);}}
    fs::remove_all(root);
    std::cout<<checks<<" surface proxy checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
