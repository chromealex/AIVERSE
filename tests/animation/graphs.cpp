#include "engine/render/AnimationGraph.h"
#include "engine/render/AnimationRig.h"
#include "engine/render/FullBodyIK.h"
#include "engine/render/AnimationAsset.h"
#include "apps/editor/core/AnimationDocument.h"
#include "engine/render/AnimationMotion.h"
#include <cmath>
#include <cstdio>
#include <limits>
#include <cstdlib>
using namespace aiverse;
using namespace aiverse::render;
namespace aiverse::platform { bool readContentFile(const std::string&,std::string&) { return false; } }
static int checks=0;
void check(bool ok,const char* message) { ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);} }
bool near(float a,float b){return std::abs(a-b)<.002f;}
Model model() {
    Model m; m.nodes.resize(4); m.order={0,1,2,3};
    m.nodes[0].name="root";m.nodes[0].children={1,3};
    m.nodes[1].name="knee";m.nodes[1].parent=0;m.nodes[1].children={2};m.nodes[1].t={0,1,0};
    m.nodes[2].name="foot";m.nodes[2].parent=1;m.nodes[2].t={0,1,0};
    m.nodes[3].name="other";m.nodes[3].parent=0;
    AnimClip a; a.name="Move";a.duration=1;
    AnimChannel c;c.node=0;c.times={0,1};c.values={0,0,0,2,0,0};a.channels.push_back(c);
    c.node=3;c.values={0,0,0,0,4,0};a.channels.push_back(c);m.clips.push_back(a);
    return m;
}
int main() {
    auto m=model();
    auto parse=[](const char* s){return Json::parse(s);};
    auto def=parse(R"({"parameters":{"mix":0.5,"speed":1,"go":0,"target":[1,1,0],"pole":[0,0,1]},"nodes":[
      {"id":"base","kind":"bind"},{"id":"move","kind":"clip","clip":"Move","parameter":"speed"},
      {"id":"mix","kind":"blend","a":"base","b":"move","parameter":"mix"},
      {"id":"mask","kind":"blend","a":"base","b":"move","mask":"other"},
      {"id":"add","kind":"additive","a":"move","b":"mix","mask":"other"},
      {"id":"ik","kind":"twoBoneIK","input":"base","hip":"root","knee":"knee","foot":"foot","target":"target","pole":"pole"}],"output":"mix"})");
    AnimationGraphInstance a;a.reset(AnimationGraph::compile(m,def));a.update(.5f);Pose p;a.evaluate(p);
    check(near(p[0].t.x,.5f)&&near(p[3].t.y,1),"half-weight pose blend");
    a.parameters().set("mix",1.f);a.evaluate(p);check(near(p[0].t.x,1),"parameter drives blend");
    auto other=def;other.set("output",Json::string("mask"));a.reset(AnimationGraph::compile(m,other));a.update(.5f);a.evaluate(p);
    check(near(p[0].t.x,0)&&near(p[3].t.y,2),"subtree mask preserves other bones");
    other.set("output",Json::string("add"));a.reset(AnimationGraph::compile(m,other));a.update(.5f);a.evaluate(p);
    check(near(p[0].t.x,1)&&near(p[3].t.y,3),"additive bind-relative masked pose");
    other.set("output",Json::string("ik"));a.reset(AnimationGraph::compile(m,other));a.evaluate(p);
    std::vector<mat4> world;worldMatrices(m,p,world);
    check(near(world[2].at(0,3),1)&&near(world[2].at(1,3),1),"IK reaches target");
    a.parameters().set("target",float3{0,-1.5f,0});a.evaluate(p);worldMatrices(m,p,world);
    check(near(world[2].at(1,3),-1.5f),"IK antiparallel target");
    a.parameters().set("target",float3{0,1.5f,0});a.parameters().set("pole",float3{1,0,0});a.evaluate(p);worldMatrices(m,p,world);
    check(world[1].at(0,3)>.1f,"IK pole chooses positive bend");
    a.parameters().set("pole",float3{-1,0,0});a.evaluate(p);worldMatrices(m,p,world);
    check(world[1].at(0,3)<-.1f,"IK pole switches bend plane");
    a.parameters().set("target",float3{0,20,0});a.evaluate(p);worldMatrices(m,p,world);
    check(near(world[2].at(1,3),2),"IK unreachable target is clamped");
    other=def;other.erase("output");other.set("initial",Json::string("idle"));
    other.set("states",parse(R"([{"id":"idle","output":"base","transitions":[{"to":"walk","fade":0.4,"when":[{"parameter":"go","op":"gt","value":0}]}]},{"id":"walk","output":"move","transitions":[{"to":"idle","fade":0.4,"when":[{"parameter":"go","op":"eq","value":0}]}]}])"));
    a.reset(AnimationGraph::compile(m,other));a.parameters().set("go",1.f);a.update(.1f);check(a.state()=="walk","state transition");
    a.update(.2f);a.evaluate(p);check(near(p[0].t.x,.2f),"crossfade interpolation");
    a.parameters().set("go",0.f);a.update(0);a.evaluate(p);check(near(p[0].t.x,.2f),"interrupted fade has no pop");
    a.update(.2f);a.evaluate(p);check(near(p[0].t.x,.1f),"interrupted fade decays to destination");
    a.update(std::numeric_limits<float>::quiet_NaN());check(near(a.stateTime(),.2f),"reject non-finite dt");
    check(!a.parameters().set("bad",std::numeric_limits<float>::infinity()),"reject infinite parameter");
    AnimationGraphInstance b;b.reset(AnimationGraph::compile(m,def));b.update(.25f);b.evaluate(p);check(near(p[0].t.x,.25f),"independent instances");
    b.parameters().set("speed",0.f);b.update(.25f);b.evaluate(p);check(near(p[0].t.x,.25f),"speed change preserves phase");
    b.parameters().set("speed",2.f);b.update(.25f);b.evaluate(p);check(near(p[0].t.x,.75f),"speed integrates over time");
    auto native=std::make_shared<AnimationOperation>();native->stateFloats=1;
    native->update=[](const Json&,const AnimationParameters&,float dt,std::span<float> state) noexcept {state[0]+=dt;};
    native->evaluate=[](const Model&,const Json&,const AnimationParameters&,std::span<const float> state,Pose& pose) noexcept {pose[0].t.y+=state[0];};
    auto nativeDef=parse(R"({"nodes":[{"id":"base","kind":"bind"},{"id":"custom","kind":"native","input":"base","operation":"lift"}],"output":"custom"})");
    auto nativeGraph=AnimationGraph::compile(m,nativeDef,{{"lift",native}});
    a.reset(nativeGraph);b.reset(nativeGraph);a.update(.25f);a.evaluate(p);check(near(p[0].t.y,.25f),"native operation executes");
    b.evaluate(p);check(near(p[0].t.y,0),"native state is per instance");
    a.evaluate(p);a.evaluate(p);check(near(p[0].t.y,.25f),"evaluation does not advance native state");
    check(nativeGraph->states()[0].program.size()==2,"compiled evaluation program");
    auto rejects=[&](Json j,const char* label){bool caught=false;try{AnimationGraph::compile(m,j);}catch(const std::exception&){caught=true;}check(caught,label);};
    rejects(nativeDef,"unregistered native operation is rejected");
    auto broken=def;broken.member("nodes").at(2).set("a",Json::string("mix"));rejects(broken,"reject graph cycle");
    broken=def;broken.member("nodes").at(1).set("clip",Json::string("absent"));rejects(broken,"reject missing clip");
    broken=def;broken.member("nodes").at(3).set("mask",Json::string("absent"));rejects(broken,"reject missing bone mask");
    broken=def;broken.member("nodes").at(5).set("foot",Json::string("root"));rejects(broken,"reject malformed IK chain");
    broken=def;broken.member("nodes").at(2).set("parameter",Json::string("absent"));rejects(broken,"reject missing parameter");
    broken=other;broken.member("states").at(0).member("transitions").at(0).set("to",Json::string("absent"));rejects(broken,"reject missing destination");
    #include "uaf.inc"
#include "fullbody.inc"
#include "contacts.inc"
#include "climbing.inc"
    std::printf("Animation graph checks: %d passed\n",checks);
}
