#pragma once
// JSON definitions for sports-rules (example: examples/libraries/genre/vehicles-sports/sports.json).
// Top level: "field" (milli-metres, boundary lines|walls|faceoff, faceoffSpotsMilli) and "formations" (id, slots
// with role keeper|defender|midfielder|forward, baseMilli/attackMilli/defendMilli as [x,y] milli fractions of the
// half field in the attacking frame, ballPullMilli). Scalar rules come from the generated `sports` settings
// section (SportsConfigured.h). Errors: JsonError("sports.<path>: ...").
#include "Sports.h"
#include "engine/data/Json.h"
#include <charconv>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
namespace aiverse::lib::genre {
struct SportsDefinitions{SportsField field;std::map<int64_t,SportsFormation> formations;};
namespace sportsDataDetail {
inline int64_t integer(const Json& j,const std::string& path,int64_t low,int64_t high){
    if(!j.isNumber())throw JsonError(path+": expected integer");
    const auto& text=j.numberLiteral();int64_t value=0;auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())throw JsonError(path+": expected integer");
    if(value<low||value>high)throw JsonError(path+": must be in "+std::to_string(low)+".."+std::to_string(high));return value;
}
inline void keys(const Json& j,const std::string& path,std::initializer_list<std::string_view> names){
    if(!j.isObject())throw JsonError(path+": expected object");
    for(const auto& [key,value]:j.members()){(void)value;if(std::find(names.begin(),names.end(),key)==names.end())throw JsonError(path+": unknown key '"+key+"'");}
}
inline std::string at(const std::string& path,std::size_t i){return path+"["+std::to_string(i)+"]";}
inline Vec2 pair(const Json& j,const std::string& path,int64_t limit){
    if(!j.isArray()||j.size()!=2)throw JsonError(path+": expected [x,y] in milli-units");
    return {Fixed::fromRatio(integer(j[0],path+"[0]",-limit,limit),1000),Fixed::fromRatio(integer(j[1],path+"[1]",-limit,limit),1000)};
}
inline void opt(const Json& j,const std::string& path,const char* key,Fixed& out,int64_t low,int64_t high){if(j.has(key))out=Fixed::fromRatio(integer(j[key],path+"."+key,low,high),1000);}
inline SportsRole role(const Json& j,const std::string& path){
    auto s=j.str();if(s=="keeper")return SportsRole::Keeper;if(s=="defender")return SportsRole::Defender;if(s=="midfielder")return SportsRole::Midfielder;if(s=="forward")return SportsRole::Forward;
    throw JsonError(path+": expected keeper, defender, midfielder or forward");
}
}
inline SportsDefinitions readSportsDefinitions(const Json& root){
    using namespace sportsDataDetail;SportsDefinitions out;keys(root,"sports",{"field","formations"});
    if(root.has("field")){
        const auto& f=root["field"];const std::string p="sports.field";
        keys(f,p,{"halfLengthMilli","halfWidthMilli","goalHalfWidthMilli","penaltyDepthMilli","penaltyHalfWidthMilli","penaltySpotMilli","goalKickDepthMilli","boundary","faceoffSpotsMilli"});
        auto& d=out.field;
        opt(f,p,"halfLengthMilli",d.halfLength,1,10000000);opt(f,p,"halfWidthMilli",d.halfWidth,1,10000000);opt(f,p,"goalHalfWidthMilli",d.goalHalfWidth,1,10000000);
        opt(f,p,"penaltyDepthMilli",d.penaltyDepth,0,10000000);opt(f,p,"penaltyHalfWidthMilli",d.penaltyHalfWidth,0,10000000);opt(f,p,"penaltySpotMilli",d.penaltySpot,0,10000000);
        opt(f,p,"goalKickDepthMilli",d.goalKickDepth,0,10000000);
        if(f.has("boundary")){auto b=f["boundary"].str();if(b=="lines")d.boundary=SportsBoundary::Lines;else if(b=="walls")d.boundary=SportsBoundary::Walls;else if(b=="faceoff")d.boundary=SportsBoundary::FaceOff;
            else throw JsonError(p+".boundary: expected lines, walls or faceoff");}
        if(f.has("faceoffSpotsMilli")){const auto& s=f["faceoffSpotsMilli"];if(!s.isArray()||s.size()>kSportsMaxSpots)throw JsonError(p+".faceoffSpotsMilli: expected an array of at most 32 spots");
            for(std::size_t i=0;i<s.size();++i)d.faceoffSpots.push_back(pair(s[i],at(p+".faceoffSpotsMilli",i),10000000));}
        if(d.goalHalfWidth>d.halfWidth)throw JsonError(p+".goalHalfWidthMilli: must not exceed halfWidthMilli");
        if(d.penaltyDepth>d.halfLength||d.penaltySpot>d.halfLength||d.goalKickDepth>d.halfLength)throw JsonError(p+".penaltyDepthMilli: penalty/goal-kick depths must not exceed halfLengthMilli");
        if(d.penaltyHalfWidth>d.halfWidth)throw JsonError(p+".penaltyHalfWidthMilli: must not exceed halfWidthMilli");
        for(std::size_t i=0;i<d.faceoffSpots.size();++i)if(fxAbs(d.faceoffSpots[i].x)>d.halfLength||fxAbs(d.faceoffSpots[i].y)>d.halfWidth)throw JsonError(at(p+".faceoffSpotsMilli",i)+": outside the field");
        if(!valid(d))throw JsonError(p+": invalid field");
    }
    if(root.has("formations")){
        const auto& fs=root["formations"];if(!fs.isArray()||fs.size()>64)throw JsonError("sports.formations: expected an array of at most 64 formations");
        for(std::size_t i=0;i<fs.size();++i){
            auto p=at("sports.formations",i);keys(fs[i],p,{"id","slots"});if(!fs[i].has("id")||!fs[i].has("slots"))throw JsonError(p+": id and slots are required");
            SportsFormation f;f.id=integer(fs[i]["id"],p+".id",1,INT64_MAX);if(out.formations.contains(f.id))throw JsonError(p+".id: duplicate id "+std::to_string(f.id));
            const auto& ss=fs[i]["slots"];if(!ss.isArray()||!ss.size()||ss.size()>kSportsMaxSlots)throw JsonError(p+".slots: expected 1..32 slots");
            for(std::size_t k=0;k<ss.size();++k){
                auto sp=at(p+".slots",k);keys(ss[k],sp,{"id","role","baseMilli","attackMilli","defendMilli","ballPullMilli"});
                if(!ss[k].has("id")||!ss[k].has("role")||!ss[k].has("baseMilli"))throw JsonError(sp+": id, role and baseMilli are required");
                SportsSlot s;s.id=integer(ss[k]["id"],sp+".id",1,INT64_MAX);if(!f.slots.empty()&&s.id<=f.slots.back().id)throw JsonError(sp+".id: slot ids must be strictly ascending");
                s.role=role(ss[k]["role"],sp+".role");s.base=pair(ss[k]["baseMilli"],sp+".baseMilli",1000);
                if(ss[k].has("attackMilli"))s.attack=pair(ss[k]["attackMilli"],sp+".attackMilli",1000);if(ss[k].has("defendMilli"))s.defend=pair(ss[k]["defendMilli"],sp+".defendMilli",1000);
                opt(ss[k],sp,"ballPullMilli",s.ballPull,0,1000);f.slots.push_back(s);
            }
            out.formations[f.id]=std::move(f);
        }
    }
    return out;
}
}
