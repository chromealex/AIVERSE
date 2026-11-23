// In-memory graph edits only; native compilers remain the final authority.
#pragma once
#include "AnimationDocument.h"
#include "engine/render/graph/ShaderGraph.h"
#include "tools/modeling/Modeling.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace aiverse::editor::graph_draft {
inline void require(bool ok,const char* why){if(!ok)throw std::invalid_argument(why);}
inline Json& entry(Json& doc,const std::string& collection,const std::string& id){
    auto* rows=doc.find(collection);
    require(rows&&rows->isArray()&&!id.empty(),"invalid collection or ID");
    for(auto& n:rows->itemsMut())if(n.getStr("id")==id)return n;
    throw std::invalid_argument("unknown graph entry");
}
inline bool exists(const Json& doc,const std::string& collection,const std::string& id){
    if(id.empty())return false;
    for(const auto& n:doc[collection].items())if(n.getStr("id")==id)return true;
    return false;
}
inline void append(Json& doc,const std::string& collection,const Json& value,size_t limit){
    require(value.isObject()&&value["id"].isString()&&!value.getStr("id").empty()&&value.getStr("id").size()<=64,"invalid new ID");
    auto& rows=doc.member(collection);
    if(rows.isNull())rows=Json::array();
    require(rows.isArray()&&rows.size()<limit&&!exists(doc,collection,value.getStr("id")),"duplicate ID or collection limit");
    rows.push(value);
}
inline void erase(Json& doc,const std::string& collection,const std::string& id){
    (void)entry(doc,collection,id);
    auto& rows=doc.member(collection).itemsMut();
    rows.erase(std::remove_if(rows.begin(),rows.end(),[&](const Json& n){return n.getStr("id")==id;}),rows.end());
}
inline void replace(Json& doc,const std::string& collection,const Json& command){
    const auto id=command.getStr("node");const auto& value=command["value"];
    require(value.isObject()&&value.getStr("id")==id&&!id.empty(),"replacement must preserve ID");
    entry(doc,collection,id)=value;
}
inline void move(Json& n,const Json& p){
    const auto& v=p["value"];
    require(v.isArray()&&v.size()==2&&v[0].isNumber()&&v[1].isNumber()&&std::isfinite(v[0].toDouble())&&std::isfinite(v[1].toDouble()),"position requires two finite numbers");
    n.set("position",v);
}
inline void shader(Json& g,const Json& p){
    const auto action=p.getStr("action"),id=p.getStr("node");
    if(action=="add")append(g,"nodes",p["value"],192);
    else if(action=="replaceNode")replace(g,"nodes",p);
    else if(action=="deleteNode")erase(g,"nodes",id);
    else if(action=="move")move(entry(g,"nodes",id),p);
    else if(action=="connect"||action=="disconnect"){
        auto& n=entry(g,"nodes",p.getStr("to"));const auto slot=p.getInt("index",-1);
        require(p["index"].isNumber()&&p["index"].toDouble()==double(slot)&&slot>=0,"input index required");
        auto* inputs=n.find("inputs");require(inputs&&inputs->isArray()&&size_t(slot)<inputs->size(),"input slot not found");
        if(action=="connect")require(exists(g,"nodes",p.getStr("from")),"source node missing");
        inputs->at(size_t(slot))=Json::string(action=="connect"?p.getStr("from"):"");
    }else if(action=="setOutput"){
        require(p["output"].isString()&&!p.getStr("output").empty()&&exists(g,"nodes",id),"output and existing node required");
        require(g["outputs"].isObject(),"outputs object required");g.member("outputs").set(p.getStr("output"),Json::string(id));
    }else throw std::invalid_argument("unsupported shader draft action");
}
inline void motion(Json& g,const Json& p){
    const auto action=p.getStr("action"),id=p.getStr("node");
    if(action=="add")append(g,"nodes",p["value"],128);
    else if(action=="replaceNode")replace(g,"nodes",p);
    else if(action=="deleteNode")animation::remove(g,id);
    else if(action=="rename")animation::rename(g,id,p.getStr("to"));
    else if(action=="connect")animation::connect(g,p.getStr("from"),p.getStr("to"),p.getStr("toPort"));
    else if(action=="move")move(entry(g,"nodes",id),p);
    else if(action=="addState")append(g,"states",p["value"],64);
    else if(action=="replaceState")replace(g,"states",p);
    else if(action=="setInitial"){
        require(exists(g,"states",id),"state not found");g.set("initial",Json::string(id));
    }else if(action=="setOutput"){
        require(exists(g,"nodes",id),"pose node not found");
        if(p.has("state"))entry(g,"states",p.getStr("state")).set("output",Json::string(id));
        else g.set("output",Json::string(id));
    }else if(action=="addTransition"||action=="removeTransition"||action=="replaceTransition"){
        auto& state=entry(g,"states",p.getStr("state"));auto& list=state.member("transitions");
        if(list.isNull())list=Json::array();
        require(list.isArray(),"transitions array required");
        if(action=="addTransition"){
            require(list.size()<64&&p["value"].isObject()&&exists(g,"states",p["value"].getStr("to")),"invalid transition destination or limit");
            list.push(p["value"]);
        }else{
            const auto i=p.getInt("index",-1);
            require(p["index"].isNumber()&&p["index"].toDouble()==double(i)&&i>=0&&size_t(i)<list.size(),"transition index invalid");
            if(action=="removeTransition")list.eraseAt(size_t(i));
            else{require(p["value"].isObject()&&exists(g,"states",p["value"].getStr("to")),"invalid transition destination");list.at(size_t(i))=p["value"];}
        }
    }else throw std::invalid_argument("unsupported animation draft action");
}
inline Json actions(const Json& p){
    if(p.has("edits")){
        require(!p.has("edit")&&p["edits"].isArray()&&p["edits"].size()>0&&p["edits"].size()<=64,"edits requires 1..64 commands; do not also supply edit");
        return p["edits"];
    }
    require(p["edit"].isObject(),"edit or edits required");
    Json list=Json::array();list.push(p["edit"]);return list;
}
} // namespace aiverse::editor::graph_draft
