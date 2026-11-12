#include "Workspace.h"
#include "tools/modeling/SurfaceProxy.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace aiverse::editor {
namespace {
namespace fs=std::filesystem;
fs::path path(const Workspace& ws,const std::string& name){
    if(ws.project.dir.empty())throw std::runtime_error("open a project first");
    if(name.empty()||fs::path(name).is_absolute())throw std::runtime_error("surface proxy: expected project-relative path");
    auto root=fs::weakly_canonical(ws.project.dir),p=fs::weakly_canonical(root/name),rel=p.lexically_relative(root);
    if(rel.empty()||*rel.begin()=="..")throw std::runtime_error("surface proxy: path escapes project");
    return p;
}
render::FileReader reader(const Workspace& ws){return [&ws](const std::string& name,std::string& out){std::ifstream f(path(ws,name),std::ios::binary);if(!f)return false;std::ostringstream s;s<<f.rdbuf();out=s.str();return true;};}
}
void registerSurfaceProxyOps(Workspace& ws){
    ws.registerOp({"geometry_proxy_bake","Bake a rigid model into a distant plane (2 faces) or box (6 faces) GLB with embedded base colour, normal and metallic/roughness/occlusion maps. Preserves sampled holes. New target only; source is unchanged. Inspect oblique views before binding as LOD.",
        Json::parse(R"({"type":"object","properties":{"model":{"type":"string"},"target":{"type":"string"},"mode":{"enum":["plane","box"]},"axis":{"enum":["auto","x","y","z"]},"resolution":{"type":"integer","minimum":16,"maximum":512}},"required":["model","target"]})"),
        [&ws](const Json& p){
            const std::string name=p.getStr("target");const auto dest=path(ws,name);
            if(dest.extension()!=".glb"||fs::exists(dest))throw std::runtime_error("surface proxy: target must be a new .glb file");
            // Serialize cooperating bakers without truncating an existing output or following temp symlinks.
            fs::create_directories(dest.parent_path());const fs::path lock=dest.string()+".lock";
            if(!fs::create_directory(lock))throw std::runtime_error("surface proxy: target is locked");
            struct Guard{fs::path p;~Guard(){std::error_code ec;fs::remove(p/"output.glb",ec);fs::remove(p,ec);}} guard{lock};
            if(fs::exists(dest))throw std::runtime_error("surface proxy: target already exists");
            std::vector<std::string> warnings;auto source=render::loadModel(p.getStr("model"),reader(ws),&warnings);
            if(!warnings.empty())throw std::runtime_error("surface proxy: resolve source loader warning before baking: "+warnings.front());
            Json options=Json::object();for(const auto key:{"mode","axis","resolution"})if(p.has(key))options.set(key,p[key]);
            auto baked=modeling::bakeSurfaceProxy(*source,options);const auto bytes=modeling::exportGlb(*baked.model);
            const auto tmp=lock/"output.glb";
            {std::ofstream f(tmp,std::ios::binary);f.write(bytes.data(),std::streamsize(bytes.size()));f.close();if(!f)throw std::runtime_error("surface proxy: write failed");}
            // Hard-link publication is atomic and fails if another writer created the destination.
            fs::create_hard_link(tmp,dest);
            auto result=baked.report;result.set("ok",Json::fromBool(true));result.set("model",Json::string(name));
            auto proxy=Json::object();proxy.set("model",Json::string(name));proxy.set("maxPixels",Json::number(int64_t(96)));result.set("sceneProxy",proxy);
            result.set("next",Json::string("Inspect source/proxy from front, side and oblique cameras; use geometry_proxy_lod for an RTS type or instance.proxy in geometry_scene_set. No automatic gameplay/collision changes."));
            return result;
        }});
    ws.registerOp({"geometry_proxy_lod","Append a baked surface proxy to an RTS type's existing view.lod. Requires the expected source model; preserves other view settings and rejects animated sources. Distance is explicit; inspect transitions and material overrides.",
        Json::parse(R"({"type":"object","properties":{"type":{"type":"string"},"source":{"type":"string"},"model":{"type":"string"},"distance":{"type":"number","exclusiveMinimum":0}},"required":["type","source","model","distance"]})"),
        [&ws](const Json& p){
            const auto type=p.getStr("type"),model=p.getStr("model"),source=p.getStr("source");
            auto view=ws.project.rulesJson["types"][type]["view"];
            if(!view.isObject()||view.getStr("model")!=source)throw std::runtime_error("surface proxy: type source model changed or is not a single model");
            if(view.has("animationGraph")||view.getBool("programmableAnimation")||view["procedural"].size()>0)
                throw std::runtime_error("surface proxy: animated view policies cannot use a baked rigid proxy");
            const double distance=p["distance"].toDouble();if(!p["distance"].isNumber()||!std::isfinite(distance)||distance<=0||distance>1000000)throw std::runtime_error("surface proxy: invalid distance");
            auto original=render::loadModel(source,reader(ws)),baked=render::loadModel(model,reader(ws));
            if(!original->skins.empty()||!original->clips.empty())throw std::runtime_error("surface proxy: source is animated");
            for(auto& mesh:original->meshes)for(auto& prim:mesh.primitives)if(prim.morphTargets)throw std::runtime_error("surface proxy: source has morph targets");
            if(baked->materials.size()!=1||baked->materials[0].name!="surfaceProxy"||baked->images.size()!=3||baked->indices.size()>36||!baked->skins.empty()||!baked->clips.empty())throw std::runtime_error("surface proxy: expected a baked surface proxy");
            Json lod=view["lod"].isArray()?view["lod"]:Json::array();
            for(auto& item:lod.items())if(item.getStr("model")==model||item["distance"].toDouble()>=distance)throw std::runtime_error("surface proxy: append beyond existing LOD distances without duplicating a model");
            auto item=Json::object();item.set("model",Json::string(model));item.set("distance",p["distance"]);lod.push(item);
            Json fields=Json::object(),v=Json::object(),t=Json::object(),patch=Json::object(),request=Json::object();fields.set("lod",lod);v.set("view",fields);t.set(type,v);patch.set("types",t);request.set("path",Json::string(""));request.set("value",patch);
            return ws.call("rules_merge",request);
        }});
}
}
