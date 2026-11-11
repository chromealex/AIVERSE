#include "AnimationDocument.h"
#include <algorithm>
#include <functional>
#include <map>
#include <stdexcept>
namespace aiverse::editor::animation {
namespace {void fail(const std::string& s){throw std::runtime_error("animation document: "+s);}std::vector<std::string> ports(const Json& n){auto k=n.getStr("kind");if(k=="blend"||k=="additive")return {"a","b"};if(k=="rig"||k=="stack"||k=="native"||k=="twoBoneIK")return {"input"};return {};}}
Json makeGraph(){return Json::parse(R"({"parameters":{},"nodes":[{"id":"bind","kind":"bind","position":[30,50]}],"output":"bind"})");}
Json makeNode(const std::string& kind,const std::string& id,const std::string& input){
    Json n=Json::object();n.set("id",Json::string(id));n.set("kind",Json::string(kind));n.set("position",Json::parse("[250,80]"));for(auto& p:ports(n))n.set(p,Json::string(input));
    if(kind=="clip"){n.set("clip",Json::string(""));n.set("loop",Json::fromBool(true));n.set("speed",Json::number(1.0));}
    if(kind=="blend"||kind=="additive")n.set("weight",Json::number(.5));
    if(kind=="rig")n.set("units",Json::array());
    if(kind=="stack")n.set("traits",Json::array());
    if(kind=="subgraph"){n.set("graph",Json::string(""));n.set("bindings",Json::object());}
    if(kind=="native"){n.set("operation",Json::string(""));n.set("config",Json::object());}
    if(kind=="twoBoneIK")for(auto k:{"hip","knee","foot","target","pole"})n.set(k,Json::string(""));
    if(kind=="motionMatching")n.set("database",Json::parse(R"({"root":"","clips":[""],"bones":[],"trajectory":[{"time":0.3,"parameter":"future"}],"fps":30,"searchInterval":0.1,"blend":0.15,"stripRoot":true})"));
    return n;
}
Json makeContacts(const std::string& root,const std::string& foot){
    auto c=Json::parse(R"({"world":true,"up":[0,1,0],"maxSlope":45,"rootAdjust":0.5,"collisionWeight":30,"maxCorrection":0.05,"feet":[{"bone":"","offset":[0,0,0],"up":[0,1,0],"radius":0.08,"probeUp":0.3,"probeDown":0.6,"releaseDistance":0.5,"weight":20,"align":0.5}],"capsules":[{"bone":"","offset":[0,0,0],"radius":0.1,"samples":4}],"obstacles":[],"balance":{"strength":0.5,"margin":0.02,"masses":[{"bone":"","mass":1,"offset":[0,0,0]}]}})");
    c.member("feet").at(0).set("bone",Json::string(foot));c.member("capsules").at(0).set("bone",Json::string(root));c.member("balance").member("masses").at(0).set("bone",Json::string(root));return c;
}
Json makeUnit(const std::string& op){Json u=Json::object();u.set("op",Json::string(op));u.set("bone",Json::string(""));u.set("weight",Json::number(1.0));
    if(op=="transform"){u.set("translation",Json::parse("[0,0,0]"));u.set("rotation",Json::parse("[0,0,0]"));u.set("scale",Json::parse("[1,1,1]"));u.set("space",Json::string("local"));u.set("additive",Json::fromBool(true));}
    if(op=="copy"){u.set("source",Json::string(""));u.set("space",Json::string("local"));}
    if(op=="aim"){u.set("axis",Json::parse("[0,0,1]"));u.set("target",Json::string("target"));}
    if(op=="twoBoneIK")for(auto k:{"knee","end","target","pole"})u.set(k,Json::string(""));
    if(op=="limit"){u.set("min",Json::parse("[-1,-1,-1]"));u.set("max",Json::parse("[1,1,1]"));}
    if(op=="surfaceLocomotion"){u.set("velocity",Json::string("velocity"));u.set("clearance",Json::number(.5));u.set("probe",Json::number(.8));u.set("stepDistance",Json::number(.3));u.set("stepHeight",Json::number(.15));u.set("stepDuration",Json::number(.25));u.set("turnSpeed",Json::number(4.0));u.set("maxSpeed",Json::number(2.0));u.set("bodyRadius",Json::number(.2));u.set("minSupport",Json::number(int64_t(3)));u.set("maxSwing",Json::number(int64_t(1)));u.set("world",Json::fromBool(true));u.set("followSurface",Json::fromBool(true));u.set("legs",Json::array());u.set("obstacles",Json::array());u.set("capsules",Json::array());u.set("collisionIterations",Json::number(int64_t(24)));}
    if(op=="fullBodyIK"){u.set("iterations",Json::number(int64_t(24)));u.set("rootPull",Json::number(0.0));u.set("damping",Json::number(.05));u.set("tolerance",Json::number(.001));u.set("effectors",Json::parse(R"([{"bone":"","target":"target","weight":1}])"));u.set("joints",Json::array());}return u;
}
void sort(Json& graph){Json copy=graph;auto& nodes=copy.member("nodes");if(!nodes.isArray()||nodes.size()==0||nodes.size()>128)fail("expected 1..128 nodes");std::map<std::string,size_t> ids;for(size_t i=0;i<nodes.size();++i){auto id=nodes[i].getStr("id");if(id.empty()||!ids.emplace(id,i).second)fail("duplicate/empty node id");}std::vector<int> marks(nodes.size());Json sorted=Json::array();std::function<void(size_t)> visit=[&](size_t i){if(marks[i]==1)fail("cycle");if(marks[i]==2)return;marks[i]=1;for(auto& p:ports(nodes[i])){auto it=ids.find(nodes[i].getStr(p));if(it==ids.end())fail("missing input");visit(it->second);}marks[i]=2;sorted.push(nodes[i]);};for(size_t i=0;i<nodes.size();++i)visit(i);copy.set("nodes",sorted);graph=std::move(copy);}
void connect(Json& graph,const std::string& source,const std::string& target,const std::string& port){Json copy=graph;bool found=false;for(auto& n:copy.member("nodes").itemsMut())if(n.getStr("id")==target){auto p=ports(n);if(std::find(p.begin(),p.end(),port)==p.end())fail("invalid input port");n.set(port,Json::string(source));found=true;}if(!found)fail("unknown node");sort(copy);graph=std::move(copy);}
void rename(Json& graph,const std::string& from,const std::string& to){if(to.empty())fail("empty id");Json copy=graph;bool found=false;for(auto& n:copy.member("nodes").itemsMut()){if(n.getStr("id")==to)fail("duplicate id");if(n.getStr("id")==from){n.set("id",Json::string(to));found=true;}for(auto& p:ports(n))if(n.getStr(p)==from)n.set(p,Json::string(to));}if(!found)fail("unknown node");if(copy.getStr("output")==from)copy.set("output",Json::string(to));if(auto* states=copy.find("states"))for(auto& s:states->itemsMut())if(s.getStr("output")==from)s.set("output",Json::string(to));graph=std::move(copy);}
void remove(Json& graph,const std::string& id){for(const auto& n:graph["nodes"].items())for(auto& p:ports(n))if(n.getStr(p)==id)fail("disconnect consumers first");if(graph.getStr("output")==id)fail("select another output first");for(const auto& s:graph["states"].items())if(s.getStr("output")==id)fail("state uses this node");auto& a=graph.member("nodes").itemsMut();a.erase(std::remove_if(a.begin(),a.end(),[&](auto& n){return n.getStr("id")==id;}),a.end());}
}
