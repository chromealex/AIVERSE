#pragma once
// Tiered protective gear for any genre: helmets, vests, shields, backpacks, rings - pieces that sit in
// equipment slots, reduce damage to the hit zones they cover by a share, lose durability with the damage
// they absorb, break at zero and may add carry capacity. A loadout holds one piece per slot; equipping
// returns the replaced piece so the owner can drop it as loot with its remaining durability. betterGear
// orders pieces for automatic pickups and bots. Integer math only (milli-shares), deterministic, bounded.
// The owner decides the hit zone and applies the returned damage; health stays with the owner.
#include "engine/core/Archive.h"
#include "engine/core/Hash.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace aiverse::lib::systems {
inline constexpr std::size_t kGearMaxDefs=256,kGearMaxSlots=16;
inline constexpr int64_t kGearMaxDurability=1000000;
struct GearDef{
    int64_t id=0;uint32_t slot=0,tier=1;int64_t durability=100;
    int32_t reductionMilli=0;      // share of covered damage absorbed, 0..1000
    int32_t wearMilli=1000;        // durability lost per absorbed point, in milli (1000 = 1:1)
    uint32_t zones=0;              // bit mask of covered hit zones (owner-defined: e.g. 1 head, 2 body, 4 legs)
    int32_t capacity=0;            // extra carry capacity (slots, weight - the owner decides the unit)
    bool operator==(const GearDef&)const=default;
};
using GearCatalog=std::map<int64_t,GearDef>;
struct GearPiece{int64_t def=0,durability=0;bool operator==(const GearPiece&)const=default;};
struct GearLoadout{std::map<uint32_t,GearPiece> slots;bool operator==(const GearLoadout&)const=default;};
struct GearHit{int64_t damage=0,absorbed=0,worn=0;std::vector<uint32_t> broken;bool operator==(const GearHit&)const=default;};

inline bool valid(const GearDef& d){
    return d.id>0&&d.slot<kGearMaxSlots&&d.tier>=1&&d.tier<=100&&d.durability>0&&d.durability<=kGearMaxDurability&&d.reductionMilli>=0&&d.reductionMilli<=1000
        &&d.wearMilli>=0&&d.wearMilli<=100000&&d.capacity>=0&&d.capacity<=1000000;
}
inline bool valid(const GearCatalog& c){if(c.size()>kGearMaxDefs)return false;for(const auto& [id,d]:c)if(id!=d.id||!valid(d))return false;return true;}
inline bool valid(const GearCatalog& c,const GearLoadout& l){
    if(l.slots.size()>kGearMaxSlots)return false;
    for(const auto& [slot,p]:l.slots){auto it=c.find(p.def);if(it==c.end()||it->second.slot!=slot||p.durability<=0||p.durability>it->second.durability)return false;}
    return true;
}
inline GearPiece freshGear(const GearDef& d){return {d.id,d.durability};}
// Absorbs part of one hit on `zone` (bit). Each covering piece in ascending slot order takes its share of the
// remaining damage; absorbed points never exceed the piece's durability. Broken pieces are removed.
inline std::optional<GearHit> absorbHit(const GearCatalog& c,GearLoadout& loadout,uint32_t zone,int64_t damage){
    if(damage<0||damage>kGearMaxDurability||!valid(c,loadout))return {};
    GearHit hit;hit.damage=damage;auto next=loadout;
    for(auto it=next.slots.begin();it!=next.slots.end();){
        const auto& d=c.at(it->second.def);
        if(!(d.zones&zone)||!d.reductionMilli||!hit.damage){++it;continue;}
        auto share=hit.damage*d.reductionMilli/1000;
        auto wearPer=std::max<int64_t>(d.wearMilli,0);
        if(wearPer){auto capacity=it->second.durability*1000/wearPer;share=std::min(share,std::max<int64_t>(capacity,1));}
        auto wear=(share*wearPer+999)/1000;
        hit.damage-=share;hit.absorbed+=share;hit.worn+=std::min(wear,it->second.durability);it->second.durability-=wear;
        if(it->second.durability<=0){hit.broken.push_back(it->first);it=next.slots.erase(it);}else ++it;
    }
    loadout=std::move(next);return hit;
}
// Puts `piece` into its slot; returns the replaced piece (or a zero piece when the slot was empty).
inline std::optional<GearPiece> equipGear(const GearCatalog& c,GearLoadout& loadout,GearPiece piece){
    auto it=c.find(piece.def);if(it==c.end()||piece.durability<=0||piece.durability>it->second.durability||!valid(c,loadout))return {};
    GearPiece previous;auto slot=it->second.slot;if(auto old=loadout.slots.find(slot);old!=loadout.slots.end())previous=old->second;
    loadout.slots[slot]=piece;return previous;
}
inline std::optional<GearPiece> unequipGear(GearLoadout& loadout,uint32_t slot){auto it=loadout.slots.find(slot);if(it==loadout.slots.end())return {};auto p=it->second;loadout.slots.erase(it);return p;}
inline int64_t gearCapacity(const GearCatalog& c,const GearLoadout& l){int64_t n=0;for(const auto& [slot,p]:l.slots){(void)slot;if(auto it=c.find(p.def);it!=c.end())n+=it->second.capacity;}return n;}
// True when `candidate` should replace `current` in the same slot: higher tier, else more remaining share.
inline bool betterGear(const GearCatalog& c,const GearPiece& candidate,const GearPiece* current){
    auto a=c.find(candidate.def);if(a==c.end()||candidate.durability<=0)return false;if(!current||!current->def)return true;
    auto b=c.find(current->def);if(b==c.end())return true;if(a->second.slot!=b->second.slot)return false;
    if(a->second.tier!=b->second.tier)return a->second.tier>b->second.tier;
    return candidate.durability*b->second.durability>current->durability*a->second.durability;
}
// {"gear":[{"id":1,"slot":0,"tier":1,"durability":80,"reduction":300,"wear":1000,"zones":1,"capacity":0}]}
// reduction / wear in milli; slot / zones are the owner's numbering. Errors: JsonError("<root>.gear[i].<key>: ...").
namespace gearDetail {
inline int64_t integer(const Json& j,const std::string& key,const std::string& path,int64_t def,int64_t low,int64_t high){
    if(!j.has(key))return def;const auto& v=j[key];if(!v.isNumber())throw JsonError(path+"."+key+": expected integer");
    const auto& t=v.numberLiteral();int64_t x=0;auto r=std::from_chars(t.data(),t.data()+t.size(),x);
    if(r.ec!=std::errc{}||r.ptr!=t.data()+t.size())throw JsonError(path+"."+key+": expected integer");
    if(x<low||x>high)throw JsonError(path+"."+key+": must be in "+std::to_string(low)+".."+std::to_string(high));return x;
}
}
inline GearCatalog readGearCatalog(const Json& list,const std::string& path="gear"){
    if(!list.isArray()||list.size()>kGearMaxDefs)throw JsonError(path+": expected an array of at most "+std::to_string(kGearMaxDefs)+" pieces");
    GearCatalog c;
    for(std::size_t i=0;i<list.size();++i){
        auto p=path+"["+std::to_string(i)+"]";const auto& j=list[i];if(!j.isObject())throw JsonError(p+": expected object");
        for(const auto& [key,value]:j.members()){(void)value;static const char* names[]={"id","slot","tier","durability","reduction","wear","zones","capacity"};
            if(std::none_of(std::begin(names),std::end(names),[&](const char* n){return key==n;}))throw JsonError(p+": unknown key '"+key+"'");}
        using gearDetail::integer;GearDef d;
        if(!j.has("id"))throw JsonError(p+".id: required");
        d.id=integer(j,"id",p,0,1,INT32_MAX);if(c.contains(d.id))throw JsonError(p+".id: duplicate gear id "+std::to_string(d.id));
        d.slot=uint32_t(integer(j,"slot",p,0,0,kGearMaxSlots-1));d.tier=uint32_t(integer(j,"tier",p,1,1,100));
        d.durability=integer(j,"durability",p,100,1,kGearMaxDurability);d.reductionMilli=int32_t(integer(j,"reduction",p,0,0,1000));
        d.wearMilli=int32_t(integer(j,"wear",p,1000,0,100000));d.zones=uint32_t(integer(j,"zones",p,0,0,0xFFFF));d.capacity=int32_t(integer(j,"capacity",p,0,0,1000000));
        c[d.id]=d;
    }
    return c;
}
inline void io(Archive& a,GearPiece& p){a(p.def,p.durability);}
inline void io(Archive& a,GearLoadout& l){a(l.slots);}
inline uint64_t gearHash(const GearLoadout& l){std::string b;Archive a(&b);auto c=l;io(a,c);Hasher h;h.str(b);return h.value();}
}
