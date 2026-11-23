// Bounded, CPU-only procedural texture DAG. No GPU or filesystem dependencies.
#pragma once
#include "engine/render/TextureGen.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <stdexcept>

namespace aiverse::render::texture_graph {
enum class Kind { Scalar, Color, Normal, Packed };
using Pixel=std::array<float,3>;
inline const char* kindName(Kind k){switch(k){case Kind::Scalar:return "scalar";case Kind::Color:return "color";case Kind::Normal:return "normal";default:return "packed";}}
struct Op {const char* id;Kind output;std::vector<Kind> inputs;};
inline const std::vector<Op>& catalog(){
    static const std::vector<Op> ops={
        {"constant",Kind::Scalar,{}},{"color",Kind::Color,{}},{"noise",Kind::Scalar,{}},{"fbm",Kind::Scalar,{}},{"voronoi",Kind::Scalar,{}},{"gradient",Kind::Scalar,{}},{"pattern",Kind::Color,{}},{"patternMask",Kind::Scalar,{}},
        {"add",Kind::Scalar,{Kind::Scalar,Kind::Scalar}},{"multiply",Kind::Scalar,{Kind::Scalar,Kind::Scalar}},{"subtract",Kind::Scalar,{Kind::Scalar,Kind::Scalar}},{"min",Kind::Scalar,{Kind::Scalar,Kind::Scalar}},{"max",Kind::Scalar,{Kind::Scalar,Kind::Scalar}},{"invert",Kind::Scalar,{Kind::Scalar}},{"threshold",Kind::Scalar,{Kind::Scalar}},{"remap",Kind::Scalar,{Kind::Scalar}},{"blend",Kind::Scalar,{Kind::Scalar,Kind::Scalar,Kind::Scalar}},{"colorize",Kind::Color,{Kind::Scalar,Kind::Color,Kind::Color}},{"blendColor",Kind::Color,{Kind::Color,Kind::Color,Kind::Scalar}},{"normal",Kind::Normal,{Kind::Scalar}},{"metallicRoughness",Kind::Packed,{Kind::Scalar,Kind::Scalar}}
    };return ops;
}
inline void require(bool condition,const char* error){if(!condition)throw std::invalid_argument(error);}
inline float scalar(const Json& j,float fallback,float lo=0,float hi=1){
    if(j.isNull())return fallback;
    require(j.isNumber()&&std::isfinite(j.toDouble())&&j.toDouble()>=lo&&j.toDouble()<=hi,"texture graph numeric parameter out of range");
    return float(j.toDouble());
}
inline int integer(const Json& j,int fallback,int lo,int hi){
    if(j.isNull())return fallback;
    require(j.isNumber()&&j.toDouble()==double(j.integer())&&j.integer()>=lo&&j.integer()<=hi,"texture graph integer parameter out of range");return int(j.integer());
}
inline uint32_t seed(const Json& j){if(j.isNull())return 1;require(j.isNumber()&&j.toDouble()==double(j.integer())&&j.integer()>=0&&j.integer()<=4294967295LL,"invalid texture seed");return uint32_t(j.integer());}
inline bool safeId(const std::string& id){return !id.empty()&&id.size()<=64&&id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==std::string::npos;}
inline const Op& op(const std::string& id){for(const auto& o:catalog())if(id==o.id)return o;throw std::invalid_argument("unknown texture graph operation: "+id);}
inline Kind outputKind(const std::string& name){
    if(name=="color"||name=="emissive")return Kind::Color;
    if(name=="normal")return Kind::Normal;
    if(name=="metallicRoughness")return Kind::Packed;
    if(name=="height"||name=="roughness"||name=="metallic"||name=="ao"||name=="mask")return Kind::Scalar;
    throw std::invalid_argument("unknown texture graph output: "+name);
}
struct Node {std::string id;const Op* info=nullptr;std::vector<size_t> inputs;Json parameters;};
struct Program {int size=256;std::vector<Node> nodes;std::vector<size_t> order;std::map<std::string,size_t> outputs;};
inline Json properties(const std::string& id){
    Json result=Json::object();
    auto field=[&](const char* key,const char* type,const Json& value,double lo=0,double hi=1){
        Json p=Json::object();p.set("type",Json::string(type));p.set("default",value);
        p.set("min",Json::number(lo));p.set("max",Json::number(hi));result.set(key,p);
    };
    auto options=[&](const char* key,const std::string& value,const std::vector<std::string>& values){
        Json p=Json::object(),list=Json::array();p.set("type",Json::string("enum"));p.set("default",Json::string(value));
        for(const auto& v:values)list.push(Json::string(v));p.set("options",list);result.set(key,p);
    };
    if(id=="constant")field("value","number",Json::number(.5));
    if(id=="color")field("value","rgb",Json::parse("[0.5,0.5,0.5]"));
    if(id=="noise"||id=="fbm"||id=="voronoi"){
        field("period","integer",Json::number(int64_t(8)),1,256);
        field("seed","integer",Json::number(int64_t(1)),0,4294967295.0);
        if(id=="fbm")field("octaves","integer",Json::number(int64_t(4)),1,6);
    }
    if(id=="gradient")options("axis","y",{"x","y"});
    if(id=="pattern"||id=="patternMask"){
        options("pattern","brick",texturePatterns());field("scale","integer",Json::number(int64_t(8)),1,64);
        field("seed","integer",Json::number(int64_t(1)),0,4294967295.0);
    }
    if(id=="threshold")field("threshold","number",Json::number(.5));
    if(id=="remap"){field("low","number",Json::number(0.0));field("high","number",Json::number(1.0));}
    if(id=="normal")field("strength","number",Json::number(2.0),0,16);
    return result;
}
inline void validateParameters(const Node& n){
    const std::string id=n.info->id;const auto& p=n.parameters;
    const auto fields=properties(id);
    for(const auto& [key,value]:p.members()){require(fields.has(key)&&!value.isNull(),"unknown or null texture node property");}
    if(id=="constant")scalar(p["value"],.5f);
    if(id=="color"){require(p["value"].isArray()&&p["value"].size()==3,"color requires RGB triple");for(const auto& v:p["value"].items()){require(v.isNumber(),"RGB channel must be number");scalar(v,0);}}
    if(id=="noise"||id=="fbm"||id=="voronoi"){integer(p["period"],8,1,256);seed(p["seed"]);if(id=="fbm")integer(p["octaves"],4,1,6);}
    if(id=="gradient")require(!p.has("axis")||p.getStr("axis")=="x"||p.getStr("axis")=="y","gradient axis must be x or y");
    if(id=="pattern"||id=="patternMask"){auto patterns=texturePatterns();require(std::find(patterns.begin(),patterns.end(),p.getStr("pattern"))!=patterns.end(),"unknown native pattern");integer(p["scale"],8,1,64);seed(p["seed"]);}
    if(id=="threshold")scalar(p["threshold"],.5f);
    if(id=="remap"){scalar(p["low"],0);scalar(p["high"],1);}
    if(id=="normal")scalar(p["strength"],2,0,16);
}
inline Program compileResolved(const Json& g){
    require(g.isObject()&&g.getInt("version")==1&&g.dump(-1).size()<=256*1024,"texture graph version 1 / 256 KiB required");
    Program p;p.size=integer(g["size"],256,8,1024);
    const auto& nodes=g["nodes"];require(nodes.isArray()&&nodes.size()>0&&nodes.size()<=32,"texture graph requires 1..32 nodes");
    require(size_t(p.size)*p.size*nodes.size()<=4194304,"texture graph pixel-node budget exceeded (reduce resolution or nodes)");
    std::map<std::string,size_t> ids;
    for(const auto& j:nodes.items()){
        Node n;n.id=j.getStr("id");require(safeId(n.id)&&ids.emplace(n.id,p.nodes.size()).second,"invalid or duplicate texture node ID");
        n.info=&op(j.getStr("op"));n.parameters=j.has("properties")?j["properties"]:Json::object();require(n.parameters.isObject(),"properties must be object");validateParameters(n);
        require(j["inputs"].isArray()&&j["inputs"].size()==n.info->inputs.size(),"texture input arity mismatch");
        p.nodes.push_back(std::move(n));
    }
    for(size_t i=0;i<p.nodes.size();++i)for(size_t j=0;j<p.nodes[i].info->inputs.size();++j){
        auto it=ids.find(nodes[i]["inputs"][j].str());require(it!=ids.end(),"missing texture input");
        require(p.nodes[it->second].info->output==p.nodes[i].info->inputs[j],"texture port type mismatch");p.nodes[i].inputs.push_back(it->second);
    }
    std::vector<int> marks(p.nodes.size());std::function<void(size_t)> visit=[&](size_t i){
        require(marks[i]!=1,"texture graph cycle");if(marks[i]==2)return;marks[i]=1;
        for(auto dep:p.nodes[i].inputs)visit(dep);marks[i]=2;p.order.push_back(i);
    };for(size_t i=0;i<p.nodes.size();++i)visit(i);
    require(g["outputs"].isObject()&&g["outputs"].size()>0&&g["outputs"].size()<=9,"texture outputs required");
    for(const auto& [key,value]:g["outputs"].members()){
        auto it=ids.find(value.str());require(it!=ids.end(),"missing texture output node");
        require(outputKind(key)==p.nodes[it->second].info->output,"texture output type mismatch");p.outputs.emplace(key,it->second);
    }return p;
}
// A declaration binds one named value to one or more existing native properties.
// Source graph is never mutated. Validate both authored defaults and overrides.
inline Json resolveParameters(const Json& source,const Json& overrides=Json::object()){
    require(source.isObject()&&source.dump(-1).size()<=256*1024,"invalid texture source");
    require(overrides.isObject()&&overrides.size()<=32&&overrides.dump(-1).size()<=32768,"invalid texture overrides");
    const auto& defs=source["parameters"];
    require(defs.isNull()||(defs.isObject()&&defs.size()<=32),"invalid texture parameter declarations");
    for(const auto& [name,value]:overrides.members()){(void)value;require(defs.has(name),"unknown texture override");}
    Json resolved=source;resolved.erase("parameters");
    require(resolved["nodes"].isArray()&&resolved["nodes"].size()<=32,"invalid texture nodes");
    std::map<std::string,size_t> ids;for(size_t i=0;i<resolved["nodes"].size();++i)ids.emplace(resolved["nodes"][i].getStr("id"),i);
    std::map<std::pair<std::string,std::string>,bool> bound;
    for(const auto& [name,def]:defs.members()){
        require(safeId(name)&&def.isObject()&&def.has("default")&&!def["default"].isNull(),"invalid parameter declaration");
        const auto& bindings=def["bindings"];require(bindings.isArray()&&bindings.size()>0&&bindings.size()<=32,"invalid parameter bindings");
        const Json& value=overrides.has(name)?overrides[name]:def["default"];
        require(!value.isNull(),"null parameter override");
        std::string commonType;
        for(const auto& b:bindings.items()){
            const auto node=b.getStr("node"),key=b.getStr("property");auto it=ids.find(node);
            require(it!=ids.end()&&bound.emplace(std::make_pair(node,key),true).second,"unknown or duplicate parameter binding");
            auto& entry=resolved.member("nodes").at(it->second);
            auto schema=properties(entry.getStr("op"));
            require(schema.has(key),"unknown bound node property");
            const auto type=schema[key].getStr("type");require(commonType.empty()||commonType==type,"parameter binding type mismatch");commonType=type;
            if(!entry.has("properties"))entry.set("properties",Json::object());
            require(entry["properties"].isObject(),"invalid bound properties");
            entry.member("properties").set(key,value);
        }
    }
    return resolved;
}
inline Program compile(const Json& g,const Json& overrides=Json::object()){
    auto defaults=resolveParameters(g);auto p=compileResolved(defaults);
    if(overrides.isObject()&&overrides.size()==0)return p;
    return compileResolved(resolveParameters(g,overrides));
}
inline float clamp(float x){return std::clamp(x,0.f,1.f);}
inline Pixel gray(float x){return {x,x,x};}
inline Pixel mix(Pixel a,Pixel b,float t){return {a[0]+(b[0]-a[0])*t,a[1]+(b[1]-a[1])*t,a[2]+(b[2]-a[2])*t};}
inline std::vector<Image> evaluateMany(const Program& p,const std::vector<size_t>& targets,int resolution=0){
    const int n=resolution?resolution:p.size;require(!targets.empty()&&targets.size()<=9&&n>=8&&n<=p.size,"invalid texture evaluation target or resolution");
    for(auto target:targets)require(target<p.nodes.size(),"invalid target");
    std::vector<std::vector<Pixel>> images(p.nodes.size());
    std::vector<bool> needed(p.nodes.size());std::function<void(size_t)> mark=[&](size_t i){if(needed[i])return;needed[i]=true;for(auto dep:p.nodes[i].inputs)mark(dep);};for(auto target:targets)mark(target);
    const size_t count=size_t(n)*n;
    for(auto i:p.order){
        if(!needed[i])continue;
        const auto& node=p.nodes[i];const auto& q=node.parameters;const std::string id=node.info->id;
        auto& out=images[i];out.resize(count);
        if(id=="pattern"||id=="patternMask"){
            Json recipe=q;recipe.set("size",Json::number(int64_t(n)));recipe.set("output",Json::string(id=="pattern"?"color":"mask"));
            Image img;generateTexture(recipe,img);
            for(size_t k=0;k<count;++k)out[k]={img.rgba[k*4]/255.f,img.rgba[k*4+1]/255.f,img.rgba[k*4+2]/255.f};continue;
        }
        const float value=scalar(q["value"].isArray()?Json():q["value"],.5f);
        const int period=integer(q["period"],8,1,256),octaves=id=="fbm"?integer(q["octaves"],4,1,6):1;const auto randomSeed=seed(q["seed"]);
        const float strength=scalar(q["strength"],2,0,16),low=scalar(q["low"],0),high=scalar(q["high"],1),threshold=scalar(q["threshold"],.5f);
        const bool axisX=q.getStr("axis","y")=="x";
        const Pixel color=id=="color"?Pixel{scalar(q["value"][0],0),scalar(q["value"][1],0),scalar(q["value"][2],0)}:Pixel{};
        for(int y=0;y<n;++y)for(int x=0;x<n;++x){
            const size_t k=size_t(y)*n+x;const float u=(x+.5f)/n,v=(y+.5f)/n;
            auto input=[&](size_t slot)->const Pixel&{return images[node.inputs[slot]][k];};
            auto a=[&](){return input(0)[0];};auto b=[&](){return input(1)[0];};
            Pixel r{};
            if(id=="constant")r=gray(value);else if(id=="color")r=color;
            else if(id=="noise"||id=="fbm")r=gray(textureNoise(u,v,period,octaves,randomSeed));
            else if(id=="voronoi")r=gray(textureVoronoi(u,v,period,randomSeed));
            else if(id=="gradient")r=gray(axisX?u:v);
            else if(id=="add")r=gray(a()+b());else if(id=="subtract")r=gray(a()-b());else if(id=="multiply")r=gray(a()*b());
            else if(id=="min")r=gray(std::min(a(),b()));else if(id=="max")r=gray(std::max(a(),b()));else if(id=="invert")r=gray(1-a());
            else if(id=="threshold")r=gray(a()>=threshold?1.f:0.f);else if(id=="remap")r=gray(low+(high-low)*a());
            else if(id=="blend")r=mix(input(0),input(1),input(2)[0]);else if(id=="colorize")r=mix(input(1),input(2),a());
            else if(id=="blendColor")r=mix(input(0),input(1),input(2)[0]);
            else if(id=="metallicRoughness")r={1.f,a(),b()}; // G roughness, B metallic
            else if(id=="normal"){
                auto h=[&](int xx,int yy){return images[node.inputs[0]][size_t((yy+n)%n)*n+(xx+n)%n][0];};
                const float dx=(h(x+1,y)-h(x-1,y))*strength*n/256.f,dy=(h(x,y+1)-h(x,y-1))*strength*n/256.f;
                const float len=std::sqrt(dx*dx+dy*dy+1);r={.5f-.5f*dx/len,.5f+.5f*dy/len,.5f+.5f/len};
            }
            for(float& c:r)c=clamp(c);out[k]=r;
        }
    }
    std::vector<Image> result;result.reserve(targets.size());
    for(auto target:targets){Image img;img.width=img.height=n;img.name="generated:texture-graph";img.rgba.resize(count*4,255);
        for(size_t i=0;i<count;++i)for(size_t c=0;c<3;++c)img.rgba[i*4+c]=uint8_t(std::lround(clamp(images[target][i][c])*255.f));
        result.push_back(std::move(img));}
    return result;
}
inline Image evaluate(const Program& p,size_t target,int resolution=0){return std::move(evaluateMany(p,{target},resolution).front());
}
inline Json describeCatalog(){
    Json r=Json::object(),nodes=Json::array();
    for(const auto& o:catalog()){Json n=Json::object(),inputs=Json::array();n.set("id",Json::string(o.id));n.set("output",Json::string(kindName(o.output)));for(auto k:o.inputs)inputs.push(Json::string(kindName(k)));n.set("inputs",inputs);n.set("properties",properties(o.id));nodes.push(n);}
    r.set("ok",Json::fromBool(true));r.set("nodes",nodes);r.set("outputs",Json::parse(R"(["color","normal","height","roughness","metallic","metallicRoughness","ao","emissive","mask"])"));Json outputTypes=Json::object();for(const auto& v:r["outputs"].items())outputTypes.set(v.str(),Json::string(kindName(outputKind(v.str()))));r.set("outputTypes",outputTypes);return r;
}
} // namespace aiverse::render::texture_graph
