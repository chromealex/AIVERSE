#pragma once
// Turn planner bot for tactics-grid. It plans for the active unit from the team's TacticsView only (own units and
// enemies currently in sight): destinations come from a reachable set that treats only seen units as obstacles,
// each destination is scored on the expected damage it enables (hit chance x damage, flanking, kill bonus), the
// threat from seen enemies (their hit chance on that cell), cover there and advance toward an owner-supplied cell
// (an objective, never a live enemy position) when no enemy is seen; overwatch is valued only when a seen enemy
// could walk into range. Evaluations are bounded by searchBudget and visited in (move cost, cell) order; ties keep
// the earlier candidate, so the plan is deterministic and consumes no RNG. It returns one TacticsAction; call it
// again after every accepted action. A move into a cell held by an unseen enemy is rejected by stepTactics: the
// owner then submits EndTurn (never retries with hidden knowledge).
#include "../genre/TacticsTurns.h"
#include "../gameplay/Configuration.h"
#include <optional>
namespace aiverse::lib::bots {
struct TacticsBotConfig{
    uint32_t searchBudget=512;                       // destination x enemy evaluations per decision
    int32_t damageWeight=100,killBonus=300,threatWeight=60,coverWeight=40,advanceWeight=10,overwatchValue=60;
    bool operator==(const TacticsBotConfig&)const=default;
};
inline bool valid(const TacticsBotConfig& c){return c.searchBudget>=1&&c.searchBudget<=100000&&c.damageWeight>=0&&c.killBonus>=0&&c.threatWeight>=0&&c.coverWeight>=0&&c.advanceWeight>=0&&c.overwatchValue>=0;}
inline TacticsBotConfig tacticsBotConfig(const gameplay::GameplaySettings& s){TacticsBotConfig c;c.searchBudget=uint32_t(s.tactics.botSearchBudget);return c;}
struct TacticsBotChoice{genre::TacticsAction action;int64_t score=0;uint32_t evaluated=0;};

inline std::optional<TacticsBotChoice> tacticsBotChoose(const genre::TacticsScenario& sc,const genre::TacticsView& view,int32_t advanceTo=-1,const TacticsBotConfig& cfg=TacticsBotConfig{}){
    using namespace genre;
    if(!valid(cfg)||!valid(sc)||!view.active||view.winner!=kTacticsNoWinner)return std::nullopt;
    const TacticsUnitView* me=nullptr;for(const auto& u:view.units)if(u.id==view.active)me=&u;
    if(!me||me->team!=view.team||me->hp<=0||!sc.defs.contains(me->def))return std::nullopt;
    const auto& d=sc.defs.at(me->def);const auto& g=sc.grid;
    if(!me->ap)return TacticsBotChoice{{},0,0};
    std::vector<const TacticsUnitView*> enemies;std::vector<uint8_t> occ(g.cells.size(),kTacticsFree);
    for(const auto& u:view.units){if(u.hp<=0||!tacticsInside(g,u.cell)||!sc.defs.contains(u.def))continue;
        occ[std::size_t(u.cell)]=u.team==view.team?kTacticsPassOnly:kTacticsBlocked;if(u.team!=view.team)enemies.push_back(&u);}
    auto per=d.mobility*kTacticsStep;
    auto reach=tacticsReachable(g,me->cell,me->ap*per,occ);if(!reach)return std::nullopt;
    std::vector<std::pair<int32_t,int32_t>> cand{{0,me->cell}};
    for(int32_t c=0;c<tacticsCellCount(g);++c)if(reach->canStop(c,occ))cand.push_back({reach->cost[std::size_t(c)],c});
    std::sort(cand.begin()+1,cand.end());
    uint32_t used=0;TacticsBotChoice best{{},INT64_MIN,0};
    for(auto [cost,cell]:cand){
        if(used>=cfg.searchBudget)break;
        int32_t apCost=cell==me->cell?0:(per?std::max(1,(cost+per-1)/per):me->ap);int32_t apLeft=me->ap-apCost;
        int64_t defense=0,offense=INT64_MIN;TacticsAction act{};
        int32_t nearest=INT32_MAX;bool contact=false; // some seen enemy could walk into this unit's range next turn
        for(const auto* e:enemies){
            ++used;const auto& ed=sc.defs.at(e->def);
            auto threat=tacticsPreviewShot(g,sc.rules.fire,e->cell,cell,tacticsWeapon(ed));
            if(tacticsCanSee(g,e->cell,cell,ed.sight))defense-=int64_t(threat.hitMilli)*ed.damage*cfg.threatWeight/1000;
            defense+=int64_t(uint8_t(tacticsCoverAgainst(g,cell,e->cell,sc.rules.fire.coverCosMilli).cover))*cfg.coverWeight;
            nearest=std::min(nearest,tacticsDistance(g,cell,e->cell));if(tacticsDistance(g,cell,e->cell)<=d.maxRange+ed.mobility*ed.ap)contact=true;
            if(apLeft<1||!tacticsCanSee(g,cell,e->cell,d.sight))continue;
            auto shot=tacticsPreviewShot(g,sc.rules.fire,cell,e->cell,tacticsWeapon(d));if(!shot.hitMilli)continue;
            auto dmg=d.damage+(shot.cover.flanked?d.flankDamage:0);
            int64_t v=int64_t(shot.hitMilli)*std::min(dmg,e->hp)*cfg.damageWeight/1000+(dmg>=e->hp?int64_t(shot.hitMilli)*cfg.killBonus/1000:0);
            if(v>offense){offense=v;act={TacticsActionKind::Attack,-1,e->id};}
        }
        if(enemies.empty()){
            ++used;if(tacticsInside(g,advanceTo))defense-=int64_t(tacticsDistance(g,cell,advanceTo))*cfg.advanceWeight;
            int32_t shelter=0;for(auto n:tacticsNeighbors(g,cell,true))shelter=std::max(shelter,int32_t(uint8_t(g.cells[std::size_t(n)].cover)));
            defense+=int64_t(shelter)*cfg.coverWeight/2; // unseen enemies: prefer stopping next to cover
        }
        else defense-=int64_t(nearest)*cfg.advanceWeight/2;
        if(apLeft>=1&&contact&&cfg.overwatchValue>offense){offense=cfg.overwatchValue;act={TacticsActionKind::Overwatch,-1,0};}
        auto total=defense+std::max<int64_t>(offense,0);
        if(total>best.score){best.score=total;best.action=cell==me->cell?(offense>0?act:TacticsAction{}):TacticsAction{TacticsActionKind::Move,cell,0};}
    }
    best.evaluated=used;if(best.score==INT64_MIN)best.score=0;
    return best;
}
}
