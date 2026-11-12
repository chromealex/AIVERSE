#pragma once
// sports-rules: role-based team formations. Slots are authored in the attacking frame (x forward toward the
// opponent goal, y to the left of that direction) as fractions of the half-field (-1..1), with offsets used while
// the team attacks or defends and a pull toward the ball. sportsSlotTarget converts a slot to a field position;
// sportsAssignSlots keeps previous assignments stable and fills free slots by role preference, then distance.
#include "SportsRules.h"
namespace aiverse::lib::genre {
inline constexpr std::size_t kSportsMaxSlots=32;
enum class SportsRole:uint8_t{Keeper,Defender,Midfielder,Forward};
enum class SportsTeamPhase:uint8_t{Attack,Defend,Neutral};
struct SportsSlot{int64_t id=0;SportsRole role=SportsRole::Midfielder;Vec2 base{},attack{},defend{};Fixed ballPull{};};
struct SportsFormation{int64_t id=0;std::vector<SportsSlot> slots;};   // ascending slot ids
struct SportsSlotPlayer{int64_t id=0;SportsRole role=SportsRole::Midfielder;Vec2 position{};};

inline bool valid(const SportsFormation& f){
    if(f.id<=0||f.slots.empty()||f.slots.size()>kSportsMaxSlots)return false;
    auto frac=[](Vec2 v){return sportsDetail::sportsBounded(v,1_fx);};
    for(std::size_t i=0;i<f.slots.size();++i){const auto& s=f.slots[i];if(s.id<=0||(i&&s.id<=f.slots[i-1].id)||uint8_t(s.role)>uint8_t(SportsRole::Forward)||!frac(s.base)||!frac(s.attack)||!frac(s.defend)||!sportsDetail::sportsNonneg(s.ballPull,1_fx))return false;}
    return true;
}
// attackDirection: +1 / -1 from sportsAttackDirection. The ball pull moves the slot by ballPull * (ball - slot).
inline Vec2 sportsSlotTarget(const SportsField& field,const SportsSlot& slot,int attackDirection,SportsTeamPhase phase,Vec2 ball){
    auto frame=slot.base+(phase==SportsTeamPhase::Attack?slot.attack:phase==SportsTeamPhase::Defend?slot.defend:Vec2{});
    frame={fxClamp(frame.x,-1_fx,1_fx),fxClamp(frame.y,-1_fx,1_fx)};
    auto d=Fixed::fromInt(attackDirection<0?-1:1);
    Vec2 p{frame.x*field.halfLength*d,frame.y*field.halfWidth*d};
    p+=(ball-p)*slot.ballPull;
    return {fxClamp(p.x,-field.halfLength,field.halfLength),fxClamp(p.y,-field.halfWidth,field.halfWidth)};
}
// slot id -> player id. Previous assignments survive while both exist; free slots (ascending id) take the
// unassigned player with the slot's role, then any role, nearest to the slot's base target, then smaller id.
inline std::map<int64_t,int64_t> sportsAssignSlots(const SportsField& field,const SportsFormation& f,int attackDirection,std::span<const SportsSlotPlayer> players,const std::map<int64_t,int64_t>& previous={}){
    std::map<int64_t,int64_t> out;std::vector<int64_t> used;
    auto present=[&](int64_t id){return std::any_of(players.begin(),players.end(),[&](const auto& p){return p.id==id;});};
    for(const auto& s:f.slots){auto it=previous.find(s.id);if(it!=previous.end()&&present(it->second)&&std::find(used.begin(),used.end(),it->second)==used.end()){out[s.id]=it->second;used.push_back(it->second);}}
    for(int pass=0;pass<2;++pass)for(const auto& s:f.slots){
        if(out.contains(s.id))continue;auto target=sportsSlotTarget(field,s,attackDirection,SportsTeamPhase::Neutral,{});
        const SportsSlotPlayer* best=nullptr;Fixed bestD{};
        for(const auto& p:players){
            if(std::find(used.begin(),used.end(),p.id)!=used.end()||(pass==0&&p.role!=s.role))continue;
            auto d=(p.position-target).lengthSq();if(!best||d<bestD||(d==bestD&&p.id<best->id)){best=&p;bestD=d;}
        }
        if(best){out[s.id]=best->id;used.push_back(best->id);}
    }
    return out;
}
}
