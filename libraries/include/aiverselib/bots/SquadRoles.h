#pragma once
// Squad/team roles (genre-bots): deterministic role assignment (attacker/defender/support/flanker) from the
// objective stance and team composition with stable tie-breaks and hysteresis, plus simple formation slots.
// Orders are goals for each member's own controller/navigation (melee or shooter bot, RTS move command, ...);
// the squad policy never moves anybody itself.
#include "ObjectivePolicy.h"
#include "engine/core/Hash.h"
#include <array>
#include <tuple>
namespace aiverse::lib::bots {
enum class SquadRole:uint8_t {Attacker,Defender,Support,Flanker};
inline constexpr std::size_t kSquadRoles=4,kSquadMaxMembers=256;
inline constexpr uint8_t roleBit(SquadRole r){return uint8_t(1u<<uint8_t(r));}
inline constexpr uint8_t kAllRoles=0xF;
enum class FormationShape:uint8_t {Line,Wedge,Column,Ring};
struct SquadConfig {
    std::array<int64_t,kSquadRoles> offense{450,150,200,200},defense{200,500,200,100}; // role weights by SquadRole index
    uint32_t intervalTicks=60;           // periodic reassignment; composition changes reassign immediately
    Fixed spacing=2_fx,supportDistance=6_fx,flankDistance=10_fx,defendRadius=4_fx;
    FormationShape attackShape=FormationShape::Wedge;
};
// roles: bitmask of roles this member may take (roleBit); members without a capable candidate fill remaining slots.
struct SquadMember {int64_t id=0;Vec3 position{};uint8_t roles=kAllRoles;bool alive=true;};
struct RoleSlot {int64_t id=0;SquadRole role=SquadRole::Attacker;};
// Persisted squad memory (genre::save record kind 58). roles sorted by member id; heading = unit X/Z towards the objective at assignment.
struct SquadState {uint64_t composition=0;uint32_t lastAssign=0,lastTick=0;Vec2 heading{};bool initialized=false;std::vector<RoleSlot> roles;};
struct RoleOrder {int64_t id=0;SquadRole role=SquadRole::Attacker;uint32_t slot=0;Vec3 goal{};};
struct SquadPlan {bool reassigned=false;std::array<uint32_t,kSquadRoles> counts{};std::vector<RoleOrder> orders;}; // orders sorted by id

inline bool valid(const SquadConfig& c){
    int64_t offense=0,defense=0;
    for(std::size_t i=0;i<kSquadRoles;++i){if(c.offense[i]<0||c.offense[i]>1000000||c.defense[i]<0||c.defense[i]>1000000)return false;offense+=c.offense[i];defense+=c.defense[i];}
    return offense>0&&defense>0&&c.intervalTicks>=1&&c.intervalTicks<=36000&&c.spacing>Fixed{}&&c.spacing<=kBotSizeLimit&&c.supportDistance>=Fixed{}&&c.supportDistance<=kBotSizeLimit
        &&c.flankDistance>=Fixed{}&&c.flankDistance<=kBotSizeLimit&&c.defendRadius>=Fixed{}&&c.defendRadius<=kBotSizeLimit&&uint8_t(c.attackShape)<=uint8_t(FormationShape::Ring);
}
inline bool valid(const SquadState& s){
    if(s.roles.size()>kSquadMaxMembers||s.heading.x< -1_fx||s.heading.x>1_fx||s.heading.y< -1_fx||s.heading.y>1_fx)return false;
    int64_t previous=0;for(const auto& r:s.roles){if(r.id<=previous||uint8_t(r.role)>uint8_t(SquadRole::Flanker))return false;previous=r.id;}
    if(!s.initialized)return s.composition==0&&s.lastAssign==0&&s.lastTick==0&&s.heading==Vec2{}&&s.roles.empty();
    return s.lastAssign<=s.lastTick;
}
// Largest-remainder split of n members over the weights; leftovers go to the largest remainder, then lower role index.
inline std::optional<std::array<uint32_t,kSquadRoles>> roleCounts(const std::array<int64_t,kSquadRoles>& weights,uint32_t n){
    int64_t sum=0;for(auto w:weights){if(w<0||w>1000000)return std::nullopt;sum+=w;}
    if(sum<=0||n>kSquadMaxMembers)return std::nullopt;
    std::array<uint32_t,kSquadRoles> out{};std::array<int64_t,kSquadRoles> rest{};uint32_t used=0;
    for(std::size_t i=0;i<kSquadRoles;++i){out[i]=uint32_t(int64_t(n)*weights[i]/sum);rest[i]=weights[i]?int64_t(n)*weights[i]%sum:-1;used+=out[i];}
    while(used<n){std::size_t best=0;for(std::size_t i=1;i<kSquadRoles;++i)if(rest[i]>rest[best])best=i;++out[best];rest[best]=-1;++used;}
    return out;
}
// Local slot offsets (x = right, y = forward) for count members; deterministic, index 0 leads.
inline std::vector<Vec2> formationOffsets(FormationShape shape,uint32_t count,Fixed spacing){
    std::vector<Vec2> out;out.reserve(count);
    for(uint32_t i=0;i<count;++i){
        switch(shape){
        case FormationShape::Line:out.push_back({spacing*(int64_t(2*i)-int64_t(count-1))/2,{}});break;
        case FormationShape::Column:out.push_back({{},-(spacing*int64_t(i))});break;
        case FormationShape::Wedge:{int64_t k=(i+1)/2;Fixed side=i%2?-1_fx:1_fx;out.push_back({spacing*k*side,-(spacing*k)});break;}
        case FormationShape::Ring:{auto a=Fixed::fromRatio(i,count);out.push_back({fxSinTurns(a)*spacing,fxCosTurns(a)*spacing});break;}
        }
    }
    return out;
}
inline Vec3 formationGoal(Vec3 anchor,Vec2 forward,Vec2 offset){auto right=rightOf(forward);return {anchor.x+right.x*offset.x+forward.x*offset.y,anchor.y,anchor.z+right.y*offset.x+forward.y*offset.y};}

// Once per squad per tick. Reassigns on the first call, whenever the alive composition (ids, role masks, stance)
// changes, and every intervalTicks; otherwise keeps the stored roles so goals stay stable. On reassignment roles
// are filled in the order Support, Flanker, Defender, Attacker; each slot takes the unassigned member with the key
// (cannot take the role, keep = 0 same previous role / 1 new or from a role with surplus holders / 2 other,
// squared distance to the role anchor, id), so survivors keep their roles when the counts allow. nullopt: invalid
// input or repeated/backwards tick (state untouched).
inline std::optional<SquadPlan> stepSquad(const SquadConfig& c,SquadState& state,uint32_t tick,Vec3 objective,SquadStance stance,std::span<const SquadMember> members){
    if(!valid(c)||!valid(state)||uint8_t(stance)>uint8_t(SquadStance::Defense)||!botBounded(objective,kBotCoordLimit)||members.size()>kSquadMaxMembers
        ||(state.initialized&&tick<=state.lastTick))return std::nullopt;
    std::vector<const SquadMember*> alive;
    {std::vector<int64_t> ids;for(const auto& m:members){if(m.id<=0||!botBounded(m.position,kBotCoordLimit)||m.roles==0||m.roles>kAllRoles)return std::nullopt;ids.push_back(m.id);if(m.alive)alive.push_back(&m);}
     std::sort(ids.begin(),ids.end());if(std::adjacent_find(ids.begin(),ids.end())!=ids.end())return std::nullopt;}
    std::sort(alive.begin(),alive.end(),[](auto a,auto b){return a->id<b->id;});
    Hasher h;h.u32(uint32_t(stance));for(auto* m:alive){h.i64(m->id);h.u32(m->roles);}
    auto s=state;SquadPlan plan;
    plan.reassigned=!s.initialized||h.value()!=s.composition||tick-s.lastAssign>=c.intervalTicks;
    if(plan.reassigned){
        auto counts=roleCounts(stance==SquadStance::Offense?c.offense:c.defense,uint32_t(alive.size()));if(!counts)return std::nullopt;
        Vec3 centre{};for(auto* m:alive)centre+=m->position/Fixed::fromInt(int64_t(alive.size()));
        auto heading=planarUnit({objective.x-centre.x,objective.z-centre.z});if(heading==Vec2{})heading=s.heading==Vec2{}?Vec2{{},1_fx}:s.heading;
        auto right=rightOf(heading);
        std::array<Vec3,kSquadRoles> anchor{objective,objective,
            Vec3{objective.x-heading.x*c.supportDistance,objective.y,objective.z-heading.y*c.supportDistance},
            Vec3{objective.x+right.x*c.flankDistance,objective.y,objective.z+right.y*c.flankDistance}};
        std::vector<RoleSlot> roles;std::vector<bool> taken(alive.size(),false);
        auto previous=[&](int64_t id)->int{for(const auto& r:s.roles)if(r.id==id)return int(r.role);return -1;};
        auto remaining=*counts;
        for(auto role:{SquadRole::Support,SquadRole::Flanker,SquadRole::Defender,SquadRole::Attacker}){
            for(uint32_t k=0;k<(*counts)[uint8_t(role)];++k){
                // Surplus: more untaken holders of a previous role than that role still has slots.
                std::array<int64_t,kSquadRoles> holders{};
                for(std::size_t i=0;i<alive.size();++i)if(!taken[i])if(auto p=previous(alive[i]->id);p>=0)++holders[std::size_t(p)];
                std::size_t pick=alive.size();std::tuple<bool,int,Fixed,int64_t> best{};
                for(std::size_t i=0;i<alive.size();++i){
                    if(taken[i])continue;
                    const auto* m=alive[i];
                    auto p=previous(m->id);
                    int keep=p==int(role)?0:(p<0||holders[std::size_t(p)]>int64_t(remaining[std::size_t(p)]))?1:2;
                    std::tuple<bool,int,Fixed,int64_t> key{!(m->roles&roleBit(role)),keep,(m->position-anchor[uint8_t(role)]).lengthSq(),m->id};
                    if(pick==alive.size()||key<best){pick=i;best=key;}
                }
                taken[pick]=true;--remaining[uint8_t(role)];roles.push_back({alive[pick]->id,role});
            }
        }
        std::sort(roles.begin(),roles.end(),[](auto a,auto b){return a.id<b.id;});
        s.roles=std::move(roles);s.composition=h.value();s.lastAssign=tick;s.heading=heading;plan.counts=*counts;
    }else for(const auto& r:s.roles)++plan.counts[uint8_t(r.role)];
    auto right=rightOf(s.heading);
    std::array<Vec3,kSquadRoles> anchor{objective,objective,
        Vec3{objective.x-s.heading.x*c.supportDistance,objective.y,objective.z-s.heading.y*c.supportDistance},
        Vec3{objective.x+right.x*c.flankDistance,objective.y,objective.z+right.y*c.flankDistance}};
    const std::array<FormationShape,kSquadRoles> shapes{c.attackShape,FormationShape::Ring,FormationShape::Line,FormationShape::Column};
    for(std::size_t role=0;role<kSquadRoles;++role){
        auto spacing=role==uint8_t(SquadRole::Defender)?(c.defendRadius>Fixed{}?c.defendRadius:c.spacing):c.spacing;
        auto offsets=formationOffsets(shapes[role],plan.counts[role],spacing);uint32_t slot=0;
        for(const auto& r:s.roles)if(uint8_t(r.role)==role){plan.orders.push_back({r.id,r.role,slot,formationGoal(anchor[role],s.heading,offsets[slot])});++slot;}
    }
    std::sort(plan.orders.begin(),plan.orders.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    s.lastTick=tick;s.initialized=true;state=std::move(s);return plan;
}
}
