#include "AnimationOps.h"
#include "AnimationDocument.h"
#include "engine/render/AnimationAsset.h"
#include "engine/render/AnimationGraph.h"
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
}
}
