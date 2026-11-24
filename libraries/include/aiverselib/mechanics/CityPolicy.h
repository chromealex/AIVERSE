#pragma once
#include "engine/mechanics/Namespace.h"
// city-policy: city-builder economy and politics (SimCity / Cities: Skylines / Tropico style).
// - Zoning: lots with zone type (residential, commercial, industrial, office) and density; demand per zone from jobs,
//   workers and goods; lots grow or shrink one level per review when demand and land value allow, empty lots get built.
// - Households and businesses on lots (aggregated counts, not individual agents): residents, workers, income class.
// - Budget: taxes per zone and class, service upkeep (police, fire, health, education, transit...), loans with interest,
//   a balance that can go negative, bankruptcy flag.
// - Services: coverage per lot from service buildings (radius, capacity) lowers crime/fire/sickness and raises value.
// - Factions and elections: approval per faction from their policy preferences, taxes and service levels; an election
//   every N days returns the winner by weighted approval; policies (edicts) toggle effects and upkeep.
// Integer math (money in cents, rates in milli), ascending ids; record kind 214 (CityState).
#include "PolicyCommon.h"
namespace aiverse::lib::mechanics {
enum class ZoneType:uint8_t {None,Residential,Commercial,Industrial,Office};
inline constexpr int kZoneTypes=5,kIncomeClasses=3;
struct Lot {int64_t id=0;ZoneType zone=ZoneType::None;int32_t level=0,maxLevel=3;Vec3 position{};int64_t landValue=500;};   // landValue milli
struct ServiceBuilding {int64_t id=0,service=0;Vec3 position{};Fixed radius=30_fx;int64_t capacity=1000,upkeep=10000;};    // capacity: residents served
struct Policy {int64_t id=0,upkeep=0;std::map<int64_t,int64_t> factionDelta;int64_t demandDelta[kZoneTypes]{};int64_t crimeDelta=0;};   // per-faction approval change (milli)
struct Faction {int64_t id=0;int64_t weight=1000;std::map<int64_t,int64_t> serviceWeight;int64_t taxSensitivity=500;};   // weight: share of voters (milli)
struct CityConfig {int64_t residentsPerLevel=20,jobsPerLevel=15,taxBaseCents=500;uint32_t ticksPerDay=1440,reviewDays=1,electionDays=365;int64_t interestMilliPerDay=1;
    int64_t growThreshold=100,shrinkThreshold=-200;};
struct CityData {std::map<int64_t,ServiceBuilding> services;std::map<int64_t,Policy> policies;std::map<int64_t,Faction> factions;};
struct CityState {std::map<int64_t,Lot> lots;std::set<int64_t> activePolicies;int64_t taxMilli[kZoneTypes]{0,90,90,90,90};int64_t balance=0,loan=0;
    int64_t demand[kZoneTypes]{};std::map<int64_t,int64_t> coverage,approval;int64_t lastWinner=0;uint32_t lastTick=0;bool bankrupt=false,initialized=false;};
enum class CityEventKind:uint8_t {LotGrew,LotShrank,LotAbandoned,Budget,Election,Bankrupt};
struct CityEvent {CityEventKind kind{};int64_t id=0,value=0;};
struct CityCommand {enum Kind:uint8_t {Zone,SetTax,EnablePolicy,DisablePolicy,TakeLoan,RepayLoan} kind=Zone;int64_t id=0;ZoneType zone=ZoneType::None;int64_t value=0;Vec3 position{};};   // Zone: a new lot takes `position`
struct CityReport {std::vector<CityEvent> events;int64_t residents=0,jobs=0,income=0,expenses=0;int64_t demand[kZoneTypes]{};};

inline bool valid(const CityConfig& c){return c.residentsPerLevel>0&&c.jobsPerLevel>0&&c.ticksPerDay>0&&c.reviewDays>0&&c.electionDays>0&&c.interestMilliPerDay>=0&&c.growThreshold>c.shrinkThreshold;}
inline bool valid(const CityData& d){for(auto& [id,s]:d.services)if(id!=s.id||id<=0||s.service<=0||s.radius<=Fixed{}||s.capacity<=0||s.upkeep<0)return false;
    for(auto& [id,p]:d.policies)if(id!=p.id||id<=0)return false;for(auto& [id,f]:d.factions)if(id!=f.id||id<=0||f.weight<0)return false;return true;}
inline int64_t residentsOf(const CityConfig& c,const Lot& l){return l.zone==ZoneType::Residential?l.level*c.residentsPerLevel:0;}
inline int64_t jobsOf(const CityConfig& c,const Lot& l){return l.zone==ZoneType::Residential||l.zone==ZoneType::None?0:l.level*c.jobsPerLevel;}
inline std::optional<CityReport> stepCity(const CityConfig& c,const CityData& d,CityState& state,uint32_t tick,std::span<const CityCommand> commands){
    if(!valid(c)||!valid(d)||!policyTickOk(state.initialized,state.lastTick,tick)||commands.size()>kPolicyMaxCommands)return std::nullopt;
    auto s=state;s.initialized=true;s.lastTick=tick;CityReport r;
    for(auto& cmd:commands){switch(cmd.kind){
        case CityCommand::Zone:{if(cmd.id<=0||uint8_t(cmd.zone)>4)return std::nullopt;bool fresh=!s.lots.contains(cmd.id);auto& l=s.lots[cmd.id];l.id=cmd.id;if(fresh)l.position=cmd.position;if(l.zone!=cmd.zone){l.zone=cmd.zone;l.level=0;}break;}
        case CityCommand::SetTax:if(uint8_t(cmd.zone)<1||uint8_t(cmd.zone)>4||cmd.value<0||cmd.value>500)return std::nullopt;s.taxMilli[uint8_t(cmd.zone)]=cmd.value;break;
        case CityCommand::EnablePolicy:if(!d.policies.contains(cmd.id))return std::nullopt;s.activePolicies.insert(cmd.id);break;
        case CityCommand::DisablePolicy:s.activePolicies.erase(cmd.id);break;
        case CityCommand::TakeLoan:if(cmd.value<=0)return std::nullopt;s.loan+=cmd.value;s.balance+=cmd.value;break;
        case CityCommand::RepayLoan:{auto pay=std::min({cmd.value,s.loan,std::max<int64_t>(0,s.balance)});s.loan-=pay;s.balance-=pay;break;}
        default:return std::nullopt;}}
    int64_t residents=0,jobs=0,commercial=0,industrial=0;
    for(auto& [id,l]:s.lots){residents+=residentsOf(c,l);jobs+=jobsOf(c,l);if(l.zone==ZoneType::Commercial)commercial+=l.level;if(l.zone==ZoneType::Industrial)industrial+=l.level;}
    // Demand: residents want jobs; businesses want workers; commerce wants goods (industry) and customers.
    int64_t dem[kZoneTypes]{};
    dem[1]=(jobs-residents)*1000/std::max<int64_t>(1,residents+jobs)+300-s.taxMilli[1];
    dem[2]=(residents/4-commercial*c.jobsPerLevel)*1000/std::max<int64_t>(1,residents+1)+(industrial-commercial)*50+150-s.taxMilli[2];
    dem[3]=(residents-jobs)*1000/std::max<int64_t>(1,residents+jobs)+250-s.taxMilli[3];
    dem[4]=(residents-jobs)*800/std::max<int64_t>(1,residents+jobs)+100-s.taxMilli[4];
    for(auto pid:s.activePolicies)for(int z=0;z<kZoneTypes;++z)dem[z]+=d.policies.at(pid).demandDelta[z];
    for(int z=0;z<kZoneTypes;++z){s.demand[z]=std::clamp<int64_t>(dem[z],-1000,1000);r.demand[z]=s.demand[z];}
    // Service coverage per service id: residents covered within radius until capacity.
    s.coverage.clear();
    for(auto& [sid,b]:d.services){int64_t left=b.capacity;for(auto& [id,l]:s.lots){auto res=residentsOf(c,l);if(!res||(l.position-b.position).length()>b.radius)continue;
        auto take=std::min(left,res);left-=take;s.coverage[b.service]+=take;}}
    bool day=tick%c.ticksPerDay==0;
    if(day&&(tick/c.ticksPerDay)%c.reviewDays==0){
        for(auto& [id,l]:s.lots){if(l.zone==ZoneType::None)continue;auto dz=s.demand[uint8_t(l.zone)]+(l.landValue-500);
            if(dz>=c.growThreshold&&l.level<l.maxLevel){++l.level;r.events.push_back({CityEventKind::LotGrew,id,l.level});}
            else if(dz<=c.shrinkThreshold&&l.level>0){--l.level;r.events.push_back({l.level?CityEventKind::LotShrank:CityEventKind::LotAbandoned,id,l.level});}}
    }
    if(day){
        int64_t income=0,expenses=0;
        for(auto& [id,l]:s.lots){auto units=residentsOf(c,l)+jobsOf(c,l);income+=units*c.taxBaseCents*s.taxMilli[uint8_t(l.zone)]/1000;}
        for(auto& [sid,b]:d.services)expenses+=b.upkeep;for(auto pid:s.activePolicies)expenses+=d.policies.at(pid).upkeep;
        expenses+=s.loan*c.interestMilliPerDay/1000;s.balance+=income-expenses;r.income=income;r.expenses=expenses;r.events.push_back({CityEventKind::Budget,0,income-expenses});
        if(s.balance<0&&s.loan>0&&!s.bankrupt){s.bankrupt=true;r.events.push_back({CityEventKind::Bankrupt,0,s.balance});}else if(s.balance>=0)s.bankrupt=false;
        // Approval per faction: services they care about, taxes, policies.
        int64_t avgTax=0;for(int z=1;z<kZoneTypes;++z)avgTax+=s.taxMilli[z];avgTax/=4;
        for(auto& [fid,f]:d.factions){int64_t a=500;for(auto& [svc,w]:f.serviceWeight){auto cov=residents?s.coverage[svc]*1000/residents:0;a+=(cov-500)*w/1000;}
            a-=(avgTax-90)*f.taxSensitivity/100;for(auto pid:s.activePolicies){auto& p=d.policies.at(pid);if(auto it=p.factionDelta.find(fid);it!=p.factionDelta.end())a+=it->second;}
            s.approval[fid]=std::clamp<int64_t>(a,0,1000);}
        if((tick/c.ticksPerDay)%c.electionDays==0&&!d.factions.empty()){int64_t best=-1,winner=0;
            for(auto& [fid,f]:d.factions){auto v=s.approval[fid]*f.weight;if(v>best){best=v;winner=fid;}}s.lastWinner=winner;r.events.push_back({CityEventKind::Election,winner,best});}
    }
    r.residents=residents;r.jobs=jobs;state=std::move(s);return r;
}
inline void io(Archive& a,Lot& l){a(l.id,l.zone,l.level,l.maxLevel,l.position,l.landValue);}
inline void io(Archive& a,CityState& s){std::vector<int64_t> pol(s.activePolicies.begin(),s.activePolicies.end());a(s.lots,pol);for(auto& t:s.taxMilli)a(t);a(s.balance,s.loan);
    for(auto& x:s.demand)a(x);a(s.coverage,s.approval,s.lastWinner,s.lastTick,s.bankrupt,s.initialized);s.activePolicies=std::set<int64_t>(pol.begin(),pol.end());}
inline uint16_t stateKind(const CityState&){return 214;}
inline bool valid(const CityState& s){for(auto& [id,l]:s.lots)if(id!=l.id||id<=0||uint8_t(l.zone)>4||l.level<0||l.level>l.maxLevel)return false;return s.loan>=0;}
}
