#pragma once
// Objective policy hook (genre-bots): pick which objective a bot or squad works on from an owner-supplied list.
// ObjectiveInfo is deliberately small so a match/objective owner (capture points, hold zones, payloads, ...)
// can fill it from its own authoritative state; the policy only scores and chooses. Integer scores, stable
// tie-break by smaller id, optional stickiness for the current choice to avoid flip-flopping.
#include "GenreCommon.h"
namespace aiverse::lib::bots {
enum class ObjectiveKind:uint8_t {Capture,Hold,Escort,Attack,Defend};
enum class SquadStance:uint8_t {Offense,Defense};
struct ObjectiveInfo {
    int64_t id=0;                       // stable positive id
    ObjectiveKind kind=ObjectiveKind::Capture;
    Vec3 position{};                    // point / zone centre / current payload position
    int64_t owner=0;                    // controlling team (0 neutral); for Escort the team whose payload it is
    int64_t valueMilli=1000;            // owner priority 0..1000000 (e.g. last point, final checkpoint)
    int64_t progressMilli=0;            // this team's progress 0..1000 (capture progress, payload distance)
    uint32_t allies=0,enemies=0;        // visible/authorized presence counts near the objective
    bool active=true;                   // inactive objectives (locked, finished) are never chosen
};
// Score points; distance cost per metre. ownedIdle is usually negative (an uncontested own point needs nobody).
struct ObjectiveWeights {int64_t value=1000,distancePerMetre=20,neutral=200,enemyOwned=300,progress=300,contested=300,threatened=400,ownedIdle=-500,escort=200,stickiness=150;};
struct ObjectiveChoice {int64_t id=0,score=0;SquadStance stance=SquadStance::Offense;};
inline bool valid(const ObjectiveWeights& w){
    for(auto v:{w.value,w.distancePerMetre,w.neutral,w.enemyOwned,w.progress,w.contested,w.threatened,w.ownedIdle,w.escort,w.stickiness})if(v< -1000000||v>1000000)return false;
    return w.distancePerMetre>=0;
}
inline bool valid(const ObjectiveInfo& o){
    return o.id>0&&uint8_t(o.kind)<=uint8_t(ObjectiveKind::Defend)&&botBounded(o.position,kBotCoordLimit)&&o.owner>=0&&o.valueMilli>=0&&o.valueMilli<=1000000
        &&o.progressMilli>=0&&o.progressMilli<=1000&&o.allies<=100000&&o.enemies<=100000;
}
// Defense: protect what this team holds (own capture/hold point, own payload) or an explicit Defend objective.
inline SquadStance objectiveStance(const ObjectiveInfo& o,int64_t team){
    if(o.kind==ObjectiveKind::Defend)return SquadStance::Defense;
    if(o.owner==team&&o.kind!=ObjectiveKind::Attack)return SquadStance::Defense;
    return SquadStance::Offense;
}
inline int64_t objectiveScore(const ObjectiveWeights& w,int64_t team,Vec3 from,const ObjectiveInfo& o,int64_t current){
    bool ours=o.owner==team;int64_t s=o.valueMilli*w.value/1000-((o.position-from).length()*w.distancePerMetre).toInt();
    switch(o.kind){
    case ObjectiveKind::Capture:case ObjectiveKind::Hold:case ObjectiveKind::Attack:
        if(ours)s+=o.enemies?w.threatened:w.ownedIdle;else s+=(o.owner==0?w.neutral:w.enemyOwned)+o.progressMilli*w.progress/1000;break;
    case ObjectiveKind::Defend:s+=o.enemies?w.threatened:w.ownedIdle;break;
    case ObjectiveKind::Escort:s+=w.escort+(ours?o.progressMilli*w.progress/1000:0);break;
    }
    if(o.allies&&o.enemies)s+=w.contested;
    if(o.id==current)s+=w.stickiness;
    return s;
}
// nullopt: invalid weights/team/objectives (duplicate ids, out-of-range values). id 0: nothing active.
inline std::optional<ObjectiveChoice> chooseObjective(const ObjectiveWeights& w,int64_t team,Vec3 from,std::span<const ObjectiveInfo> objectives,int64_t current=0){
    if(!valid(w)||team<=0||current<0||!botBounded(from,kBotCoordLimit)||objectives.size()>kBotMaxTargets)return std::nullopt;
    std::vector<int64_t> ids;ids.reserve(objectives.size());
    for(const auto& o:objectives){if(!valid(o))return std::nullopt;ids.push_back(o.id);}
    std::sort(ids.begin(),ids.end());if(std::adjacent_find(ids.begin(),ids.end())!=ids.end())return std::nullopt;
    ObjectiveChoice best;
    for(const auto& o:objectives){
        if(!o.active)continue;auto score=objectiveScore(w,team,from,o,current);
        if(!best.id||score>best.score||(score==best.score&&o.id<best.id))best={o.id,score,objectiveStance(o,team)};
    }
    return best;
}
}
