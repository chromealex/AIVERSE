#include "AnimationOps.h"
#include "AnimationPreviewSession.h"
#include "engine/pack/Sha256.h"
#include "AnimationDocument.h"
#include "engine/render/AnimationAsset.h"
#include "engine/render/AnimationGraph.h"
#include "engine/render/ModelClips.h"
#include "engine/animation/MotionWeave.h"
#include "libraries/include/aiverselib/animation/HumanoidProfile.h"
#include "libraries/include/aiverselib/animation/RagdollAsset.h"
#include "libraries/include/aiverselib/animation/RootMotionBaker.h"
#include "engine/animation/AnimationMontage.h"
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
std::string graphRevision(const Json& graph){
    const auto bytes=graph.dump(-1);
    const auto digest=pack::sha256(bytes.data(),bytes.size());
    return pack::toHex(digest.data(),digest.size());
}
Json animationNodePaths(const render::Model& model){
    Json paths=Json::array();
    for(size_t i=0;i<model.nodes.size();++i)
        paths.push(Json::string(motionweave::bonePath(model,int(i))));
    return paths;
}
Json result(Json graph){Json r=Json::object();r.set("ok",Json::fromBool(true));r.set("graph",std::move(graph));return r;}
Json savedResult(const Json& graph){
    auto response=result(graph);
    response.set("revision",Json::string(graphRevision(graph)));
    return response;
}
void validate(const Workspace& w,const Json& g){auto m=loadAnimationModel(w,g.getStr("model"));render::AnimationGraph::compile(*m,g);}
}
std::unique_ptr<render::Model> loadAnimationModel(const Workspace& w,const std::string& file){if(file.empty())throw std::runtime_error("select a model");auto reader=[&w](const std::string& p,std::string& out){auto f=contained(w,p);std::ifstream stream(f,std::ios::binary);if(!stream)return false;out.assign(std::istreambuf_iterator<char>(stream),{});return true;};auto m=render::loadModel(file,reader);if(!m)throw std::runtime_error("cannot load animation model");return m;}
void registerAnimationOps(Workspace& ws){auto schema=Json::parse(R"({"type":"object","properties":{"name":{"type":"string"},"graph":{"type":"object"},"expected":{"type":"object"},"expectedRevision":{"type":"string"},"target":{"type":"string"},"expectedModel":{"type":"string"}}})");
    auto previews=ws.animationPreviews=std::make_shared<AnimationPreviewSessions>();
    ws.registerOp({"animation_graph_preview","Open a native pose graph preview, advance its clock, change inputs or release it.",
        Json::parse(R"({"type":"object","properties":{"action":{"type":"string","enum":["open","frame","close"]},"panel":{"type":"string","enum":["graph","rig"]},"graph":{"type":"object"},"scope":{"type":"string"},"bindPose":{"type":"boolean"},"token":{"type":"integer"},"dt":{"type":"number"},"ground":{"type":"number"},"yaw":{"type":"number"},"inputs":{"type":"object"}},"required":["action","panel"]})"),
        [&ws,previews](const Json& p){return previews->command(ws,p);}});
    ws.registerOp({"animation_graph_edit","Edit a graph draft using the native animation document operations without saving it.",
        Json::parse(R"({"type":"object","properties":{"action":{"type":"string"},"graph":{"type":"object"},"scope":{"type":"string"},"id":{"type":"string"},"kind":{"type":"string"},"source":{"type":"string"},"index":{"type":"integer"},"port":{"type":"string"},"name":{"type":"string"}},"required":["action"]})"),
        [](const Json& p){
            const auto action=p.getStr("action");
            if(action=="new")return result(animation::makeGraph());
            Json draft=p["graph"];
            if(!draft.isObject()||draft.dump().size()>256*1024)throw std::runtime_error("Invalid animation draft");
            Json* graph=&draft;
            const auto scope=p.getStr("scope");
            if(!scope.empty()){
                auto* children=draft.find("graphs");
                graph=children?children->find(scope):nullptr;
            }
            if(!graph||!(*graph)["nodes"].isArray())throw std::runtime_error("Unknown animation graph scope");
            const auto id=p.getStr("id");
            if(action=="addSubgraph"){
                const auto name=p.getStr("name");
                if(name.empty()||draft["graphs"].has(name))throw std::runtime_error("Enter a unique subgraph name");
                if(draft["graphs"].size()>=64)throw std::runtime_error("At most 64 subgraphs are supported");
                draft.member("graphs").set(name,animation::makeGraph());
            }else if(action=="setSubgraph"){
                const auto name=p.getStr("name");
                const auto& children=draft["graphs"];
                if(!children.has(name))throw std::runtime_error("Unknown subgraph");
                std::vector<std::string> pending{name};std::set<std::string> visited;
                while(!pending.empty()){
                    const auto child=pending.back();pending.pop_back();
                    if(child==scope)throw std::runtime_error("Recursive subgraph reference");
                    if(!visited.insert(child).second)continue;
                    for(const auto& node:children[child]["nodes"].items()){
                        const auto next=node.getStr("graph");
                        if(node.getStr("kind")=="subgraph" && children.has(next))pending.push_back(next);
                    }
                }
                auto& nodes=graph->member("nodes").itemsMut();
                const auto node=std::find_if(nodes.begin(),nodes.end(),[&](const Json& n){return n.getStr("id")==id;});
                if(node==nodes.end()||node->getStr("kind")!="subgraph")throw std::runtime_error("Select a subgraph node");
                node->set("graph",Json::string(name));
                Json bindings=Json::object();
                for(const auto& [key,value]:(*node)["bindings"].members())
                    if(children[name]["parameters"].has(key))bindings.set(key,value);
                node->set("bindings",std::move(bindings));
            }else if(action=="connect")animation::connect(*graph,p.getStr("source"),id,p.getStr("port"));
            else if(action=="remove")animation::remove(*graph,id);
            else if(action=="rename")animation::rename(*graph,id,p.getStr("name"));
            else if(action=="addUnit"||action=="addContacts"){
                auto& nodes=graph->member("nodes").itemsMut();
                auto node=std::find_if(nodes.begin(),nodes.end(),[&](const Json& n){return n.getStr("id")==id&&n.getStr("kind")=="rig";});
                if(node==nodes.end())throw std::runtime_error("Select a rig node");
                auto& units=node->member("units");
                if(!units.isArray())throw std::runtime_error("Rig has invalid units");
                if(action=="addContacts"){
                    const int index=p.getInt("index",-1);
                    if(index<0||size_t(index)>=units.size())throw std::runtime_error("Select a rig unit");
                    auto& unit=units.at(index);
                    if(unit.getStr("op")!="fullBodyIK"||unit.has("contacts"))throw std::runtime_error("Contacts require a full body IK unit without contacts");
                    const auto root=unit.getStr("bone");
                    unit.set("contacts",animation::makeContacts(root,unit["effectors"][0].getStr("bone",root)));
                }else{
                    static const std::set<std::string> kinds={"transform","copy","aim","twoBoneIK","limit","fullBodyIK","surfaceLocomotion"};
                    const auto kind=p.getStr("kind");
                    if(!kinds.count(kind)||units.size()>=64)throw std::runtime_error("Invalid rig operation or rig is full");
                    units.push(animation::makeUnit(kind));
                    if(kind=="surfaceLocomotion"&&!(*graph)["parameters"].has("velocity"))
                        graph->member("parameters").set("velocity",Json::parse("[0,0,0]"));
                }
            }else if(action=="add"){
                static const std::set<std::string> kinds={"bind","clip","blend","additive","rig","stack","twoBoneIK","subgraph","motionMatching","native"};
                const auto kind=p.getStr("kind");
                if(!kinds.count(kind)||id.empty())throw std::runtime_error("Invalid animation node");
                const auto& nodes=(*graph)["nodes"];
                auto node=animation::makeNode(kind,id,graph->getStr("output",nodes[0].getStr("id")));
                int row=0;
                for(const auto& existing:nodes.items()){
                    const auto& pos=existing["position"];
                    if(std::abs(pos[0].toDouble()-250)<160)row=std::max(row,int(std::clamp(pos[1].toDouble(),0.0,100000.0))+78);
                }
                Json pos=Json::array();pos.push(Json::number(int64_t(250)));pos.push(Json::number(int64_t(row)));
                node.set("position",std::move(pos));
                graph->member("nodes").push(std::move(node));
                animation::sort(*graph);
            }else throw std::runtime_error("Unknown animation graph edit");
            return result(std::move(draft));
        }});
    ws.registerOp({"animation_graph_list","List animation/rig assets in animations/.",Json::object(),[&ws](const Json&){Json r=Json::object(),a=Json::array();auto dir=path(ws,"placeholder").parent_path();std::vector<std::string> names;if(fs::exists(dir))for(auto& e:fs::directory_iterator(dir))if(e.is_regular_file()&&e.path().extension()==".json")names.push_back(e.path().stem().string());std::sort(names.begin(),names.end());for(auto& n:names)a.push(Json::string(n));r.set("graphs",a);r.set("rigItemPresets",animation::rigItemPresets());return r;}});
    ws.registerOp({"animation_graph_get","Read an animation/rig asset without executing custom callbacks.",schema,[&ws](const Json& p){return savedResult(load(ws,p.getStr("name")));}});
    ws.registerOp({"animation_graph_validate","Compile graph and rig against the selected model. Custom host callbacks require their host registry.",schema,[&ws](const Json& p){validate(ws,p["graph"]);return result(p["graph"]);}});
    ws.registerOp({"animation_graph_set","Save an authoring draft; validates topology, not model-specific execution. expected prevents overwriting concurrent edits.",schema,[&ws](const Json& p){Json g=p["graph"];animation::sort(g);if(auto* graphs=g.find("graphs"))for(auto& [name,child]:graphs->membersMut()){(void)name;animation::sort(child);}auto file=path(ws,p.getStr("name"));if(fs::exists(file)){
        const auto current=load(ws,p.getStr("name"));
        const bool unchanged=p.has("expectedRevision")?p.getStr("expectedRevision")==graphRevision(current):
            p.has("expected")&&current==p["expected"];
        if(!unchanged)throw std::runtime_error("asset changed; reload before saving");
    }else if(p.has("expectedRevision")||p.has("expected"))throw std::runtime_error("asset removed; reload before saving");
    auto text=g.dump(2)+"\n";if(text.size()>256*1024)throw std::runtime_error("asset exceeds 256 KiB");fs::create_directories(file.parent_path());auto temp=file;temp+=".tmp";if(fs::is_symlink(temp))throw std::runtime_error("temporary path is symlink");{std::ofstream f(temp,std::ios::binary);f<<text;f.close();if(!f)throw std::runtime_error("cannot save asset");}fs::rename(temp,file);return savedResult(g);}});
    ws.registerOp({"animation_graph_apply","Validate the saved animation graph against the target model and apply it through rules_set.",schema,[&ws](const Json& p){
        auto graph=load(ws,p.getStr("name"));
        if(p.has("expectedRevision") && p.getStr("expectedRevision")!=graphRevision(graph))
            throw std::runtime_error("Animation graph changed; reload before applying");
        const auto target=p.getStr("target");
        const auto& types=ws.project.rulesJson["types"];
        if(!types.has(target))throw std::runtime_error("Unknown target type");
        Json query=Json::object();query.set("path",Json::string("types"));query.set("resolved",Json::fromBool(true));
        auto resolved=ws.call("rules_get",query);if(resolved.has("error"))return resolved;
        const auto model=resolved["value"][target]["view"].getStr("model");
        if(model.empty())throw std::runtime_error("Target needs a single model");
        if(p.has("expectedModel") && p.getStr("expectedModel")!=model)
            throw std::runtime_error("Target model changed; select the type again");
        graph.set("model",Json::string(model));validate(ws,graph);
        auto view=types[target]["view"];if(view.isNull())view=Json::object();
        view.set("animationGraph",std::move(graph));view.set("programmableAnimation",Json::fromBool(true));
        Json edit=Json::object();edit.set("path",Json::string("types."+target+".view"));edit.set("value",std::move(view));
        return ws.call("rules_set",edit);
    }});

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
    ws.registerOp({"animation_asset_list","List authored animation assets of a kind; avatar results include the semantic bone roles.",
        Json::parse(R"({"properties":{"kind":{"type":"string"}},"required":["kind"]})"),[safeAsset](const Json& p){
            const auto kind=p.getStr("kind");
            const auto folder=safeAsset(kind,"placeholder").parent_path();
            std::set<std::string> names;
            if(fs::is_directory(folder)) for(const auto& entry:fs::directory_iterator(folder)){
                const auto name=entry.path().stem().string();
                if(entry.is_regular_file() && entry.path().extension()==".json" && name.size()<=64 &&
                   name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==std::string::npos)
                    names.insert(name);
            }
            Json result=Json::object(),files=Json::array(),roles=Json::array();
            for(const auto& name:names) files.push(Json::string(name));
            if(kind=="avatar") for(const auto* role:motionweave::kHumanoidRoleNames) roles.push(Json::string(role));
            result.set("names",files);result.set("roles",roles);return result;
        }});
    const auto makeMontage=[&ws](const Json& doc) {
        auto model=loadAnimationModel(ws,doc.getStr("model"));
        motionweave::MontageDefinition spec;
        spec.model=model.get();
        spec.slot=doc.getStr("slot","UpperBody");
        spec.blendIn=float(doc["blendIn"].toDouble(.1));
        spec.blendOut=float(doc["blendOut"].toDouble(.1));
        const auto mask=doc.getStr("maskRoot");
        if(!mask.empty()) {
            spec.maskRoot=render::findNodePath(*model,mask);
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
            Json r=Json::object();r.set("document",profile);
            if(p.getBool("includeNodes"))r.set("nodes",animationNodePaths(*model));
            return r;
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
    ws.registerOp({"animation_model_info","Inspect clips and bone paths on a project-relative animation model.",
        Json::parse(R"({"properties":{"model":{"type":"string"}},"required":["model"]})"),[&ws](const Json& p){
            const auto model=loadAnimationModel(ws,p.getStr("model"));
            Json result=Json::object(),clips=Json::array();
            for(const auto& clip:model->clips){
                Json entry=Json::object();entry.set("name",Json::string(clip.name));
                entry.set("duration",Json::number(double(clip.duration)));clips.push(entry);
            }
            result.set("clips",clips);result.set("nodes",animationNodePaths(*model));return result;
        }});
    ws.registerOp({"animation_root_motion_generate",
        "Offline-bake root motion from authored clips to deterministic fixed frames.",
        Json::parse(R"({"properties":{"model":{"type":"string"},"clip":{"type":"string"},"root":{"type":"string"},"id":{"type":"integer","minimum":1,"maximum":4294967295},"rate":{"type":"integer","minimum":10,"maximum":240},"loop":{"type":"boolean"}},"required":["model","clip","root"]})"),[&ws](const Json& p){
            const auto id=p.getInt("id",1),rate=p.getInt("rate",60);
            if(id<1 || id>UINT32_MAX || rate<10 || rate>240)
                throw std::invalid_argument("Invalid root motion track ID or tick rate");
            auto model=loadAnimationModel(ws,p.getStr("model"));
            const int clip=model->findClip(p.getStr("clip"));
            const int root=render::findNodePath(*model,p.getStr("root"));
            if(clip<0 || root<0)throw std::invalid_argument("clip or root bone not found");
            motionweave::RootMotionTracksResource library;
            library.clips.push_back(motionweave::bakeRootMotion(
                *model,model->clips[size_t(clip)],root,
                uint32_t(id),uint32_t(rate),
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
            const Json doc=Json::parse(data);r.set("revision",Json::string(graphRevision(doc)));
            r.set("document",doc);return r;
        }});
    ws.registerOp({"animation_asset_save",
        "Save validated asset with optimistic concurrency. No overwrite without expected.",
        Json::object(),[safeAsset,validateAsset](const Json& p){
            const auto kind=p.getStr("kind"),name=p.getStr("name");
            const auto doc=p["document"];validateAsset(kind,doc);
            const auto file=safeAsset(kind,name);
            if(!fs::exists(file) && p.has("expectedRevision"))throw std::runtime_error("asset was removed; reload");
            if(fs::exists(file)){
                if(!p.has("expected") && !p.has("expectedRevision"))
                    throw std::runtime_error("asset already exists; reload first");
                if(fs::file_size(file)>1024*1024)
                    throw std::runtime_error("existing asset too large");
                std::ifstream prior(file,std::ios::binary);
                const std::string old((std::istreambuf_iterator<char>(prior)),{});
                const Json previous=Json::parse(old);
                if((p.has("expected") && !(previous==p["expected"])) ||
                   (p.has("expectedRevision") && graphRevision(previous)!=p.getStr("expectedRevision")))
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
            r.set("revision",Json::string(graphRevision(doc)));r.set("document",doc);return r;
        }});

}
}
