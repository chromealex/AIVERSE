#pragma once
#include "engine/mechanics/Namespace.h"
// herds-taming: group behaviour and taming for animals driven by survival-ecology (EcologyAnimals.h decides the state
// of single animals; this package adds the group and the bond with players).
// - Herds: a leader that wanders between goals around a home, members that keep cohesion around the leader with spacing,
//   panic that spreads to the whole herd when one member is alarmed (flee directly away from the threat), leader
//   succession on death (lowest id), and off-screen simulation: far herds move their home along seeded legs and grow or
//   shrink by birth/death rates without simulating members.
// - Taming: trust per (animal, player) raised by feeding preferred food, lost on scares and over time; reaching the
//   threshold makes the animal tamed by that player; tamed animals take Follow / Stay / Guard orders.
// Outputs are desired directions and speeds (length <= 1) plus events; the owner steers bodies (navmesh, flow field).
// Fixed math; record kind 209 (HerdState).
#include "PolicyCommon.h"
namespace aiverse::lib::mechanics {
struct HerdConfig {Fixed cohesionRadius=8_fx,spacing=2_fx,wanderRadius=30_fx,arriveRadius=3_fx,fleeDistance=40_fx;uint32_t panicTicks=300,wanderPauseTicks=240;
    int64_t birthPerDayMicro=20000,deathPerDayMicro=10000;uint32_t ticksPerDay=72000;std::size_t maxMembers=64;};
struct Herd {int64_t id=0,species=0,leader=0;Vec3 home{},goal{};std::vector<int64_t> members;uint32_t panicUntil=0,pauseUntil=0;Vec3 threat{};int64_t offscreenCount=0;bool offscreen=false;};
struct TameBond {int64_t trust=0,owner=0;uint32_t lastFed=0;uint8_t order=0;bool tamed=false;};   // order: 0 follow, 1 stay, 2 guard
struct HerdState {std::map<int64_t,Herd> herds;std::map<int64_t,std::map<int64_t,TameBond>> bonds;uint32_t lastTick=0;uint64_t seed=1;bool initialized=false;};   // animal -> player -> bond
struct MemberFact {int64_t id=0;Vec3 position{};bool alive=true,alarmed=false;Vec3 threat{};};
struct HerdSteer {int64_t animal=0;Vec3 direction{};Fixed speed{};bool fleeing=false;};
enum class HerdEventKind:uint8_t {Panicked,Calmed,LeaderChanged,Born,Died,Fed,Scared,Tamed,OrderChanged};
struct HerdEvent {HerdEventKind kind{};int64_t herd=0,animal=0,player=0,value=0;};   // value: births/deaths of an off-screen herd
struct TamingDef {int64_t species=0,preferredFood=0,trustPerFeed=200,otherFoodTrust=50,scareLoss=300,decayPerDay=100,threshold=1000;uint32_t feedCooldownTicks=600;};
enum class TameCommandKind:uint8_t {Feed,Scare,Order};
struct TameCommand {TameCommandKind kind{};int64_t animal=0,player=0,food=0;uint8_t order=0;};
struct HerdReport {std::vector<HerdSteer> steer;std::vector<HerdEvent> events;};

inline bool valid(const HerdConfig& c){return c.cohesionRadius>Fixed{}&&c.spacing>Fixed{}&&c.wanderRadius>Fixed{}&&c.arriveRadius>Fixed{}&&c.fleeDistance>Fixed{}&&c.ticksPerDay>0&&c.maxMembers>0
    &&c.birthPerDayMicro>=0&&c.deathPerDayMicro>=0;}
inline uint32_t herdRand(uint64_t seed,uint32_t tick,int64_t herd,uint32_t salt){Hasher h;h.u64(seed);h.u32(tick);h.i64(herd);h.u32(salt);return uint32_t(h.value());}
inline Vec3 herdGoal(const HerdConfig& c,uint64_t seed,uint32_t tick,const Herd& h){
    auto a=herdRand(seed,tick,h.id,1),b=herdRand(seed,tick,h.id,2);auto turns=Fixed::fromRaw(int64_t(a%65536));auto r=c.wanderRadius*Fixed::fromRaw(int64_t(b%65536));
    return h.home+Vec3{fxCosTurns(turns)*r,{},fxSinTurns(turns)*r};}
inline bool addHerd(HerdState& s,Herd h){if(h.id<=0||s.herds.contains(h.id)||!policyBounded(h.home))return false;std::sort(h.members.begin(),h.members.end());
    if(!policySortedUnique(h.members)||(!h.members.empty()&&h.members.front()<=0))return false;if(!h.leader&&!h.members.empty())h.leader=h.members.front();h.goal=h.home;s.herds.emplace(h.id,h);return true;}

// facts: every member of every on-screen herd this tick (ascending id). Members missing from facts are treated as dead.
inline std::optional<HerdReport> stepHerds(const HerdConfig& c,const std::map<int64_t,TamingDef>& taming,HerdState& state,uint32_t tick,std::span<const MemberFact> facts,
    std::span<const TameCommand> commands,std::span<const int64_t> offscreenHerds={}){
    if(!valid(c)||!policyTickOk(state.initialized,state.lastTick,tick)||facts.size()>kPolicyMaxObjects||commands.size()>kPolicyMaxCommands)return std::nullopt;
    std::map<int64_t,const MemberFact*> fact;for(auto& f:facts){if(f.id<=0||!policyBounded(f.position)||!fact.emplace(f.id,&f).second)return std::nullopt;}
    std::set<int64_t> far(offscreenHerds.begin(),offscreenHerds.end());for(auto id:far)if(!state.herds.contains(id))return std::nullopt;
    auto s=state;s.initialized=true;s.lastTick=tick;HerdReport r;auto& ev=r.events;
    for(auto& [hid,h]:s.herds){
        bool wasFar=h.offscreen;h.offscreen=far.contains(hid);
        if(h.offscreen){                                                            // aggregate simulation
            if(!wasFar){h.offscreenCount=int64_t(h.members.size());}
            if(tick%c.ticksPerDay==0){int64_t born=0,died=0;for(int64_t i=0;i<h.offscreenCount;++i){auto x=herdRand(s.seed,tick,hid,uint32_t(100+i))%1000000;
                if(int64_t(x)<c.birthPerDayMicro)++born;else if(int64_t(x)>=1000000-c.deathPerDayMicro)++died;}
                h.offscreenCount=std::clamp<int64_t>(h.offscreenCount+born-died,0,int64_t(c.maxMembers));if(born)ev.push_back({HerdEventKind::Born,hid,0,0,born});if(died)ev.push_back({HerdEventKind::Died,hid,0,0,died});}
            if(tick>=h.pauseUntil){h.home=h.home+(herdGoal(c,s.seed,tick,h)-h.home)*Fixed::fromRatio(1,8);h.pauseUntil=tick+c.wanderPauseTicks*8;}
            continue;}
        std::vector<int64_t> alive;for(auto m:h.members){auto f=fact.find(m);if(f!=fact.end()&&f->second->alive)alive.push_back(m);else ev.push_back({HerdEventKind::Died,hid,m,0});}
        h.members=alive;if(h.members.empty()){h.leader=0;continue;}
        if(!std::binary_search(h.members.begin(),h.members.end(),h.leader)){h.leader=h.members.front();ev.push_back({HerdEventKind::LeaderChanged,hid,h.leader,0});}
        for(auto m:h.members){auto* f=fact.at(m);if(f->alarmed){if(tick>=h.panicUntil)ev.push_back({HerdEventKind::Panicked,hid,m,0});h.panicUntil=tick+c.panicTicks;h.threat=f->threat;}}
        bool panic=tick<h.panicUntil;if(!panic&&h.panicUntil&&tick==h.panicUntil)ev.push_back({HerdEventKind::Calmed,hid,0,0});
        auto lead=fact.at(h.leader)->position;
        if(!panic&&tick>=h.pauseUntil&&(h.goal-lead).length()<=c.arriveRadius){h.goal=herdGoal(c,s.seed,tick,h);h.pauseUntil=tick+c.wanderPauseTicks;}
        for(std::size_t i=0;i<h.members.size();++i){auto m=h.members[i];auto p=fact.at(m)->position;HerdSteer st{m};
            if(panic){auto away=policyFlat(p-h.threat);if(away==Vec3{})away={1_fx,{},{}};st.direction=away;st.speed=1_fx;st.fleeing=true;}
            else if(m==h.leader){if(tick>=h.pauseUntil){st.direction=policyFlat(h.goal-p);st.speed=Fixed::fromRatio(1,2);}}
            else{auto to=lead-p;to.y={};auto dist=to.length();Vec3 dir{};if(dist>c.cohesionRadius)dir=to/dist;
                for(auto o:h.members)if(o!=m){auto d=p-fact.at(o)->position;d.y={};auto l=d.length();if(l<c.spacing)dir+=(l>Fixed{}?d/l:Vec3{Fixed::fromRatio(o<m?1:-1,1),{},{}})*((c.spacing-l)/c.spacing);}
                auto len=dir.length();if(len>1_fx)dir=dir/len;st.direction=dir;st.speed=dist>c.cohesionRadius?Fixed::fromRatio(3,5):Fixed::fromRatio(3,10);}
            r.steer.push_back(st);}
    }
    // Taming.
    for(auto& cmd:commands){if(cmd.animal<=0||cmd.player<=0)return std::nullopt;int64_t species=0;
        for(auto& [hid,h]:s.herds)if(std::binary_search(h.members.begin(),h.members.end(),cmd.animal))species=h.species;
        auto def=taming.find(species);if(def==taming.end())continue;auto& bond=s.bonds[cmd.animal][cmd.player];
        if(cmd.kind==TameCommandKind::Feed){if(bond.lastFed&&tick-bond.lastFed<def->second.feedCooldownTicks)continue;bond.lastFed=tick;
            bond.trust=std::min<int64_t>(def->second.threshold*2,bond.trust+(cmd.food==def->second.preferredFood?def->second.trustPerFeed:def->second.otherFoodTrust));ev.push_back({HerdEventKind::Fed,0,cmd.animal,cmd.player});
            bool anyOwner=false;for(auto& [p,b]:s.bonds[cmd.animal])anyOwner|=b.tamed;
            if(!anyOwner&&bond.trust>=def->second.threshold){bond.tamed=true;bond.owner=cmd.player;ev.push_back({HerdEventKind::Tamed,0,cmd.animal,cmd.player});}}
        else if(cmd.kind==TameCommandKind::Scare){bond.trust=std::max<int64_t>(0,bond.trust-def->second.scareLoss);ev.push_back({HerdEventKind::Scared,0,cmd.animal,cmd.player});}
        else if(cmd.kind==TameCommandKind::Order&&bond.tamed&&cmd.order<=2){bond.order=cmd.order;ev.push_back({HerdEventKind::OrderChanged,0,cmd.animal,cmd.player});}}
    if(tick%c.ticksPerDay==0)for(auto& [a,m]:s.bonds)for(auto& [p,b]:m)if(!b.tamed){int64_t decay=0;for(auto& [sp,t]:taming)decay=std::max(decay,t.decayPerDay);b.trust=std::max<int64_t>(0,b.trust-decay);}
    state=std::move(s);return r;
}
// Tamed animals with a Follow order leave their herd's steering to the owner: steer toward the player.
inline std::optional<int64_t> tamedOwner(const HerdState& s,int64_t animal){auto it=s.bonds.find(animal);if(it==s.bonds.end())return std::nullopt;for(auto& [p,b]:it->second)if(b.tamed)return p;return std::nullopt;}
inline void io(Archive& a,Herd& h){a(h.id,h.species,h.leader,h.home,h.goal,h.members,h.panicUntil,h.pauseUntil,h.threat,h.offscreenCount,h.offscreen);}
inline void io(Archive& a,TameBond& b){a(b.trust,b.owner,b.lastFed,b.order,b.tamed);}
inline void io(Archive& a,HerdState& s){a(s.herds,s.bonds,s.lastTick,s.seed,s.initialized);}
inline uint16_t stateKind(const HerdState&){return 209;}
inline bool valid(const HerdState& s){if(!policyPositiveKeys(s.herds)||!policyPositiveKeys(s.bonds))return false;for(auto& [id,h]:s.herds)if(id!=h.id||!policySortedUnique(h.members)||!policyBounded(h.home))return false;return true;}
}
