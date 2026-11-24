#pragma once
#include "engine/mechanics/Namespace.h"
// hero-match: rules of team hero matches (MOBA / hero shooters): draft, shop, recall, courier, kill credit, respawn.
// - Draft: a data sequence of (team, Pick|Ban) turns with a time limit per turn; a timeout picks or bans the lowest
//   available hero; no duplicates; the result is the picks of each team.
// - Shop: items with cost and recipe components; buying consumes owned components first, needs the hero inside a shop
//   zone (or a courier), sells back at a ratio; inventory slots limit.
// - Recall: channel for N ticks, cancelled by damage or movement; completion returns the hero to base.
// - Courier: carries bought items to a hero after a delivery delay (owner moves the courier body; the policy times it).
// - Kill credit: last hit gold/xp, denies of own units below a health share, assists for damage dealt within a window,
//   kill streak bounty, respawn timer from level.
// Integer math, ascending ids, strictly increasing ticks; record kinds 218 (DraftState) and 219 (HeroMatchState).
#include "PolicyCommon.h"
namespace aiverse::lib::mechanics {
// ---- Draft.
struct DraftTurn {int32_t team=0;bool ban=false;};
struct DraftRules {std::vector<DraftTurn> turns;uint32_t turnTicks=900;std::set<int64_t> pool;};
struct DraftState {std::size_t turn=0;uint32_t turnStart=0;std::map<int32_t,std::vector<int64_t>> picks;std::vector<int64_t> bans;uint32_t lastTick=0;bool done=false,initialized=false;};
struct DraftAction {int32_t team=0;int64_t hero=0;};
inline bool valid(const DraftRules& r){if(r.turns.empty()||r.turnTicks==0||r.pool.empty()||*r.pool.begin()<=0)return false;for(auto& t:r.turns)if(t.team<=0)return false;return true;}
inline bool draftTaken(const DraftState& s,int64_t hero){if(std::find(s.bans.begin(),s.bans.end(),hero)!=s.bans.end())return true;for(auto& [t,p]:s.picks)if(std::find(p.begin(),p.end(),hero)!=p.end())return true;return false;}
inline std::optional<std::vector<DraftAction>> stepDraft(const DraftRules& r,DraftState& state,uint32_t tick,std::span<const DraftAction> actions){
    if(!valid(r)||!policyTickOk(state.initialized,state.lastTick,tick))return std::nullopt;auto s=state;if(!s.initialized)s.turnStart=tick;s.initialized=true;s.lastTick=tick;
    std::vector<DraftAction> applied;
    auto take=[&](int64_t hero){auto& t=r.turns[s.turn];if(t.ban)s.bans.push_back(hero);else s.picks[t.team].push_back(hero);applied.push_back({t.team,hero});++s.turn;s.turnStart=tick;if(s.turn>=r.turns.size())s.done=true;};
    for(auto& a:actions){if(s.done)break;auto& t=r.turns[s.turn];if(a.team!=t.team||!r.pool.contains(a.hero)||draftTaken(s,a.hero))continue;take(a.hero);}
    while(!s.done&&tick-s.turnStart>=r.turnTicks){std::optional<int64_t> h;for(auto x:r.pool)if(!draftTaken(s,x)){h=x;break;}if(!h){s.done=true;break;}take(*h);}
    state=std::move(s);return applied;
}
inline void io(Archive& a,DraftState& s){uint64_t t=s.turn;a(t,s.turnStart,s.picks,s.bans,s.lastTick,s.done,s.initialized);s.turn=std::size_t(t);}
inline uint16_t stateKind(const DraftState&){return 218;}
inline bool valid(const DraftState& s){return s.turn<100000;}

// ---- Match economy and credit.
struct ShopItem {int64_t id=0,cost=0;std::vector<int64_t> components;bool sideShop=false;};
struct HeroMatchConfig {std::map<int64_t,ShopItem> items;int32_t slots=6;int64_t sellPercent=50,lastHitGold=40,denyGold=0,lastHitXp=40,denyXp=20,killGold=200,assistGold=100,
    streakBounty=50,denyHealthMilli=500;uint32_t assistWindow=600,recallTicks=480,courierTicks=300,respawnBase=200,respawnPerLevel=120;};
struct HeroRecord {int32_t team=0,level=1;int64_t gold=600,xp=0,kills=0,deaths=0,assists=0,lastHits=0,denies=0,streak=0;std::vector<int64_t> items;uint32_t recallStart=0,respawnAt=0;bool recalling=false,dead=false;};
struct Delivery {int64_t hero=0,item=0;uint32_t arrives=0;};
struct HeroMatchState {std::map<int64_t,HeroRecord> heroes;std::map<int64_t,std::vector<std::pair<int64_t,uint32_t>>> damageLog;   // victim -> (attacker, tick)
    std::vector<Delivery> deliveries;uint32_t lastTick=0;bool initialized=false;};
enum class HeroCommandKind:uint8_t {Buy,Sell,Recall,CancelRecall,Damage,UnitKilled,HeroKilled,Moved};
struct HeroCommand {HeroCommandKind kind{};int64_t hero=0,target=0,item=0;int32_t targetTeam=0;int64_t targetHealthMilli=1000;bool inShop=false,useCourier=false;};
enum class HeroEventKind:uint8_t {Bought,Sold,BuyRefused,RecallStarted,RecallDone,RecallCancelled,Delivered,LastHit,Denied,Kill,Assist,Died,Respawned};
struct HeroEvent {HeroEventKind kind{};int64_t hero=0,other=0,value=0;};
inline bool valid(const HeroMatchConfig& c){if(c.slots<1||c.slots>32||c.sellPercent<0||c.sellPercent>100)return false;
    for(auto& [id,i]:c.items){if(id!=i.id||id<=0||i.cost<0)return false;for(auto x:i.components)if(!c.items.contains(x)||x==id)return false;}return true;}
inline std::optional<std::vector<HeroEvent>> stepHeroMatch(const HeroMatchConfig& c,HeroMatchState& state,uint32_t tick,std::span<const HeroCommand> commands){
    if(!valid(c)||!policyTickOk(state.initialized,state.lastTick,tick)||commands.size()>kPolicyMaxCommands)return std::nullopt;
    for(auto& cmd:commands)if(!state.heroes.contains(cmd.hero))return std::nullopt;
    auto s=state;s.initialized=true;s.lastTick=tick;std::vector<HeroEvent> ev;
    auto give=[&](int64_t hero,int64_t gold,int64_t xp){auto& h=s.heroes.at(hero);h.gold+=gold;h.xp+=xp;while(h.xp>=int64_t(h.level)*200&&h.level<30){h.xp-=int64_t(h.level)*200;++h.level;}};
    for(auto& cmd:commands){auto& h=s.heroes.at(cmd.hero);
        switch(cmd.kind){
        case HeroCommandKind::Buy:{auto it=c.items.find(cmd.item);if(it==c.items.end()||h.dead||(!cmd.inShop&&!cmd.useCourier)){ev.push_back({HeroEventKind::BuyRefused,cmd.hero,cmd.item,0});break;}
            // pay with owned components first
            auto owned=h.items;int64_t price=it->second.cost;std::vector<int64_t> used;
            for(auto comp:it->second.components){auto f=std::find(owned.begin(),owned.end(),comp);if(f!=owned.end()){owned.erase(f);used.push_back(comp);}else price+=c.items.at(comp).cost;}
            if(price>h.gold||(int32_t(owned.size())>=c.slots&&!cmd.useCourier)){ev.push_back({HeroEventKind::BuyRefused,cmd.hero,cmd.item,price});break;}
            h.gold-=price;if(cmd.useCourier&&!cmd.inShop){h.items=owned;s.deliveries.push_back({cmd.hero,cmd.item,tick+c.courierTicks});}else{owned.push_back(cmd.item);h.items=owned;}
            ev.push_back({HeroEventKind::Bought,cmd.hero,cmd.item,price});break;}
        case HeroCommandKind::Sell:{auto f=std::find(h.items.begin(),h.items.end(),cmd.item);if(f==h.items.end()||!cmd.inShop)break;h.items.erase(f);
            auto value=c.items.at(cmd.item).cost;for(auto comp:c.items.at(cmd.item).components)value+=c.items.at(comp).cost;auto back=value*c.sellPercent/100;h.gold+=back;ev.push_back({HeroEventKind::Sold,cmd.hero,cmd.item,back});break;}
        case HeroCommandKind::Recall:if(!h.dead&&!h.recalling){h.recalling=true;h.recallStart=tick;ev.push_back({HeroEventKind::RecallStarted,cmd.hero,0,0});}break;
        case HeroCommandKind::CancelRecall:case HeroCommandKind::Moved:if(h.recalling){h.recalling=false;ev.push_back({HeroEventKind::RecallCancelled,cmd.hero,0,0});}break;
        case HeroCommandKind::Damage:{if(h.recalling){h.recalling=false;ev.push_back({HeroEventKind::RecallCancelled,cmd.hero,0,0});}   // hero dealt damage to target (hero or unit)
            if(s.heroes.contains(cmd.target)){auto& v=s.heroes.at(cmd.target);if(v.recalling){v.recalling=false;ev.push_back({HeroEventKind::RecallCancelled,cmd.target,0,0});}}
            s.damageLog[cmd.target].push_back({cmd.hero,tick});break;}
        case HeroCommandKind::UnitKilled:{bool own=cmd.targetTeam==h.team;
            if(own){if(cmd.targetHealthMilli<=c.denyHealthMilli){++h.denies;give(cmd.hero,c.denyGold,c.denyXp);ev.push_back({HeroEventKind::Denied,cmd.hero,cmd.target,c.denyGold});}}
            else{++h.lastHits;give(cmd.hero,c.lastHitGold,c.lastHitXp);ev.push_back({HeroEventKind::LastHit,cmd.hero,cmd.target,c.lastHitGold});}
            s.damageLog.erase(cmd.target);break;}
        case HeroCommandKind::HeroKilled:{auto vit=s.heroes.find(cmd.target);if(vit==s.heroes.end()||vit->second.dead||vit->second.team==h.team)break;auto& v=vit->second;
            auto bounty=c.killGold+c.streakBounty*v.streak;++h.kills;++h.streak;give(cmd.hero,bounty,int64_t(v.level)*100);ev.push_back({HeroEventKind::Kill,cmd.hero,cmd.target,bounty});
            std::set<int64_t> helpers;for(auto& [att,t]:s.damageLog[cmd.target])if(att!=cmd.hero&&tick-t<=c.assistWindow&&s.heroes.contains(att)&&s.heroes.at(att).team==h.team)helpers.insert(att);
            for(auto x:helpers){++s.heroes.at(x).assists;give(x,c.assistGold,int64_t(v.level)*50);ev.push_back({HeroEventKind::Assist,x,cmd.target,c.assistGold});}
            v.dead=true;v.recalling=false;++v.deaths;v.streak=0;v.respawnAt=tick+c.respawnBase+c.respawnPerLevel*uint32_t(v.level);ev.push_back({HeroEventKind::Died,cmd.target,cmd.hero,v.respawnAt});
            s.damageLog.erase(cmd.target);break;}
        default:return std::nullopt;}
    }
    for(auto& [id,h]:s.heroes){if(h.recalling&&tick-h.recallStart>=c.recallTicks){h.recalling=false;ev.push_back({HeroEventKind::RecallDone,id,0,0});}
        if(h.dead&&tick>=h.respawnAt){h.dead=false;ev.push_back({HeroEventKind::Respawned,id,0,0});}}
    std::vector<Delivery> pending;for(auto& d:s.deliveries){if(tick>=d.arrives){auto& h=s.heroes.at(d.hero);if(int32_t(h.items.size())<c.slots){h.items.push_back(d.item);ev.push_back({HeroEventKind::Delivered,d.hero,d.item,0});}else pending.push_back(d);}else pending.push_back(d);}
    s.deliveries=std::move(pending);
    for(auto& [v,log]:s.damageLog)std::erase_if(log,[&](auto& e){return tick-e.second>c.assistWindow;});std::erase_if(s.damageLog,[](auto& e){return e.second.empty();});
    state=std::move(s);return ev;
}
inline void io(Archive& a,HeroRecord& h){a(h.team,h.level,h.gold,h.xp,h.kills,h.deaths,h.assists,h.lastHits,h.denies,h.streak,h.items,h.recallStart,h.respawnAt,h.recalling,h.dead);}
inline void io(Archive& a,Delivery& d){a(d.hero,d.item,d.arrives);}
inline void io(Archive& a,HeroMatchState& s){a(s.heroes,s.damageLog,s.deliveries,s.lastTick,s.initialized);}
inline uint16_t stateKind(const HeroMatchState&){return 219;}
inline bool valid(const HeroMatchState& s){if(!policyPositiveKeys(s.heroes))return false;for(auto& d:s.deliveries)if(!s.heroes.contains(d.hero))return false;return true;}
}
