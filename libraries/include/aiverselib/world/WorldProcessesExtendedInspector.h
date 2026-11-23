// Read-only live Studio/WorldInspect projection for seven additional mechanics.
// Reuses canonical typed ECS fields, never edits World or allocates ECS pools.
#pragma once
#include "WorldProcessesInspector.h"
#include "WorldProcessesExtendedLinks.h"
#include <cstdint>
#include <string>
#include <utility>

namespace aiverse::lib::worldprocess::inspector {
inline Json fields(std::initializer_list<std::pair<std::string,Json>> values){
    Json out=Json::object();
    for(const auto& [key,value]:values)out.set(key,value);
    return out;
}
inline std::string hex64(std::uint64_t value){
    constexpr char digits[]="0123456789abcdef";
    std::string text(16,'0');
    for(int i=15;i>=0;--i){text[std::size_t(i)]=digits[value&15u];value>>=4u;}
    return text;
}
inline void appendAuto(Json& components,Json& schemas,
                       const char* id,const char* title,Json value){
    if(!components.isObject()||!schemas.isObject()||components.has(id))return;
    Json fieldSchema=Json::object();
    for(const auto& [key,v]:value.members()){
        Json meta=Json::object();
        meta.set("title",Json::string(key));
        meta.set("kind",Json::string(v.isNumber()?"number":
            v.type()==Json::Type::Bool?"boolean":v.isObject()?"entity":"string"));
        meta.set("editable",Json::fromBool(false));
        fieldSchema.set(key,std::move(meta));
    }
    Json schema=Json::object();
    schema.set("title",Json::string(title));
    schema.set("fields",std::move(fieldSchema));
    components.set(id,std::move(value));
    schemas.set(id,std::move(schema));
}
inline void appendWorldProcessExtendedComponents(const World& world,Entity e,
                                                  Json& components,Json& schemas){
    if(!world.alive(e)||!components.isObject()||!schemas.isObject())return;
    const auto ent=[&](Entity id){return entityValue(world,id);};
    const auto num=[&](std::int64_t v){return Json::number(v);};
    const auto boo=[&](bool v){return Json::fromBool(v);};
    const auto str=[&](std::string v){return Json::string(std::move(v));};
    if(const auto* v=world.tryGetConst<morph::Node>(e))
        appendAuto(components,schemas,"MorphNode","MORPH · Node",
            fields({{"x",num(v->x)},{"y",num(v->y)},{"z",num(v->z)},
                    {"velocityX",num(v->vx)},{"velocityY",num(v->vy)},
                    {"velocityZ",num(v->vz)},{"restX",num(v->restX)},
                    {"restY",num(v->restY)},{"restZ",num(v->restZ)},
                    {"drag",num(v->drag)},{"anchored",boo(v->anchored)},
                    {"enabled",boo(v->enabled)},{"sampledTick",num(v->sampledTick)}}));
    if(const auto* v=world.tryGetConst<morph::Spring>(e))
        appendAuto(components,schemas,"MorphSpring","MORPH · Spring",
            fields({{"a",ent(v->a)},{"b",ent(v->b)},
                    {"restDx",num(v->restDx)},{"restDy",num(v->restDy)},
                    {"restDz",num(v->restDz)},{"stiffness",num(v->stiffness)},
                    {"enabled",boo(v->enabled)}}));
    if(const auto* v=world.tryGetConst<morph::Force>(e))
        appendAuto(components,schemas,"MorphForce","MORPH · Force",
            fields({{"node",ent(v->node)},{"x",num(v->x)},{"y",num(v->y)},
                    {"z",num(v->z)},{"enabled",boo(v->enabled)}}));

    if(const auto* v=world.tryGetConst<crowd::Agent>(e))
        appendAuto(components,schemas,"CrowdAgent","CROWD · Agent",
            fields({{"x",num(v->x)},{"z",num(v->z)},
                    {"goalX",num(v->goalX)},{"goalZ",num(v->goalZ)},
                    {"radius",num(v->radius)},{"speed",num(v->speed)},
                    {"allocated",ent(v->allocated)},{"enabled",boo(v->enabled)},
                    {"sampledTick",num(v->sampledTick)}}));
    if(const auto* v=world.tryGetConst<crowd::Slot>(e))
        appendAuto(components,schemas,"CrowdSlot","CROWD · Slot",
            fields({{"x",num(v->x)},{"z",num(v->z)},{"enabled",boo(v->enabled)}}));
    if(const auto* v=world.tryGetConst<crowd::Preference>(e))
        appendAuto(components,schemas,"CrowdPreference","CROWD · Preference",
            fields({{"agent",ent(v->agent)},{"slot0",ent(v->options[0])},
                    {"slot1",ent(v->options[1])},{"slot2",ent(v->options[2])},
                    {"slot3",ent(v->options[3])},{"enabled",boo(v->enabled)}}));

    if(const auto* v=world.tryGetConst<imprint::Cell>(e))
        appendAuto(components,schemas,"ImprintCell","IMPRINT · Cell",
            fields({{"footprint",num(v->footprint)},{"scent",num(v->scent)},
                    {"residue",num(v->residue)},{"footprintFade",num(v->footprintFade)},
                    {"scentFade",num(v->scentFade)},{"residueFade",num(v->residueFade)},
                    {"enabled",boo(v->enabled)},{"sampledTick",num(v->sampledTick)}}));
    if(const auto* v=world.tryGetConst<imprint::Link>(e))
        appendAuto(components,schemas,"ImprintLink","IMPRINT · Link",
            fields({{"a",ent(v->a)},{"b",ent(v->b)},
                    {"transfer",num(v->transfer)},{"enabled",boo(v->enabled)}}));
    if(const auto* v=world.tryGetConst<imprint::Deposit>(e))
        appendAuto(components,schemas,"ImprintDeposit","IMPRINT · Deposit",
            fields({{"cell",ent(v->cell)},{"footprint",num(v->footprint)},
                    {"scent",num(v->scent)},{"residue",num(v->residue)},
                    {"enabled",boo(v->enabled)}}));

    if(const auto* v=world.tryGetConst<circuit::Node>(e))
        appendAuto(components,schemas,"CircuitNode","CIRCUIT · Node",
            fields({{"supply",num(v->supply)},{"demand",num(v->demand)},
                    {"drive",boo(v->drive)},{"powered",boo(v->powered)},
                    {"signal",boo(v->signal)},{"enabled",boo(v->enabled)},
                    {"sampledTick",num(v->sampledTick)}}));
    if(const auto* v=world.tryGetConst<circuit::Wire>(e))
        appendAuto(components,schemas,"CircuitWire","CIRCUIT · Wire",
            fields({{"from",ent(v->from)},{"to",ent(v->to)},
                    {"bidirectional",boo(v->bidirectional)},
                    {"enabled",boo(v->enabled)}}));
    if(const auto* v=world.tryGetConst<circuit::Gate>(e))
        appendAuto(components,schemas,"CircuitGate","CIRCUIT · Gate",
            fields({{"inputA",ent(v->inputA)},{"inputB",ent(v->inputB)},
                    {"output",ent(v->output)},
                    {"op",num(static_cast<std::uint8_t>(v->operation))},
                    {"enabled",boo(v->enabled)}}));

    if(const auto* v=world.tryGetConst<granular::Field>(e)){
        std::int64_t total=0,occupied=0;
        for(auto mass:v->mass){total+=mass;if(mass>0)++occupied;}
        appendAuto(components,schemas,"GranularField","GRANULAR · Field",
            fields({{"width",num(v->width)},{"height",num(v->height)},
                    {"capacity",num(v->capacity)},{"totalMass",num(total)},
                    {"occupiedCells",num(occupied)},{"enabled",boo(v->enabled)},
                    {"sampledTick",num(v->sampledTick)}}));
    }
    if(const auto* v=world.tryGetConst<granular::Source>(e))
        appendAuto(components,schemas,"GranularSource","GRANULAR · Source",
            fields({{"field",ent(v->field)},{"x",num(v->x)},{"y",num(v->y)},
                    {"delta",num(v->delta)},{"enabled",boo(v->enabled)}}));

    if(const auto* v=world.tryGetConst<evolve::Region>(e))
        appendAuto(components,schemas,"EvolveRegion","EVOLVE · Region",
            fields({{"durability",num(v->durability)},{"fertility",num(v->fertility)},
                    {"succession",num(v->succession)},{"pollution",num(v->pollution)},
                    {"enabled",boo(v->enabled)},{"sampledTick",num(v->sampledTick)}}));
    if(const auto* v=world.tryGetConst<evolve::Pressure>(e))
        appendAuto(components,schemas,"EvolvePressure","EVOLVE · Pressure",
            fields({{"region",ent(v->region)},{"wear",num(v->wear)},
                    {"recovery",num(v->recovery)},{"enrichment",num(v->enrichment)},
                    {"consumption",num(v->consumption)},
                    {"contamination",num(v->contamination)},
                    {"purification",num(v->purification)},{"growth",num(v->growth)},
                    {"regression",num(v->regression)},{"enabled",boo(v->enabled)}}));

    if(const auto* v=world.tryGetConst<society::Person>(e))
        appendAuto(components,schemas,"SocietyPerson","SOCIETY · Person",
            fields({{"knowledgeHex",str(hex64(v->knowledge))},
                    {"credibility",num(v->credibility)},
                    {"acceptance",num(v->acceptance)},{"enabled",boo(v->enabled)},
                    {"sampledTick",num(v->sampledTick)}}));
    if(const auto* v=world.tryGetConst<society::Contact>(e))
        appendAuto(components,schemas,"SocietyContact","SOCIETY · Contact",
            fields({{"source",ent(v->source)},{"target",ent(v->target)},
                    {"trust",num(v->trust)},{"enabled",boo(v->enabled)}}));
    if(const auto* v=world.tryGetConst<society::Message>(e))
        appendAuto(components,schemas,"SocietyMessage","SOCIETY · Message",
            fields({{"source",ent(v->source)},{"topicsHex",str(hex64(v->topics))},
                    {"enabled",boo(v->enabled)}}));
}
} // namespace aiverse::lib::worldprocess::inspector
