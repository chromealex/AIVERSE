#pragma once
#include "engine/mechanics/Namespace.h"
// colonist-relations: social layer for settlement-production colonists (RimWorld / Sims style).
// - Family links (parent, child, sibling, partner) with symmetric storage and validation.
// - Memories: data-defined events ("insulted me", "shared a meal", "used my bed") with value, duration and stack limit,
//   about another colonist (social) or about oneself (mood). Opinion of A about B = kind bonus + sum of social memories.
// - Friendship/rivalry form after the opinion stays above/below thresholds for N ticks and dissolve with hysteresis;
//   partnership by proposal, accepted when both opinions reach the threshold.
// - Item ownership (beds, chairs, tools): claims, and using an item owned by someone else creates a memory for the owner.
// - moodOf returns the sum of self memories plus relation-driven bonuses for the mood system of Colony.h.
// Integer math, ascending ids; record kind 210.
#include "PolicyCommon.h"
namespace aiverse::lib::mechanics {
enum RelationBits:uint32_t {RelParent=1,RelChild=2,RelSibling=4,RelPartner=8,RelFriend=16,RelRival=32};
struct MemoryDef {int64_t id=0,value=0;uint32_t durationTicks=6000,stackLimit=3;bool social=true;};
struct RelationConfig {int64_t familyBonus=20,partnerBonus=30,friendThreshold=40,rivalThreshold=-40,hysteresis=10,partnerThreshold=60,friendMood=5,rivalMood=-5,partnerMood=10,
    ownedItemUsedMemory=0;uint32_t formTicks=3000;};
struct Memory {int64_t def=0,about=0;uint32_t expires=0;};
struct Relation {uint32_t bits=0,aboveSince=0,belowSince=0;bool above=false,below=false;};
struct SocialState {std::map<int64_t,std::map<int64_t,Relation>> relations;std::map<int64_t,std::vector<Memory>> memories;std::map<int64_t,int64_t> owners;   // item -> colonist
    std::set<int64_t> colonists;uint32_t lastTick=0;bool initialized=false;};
enum class SocialCommandKind:uint8_t {AddColonist,RemoveColonist,Family,Remember,Propose,BreakUp,Claim,Release,UseItem};
struct SocialCommand {SocialCommandKind kind{};int64_t a=0,b=0,value=0;uint32_t bits=0;};   // Remember: a remembers memory `value` about b (b 0 = self)
enum class SocialEventKind:uint8_t {BecameFriends,BecameRivals,FriendshipEnded,RivalryEnded,Partnered,ProposalRejected,BrokeUp,Claimed,ItemUsedByOther};
struct SocialEvent {SocialEventKind kind{};int64_t a=0,b=0,value=0;};
using MemoryDefs=std::map<int64_t,MemoryDef>;

inline bool valid(const MemoryDefs& d){for(auto& [id,m]:d)if(id!=m.id||id<=0||m.stackLimit==0||m.durationTicks==0||m.value< -1000||m.value>1000)return false;return true;}
inline uint32_t mirror(uint32_t bits){uint32_t m=bits&(RelSibling|RelPartner|RelFriend|RelRival);if(bits&RelParent)m|=RelChild;if(bits&RelChild)m|=RelParent;return m;}
inline int64_t opinion(const RelationConfig& c,const MemoryDefs& d,const SocialState& s,int64_t a,int64_t b){
    int64_t v=0;auto r=s.relations.find(a);if(r!=s.relations.end()){auto x=r->second.find(b);if(x!=r->second.end()){
        if(x->second.bits&(RelParent|RelChild|RelSibling))v+=c.familyBonus;if(x->second.bits&RelPartner)v+=c.partnerBonus;}}
    auto m=s.memories.find(a);if(m!=s.memories.end())for(auto& x:m->second)if(x.about==b)v+=d.at(x.def).value;
    return std::clamp<int64_t>(v,-100,100);
}
inline int64_t moodOf(const RelationConfig& c,const MemoryDefs& d,const SocialState& s,int64_t a){
    int64_t v=0;auto m=s.memories.find(a);if(m!=s.memories.end())for(auto& x:m->second)if(x.about==0)v+=d.at(x.def).value;
    auto r=s.relations.find(a);if(r!=s.relations.end())for(auto& [b,rel]:r->second){if(rel.bits&RelFriend)v+=c.friendMood;if(rel.bits&RelRival)v+=c.rivalMood;if(rel.bits&RelPartner)v+=c.partnerMood;}
    return v;
}
inline std::optional<std::vector<SocialEvent>> stepSocial(const RelationConfig& c,const MemoryDefs& defs,SocialState& state,uint32_t tick,std::span<const SocialCommand> commands){
    if(!valid(defs)||!policyTickOk(state.initialized,state.lastTick,tick)||commands.size()>kPolicyMaxCommands||c.formTicks==0)return std::nullopt;
    auto s=state;s.initialized=true;s.lastTick=tick;std::vector<SocialEvent> ev;
    auto has=[&](int64_t x){return s.colonists.contains(x);};
    auto setBits=[&](int64_t a,int64_t b,uint32_t on,uint32_t off){auto& x=s.relations[a][b];x.bits=(x.bits&~off)|on;auto& y=s.relations[b][a];y.bits=(y.bits&~mirror(off))|mirror(on);};
    auto remember=[&](int64_t a,int64_t about,int64_t def){auto& list=s.memories[a];const auto& d=defs.at(def);
        uint32_t n=0;for(auto& m:list)n+=m.def==def&&m.about==about;
        if(n>=d.stackLimit){for(auto& m:list)if(m.def==def&&m.about==about){m.expires=tick+d.durationTicks;break;}}else list.push_back({def,about,tick+d.durationTicks});};
    for(auto& cmd:commands){
        switch(cmd.kind){
        case SocialCommandKind::AddColonist:if(cmd.a<=0)return std::nullopt;s.colonists.insert(cmd.a);break;
        case SocialCommandKind::RemoveColonist:{s.colonists.erase(cmd.a);s.relations.erase(cmd.a);s.memories.erase(cmd.a);
            for(auto& [x,m]:s.relations)m.erase(cmd.a);for(auto& [x,l]:s.memories)std::erase_if(l,[&](auto& mm){return mm.about==cmd.a;});
            std::erase_if(s.owners,[&](auto& o){return o.second==cmd.a;});break;}
        case SocialCommandKind::Family:if(!has(cmd.a)||!has(cmd.b)||cmd.a==cmd.b||(cmd.bits&~uint32_t(RelParent|RelChild|RelSibling)))return std::nullopt;setBits(cmd.a,cmd.b,cmd.bits,0);break;
        case SocialCommandKind::Remember:if(!has(cmd.a)||(cmd.b&&!has(cmd.b))||!defs.contains(cmd.value))return std::nullopt;remember(cmd.a,cmd.b,cmd.value);break;
        case SocialCommandKind::Propose:{if(!has(cmd.a)||!has(cmd.b)||cmd.a==cmd.b)return std::nullopt;
            bool free=true;for(auto x:{cmd.a,cmd.b})for(auto& [o,r]:s.relations[x])if(o!=cmd.a&&o!=cmd.b&&(r.bits&RelPartner))free=false;
            if(free&&opinion(c,defs,s,cmd.a,cmd.b)>=c.partnerThreshold&&opinion(c,defs,s,cmd.b,cmd.a)>=c.partnerThreshold){setBits(cmd.a,cmd.b,RelPartner,0);ev.push_back({SocialEventKind::Partnered,cmd.a,cmd.b,0});}
            else ev.push_back({SocialEventKind::ProposalRejected,cmd.a,cmd.b,0});break;}
        case SocialCommandKind::BreakUp:if(s.relations[cmd.a][cmd.b].bits&RelPartner){setBits(cmd.a,cmd.b,0,RelPartner);ev.push_back({SocialEventKind::BrokeUp,cmd.a,cmd.b,0});}break;
        case SocialCommandKind::Claim:if(!has(cmd.a)||cmd.value<=0)return std::nullopt;s.owners[cmd.value]=cmd.a;ev.push_back({SocialEventKind::Claimed,cmd.a,0,cmd.value});break;
        case SocialCommandKind::Release:if(auto it=s.owners.find(cmd.value);it!=s.owners.end()&&it->second==cmd.a)s.owners.erase(it);break;
        case SocialCommandKind::UseItem:{if(!has(cmd.a))return std::nullopt;auto it=s.owners.find(cmd.value);
            if(it!=s.owners.end()&&it->second!=cmd.a&&has(it->second)){ev.push_back({SocialEventKind::ItemUsedByOther,it->second,cmd.a,cmd.value});
                if(c.ownedItemUsedMemory&&defs.contains(c.ownedItemUsedMemory))remember(it->second,cmd.a,c.ownedItemUsedMemory);}break;}
        default:return std::nullopt;}
    }
    for(auto& [a,list]:s.memories)std::erase_if(list,[&](auto& m){return m.expires<=tick;});std::erase_if(s.memories,[](auto& e){return e.second.empty();});
    // Friendship / rivalry formation with hysteresis (evaluated for ordered pairs a<b using both opinions' minimum / maximum).
    for(auto a:s.colonists)for(auto b:s.colonists){if(b<=a)continue;auto oa=opinion(c,defs,s,a,b),ob=opinion(c,defs,s,b,a);auto& r=s.relations[a][b];
        bool up=std::min(oa,ob)>=c.friendThreshold,down=std::max(oa,ob)<=c.rivalThreshold;
        if(up&&!r.above){r.above=true;r.aboveSince=tick;}else if(!up)r.above=false;
        if(down&&!r.below){r.below=true;r.belowSince=tick;}else if(!down)r.below=false;
        if(r.above&&!(r.bits&RelFriend)&&tick-r.aboveSince>=c.formTicks){setBits(a,b,RelFriend,RelRival);ev.push_back({SocialEventKind::BecameFriends,a,b,0});}
        if(r.below&&!(r.bits&RelRival)&&tick-r.belowSince>=c.formTicks){setBits(a,b,RelRival,RelFriend);ev.push_back({SocialEventKind::BecameRivals,a,b,0});}
        auto& now=s.relations[a][b];
        if((now.bits&RelFriend)&&std::min(oa,ob)<c.friendThreshold-c.hysteresis){setBits(a,b,0,RelFriend);ev.push_back({SocialEventKind::FriendshipEnded,a,b,0});}
        if((now.bits&RelRival)&&std::max(oa,ob)>c.rivalThreshold+c.hysteresis){setBits(a,b,0,RelRival);ev.push_back({SocialEventKind::RivalryEnded,a,b,0});}}
    for(auto& [a,m]:s.relations)std::erase_if(m,[](auto& e){return !e.second.bits&&!e.second.above&&!e.second.below;});std::erase_if(s.relations,[](auto& e){return e.second.empty();});
    state=std::move(s);return ev;
}
inline void io(Archive& a,Memory& m){a(m.def,m.about,m.expires);}
inline void io(Archive& a,Relation& r){a(r.bits,r.aboveSince,r.belowSince,r.above,r.below);}
inline void io(Archive& a,SocialState& s){std::vector<int64_t> c(s.colonists.begin(),s.colonists.end());a(s.relations,s.memories,s.owners,c,s.lastTick,s.initialized);s.colonists=std::set<int64_t>(c.begin(),c.end());}
inline uint16_t stateKind(const SocialState&){return 210;}
inline bool valid(const SocialState& s){if(!s.colonists.empty()&&*s.colonists.begin()<=0)return false;for(auto& [i,o]:s.owners)if(i<=0||!s.colonists.contains(o))return false;
    for(auto& [a,m]:s.relations){if(!s.colonists.contains(a))return false;for(auto& [b,r]:m)if(!s.colonists.contains(b)||a==b)return false;}return true;}
}
