#include "GeometryOps.h"
#include "Workspace.h"
#include "engine/render/GeometryScene.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>

namespace aiverse::editor {
namespace {
namespace fs=std::filesystem;
fs::path projectPath(const Workspace& ws,const std::string& relative){
    if(ws.project.dir.empty())throw std::runtime_error("open a project first");
    if(relative.empty()||fs::path(relative).is_absolute())throw std::runtime_error("expected project-relative path");
    const auto root=fs::weakly_canonical(ws.project.dir),target=fs::weakly_canonical(root/relative),rel=target.lexically_relative(root);
    if(rel.empty()||*rel.begin()=="..")throw std::runtime_error("path escapes project");
    return target;
}
render::FileReader reader(const Workspace& ws){return [&ws](const std::string& file,std::string& out){const auto path=projectPath(ws,file);std::ifstream f(path,std::ios::binary);if(!f)return false;std::ostringstream s;s<<f.rdbuf();out=s.str();return true;};}
Json ok(){Json j=Json::object();j.set("ok",Json::fromBool(true));return j;}
}
void registerGeometryOps(Workspace& ws){
    ws.registerOp({"geometry_catalog","Geometry optimization choices, eligibility and platform-independent budgets. Analyze assets before selecting policies.",Json::object(),[](const Json&){return Json::parse(R"({"policies":{"clusters":"Large rigid opaque meshes: spatial clusters with boundary-locked coarse indices and pixel-error selection. Instanced batches remain whole.","occlusion":"Conservative current-frame CPU coverage, 64x36 cells, at most 8192 source triangles per frame. No temporal GPU readback.","occluder":"Only real opaque source triangles; never windows, bounds, transparent materials, animated or simplified HLOD proxies.","hlod":"Static decorative instances only. Spatial hierarchy merges compatible materials and chooses a proxy by pixel extent and geometric error. Not collision, entities, fog-of-war or destructible gameplay."},"selection":{"rtsCrowds":"Keep instancing and animation LOD; do not request clusters by default.","largeStaticMeshes":"Analyze triangle and material counts, enable clusters if profiling helps.","interiorsCities":"Use a small number of simple opaque occluders; test holes and camera changes.","distantScenery":"Put static decoration in geometry_scene_set with HLOD enabled.","mobileWeb":"Start with policies off; compare frame time and memory on target hardware."},"limits":{"sceneInstances":4096,"sourceVertices":2000000,"proxyVertices":2000000},"verification":["compare policy on/off","near/far and orthographic cameras","holes and near plane","shadows and reflections","CPU time and draw calls on target hardware"],"rtsScene":"game.json geometryScene; game-wide static scenery, flat maps only","fpsScene":"map geometryScene; map-local scenery","limitations":["CPU preparation at scene load, no disk streaming","two cluster detail levels, not Nanite","HLOD transitions switch without cross-fade","static scenery is visible regardless of RTS fog-of-war"]})");}});
    const auto analyze=Json::parse(R"({"type":"object","properties":{"model":{"type":"string"},"platform":{"enum":["desktop","mobile","web"]},"context":{"enum":["largeStatic","crowd","interior","scenery"]}},"required":["model"]})");
    ws.registerOp({"geometry_analyze","Inspect a real model and report eligibility, cluster reduction and a suggested policy. Does not modify the game or claim measured GPU speedup.",analyze,[&ws](const Json& p){
        auto model=render::loadModel(p.getStr("model"),reader(ws));
        if(model->vertices.size()>2000000)throw std::runtime_error("geometry analyze: exceeds 2 million vertices");
        size_t triangles=0;bool morph=false,opaque=true;
        for(auto& mesh:model->meshes)for(auto& primitive:mesh.primitives){triangles+=primitive.indexCount/3;morph|=primitive.morphTargets>0;}
        for(auto& material:model->materials)opaque&=material.alpha==render::AlphaMode::Opaque&&!material.transparent&&!material.shaderGraph;
        const bool rigid=model->skins.empty()&&model->clips.empty()&&!morph;
        render::prepareGeometry(*model);
        size_t clusters=0,fine=0,coarse=0;
        if(model->geometry)for(auto& group:model->geometry->primitives)for(auto& c:group){++clusters;fine+=c.indexCount/3;coarse+=c.coarseCount/3;}
        auto r=ok();r.set("triangles",Json::number(int64_t(triangles)));r.set("vertices",Json::number(int64_t(model->vertices.size())));r.set("materials",Json::number(int64_t(model->materials.size())));
        r.set("rigid",Json::fromBool(rigid));r.set("opaque",Json::fromBool(opaque));r.set("clusters",Json::number(int64_t(clusters)));r.set("clusterFineTriangles",Json::number(int64_t(fine)));r.set("clusterCoarseTriangles",Json::number(int64_t(coarse)));
        const auto context=p.getStr("context","largeStatic"),platform=p.getStr("platform","desktop");
        if(context!="largeStatic"&&context!="crowd"&&context!="interior"&&context!="scenery")throw std::runtime_error("unknown geometry context");
        if(platform!="desktop"&&platform!="mobile"&&platform!="web")throw std::runtime_error("unknown geometry platform");
        Json policy=Json::object();policy.set("clusters",Json::fromBool(rigid&&opaque&&triangles>=4096&&context!="crowd"&&platform=="desktop"));
        policy.set("occlusion",Json::fromBool(rigid&&context=="interior"));policy.set("occluder",Json::fromBool(rigid&&opaque&&triangles<=1024&&context=="interior"));policy.set("errorPixels",Json::number(int64_t(platform=="desktop"?1:2)));
        r.set("suggestedPolicy",policy);r.set("hlodCandidate",Json::fromBool(rigid&&opaque&&context=="scenery"));r.set("note",Json::string("Heuristic recommendation, not a performance measurement. HLOD only for immutable decorative scenery."));return r;
    }});
    const auto policy=Json::parse(R"({"type":"object","properties":{"type":{"type":"string"},"policy":{"type":"object"}},"required":["type","policy"]})");
    ws.registerOp({"geometry_policy_set","Validate and save an RTS type's view.geometry policy. Existing LOD and instancing settings are preserved.",policy,[&ws](const Json& p){
        render::parseGeometryPolicy(p["policy"]);const auto type=p.getStr("type");
        if(!ws.project.rulesJson["types"][type].isObject())throw std::runtime_error("unknown type '"+type+"'");
        // Avoid interpreting user IDs as JSON paths: merge an object rooted at types.
        Json v=Json::object(),t=Json::object(),view=Json::object(),fields=Json::object();fields.set("geometry",p["policy"]);view.set("view",fields);t.set(type,view);v.set("types",t);
        Json request=Json::object();request.set("path",Json::string(""));request.set("value",v);return ws.call("rules_merge",request);
    }});
    const auto scene=Json::parse(R"({"type":"object","properties":{"scene":{"type":"object"}},"required":["scene"]})");
    ws.registerOp({"geometry_scene_validate","Validate static scenery/HLOD schema without saving. Does not load assets or render.",scene,[](const Json& p){render::GeometryScene::validate(p["scene"]);return ok();}});
    ws.registerOp({"geometry_scene_get","Read the saved geometry.json static scenery manifest.",Json::object(),[&ws](const Json&){auto r=ok();r.set("scene",Json::parseFile(projectPath(ws,"geometry.json").string()));return r;}});
    ws.registerOp({"geometry_scene_set","Validate and atomically save geometry.json. RTS: applies game-wide static scenery and rebuilds views. FPS: set map geometryScene=geometry.json separately. Never move gameplay entities into static scenery.",scene,[&ws](const Json& p){
        render::GeometryScene::validate(p["scene"]);
        if(fs::is_symlink(fs::path(ws.project.dir)/"geometry.json.tmp"))throw std::runtime_error("geometry scene: temporary path is symlink");
        const auto file=projectPath(ws,"geometry.json"),tmp=projectPath(ws,"geometry.json.tmp");
        const auto text=p["scene"].dump(2)+"\n";if(text.size()>2*1024*1024)throw std::runtime_error("geometry scene: exceeds 2 MiB");
        // Validate all referenced assets before committing the scene file.
        std::set<std::string> seen; size_t vertices=0;
        for(auto& i:p["scene"]["instances"].items())if(seen.insert(i.getStr("model")).second){auto m=render::loadModel(i.getStr("model"),reader(ws));
            if(m->vertices.empty()||m->indices.empty())throw std::runtime_error("geometry scene: source has no triangles");
            vertices+=m->vertices.size();if(vertices>2000000)throw std::runtime_error("geometry scene: exceeds 2 million source vertices");
            if(!m->skins.empty()||!m->clips.empty())throw std::runtime_error("geometry scene: animated assets must remain entities");
            for(auto& mesh:m->meshes)for(auto& primitive:mesh.primitives)if(primitive.morphTargets)throw std::runtime_error("geometry scene: morph assets must remain entities");}
        if(fs::is_symlink(tmp))throw std::runtime_error("geometry scene: temporary path is symlink");
        {std::ofstream f(tmp,std::ios::binary|std::ios::trunc);f<<text;f.close();if(!f)throw std::runtime_error("geometry scene: write failed");}fs::rename(tmp,file);
        if(!ws.project.isFps()){Json q=Json::object();q.set("path",Json::string("geometryScene"));q.set("value",Json::string("geometry.json"));auto result=ws.call("set_manifest",q);if(result.has("error"))return result;}
        ws.note({Change::Rules,0,0,-1,-1,-1,"Geometry scene updated"});return ok();
    }});
}
}
