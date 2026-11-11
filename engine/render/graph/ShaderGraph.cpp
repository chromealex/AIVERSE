#include "ShaderGraph.h"
#include "libraries/include/aiverselib/Graphs.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <set>

namespace aiverse::render::graph {
const std::vector<Op>& catalog() {
    static const std::vector<Op> ops = {{"constant",0},{"uv",0},{"input",0},{"texture",1},
        {"add",2},{"subtract",2},{"multiply",2},{"divide",2},{"mix",3},{"sin",1},
        {"fract",1},{"clamp",1},{"oneMinus",1},{"dot",2},{"length",1},{"step",2},
        {"red",1},{"green",1},{"blue",1},{"alpha",1}};
    return ops;
}
namespace {
[[noreturn]] void fail(const std::string& s) { throw std::runtime_error("shader graph: " + s); }
std::string number(const Json& j) {
    if (!j.isNumber()) fail("constant components must be numbers");
    double x = j.toDouble();
    if (!std::isfinite(x) || std::abs(x) > 1000000) fail("constant must be finite and within +/-1000000");
    std::ostringstream s; s.imbue(std::locale::classic()); s << std::fixed << std::setprecision(8) << x;
    return s.str();
}
}
Program compile(const Json& g, const std::string& expected) {
    if (!g.isObject() || g.getInt("version") != 1) fail("expected version 1 object");
    Program p; p.domain = g.getStr("domain");
    if (p.domain != "material" && p.domain != "postfx" && p.domain != "particle") fail("unknown domain");
    if (!expected.empty() && p.domain != expected) fail("expected " + expected + ", got " + p.domain);
    const auto& ns = g["nodes"];
    if (!ns.isArray() || ns.size() == 0 || ns.size() > 128) fail("expected 1..128 nodes");
    std::map<std::string,size_t> ids;
    for (size_t i=0;i<ns.size();++i) {
        const auto& n=ns[i]; const auto id=n.getStr("id");
        if (id.empty() || id.size()>64 || !ids.emplace(id,i).second) fail("empty, duplicate or oversized node id");
        auto it=std::find_if(catalog().begin(),catalog().end(),[&](auto& o){return n.getStr("op")==o.id;});
        if (it==catalog().end()) fail("node '"+id+"': unknown operation");
        if(n.has("position")) {
            const auto& pos=n["position"];
            if(!pos.isArray() || pos.size()!=2) fail("position must be [x,y]");
            number(pos[0]); number(pos[1]);
        }
        if ((!n["inputs"].isArray() && it->inputs) || n["inputs"].size()!=size_t(it->inputs)) fail("node '"+id+"': wrong input count");
    }
    lib::Graph edges(ns.size());
    for (size_t i=0;i<ns.size();++i) for (const auto& in:ns[i]["inputs"].items()) {
        auto it=ids.find(in.str());
        if (!in.isString() || it==ids.end()) fail("node '"+ns[i].getStr("id")+"': missing input node");
        edges[it->second].push_back(i);
    }
    auto order=lib::topological(edges); if (!order) fail("cycle in node connections");
    if (!g["outputs"].isObject() || !g["outputs"]["color"].isString()) fail("color output is required");
    for (const auto& [key,v]:g["outputs"].members()) {
        if (key!="color" && (p.domain!="material" || (key!="roughness" && key!="metallic" && key!="emission"))) fail("unsupported output '"+key+"'");
        auto it=ids.find(v.str()); if (!v.isString() || it==ids.end()) fail("output '"+key+"': missing node");
        p.outputs[key]="sg_n"+std::to_string(it->second);
    }
    p.nodes=int(ns.size());
    for (bool metal : {false,true}) {
        const std::string vec=metal?"float4":"vec4";
        std::string code;
        for (auto i:*order) {
            const auto& n=ns[i]; const auto op=n.getStr("op");
            auto a=[&](size_t k){return "sg_n"+std::to_string(ids.at(n["inputs"][k].str()));};
            std::string e;
            if(op=="constant") {
                const auto& v=n["value"];
                if(v.isNumber()) e=vec+"("+number(v)+")";
                else { if(!v.isArray() || v.size()!=4) fail("constant value must be a number or RGBA array");
                    e=vec+"("+number(v[0])+","+number(v[1])+","+number(v[2])+","+number(v[3])+")"; }
            } else if(op=="uv") e=vec+"(sg_uv,0.0,1.0)";
            else if(op=="input") e="sg_input";
            else if(op=="texture") { e="SG_SAMPLE("+a(0)+".xy)"; if(!metal) ++p.samples; }
            else if(op=="add" || op=="subtract" || op=="multiply") e=a(0)+(op=="add"?" + ":op=="subtract"?" - ":" * ")+a(1);
            else if(op=="divide") e=a(0)+" / (mix("+vec+"(-1.0),"+vec+"(1.0),step("+vec+"(0.0),"+a(1)+"))*max(abs("+a(1)+"),"+vec+"(0.00001)))";
            else if(op=="mix") e="mix("+a(0)+","+a(1)+","+a(2)+")";
            else if(op=="sin" || op=="fract") e=op+"("+a(0)+")";
            else if(op=="clamp") e="clamp("+a(0)+","+vec+"(0.0),"+vec+"(1.0))";
            else if(op=="oneMinus") e=vec+"(1.0)-"+a(0);
            else if(op=="dot") e=vec+"(dot("+a(0)+","+a(1)+"))";
            else if(op=="length") e=vec+"(length("+a(0)+"))";
            else if(op=="step") e="step("+a(0)+","+a(1)+")";
            else { const char* c=op=="red"?"r":op=="green"?"g":op=="blue"?"b":"a"; e=vec+"("+a(0)+"."+c+")"; }
            code+=vec+" sg_n"+std::to_string(i)+" = "+e+";\n";
            if(op=="texture" && p.domain=="material") {
                const std::string c="sg_n"+std::to_string(i)+".rgb";
                code+=c+" = "+c+"*("+c+"*("+c+"*0.305306011+0.682171111)+0.012522878);\n";
            }
        }
        (metal?p.msl:p.glsl)=std::move(code);
    }
    if(p.samples>8) fail("at most 8 texture samples are allowed");
    return p;
}
std::string statements(const Program& p,bool metal) { return metal?p.msl:p.glsl; }
Json makeDefault(const std::string& domain) {
    Json g=Json::parse(R"({"version":1,"domain":"material","nodes":[{"id":"input","op":"input","position":[40,60]}],"outputs":{"color":"input"}})");
    g.set("domain",Json::string(domain)); compile(g); return g;
}
}
