#pragma once
// JSON tactics scenario (example: examples/libraries/genre/cards-turns/skirmish.json):
// {"grid":{"shape":"hex","rows":["..h..","....."],"legend":{"r":{"cost":2}}},          shape: square|hex
//  "rules":{"roundLimit":30,"attackEndsTurn":true,"halfCoverMilli":200,"fullCoverMilli":400,"flankBonusMilli":150,
//           "rangePenaltyMilli":40,"overwatchPenaltyMilli":150,"minHitMilli":50,"maxHitMilli":950,"coverCosMilli":707},
//  "unitTypes":[{"id":1,"name":"unit.rifle","hp":6,"ap":2,"mobility":4,"aimMilli":700,"damage":3,"flankDamage":1,
//                "range":[5,8],"sight":10,"initiative":10}],
//  "units":[{"id":1,"type":1,"team":1,"at":[0,0]}]}
// Default legend: '.' floor, ',' rough (cost 2), 'h' low wall (half cover), 'H' high wall and '#' rock (full cover,
// block sight), '~' water (impassable, open). Rows are y from 0, characters x from 0 (hex: axial q, r).
// `rules` fields override the passed-in defaults (tacticsRulesFromSettings). Errors: JsonError("tactics.<path>: ...").
#include "TacticsTurns.h"
#include "engine/data/Json.h"
#include <array>
#include <charconv>
#include <initializer_list>
#include <string_view>
namespace aiverse::lib::genre {
struct TacticsScenarioData{TacticsScenario scenario;std::vector<TacticsUnit> units;};
namespace detail_tactics_data {
inline int64_t integer(const Json& j,const std::string& path,int64_t low,int64_t high){
    if(!j.isNumber())throw JsonError(path+": expected integer");
    const auto& text=j.numberLiteral();int64_t value=0;auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())throw JsonError(path+": expected integer");
    if(value<low||value>high)throw JsonError(path+": must be in "+std::to_string(low)+".."+std::to_string(high));
    return value;
}
inline bool optBool(const Json& j,const char* key,const std::string& path,bool fallback){
    if(!j.has(key))return fallback;
    if(j[key].type()!=Json::Type::Bool)throw JsonError(path+"."+key+": expected boolean");
    return j[key].boolean();
}
inline void keys(const Json& j,const std::string& path,std::initializer_list<std::string_view> names){
    if(!j.isObject())throw JsonError(path+": expected object");
    for(const auto& [key,value]:j.members()){(void)value;bool found=false;for(auto n:names)if(n==key)found=true;if(!found)throw JsonError(path+": unknown key '"+key+"'");}
}
inline const Json& list(const Json& j,const char* key,const std::string& path,std::size_t limit){
    if(!j.has(key)||!j[key].isArray()||j[key].size()==0||j[key].size()>limit)throw JsonError(path+"."+key+": expected an array of 1.."+std::to_string(limit)+" entries");
    return j[key];
}
inline TacticsCell legendCell(const Json& j,const std::string& p){
    keys(j,p,{"cost","cover","blocksSight"});TacticsCell c;
    if(j.has("cost"))c.cost=uint8_t(integer(j["cost"],p+".cost",0,kTacticsMaxCost));
    if(j.has("cover")){const auto& v=j["cover"];if(!v.isString())throw JsonError(p+".cover: expected string");
        if(v.str()=="none")c.cover=TacticsCover::None;else if(v.str()=="half")c.cover=TacticsCover::Half;else if(v.str()=="full")c.cover=TacticsCover::Full;
        else throw JsonError(p+".cover: unknown value '"+v.str()+"' (expected none, half, full)");}
    c.blocksSight=optBool(j,"blocksSight",p,false);return c;
}
}
inline TacticsRules readTacticsRules(const Json& j,TacticsRules r){
    using namespace detail_tactics_data;const std::string p="tactics.rules";
    keys(j,p,{"roundLimit","attackEndsTurn","halfCoverMilli","fullCoverMilli","flankBonusMilli","rangePenaltyMilli","overwatchPenaltyMilli","minHitMilli","maxHitMilli","coverCosMilli"});
    auto milli=[&](const char* key,int32_t& out){if(j.has(key))out=int32_t(integer(j[key],p+"."+key,0,1000));};
    if(j.has("roundLimit"))r.roundLimit=int32_t(integer(j["roundLimit"],p+".roundLimit",0,100000));
    r.attackEndsTurn=optBool(j,"attackEndsTurn",p,r.attackEndsTurn);auto& f=r.fire;
    milli("halfCoverMilli",f.halfCoverMilli);milli("fullCoverMilli",f.fullCoverMilli);milli("flankBonusMilli",f.flankBonusMilli);milli("rangePenaltyMilli",f.rangePenaltyMilli);
    milli("overwatchPenaltyMilli",f.overwatchPenaltyMilli);milli("minHitMilli",f.minHitMilli);milli("maxHitMilli",f.maxHitMilli);milli("coverCosMilli",f.coverCosMilli);
    if(f.halfCoverMilli>f.fullCoverMilli)throw JsonError(p+".halfCoverMilli: must not exceed fullCoverMilli");
    if(f.minHitMilli>f.maxHitMilli)throw JsonError(p+".minHitMilli: must not exceed maxHitMilli");
    return r;
}
inline TacticsScenarioData readTacticsScenario(const Json& root,const TacticsRules& defaults=TacticsRules{}){
    using namespace detail_tactics_data;
    keys(root,"tactics",{"grid","rules","unitTypes","units"});TacticsScenarioData out;auto& sc=out.scenario;
    const auto& g=root["grid"];keys(g,"tactics.grid",{"shape","rows","legend"});
    if(!g["shape"].isString()||(g["shape"].str()!="square"&&g["shape"].str()!="hex"))throw JsonError("tactics.grid.shape: expected 'square' or 'hex'");
    sc.grid.shape=g["shape"].str()=="hex"?TacticsShape::Hex:TacticsShape::Square;
    std::array<std::optional<TacticsCell>,128> legend{};
    legend['.']=TacticsCell{};legend[',']=TacticsCell{2,TacticsCover::None,false};legend['h']=TacticsCell{0,TacticsCover::Half,false};
    legend['H']=TacticsCell{0,TacticsCover::Full,true};legend['#']=TacticsCell{0,TacticsCover::Full,true};legend['~']=TacticsCell{0,TacticsCover::None,false};
    if(g.has("legend")){
        const auto& l=g["legend"];if(!l.isObject())throw JsonError("tactics.grid.legend: expected object");
        for(const auto& [key,value]:l.members()){if(key.size()!=1||key[0]<=' '||key[0]>'~')throw JsonError("tactics.grid.legend: keys must be single printable characters");
            legend[std::size_t(key[0])]=legendCell(value,"tactics.grid.legend."+key);}
    }
    const auto& rows=list(g,"rows","tactics.grid",std::size_t(kTacticsMaxSide));
    sc.grid.height=int32_t(rows.size());
    for(std::size_t y=0;y<rows.size();++y){
        auto p="tactics.grid.rows["+std::to_string(y)+"]";if(!rows[y].isString())throw JsonError(p+": expected string");const auto& row=rows[y].str();
        if(row.empty()||row.size()>std::size_t(kTacticsMaxSide))throw JsonError(p+": expected 1..256 cells");
        if(y==0)sc.grid.width=int32_t(row.size());else if(int32_t(row.size())!=sc.grid.width)throw JsonError(p+": expected "+std::to_string(sc.grid.width)+" cells like row 0");
        for(std::size_t x=0;x<row.size();++x){auto ch=std::size_t((unsigned char)row[x]);if(ch>=legend.size()||!legend[ch])throw JsonError(p+"["+std::to_string(x)+"]: unknown cell '"+std::string(1,row[x])+"'");sc.grid.cells.push_back(*legend[ch]);}
    }
    sc.rules=root.has("rules")?readTacticsRules(root["rules"],defaults):defaults;
    const auto& types=list(root,"unitTypes","tactics",kTacticsMaxDefs);
    for(std::size_t i=0;i<types.size();++i){
        auto p="tactics.unitTypes["+std::to_string(i)+"]";const auto& t=types[i];
        keys(t,p,{"id","name","hp","ap","mobility","aimMilli","damage","flankDamage","range","sight","initiative"});TacticsUnitDef d;
        d.id=integer(t["id"],p+".id",1,INT32_MAX);
        if(!t["name"].isString()||t["name"].str().empty()||t["name"].str().size()>128)throw JsonError(p+".name: expected a localization key (1..128 chars)");
        d.name=t["name"].str();
        auto num=[&](const char* key,int32_t& out,int64_t lo,int64_t hi){if(t.has(key))out=int32_t(integer(t[key],p+"."+key,lo,hi));};
        num("hp",d.hp,1,kTacticsValueLimit);num("ap",d.ap,1,10);num("mobility",d.mobility,0,64);num("aimMilli",d.aimMilli,0,1000);num("damage",d.damage,0,kTacticsValueLimit);
        num("flankDamage",d.flankDamage,0,kTacticsValueLimit);num("sight",d.sight,0,kTacticsMaxSide);num("initiative",d.initiative,-1000,1000);
        if(t.has("range")){const auto& r=t["range"];if(!r.isArray()||r.size()!=2)throw JsonError(p+".range: expected [optimal, max]");
            d.optimalRange=int32_t(integer(r[0],p+".range[0]",0,kTacticsMaxSide));d.maxRange=int32_t(integer(r[1],p+".range[1]",1,kTacticsMaxSide));
            if(d.optimalRange>d.maxRange)throw JsonError(p+".range: optimal must not exceed max");}
        if(!sc.defs.emplace(d.id,d).second)throw JsonError(p+".id: duplicate id "+std::to_string(d.id));
    }
    const auto& units=list(root,"units","tactics",kTacticsMaxUnits);std::vector<uint8_t> used(sc.grid.cells.size(),0);
    for(std::size_t i=0;i<units.size();++i){
        auto p="tactics.units["+std::to_string(i)+"]";const auto& u=units[i];keys(u,p,{"id","type","team","at"});TacticsUnit unit;
        unit.id=integer(u["id"],p+".id",1,INT32_MAX);unit.def=integer(u["type"],p+".type",1,INT32_MAX);unit.team=int32_t(integer(u["team"],p+".team",1,1000));
        if(!sc.defs.contains(unit.def))throw JsonError(p+".type: unknown unit type "+std::to_string(unit.def));
        const auto& at=u["at"];if(!at.isArray()||at.size()!=2)throw JsonError(p+".at: expected [x, y]");
        unit.cell=tacticsIndex(sc.grid,{int32_t(integer(at[0],p+".at[0]",0,sc.grid.width-1)),int32_t(integer(at[1],p+".at[1]",0,sc.grid.height-1))});
        if(!sc.grid.cells[std::size_t(unit.cell)].cost)throw JsonError(p+".at: cell is impassable");
        if(used[std::size_t(unit.cell)]++)throw JsonError(p+".at: cell already occupied");
        for(const auto& o:out.units)if(o.id==unit.id)throw JsonError(p+".id: duplicate id "+std::to_string(unit.id));
        out.units.push_back(unit);
    }
    auto errors=tacticsScenarioErrors(sc);if(!errors.empty())throw JsonError(errors.front());
    return out;
}
}
