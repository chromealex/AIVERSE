// SpectraFX graph-v2 -> runtime scalar VM authoring expression lowering.
// Converts typed ValueGraph math links into declarative particleProgram AST
// at graph COMPILE time. No runtime JSON parsing or dependencies on ECS/RHI.
#pragma once
#include "SpectraValueGraph.h"
#include "SpectraParticleProgram.h"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace aiverse::fx::vfx2 {

inline bool lowerRuntimeExpression(const Json& expression,
                                   const std::map<std::string,Json>& nodes,
                                   Json& lowered,
                                   std::vector<std::string>& errors,
                                   std::set<std::string>& active,
                                   unsigned depth=0) {
    auto fail=[&](const std::string& text) {
        errors.push_back("vfx graph v2 runtime dataflow: "+text);
        return false;
    };
    if(depth>16)return fail("runtime expression depth exceeds 16");
    if(expression.isObject() && (expression.has("$runtime") ||
                                 expression.has("$node"))) {
        if(expression.size()!=1)
            return fail("runtime data reference must contain one string ID");
        const Json& ref=expression.has("$runtime")?
                           expression["$runtime"]:expression["$node"];
        if(!ref.isString())return fail("runtime data reference needs a string node ID");
        const std::string id=ref.str();
        auto it=nodes.find(id);
        if(it==nodes.end())return fail("unknown runtime data node '"+id+"'");
        if(!active.insert(id).second)return fail("cycle in runtime data graph at '"+id+"'");
        const Json& source=it->second;
        const std::string type=source.getStr("type");
        const Json& p=source["properties"];
        bool ok=true;
        if(type=="InstanceParameter") {
            // The graph-v2 compiler already resolved the named declaration
            // to a strict 0..7 slot. This is a dynamic per-instance read,
            // never a constant-folded graph parameter.
            const int64_t slot=p.getInt("slot",-1);
            if(slot<0 || slot>=8)
                ok=fail("InstanceParameter '"+id+"' needs a validated slot 0..7");
            else {
                lowered=Json::object();
                lowered.set("var",Json::string("param"+std::to_string(slot)));
            }
        } else if(type=="ParticleRead" || type=="AgeNormalized") {
            const std::string field=type=="AgeNormalized"?
                                    "ageNormalized":p.getStr("field");
            if(spectra::program::fieldOf(field)==spectra::program::Field::Invalid)
                ok=fail("unknown particle field '"+field+"'");
            else {
                lowered=Json::object();
                lowered.set("var",Json::string(field));
            }
        } else if(type=="Float"||type=="Integer"||type=="Parameter") {
            if(type=="Parameter") {
                const std::string name=p.getStr("name",source.getStr("name"));
                lowered=Json::object();
                lowered.set("$param",Json::string(name));
            } else if(!p["value"].isNumber()) {
                ok=fail("runtime scalar literal '"+id+"' must contain a number");
            } else lowered=p["value"];
        } else {
            std::string op;
            if(type=="Add")op="add";
            else if(type=="Subtract")op="subtract";
            else if(type=="Multiply")op="multiply";
            else if(type=="Divide")op="divide";
            else if(type=="Min")op="min";
            else if(type=="Max")op="max";
            else if(type=="Pow")op="pow";
            else if(type=="Sin")op="sin";
            else if(type=="Cos")op="cos";
            else if(type=="Abs")op="abs";
            else if(type=="Negate")op="negate";
            else if(type=="Sqrt")op="sqrt";
            else if(type=="Floor")op="floor";
            else if(type=="Ceil")op="ceil";
            else if(type=="Saturate")op="saturate";
            else if(type=="Clamp")op="clamp";
            else if(type=="Lerp")op="lerp";
            else if(type=="Select")op="select";
            else if(type=="Greater")op="greater";
            else if(type=="Less")op="less";
            else if(type=="Equal")op="equal";
            if(op.empty())ok=fail("data node '"+id+"' cannot generate a runtime scalar expression");
            else {
                lowered=Json::object();
                lowered.set("op",Json::string(op));
                for(const char* arg:{"a","b","t"}) {
                    if(std::string(arg)=="t"&&
                       (type=="Clamp"||type=="Select")) {
                        Json value;
                        if(!lowerRuntimeExpression(p["c"],nodes,value,errors,active,depth+1)) {
                            ok=false;break;
                        }
                        lowered.set("t",std::move(value));
                        continue;
                    }
                    if(!p.has(arg))continue;
                    Json value;
                    if(!lowerRuntimeExpression(p[arg],nodes,value,errors,active,depth+1)) {
                        ok=false;break;
                    }
                    lowered.set(arg,std::move(value));
                }
            }
        }
        active.erase(id);
        return ok;
    }
    if(expression.isObject()) {
        lowered=Json::object();
        for(const auto& [name,value]:expression.members()) {
            Json result;
            if(!lowerRuntimeExpression(value,nodes,result,errors,active,depth+1))
                return false;
            lowered.set(name,std::move(result));
        }
        return true;
    }
    if(expression.isArray()) {
        lowered=Json::array();
        for(const Json& part:expression.items()) {
            Json result;
            if(!lowerRuntimeExpression(part,nodes,result,errors,active,depth+1))
                return false;
            lowered.push(std::move(result));
        }
        return true;
    }
    lowered=expression;
    return true;
}
inline bool lowerRuntimeProgram(const Json& properties,
                                const std::map<std::string,Json>& nodes,
                                Json& lowered,
                                std::vector<std::string>& errors) {
    std::set<std::string> active;
    if(!lowerRuntimeExpression(properties,nodes,lowered,errors,active))
        return false;
    if(!lowered.isObject()||lowered.getInt("version")!=1) {
        errors.push_back("vfx graph v2: ParticleProgramModule requires version 1");
        return false;
    }
    return true;
}

} // namespace aiverse::fx::vfx2
