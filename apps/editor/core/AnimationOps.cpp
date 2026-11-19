#include "AnimationOps.h"
#include "AnimationDocument.h"
#include "engine/render/AnimationAsset.h"
#include "engine/render/AnimationGraph.h"
#include "libraries/include/aiverselib/motionweave/MotionWeave.h"
#include "libraries/include/aiverselib/animation/HumanoidProfile.h"
#include "libraries/include/aiverselib/animation/RagdollAsset.h"
#include "libraries/include/aiverselib/animation/RootMotionBaker.h"
#include "libraries/include/aiverselib/animation/AnimationMontage.h"
#include <set>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <algorithm>
namespace aiverse::editor {
namespace {
namespace fs=std::filesystem;
fs::path contained(const Workspace& w,const std::string& relative){if(w.project.dir.empty())throw std::runtime_error("open a project first");auto root=fs::weakly_canonical(w.project.dir),p=fs::weakly_canonical(root/relative);auto r=p.lexically_relative(root);if(r.empty()||*r.begin()=="..")throw std::runtime_error("path escapes project");return p;}
fs::path path(const Workspace& w,const std::string& name){if(name.empty()||name.size()>64||name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::runtime_error("asset name: use letters, digits, _ or -");return contained(w,"animations/"+name+".json");}
Json load(const Workspace& w,const std::string& name){auto p=path(w,name);if(fs::file_size(p)>256*1024)throw std::runtime_error("asset exceeds 256 KiB");std::ifstream file(p,std::ios::binary);std::string text((std::istreambuf_iterator<char>(file)),{});return render::parseAnimationAsset(text);}
Json result(Json graph){Json r=Json::object();r.set("ok",Json::fromBool(true));r.set("graph",std::move(graph));return r;}
void validate(const Workspace& w,const Json& g){auto m=loadAnimationModel(w,g.getStr("model"));render::AnimationGraph::compile(*m,g);}
}
std::unique_ptr<render::Model> loadAnimationModel(const Workspace& w,const std::string& file){if(file.empty())throw std::runtime_error("select a model");auto reader=[&w](const std::string& p,std::string& out){auto f=contained(w,p);std::ifstream stream(f,std::ios::binary);if(!stream)return false;out.assign(std::istreambuf_iterator<char>(stream),{});return true;};auto m=render::loadModel(file,reader);if(!m)throw std::runtime_error("cannot load animation model");return m;}
void registerAnimationOps(Workspace& ws){auto schema=Json::parse(R"({"type":"object","properties":{"name":{"type":"string"},"graph":{"type":"object"},"expected":{"type":"object"},"target":{"type":"string"}}})");
    ws.registerOp({"animation_graph_list","List animation/rig assets in animations/.",Json::object(),[&ws](const Json&){Json r=Json::object(),a=Json::array();auto dir=path(ws,"placeholder").parent_path();std::vector<std::string> names;if(fs::exists(dir))for(auto& e:fs::directory_iterator(dir))if(e.is_regular_file()&&e.path().extension()==".json")names.push_back(e.path().stem().string());std::sort(names.begin(),names.end());for(auto& n:names)a.push(Json::string(n));r.set("graphs",a);return r;}});
    ws.registerOp({"animation_graph_get","Read an animation/rig asset without executing custom callbacks.",schema,[&ws](const Json& p){return result(load(ws,p.getStr("name")));}});
    ws.registerOp({"animation_graph_validate","Compile graph and rig against the selected model. Custom host callbacks require their host registry.",schema,[&ws](const Json& p){validate(ws,p["graph"]);return result(p["graph"]);}});
    ws.registerOp({"animation_graph_set","Save an authoring draft; validates topology, not model-specific execution. expected prevents overwriting concurrent edits.",schema,[&ws](const Json& p){Json g=p["graph"];animation::sort(g);if(auto* graphs=g.find("graphs"))for(auto& [name,child]:graphs->membersMut()){(void)name;animation::sort(child);}auto file=path(ws,p.getStr("name"));if(fs::exists(file)&&(!p.has("expected")||!(load(ws,p.getStr("name"))==p["expected"])))throw std::runtime_error("asset changed; reload before saving");auto text=g.dump(2)+"\n";if(text.size()>256*1024)throw std::runtime_error("asset exceeds 256 KiB");fs::create_directories(file.parent_path());auto temp=file;temp+=".tmp";if(fs::is_symlink(temp))throw std::runtime_error("temporary path is symlink");{std::ofstream f(temp,std::ios::binary);f<<text;f.close();if(!f)throw std::runtime_error("cannot save asset");}fs::rename(temp,file);return result(g);}});
    ws.registerOp({"animation_graph_apply","Validate saved asset against target type model and apply a graph snapshot through rules_set.",schema,[&ws](const Json& p){Json g=load(ws,p.getStr("name"));auto target=p.getStr("target");const auto& types=ws.project.rulesJson["types"];if(!types.has(target))throw std::runtime_error("unknown target type");auto view=types[target]["view"];std::string model=view.getStr("model");if(model.empty())throw std::runtime_error("target needs a single model");g.set("model",Json::string(model));validate(ws,g);view.set("animationGraph",g);view.set("programmableAnimation",Json::fromBool(true));Json q=Json::object();q.set("path",Json::string("types."+target+".view"));q.set("value",view);return ws.call("rules_set",q);}});

    // Advanced animation authoring is reusable by the editor and its MCP agent.
    // All files are confined to project/animations/<kind>/<name>.json.
    const auto safeAsset=[&ws](const std::string& kind,const std::string& name){
        static const std::set<std::string> kinds={
            "avatar","ragdoll","root_motion","montage"};
        if(!kinds.count(kind) || name.empty() || name.size()>64 ||
           name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")
               !=std::string::npos)
            throw std::invalid_argument("invalid animation asset kind/name");
        return contained(ws,"animations/"+kind+"/"+name+".json");
    };
    const auto makeMontage=[&ws](const Json& doc) {
        auto model=loadAnimationModel(ws,doc.getStr("model"));
        motionweave::MontageDefinition spec;
        spec.model=model.get();
        spec.slot=doc.getStr("slot","UpperBody");
        spec.blendIn=float(doc["blendIn"].toDouble(.1));
        spec.blendOut=float(doc["blendOut"].toDouble(.1));
        const auto mask=doc.getStr("maskRoot");
        if(!mask.empty()) {
            spec.maskRoot=motionweave::findNodePath(*model,mask);
            if(spec.maskRoot<0)throw std::invalid_argument("unknown montage mask bone");
        }
        if(!doc["sections"].isArray())
            throw std::invalid_argument("montage sections must be an array");
        for(const auto& item:doc["sections"].items()){
            motionweave::MontageSection s;
            s.name=item.getStr("name");
            s.clip=model->findClip(item.getStr("clip"));
            s.begin=float(item["begin"].toDouble());
            s.end=float(item["end"].toDouble());
            s.next=int(item.getInt("next",-1));
            for(const auto& n:item["notifies"].items())
                s.notifies.push_back({float(n["time"].toDouble()),n.getStr("name")});
            spec.sections.push_back(std::move(s));
        }
        auto compiled=motionweave::MontageDefinition::compile(std::move(spec));
        return doc; // asset validated; compiled pointers never outlive model
    };
    ws.registerOp({"animation_avatar_suggest",
        "Detect conservative semantic humanoid bone paths from a model.",
        Json::object(),[&ws](const Json& p){
            const auto file=p.getStr("model");
            auto model=loadAnimationModel(ws,file);
            auto profile=motionweave::humanoidProfileJson(
                file,motionweave::suggestHumanoidRoles(*model));
            Json r=Json::object();r.set("document",profile);return r;
        }});
    ws.registerOp({"animation_avatar_validate",
        "Validate explicit humanoid bone roles against a model.",
        Json::object(),[&ws](const Json& p){
            auto doc=p["document"];
            auto model=loadAnimationModel(ws,doc.getStr("model"));
            auto avatar=motionweave::HumanoidAvatar::compile(
                *model,motionweave::readHumanoidProfile(doc));
            Json r=Json::object();r.set("document",doc);
            r.set("mapped",Json::number(int64_t(
                std::count_if(avatar.nodes.begin(),avatar.nodes.end(),
                              [](int n){return n>=0;}))));
            return r;
        }});
    ws.registerOp({"animation_ragdoll_generate",
        "Build a ragdoll physics scene and bind mappings from a Humanoid Avatar.",
        Json::object(),[&ws](const Json& p){
            const auto& profile=p["profile"];
            auto model=loadAnimationModel(ws,profile.getStr("model"));
            auto avatar=motionweave::HumanoidAvatar::compile(
                *model,motionweave::readHumanoidProfile(profile));
            motionweave::RagdollBuildOptions opt;
            const auto& options=p["options"];
            opt.radiusRatio=float(options["radiusRatio"].toDouble(.17));
            opt.density=float(options["density"].toDouble(750));
            opt.friction=float(options["friction"].toDouble(.65));
            opt.jointLimit=float(options["jointLimit"].toDouble(.85));
            opt.collisionGroup=int(options.getInt("collisionGroup",-1));
            opt.tickRate=uint32_t(options.getInt("tickRate",60));
            opt.substeps=uint32_t(options.getInt("substeps",4));
            auto rag=motionweave::RagdollAsset::compile(avatar,{},opt);
            Json r=Json::object();r.set("document",rag.scene());
            r.set("bodyCount",Json::number(int64_t(rag.bindings().size())));
            return r;
        }});
    ws.registerOp({"animation_root_motion_generate",
        "Offline-bake root motion from authored clips to deterministic fixed frames.",
        Json::object(),[&ws](const Json& p){
            auto model=loadAnimationModel(ws,p.getStr("model"));
            const int clip=model->findClip(p.getStr("clip"));
            const int root=motionweave::findNodePath(*model,p.getStr("root"));
            if(clip<0 || root<0)throw std::invalid_argument("clip or root bone not found");
            motionweave::RootMotionTracksResource library;
            library.clips.push_back(motionweave::bakeRootMotion(
                *model,model->clips[size_t(clip)],root,
                uint32_t(p.getInt("id",1)),uint32_t(p.getInt("rate",60)),
                p.getBool("loop")));
            Json r=Json::object();
            r.set("document",motionweave::encodeRootMotionTracks(library));
            return r;
        }});
    ws.registerOp({"animation_montage_validate",
        "Validate authored animation Montage against its model and clips.",
        Json::object(),[makeMontage](const Json& p){
            Json r=Json::object();r.set("document",makeMontage(p["document"]));return r;
        }});
    const auto validateAsset=[&ws,makeMontage](const std::string& kind,const Json& doc){
        if(kind=="avatar"){
            auto model=loadAnimationModel(ws,doc.getStr("model"));
            motionweave::HumanoidAvatar::compile(
                *model,motionweave::readHumanoidProfile(doc));
        } else if(kind=="root_motion"){
            (void)motionweave::decodeRootMotionTracks(doc);
        } else if(kind=="montage"){
            (void)makeMontage(doc);
        } else if(kind=="ragdoll"){
            if(!doc["bodies"].isArray() || !doc["joints"].isArray() ||
               doc["bodies"].size()>4096 || doc["joints"].size()>4096)
                throw std::invalid_argument("invalid ragdoll body/joint document");
        } else throw std::invalid_argument("unknown animation asset kind");
    };
    ws.registerOp({"animation_asset_get",
        "Read versioned avatar/montage/root track/ragdoll animation asset.",
        Json::object(),[safeAsset](const Json& p){
            const auto file=safeAsset(p.getStr("kind"),p.getStr("name"));
            if(!fs::exists(file) || fs::file_size(file)>1024*1024)
                throw std::invalid_argument("asset missing or exceeds size limit");
            std::ifstream in(file,std::ios::binary);
            if(!in)throw std::runtime_error("cannot read asset");
            const std::string data((std::istreambuf_iterator<char>(in)),{});
            Json r=Json::object();
            r.set("document",Json::parse(data));return r;
        }});
    ws.registerOp({"animation_asset_save",
        "Save validated asset with optimistic concurrency. No overwrite without expected.",
        Json::object(),[safeAsset,validateAsset](const Json& p){
            const auto kind=p.getStr("kind"),name=p.getStr("name");
            const auto doc=p["document"];validateAsset(kind,doc);
            const auto file=safeAsset(kind,name);
            if(fs::exists(file)){
                if(!p.has("expected"))
                    throw std::runtime_error("asset already exists; reload first");
                if(fs::file_size(file)>1024*1024)
                    throw std::runtime_error("existing asset too large");
                std::ifstream prior(file,std::ios::binary);
                const std::string old((std::istreambuf_iterator<char>(prior)),{});
                if(!(Json::parse(old)==p["expected"]))
                    throw std::runtime_error("asset changed concurrently; reload");
            }
            const auto data=doc.dump(2)+"\n";
            if(data.size()>1024*1024)throw std::runtime_error("asset exceeds 1 MiB");
            fs::create_directories(file.parent_path());
            const auto tmp=fs::path(file.string()+".tmp");
            if(fs::exists(tmp))throw std::runtime_error("temporary file exists");
            {std::ofstream out(tmp,std::ios::binary);
             out<<data;out.close();if(!out)throw std::runtime_error("cannot save asset");}
            fs::rename(tmp,file);
            Json r=Json::object();r.set("ok",Json::fromBool(true));
            r.set("document",doc);return r;
        }});

}
}
