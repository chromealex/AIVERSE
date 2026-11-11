#include "engine/render/graph/ShaderGraph.h"
#include "engine/render/PostFxData.h"
#include "engine/fx/FxEffect.h"
#include <iostream>
#include <functional>
#include <stdexcept>
using namespace aiverse;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void rejects(const Json& g){try {render::graph::compile(g);}catch(const std::exception&){return;}throw std::runtime_error("invalid graph accepted");}
int main(){try {
    using namespace render::graph;
    for(auto domain:{"material","postfx","particle"}) {
        auto g=makeDefault(domain);auto p=compile(g,domain);require(p.outputs.at("color")=="sg_n0","identity output");
        for(const auto& op:catalog()) {
            auto test=g;Json n=Json::object();n.set("id",Json::string("test"));n.set("op",Json::string(op.id));n.set("value",Json::parse("[0.1,0.2,0.3,1]"));Json ins=Json::array();for(int i=0;i<op.inputs;++i)ins.push(Json::string("input"));n.set("inputs",ins);test.find("nodes")->push(n);test.find("outputs")->set("color",Json::string("test"));
            auto c=compile(test);require(c.glsl.find("sg_n1")!=std::string::npos,"GLSL node missing");require(c.msl.find("sg_n1")!=std::string::npos,"MSL node missing");
        }
    }
    Json g=Json::parse(R"({"version":1,"domain":"material","nodes":[{"id":"multiply","op":"multiply","inputs":["c","c"]},{"id":"c","op":"constant","value":[0.2,0.4,0.8,1]}],"outputs":{"color":"multiply","roughness":"c"}})");
    auto p=compile(g);require(p.glsl.find("sg_n1 =")<p.glsl.find("sg_n0 ="),"topological order");
    auto bad=g;bad.find("nodes")->at(0).find("inputs")->at(0)=Json::string("multiply");rejects(bad);
    bad=g;bad.find("nodes")->at(1).set("id",Json::string("multiply"));rejects(bad);
    bad=g;bad.find("nodes")->at(0).find("inputs")->at(1)=Json::string("absent");rejects(bad);
    bad=g;bad.find("nodes")->at(1).set("value",Json::numberText("1e999"));rejects(bad);
    bad=g;bad.set("domain",Json::string("postfx"));rejects(bad);
    bad=g;bad.find("nodes")->at(0).set("op",Json::string("injected;code"));rejects(bad);
    bad=g;bad.find("outputs")->set("color",Json::string("absent"));rejects(bad);
    bad=g;bad.find("nodes")->at(1).set("value",Json::parse("[1,2,3]"));rejects(bad);
    bad=g;bad.set("version",Json::number(int64_t(2)));rejects(bad);
    bad=makeDefault("material");
    for(int i=0;i<128;++i){Json n=Json::object();n.set("id",Json::string("extra"+std::to_string(i)));n.set("op",Json::string("input"));bad.find("nodes")->push(n);}rejects(bad);
    bad=makeDefault("postfx");
    for(int i=0;i<9;++i){Json n=Json::object();n.set("id",Json::string("tex"+std::to_string(i)));n.set("op",Json::string("texture"));n.set("inputs",Json::parse("[\"input\"]"));bad.find("nodes")->push(n);}rejects(bad);
    bad=g;bad.find("nodes")->at(0).set("position",Json::parse("[1]"));rejects(bad);
    Json fx=Json::parse(R"({"emitters":[{"burst":1}]})");fx.find("emitters")->at(0).set("shaderGraph",makeDefault("particle"));
    fx::EffectDef def;std::vector<std::string> errors;require(fx::parseEffect(fx,"test",def,errors),"particle graph parser");
    require(def.emitters[0].shaderGraph==makeDefault("particle"),"particle graph lost");
    require(fx::effectToJson(def)["emitters"][0]["shaderGraph"]==makeDefault("particle"),"particle graph serialization");
    Json effect=Json::object();effect.set("type",Json::string("shaderGraph"));effect.set("graph",makeDefault("postfx"));
    auto e=render::parsePostFxEffect(effect,true);require(render::postFxEffectJson(e)["graph"]==effect["graph"],"postfx round trip");
    effect.set("graph",makeDefault("material"));auto invalid=render::parsePostFxEffect(effect,false,&errors);require(!invalid.enabled,"invalid postfx must be disabled");
    std::cout<<"shader graph compiler/domain/round-trip tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
