// Read-only Studio/WorldInspect projection of actual World Processes ECS data.
// This is presentation only: no new component pools, state writes or editors.
// Existing runtime Studio.gameInspect consumes this same WorldInspect JSON path.
#pragma once
#include "EchoEcs.h"
#include "FractureEcs.h"
#include "AetherEcs.h"
#include "AetherPhysisBridge.h"
#include "engine/data/Json.h"
#include <cstdint>
#include <initializer_list>
#include <string>

namespace aiverse::lib::worldprocess::inspector {

struct Field {const char* id;const char* title;const char* kind;};

inline Json entityValue(const World& world,Entity target) {
    if(!target.valid())return Json{};
    // Store the full generation-safe identity as text; JS Number cannot
    // exactly represent every possible 64-bit packed Entity.
    Json out=Json::object();
    out.set("packed",Json::string(std::to_string(target.packed())));
    out.set("alive",Json::fromBool(world.alive(target)));
    return out;
}
inline void append(Json& values,Json& schemas,const char* id,
                   const char* title,Json value,
                   std::initializer_list<Field> fields){
    if(!values.isObject()||!schemas.isObject()||values.has(id))return;
    Json schema=Json::object(),allFields=Json::object();
    schema.set("title",Json::string(title));
    for(const Field& field:fields){
        Json fieldSchema=Json::object();
        fieldSchema.set("title",Json::string(field.title));
        fieldSchema.set("kind",Json::string(field.kind));
        fieldSchema.set("editable",Json::fromBool(false));
        allFields.set(field.id,std::move(fieldSchema));
    }
    schema.set("fields",std::move(allFields));
    values.set(id,std::move(value));
    schemas.set(id,std::move(schema));
}
inline Json number(std::int64_t v){return Json::number(v);}
inline Json boolValue(bool v){return Json::fromBool(v);}
inline Json eventKey(std::uint64_t v){return Json::string(std::to_string(v));}

inline void appendWorldProcessComponents(const World& world,Entity entity,
                                          Json& components,Json& schemas){
    if(!world.alive(entity)||!components.isObject()||!schemas.isObject())return;
    if(const auto* v=world.tryGetConst<echo::AcousticZone>(entity)){
        Json fields=Json::object();
        fields.set("enabled",boolValue(v->enabled));
        append(components,schemas,"EchoZone","ECHO · Acoustic Zone",std::move(fields),
               {{"enabled","Enabled","boolean"}});
    }
    if(const auto* v=world.tryGetConst<echo::AcousticPortal>(entity)){
        Json fields=Json::object();
        fields.set("from",entityValue(world,v->from));
        fields.set("to",entityValue(world,v->to));
        fields.set("loss",number(v->loss));
        fields.set("bidirectional",boolValue(v->bidirectional));
        fields.set("enabled",boolValue(v->enabled));
        append(components,schemas,"EchoPortal","ECHO · Acoustic Portal",std::move(fields),
               {{"from","From zone","entity"},{"to","To zone","entity"},
                {"loss","Attenuation","number"},{"bidirectional","Bidirectional","boolean"},
                {"enabled","Enabled","boolean"}});
    }
    if(const auto* v=world.tryGetConst<echo::AcousticEmitter>(entity)){
        Json fields=Json::object();
        fields.set("zone",entityValue(world,v->zone));
        fields.set("intensity",number(v->intensity));
        fields.set("enabled",boolValue(v->enabled));
        append(components,schemas,"EchoEmitter","ECHO · Source",std::move(fields),
               {{"zone","Zone","entity"},{"intensity","Intensity","number"},
                {"enabled","Enabled","boolean"}});
    }
    if(const auto* v=world.tryGetConst<echo::AcousticReceiver>(entity)){
        Json fields=Json::object();
        fields.set("zone",entityValue(world,v->zone));
        fields.set("threshold",number(v->threshold));
        fields.set("enabled",boolValue(v->enabled));
        append(components,schemas,"EchoReceiver","ECHO · Listener",std::move(fields),
               {{"zone","Zone","entity"},{"threshold","Hearing threshold","number"},
                {"enabled","Enabled","boolean"}});
    }
    if(const auto* v=world.tryGetConst<echo::AcousticHearing>(entity)){
        Json fields=Json::object();
        fields.set("source",entityValue(world,v->source));
        fields.set("intensity",number(v->intensity));
        fields.set("sampledTick",number(v->sampledTick));
        fields.set("audible",boolValue(v->audible));
        fields.set("valid",boolValue(v->valid));
        append(components,schemas,"EchoHearing","ECHO · Hearing Result",std::move(fields),
               {{"source","Source","entity"},{"intensity","Perceived intensity","number"},
                {"sampledTick","Sample tick","number"},{"audible","Audible","boolean"},
                {"valid","Valid","boolean"}});
    }
    if(const auto* v=world.tryGetConst<fracture::Member>(entity)){
        Json fields=Json::object();
        fields.set("anchored",boolValue(v->anchored));
        fields.set("broken",boolValue(v->broken));
        fields.set("detached",boolValue(v->detached));
        append(components,schemas,"FractureMember","FRACTURE · Member",std::move(fields),
               {{"anchored","Support anchor","boolean"},{"broken","Broken","boolean"},
                {"detached","Detached","boolean"}});
    }
    if(const auto* v=world.tryGetConst<fracture::Bond>(entity)){
        Json fields=Json::object();
        fields.set("first",entityValue(world,v->first));
        fields.set("second",entityValue(world,v->second));
        fields.set("strength",number(v->strength));
        fields.set("damage",number(v->damage));
        fields.set("enabled",boolValue(v->enabled));
        append(components,schemas,"FractureBond","FRACTURE · Bond",std::move(fields),
               {{"first","Member A","entity"},{"second","Member B","entity"},
                {"strength","Strength","number"},{"damage","Damage","number"},
                {"enabled","Enabled","boolean"}});
    }
    if(const auto* v=world.tryGetConst<fracture::Collapse>(entity)){
        Json fields=Json::object();
        fields.set("eventKey",eventKey(v->eventKey));
        fields.set("detachedTick",number(v->detachedTick));
        fields.set("delivery",number(static_cast<std::uint8_t>(v->delivery)));
        append(components,schemas,"FractureCollapse","FRACTURE · Collapse Event",
               std::move(fields),
               {{"eventKey","Event key","string"},{"detachedTick","Detach tick","number"},
                {"delivery","Delivery state (0/1/2)","number"}});
    }
    if(const auto* v=world.tryGetConst<aether::GasCell>(entity)){
        Json fields=Json::object();
        fields.set("smoke",number(v->smoke));
        fields.set("toxin",number(v->toxin));
        fields.set("oxygen",number(v->oxygen));
        fields.set("sampledTick",number(v->sampledTick));
        fields.set("enabled",boolValue(v->enabled));
        append(components,schemas,"AetherGasCell","AETHER · Gas Cell",std::move(fields),
               {{"smoke","Smoke mass","number"},{"toxin","Toxin mass","number"},
                {"oxygen","Oxygen mass","number"},{"sampledTick","Sample tick","number"},
                {"enabled","Enabled","boolean"}});
    }
    if(const auto* v=world.tryGetConst<aether::GasDuct>(entity)){
        Json fields=Json::object();
        fields.set("from",entityValue(world,v->from));
        fields.set("to",entityValue(world,v->to));
        fields.set("maximumPerTick",number(v->maximumPerTick));
        fields.set("oneWay",boolValue(v->oneWay));
        fields.set("enabled",boolValue(v->enabled));
        append(components,schemas,"AetherDuct","AETHER · Duct",std::move(fields),
               {{"from","From cell","entity"},{"to","To cell","entity"},
                {"maximumPerTick","Max flow/tick","number"},{"oneWay","One-way","boolean"},
                {"enabled","Enabled","boolean"}});
    }
    if(const auto* v=world.tryGetConst<aether::GasFlux>(entity)){
        Json fields=Json::object();
        fields.set("cell",entityValue(world,v->cell));
        fields.set("smoke",number(v->smoke));
        fields.set("toxin",number(v->toxin));
        fields.set("oxygen",number(v->oxygen));
        fields.set("enabled",boolValue(v->enabled));
        append(components,schemas,"AetherFlux","AETHER · Source/Sink",std::move(fields),
               {{"cell","Target cell","entity"},{"smoke","Smoke per tick","number"},
                {"toxin","Toxin per tick","number"},{"oxygen","Oxygen per tick","number"},
                {"enabled","Enabled","boolean"}});
    }
    if(const auto* v=world.tryGetConst<aether::FireCoupling>(entity)){
        Json fields=Json::object();
        fields.set("oxygenConsumedAtFullFire",number(v->oxygenConsumedAtFullFire));
        fields.set("smokeProducedAtFullFire",number(v->smokeProducedAtFullFire));
        fields.set("processedClimateTick",eventKey(v->processedClimateTick));
        fields.set("enabled",boolValue(v->enabled));
        append(components,schemas,"AetherFireCoupling","AETHER · PHYSIS Fire",
               std::move(fields),
               {{"oxygenConsumedAtFullFire","Oxygen consumption","number"},
                {"smokeProducedAtFullFire","Smoke production","number"},
                {"processedClimateTick","Climate tick","string"},
                {"enabled","Enabled","boolean"}});
    }
}
} // namespace aiverse::lib::worldprocess::inspector
