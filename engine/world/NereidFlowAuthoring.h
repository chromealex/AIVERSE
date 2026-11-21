// Typed authoring operations; hosts own Undo/save transactions.
#pragma once
#include "engine/world/NereidWaterManifest.h"
namespace aiverse::world {
inline Json editNereidFlow(const Json& root,const Json& input){
    auto result=root;auto water=result["nereidWater"];
    if(!input["id"].isNumber())throw JsonError("NEREID flow requires id");
    const auto id=input["id"].integer();if(id<1||id>2147483647||input["id"].fx()!=Fixed::fromInt(id))throw JsonError("NEREID flow requires integer id");
    // Edit entity-local canonical data, then validate its translated presentation.
    for(size_t i=0;i<root["entities"].size();++i) {
        const auto& e=root["entities"][i];const auto& component=e["components"]["NereidWater"];
        if(!component.isObject()||component["id"].integer()!=id)continue;
        auto local=Json::object(),block=Json::object(),list=Json::array();list.push(component);block.set("surfaces",list);local.set("nereidWater",block);
        auto edited=editNereidFlow(local,input);auto entities=root["entities"];auto entity=e;auto components=e["components"];components.set("NereidWater",edited["nereidWater"]["surfaces"][size_t(0)]);entity.set("components",components);entities.at(i)=entity;result.set("entities",entities);
        (void)parseNereidWaterManifest(result);return result;
    }
    size_t found=water["surfaces"].size();
    for(size_t i=0;i<water["surfaces"].size();++i)if(water["surfaces"][i]["id"].integer()==input["id"].integer())found=i;
    if(found==water["surfaces"].size())throw JsonError("NEREID water id not found");
    auto surface=water["surfaces"][found];auto grade=surface["grade"].isObject()?surface["grade"]:Json::object();
    const auto mode=input.getStr("mode","automatic");if(mode!="automatic"&&mode!="manual")throw JsonError("NEREID flow mode");
    grade.set("automaticCurrent",Json::fromBool(mode=="automatic"));
    if(input.has("flow"))surface.set("flow",input["flow"]);
    for(auto key:{"slopeDegrees","downhillHeadingDegrees","responseSeconds","maxSpeed","origin"})if(input.has(key))grade.set(key,input[key]);
    if(!grade.has("slopeDegrees")&&!grade.has("risePerMetre"))grade.set("slopeDegrees",Json::number(mode=="automatic"?1.0:0.0));
    surface.set("grade",grade);water.member("surfaces").at(found)=surface;result.set("nereidWater",water);
    (void)parseNereidWaterManifest(result);return result;
}
inline Json nereidFlowState(const Json& root){
    auto result=nereidEntityManifest(root);if(!result.has("surfaces"))result.set("surfaces",Json::array());
    for(size_t i=0;i<result["surfaces"].size();++i)for(const auto& e:root["entities"].items())if(e["components"]["NereidWater"].isObject()&&e["components"]["NereidWater"]["id"]==result["surfaces"][i]["id"])result.member("surfaces").at(i)=e["components"]["NereidWater"];
    auto computed=Json::array();for(const auto& s:parseNereidWaterManifest(root).surfaces){
        const auto& g=s.mesh.grade;const Vec3 p{g.origin.x,{},g.origin.y};
        const auto body=lib::nereid::gradedWaterBody(s.body,s.mesh.useGrade?&g:nullptr,p,s.useGeometry?&s.geometry:nullptr);
        auto e=Json::object(),flow=Json::array();flow.push(Json::number(body.flow.x.toDouble()));flow.push(Json::number(body.flow.y.toDouble()));flow.push(Json::number(body.flow.z.toDouble()));e.set("id",Json::number(int64_t(s.id)));e.set("flow",flow);computed.push(e);
    }result.set("computed",computed);return result;
}
}
