#ifdef NDEBUG
#undef NDEBUG
#endif
#include "apps/editor/core/Workspace.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <chrono>
using namespace aiverse;
using namespace aiverse::editor;
namespace fs=std::filesystem;
int main(){
    const auto root=fs::temp_directory_path()/("strata-editor-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    struct Cleanup {fs::path path;~Cleanup(){fs::remove_all(path);}} cleanup{root};
    Workspace ws;
    assert(ws.call("strata_capabilities",Json::object(),"editor").getBool("builderAvailable",false));
    auto p=Json::parse(R"({"model":"model.gltf"})");
    assert(ws.call("strata_model_analyze",p,"editor").has("error"));
    ws.project.dir=root.string();
    const float vertices[]={0,0,0,1,0,0,0,1,0};const uint32_t indices[]={0,1,2};
    {std::ofstream f(root/"mesh.bin",std::ios::binary);f.write(reinterpret_cast<const char*>(vertices),sizeof vertices);f.write(reinterpret_cast<const char*>(indices),sizeof indices);}
    auto gltf=Json::parse(R"({"asset":{"version":"2.0"},"buffers":[{"uri":"mesh.bin","byteLength":48}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":12}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5125,"count":3,"type":"SCALAR"}],"materials":[{}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})");
    auto save=[&]{std::ofstream(root/"model.gltf")<<gltf.dump();};save();
    const auto revision=ws.map.revision;
    auto r=ws.call("strata_model_analyze",p,"editor");
    assert(!r.has("error")&&r.getInt("eligiblePrimitives",0)==1);
    assert(r.getInt("meshlets",0)>0&&r.getInt("auxiliaryIndexBytes",0)>0);
    assert(ws.map.revision==revision&&!ws.project.manifestDirty&&!ws.project.rulesDirty);
    gltf.set("materials",Json::parse(R"([{"alphaMode":"BLEND"}])"));save();
    r=ws.call("strata_model_analyze",p,"editor");
    assert(!r.has("error")&&r.getInt("eligiblePrimitives",-1)==0&&r["skipped"].size()==1);
    for(const auto& path:{"../outside.gltf","/tmp/outside.gltf","", "missing.gltf"}){p.set("model",Json::string(path));assert(ws.call("strata_model_analyze",p,"editor").has("error"));}
    fs::create_directory_symlink(root.parent_path(),root/"escape");
    p.set("model",Json::string("escape/outside.gltf"));assert(ws.call("strata_model_analyze",p,"editor").has("error"));
    p.set("model",Json::string("model.gltf"));
    gltf.set("buffers",Json::parse(R"([{"uri":"../outside.bin","byteLength":48}])"));save();
    assert(ws.call("strata_model_analyze",p,"editor").has("error"));
    // Reject oversized input before allocating the complete source buffer.
    {std::ofstream f(root/"large.glb",std::ios::binary);f.seekp(64*1024*1024);f.put('x');}
    p.set("model",Json::string("large.glb"));assert(ws.call("strata_model_analyze",p,"editor").has("error"));
}
