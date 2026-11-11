#include "ShaderGraph.h"
#include "ShaderGraphNodes.h"
#include "libraries/include/aiverselib/Graphs.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <set>
#include <functional>

namespace aiverse::render::graph {
bool supports(const Op& op, const std::string& domain) {
    return (op.domains & (domain=="material"?1u:domain=="postfx"?2u:4u)) != 0;
}
const std::vector<Op>& catalog() {
    static const std::vector<Op> ops = {
        {"constant",0},{"parameter",0},{"uv",0},{"input",0,7,false},{"texture",1,7,false},{"sampleTexture",1,7,false},{"sampleTextureLod",2,1,true},
        {"time",0},{"worldPosition",0,5},{"objectPosition",0,1},{"normal",0,1},{"viewDirection",0,5},
        {"vertexColor",0,5},{"particleAge",0,4},{"particleLifetime",0,4},{"particleAge01",0,4},
        {"particleRandom",0,4},{"screenPosition",0,7,false},
        {"add",2},{"subtract",2},{"multiply",2},{"divide",2},{"mix",3},{"sin",1},
        {"fract",1},{"clamp",1},{"oneMinus",1},{"dot",2},{"length",1},{"step",2},
        {"red",1},{"green",1},{"blue",1},{"alpha",1},
        {"abs",1},{"negate",1},{"sign",1},{"floor",1},{"ceil",1},{"round",1},{"truncate",1},
        {"cos",1},{"tan",1},{"asin",1},{"acos",1},{"atan",1},{"atan2",2},
        {"radians",1},{"degrees",1},{"exp",1},{"exp2",1},{"log",1},{"log2",1},
        {"sqrt",1},{"inverseSqrt",1},{"pow",2},{"min",2},{"max",2},{"modulo",2},
        {"reciprocal",1},{"smoothstep",3},{"clampRange",3},{"remap",5},{"inverseLerp",3},
        {"normalize",1},{"normalize3",1},{"dot3",2},{"length3",1},{"distance",2},{"distance3",2},
        {"cross",2},{"reflect",2},{"refract",3},{"fresnel",3},
        {"combine",4},{"swizzle",1},{"append",2},{"rgb",1},{"setAlpha",2},
        {"tilingOffset",3},{"rotateUV",3},{"polarUV",2},{"panner",3},{"flipbook",4},
        {"noise",1},{"gradientNoise",1},{"voronoi",1},{"checkerboard",1},
        {"luminance",1},{"desaturate",2},{"contrast",2},{"saturation",2},
        {"linearToSrgb",1},{"srgbToLinear",1},{"normalUnpack",1},{"normalStrength",2},
        {"lessThan",2},{"greaterThan",2},{"equal",2},{"branch",3},
        {"ddx",1,7,false},{"ddy",1,7,false},{"fwidth",1,7,false}
    };
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
    if(g.has("minQuality")) {
        const auto q=g.getStr("minQuality");
        if(p.domain!="material" || (q!="low" && q!="medium" && q!="high"))fail("minQuality is low|medium|high for material graphs only");
        p.minQuality=q=="high"?2:q=="medium"?1:0;
    }
    if(g.has("parameters")) {
        if(!g["parameters"].isObject())fail("parameters must be an object");
        for(const auto& [name,value]:g["parameters"].members()) {
            if(name.empty() || name.size()>64)fail("invalid parameter name");
            if(value.isNumber())number(value);else {
                if(!value.isArray() || value.size()!=4)fail("parameter must be a number or RGBA array");
                for(const auto& c:value.items())number(c);
            }
        }
    }
    const auto& ns = g["nodes"];
    if (!ns.isArray() || ns.size() == 0 || ns.size() > 128) fail("expected 1..128 nodes");
    std::map<std::string,size_t> ids;
    for (size_t i=0;i<ns.size();++i) {
        const auto& n=ns[i]; const auto id=n.getStr("id");
        if (id.empty() || id.size()>64 || !ids.emplace(id,i).second) fail("empty, duplicate or oversized node id");
        auto it=std::find_if(catalog().begin(),catalog().end(),[&](auto& o){return n.getStr("op")==o.id;});
        if (it==catalog().end()) fail("node '"+id+"': unknown operation");
        if(!supports(*it,p.domain)) fail("node '"+id+"': operation is not available in "+p.domain);
        if((n.getStr("op")=="sampleTexture" || n.getStr("op")=="sampleTextureLod")) {
            const auto path=n.getStr("texture");
            if(path.empty() || path[0]=='/' || path.find("..")!=std::string::npos || path.find('\\')!=std::string::npos || path.find(':')!=std::string::npos)
                fail("sampleTexture requires a project-relative texture path");
            if(std::find(p.textures.begin(),p.textures.end(),path)==p.textures.end()) p.textures.push_back(path);
            if(p.textures.size()>4)fail("at most four independent textures are allowed");
        }
        if(n.getStr("op")=="swizzle") {
            auto mask=n.getStr("mask","rgba");if(mask.size()!=4 || mask.find_first_not_of("rgba")!=std::string::npos)fail("swizzle requires four rgba letters");
        }
        if(n.getStr("op")=="parameter" && (n.getStr("name",id).empty() || n.getStr("name",id).size()>64))fail("invalid parameter name");
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
        if (key!="color" && (p.domain!="material" || (key!="roughness" && key!="metallic" && key!="emission" && key!="vertexOffset" && key!="vertexNormal" && key!="normal"))) fail("unsupported output '"+key+"'");
        auto it=ids.find(v.str()); if (!v.isString() || it==ids.end()) fail("output '"+key+"': missing node");
        p.outputs[key]="sg_n"+std::to_string(it->second);
    }
    p.nodes=int(ns.size());
    // Compile only ancestors of each stage's outputs; fragment-only nodes cannot leak into vertices.
    for(bool vertex : {false,true}) {
        std::set<size_t> needed;
        std::function<void(size_t)> visit=[&](size_t i){if(!needed.insert(i).second)return;for(const auto& in:ns[i]["inputs"].items())visit(ids.at(in.str()));};
        for(const auto& [key,v]:g["outputs"].members()) if((key=="vertexOffset" || key=="vertexNormal")==vertex) visit(ids.at(v.str()));
        for(bool metal : {false,true}) {
            std::string code;
            for(auto i:*order) if(needed.count(i)) {
                const auto& n=ns[i];const auto op=n.getStr("op");
                const auto info=std::find_if(catalog().begin(),catalog().end(),[&](const Op& x){return op==x.id;});
                if(vertex && !info->vertex)fail("node '"+n.getStr("id")+"': operation cannot feed a vertex output");
                auto arg=[&](size_t k){return "sg_n"+std::to_string(ids.at(n["inputs"][k].str()));};
                const std::string vec=metal?"float4":"vec4", name="sg_n"+std::to_string(i);
                std::string e;
                if(op=="constant" || op=="parameter") {
                    const std::string key=n.getStr("name",n.getStr("id"));
                    const auto& v=op=="parameter" && g["parameters"].has(key)?g["parameters"][key]:n["value"];
                    if(v.isNumber())e=vec+"("+number(v)+")";
                    else {if(!v.isArray()||v.size()!=4)fail("constant/parameter value must be a number or RGBA array");e=vec+"("+number(v[0])+","+number(v[1])+","+number(v[2])+","+number(v[3])+")";}
                } else if(op=="texture" || op=="sampleTexture" || op=="sampleTextureLod") {
                    std::string sample="SG_SAMPLE";
                    if(op=="sampleTexture" || op=="sampleTextureLod")sample=std::string(op=="sampleTextureLod"?"SG_TEXTURE_LOD":"SG_TEXTURE")+std::to_string(std::find(p.textures.begin(),p.textures.end(),n.getStr("texture"))-p.textures.begin());
                    e=sample+"("+arg(0)+".xy"+(op=="sampleTextureLod"?",max("+arg(1)+".r,0.0)":"")+")";
                } else e=nodeExpression(n,arg,metal,name,code);
                code+=vec+" "+name+" = "+e+";\n";
                if((op=="texture" && p.domain=="material") || ((op=="sampleTexture" || op=="sampleTextureLod") && n.getBool("srgb",false))) {
                    const std::string c=name+".rgb";
                    code+=c+" = "+c+"*("+c+"*("+c+"*0.305306011+0.682171111)+0.012522878);\n";
                }
            }
            (vertex?(metal?p.vertexMsl:p.vertexGlsl):(metal?p.msl:p.glsl))=std::move(code);
        }
    }
    // Validate constants and sample limits even for disconnected nodes.
    for(const auto& n:ns.items()) {
        const auto op=n.getStr("op");
        if(op=="texture" || op=="sampleTexture" || op=="sampleTextureLod")++p.samples;
        if(op=="constant" || op=="parameter") {
            const auto& v=n["value"];if(v.isNumber())number(v);else {if(!v.isArray()||v.size()!=4)fail("constant/parameter value must be RGBA");for(auto& c:v.items())number(c);}
        }
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
