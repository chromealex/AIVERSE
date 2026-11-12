#pragma once
// survival-ecology: population model per species per region. Every `periodTicks` each population changes by
// logistic growth r*N*(K-N)/K (negative above the carrying capacity K), base mortality, predation with a
// saturating functional response attack*Npred*Nprey/(Nprey+half) (prey shared by predators in id order),
// predator births from converted prey, starvation when predators eat less than they need, then migration of
// a share of the excess over K to neighbour regions. All rates are milli per period; fractional individuals
// are carried in per-population milli accumulators, so small populations still grow deterministically.
// Harvests (hunting, kills by animal agents) apply on the tick they arrive. The only randomness is optional
// birth noise drawn from the passed-in engine Random.
#include "engine/core/Random.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>
namespace aiverse::lib::genre {
inline constexpr int64_t kEcologyPopulationLimit=1000000000;
inline constexpr std::size_t kEcologyMaxDefs=4096;
struct EcologyPredation {int64_t prey=0,attackMilli=0,halfSaturation=1,conversionMilli=0;};
struct EcologySpeciesDef {
    int64_t id=0,growthMilli=0,mortalityMilli=0;
    int64_t foodNeedMilli=0,starvationMilli=0;   // predators: prey needed per individual per period (milli) and deaths when unfed
    int64_t migrateMilli=0;                      // share of the excess over K that leaves to neighbours per period
    std::vector<EcologyPredation> diet;          // sorted by prey id
};
struct EcologyRegionDef {int64_t id=0;std::map<int64_t,int64_t> capacity;std::vector<int64_t> neighbors;}; // species -> K; neighbours sorted
struct EcologyDefs {int64_t periodTicks=60,noiseMilli=0;std::map<int64_t,EcologySpeciesDef> species;std::map<int64_t,EcologyRegionDef> regions;};
struct EcologyPopulation {int64_t count=0,carry=0;bool operator==(const EcologyPopulation&)const=default;}; // carry: milli individuals, |carry| < 1000
struct EcologyState {std::map<std::pair<int64_t,int64_t>,EcologyPopulation> populations;int64_t lastTick=0;bool initialized=false;}; // (region, species); record kind 107
struct EcologyHarvest {int64_t region=0,species=0,count=0;};
struct EcologyInput {
    std::vector<EcologyHarvest> harvests;        // applied in order, clamped to the population
    std::map<int64_t,int64_t> capacityMilli;     // region -> K factor (vegetation, season), default 1000
    int64_t growthMilli=1000;                    // weather growth factor for species without a diet (SurvivalWeatherEffects::growthMilli)
};
enum class EcologyEventKind:uint8_t {Harvested,Extinct,Arrived}; // Harvested value = taken; Arrived: first individuals in a region
struct EcologyEvent {EcologyEventKind kind=EcologyEventKind::Harvested;int64_t region=0,species=0,value=0;};
struct EcologyChange {int64_t region=0,species=0,before=0,after=0,births=0,deaths=0,eaten=0,migrated=0;}; // births..migrated in milli
struct EcologyReport {bool period=false;std::vector<EcologyChange> changes;std::vector<EcologyEvent> events;};

inline std::string ecologyDefsError(const EcologyDefs& d){
    auto milli=[](int64_t v){return v>=0&&v<=100000;};
    if(d.periodTicks<1||d.periodTicks>100000000)return "ecology.periodTicks: 1..100000000";
    if(d.noiseMilli<0||d.noiseMilli>1000)return "ecology.noiseMilli: 0..1000";
    if(d.species.size()>kEcologyMaxDefs||d.regions.size()>kEcologyMaxDefs)return "ecology: too many definitions";
    for(const auto& [id,s]:d.species){
        auto p="ecology.species "+std::to_string(id);
        if(id<=0||s.id!=id||!milli(s.growthMilli)||!milli(s.mortalityMilli)||!milli(s.foodNeedMilli)||!milli(s.starvationMilli)||!milli(s.migrateMilli)||s.migrateMilli>1000)return p+": invalid rates";
        int64_t prev=0;for(const auto& e:s.diet){
            if(e.prey<=prev||!d.species.contains(e.prey)||e.prey==id)return p+".diet: prey must be known, sorted, unique and not itself";
            if(!milli(e.attackMilli)||e.halfSaturation<1||e.halfSaturation>kEcologyPopulationLimit||!milli(e.conversionMilli))return p+".diet: invalid predation";
            prev=e.prey;
        }
        if(s.diet.empty()&&s.foodNeedMilli)return p+": foodNeedMilli needs a diet";
    }
    for(const auto& [id,r]:d.regions){
        auto p="ecology.region "+std::to_string(id);
        if(id<=0||r.id!=id)return p+": id mismatch";
        for(auto [sp,k]:r.capacity)if(!d.species.contains(sp)||k<0||k>kEcologyPopulationLimit)return p+".capacity: unknown species or out of range";
        int64_t prev=0;for(auto n:r.neighbors){if(n<=prev||n==id||!d.regions.contains(n))return p+".neighbors: known, sorted, unique, not itself";prev=n;}
    }
    return {};
}
inline bool valid(const EcologyDefs& d){return ecologyDefsError(d).empty();}
inline bool valid(const EcologyState& s){
    if(s.populations.size()>kEcologyMaxDefs*16||s.lastTick<0)return false;
    for(const auto& [k,p]:s.populations)if(k.first<=0||k.second<=0||p.count<0||p.count>kEcologyPopulationLimit||p.carry<= -1000||p.carry>=1000)return false;
    return true;
}
inline int64_t ecologyCount(const EcologyState& s,int64_t region,int64_t species){auto it=s.populations.find({region,species});return it==s.populations.end()?0:it->second.count;}
inline bool ecologySetPopulation(const EcologyDefs& d,EcologyState& s,int64_t region,int64_t species,int64_t count){
    if(!d.regions.contains(region)||!d.species.contains(species)||count<0||count>kEcologyPopulationLimit)return false;
    s.populations[{region,species}]={count,0};return true;
}
namespace detail_ecology {
inline int64_t floorDiv(int64_t a,int64_t b){auto q=a/b;if((a%b!=0)&&((a<0)!=(b<0)))--q;return q;}
// a*b/c with c > 0; callers keep a*b below 1e18 (populations <= 1e9, milli rates <= 1e5, ratios <= 1000).
inline int64_t mulDiv(int64_t a,int64_t b,int64_t c){return a*b/c;}
inline int64_t ratio(int64_t num,int64_t den){return std::clamp<int64_t>(num*1000/den,-1000,1000);} // milli, |num| <= 1e15
}
inline std::optional<EcologyReport> stepEcology(const EcologyDefs& d,EcologyState& state,int64_t tick,const EcologyInput& in,Random& rng){
    using namespace detail_ecology;
    if(tick<0||(state.initialized&&tick<=state.lastTick)||!valid(d)||!valid(state)||in.growthMilli<0||in.growthMilli>10000||in.harvests.size()>65536)return std::nullopt;
    for(const auto& [k,p]:state.populations){(void)p;if(!d.regions.contains(k.first)||!d.species.contains(k.second))return std::nullopt;}
    for(const auto& h:in.harvests)if(!d.regions.contains(h.region)||!d.species.contains(h.species)||h.count<0||h.count>kEcologyPopulationLimit)return std::nullopt;
    for(auto [r,f]:in.capacityMilli)if(!d.regions.contains(r)||f<0||f>10000)return std::nullopt;
    auto s=state;EcologyReport rep;
    for(const auto& h:in.harvests){
        auto it=s.populations.find({h.region,h.species});if(it==s.populations.end()||!h.count)continue;
        auto taken=std::min(h.count,it->second.count);it->second.count-=taken;rep.events.push_back({EcologyEventKind::Harvested,h.region,h.species,taken});
        if(taken&&it->second.count==0)rep.events.push_back({EcologyEventKind::Extinct,h.region,h.species,0});
    }
    if(tick%d.periodTicks==0&&(!state.initialized||tick>state.lastTick)){
        rep.period=true;std::map<std::pair<int64_t,int64_t>,int64_t> delta,emigrants;std::map<std::pair<int64_t,int64_t>,EcologyChange> change;
        for(const auto& [rid,region]:d.regions){
            const auto capFactor=in.capacityMilli.contains(rid)?in.capacityMilli.at(rid):1000;
            std::map<int64_t,int64_t> left;for(const auto& [sid,sp]:d.species){(void)sp;left[sid]=ecologyCount(s,rid,sid)*1000;} // prey available, milli
            for(const auto& [sid,sp]:d.species){
                const auto n=ecologyCount(s,rid,sid);if(!n&&!s.populations.contains({rid,sid}))continue;
                auto& ch=change[{rid,sid}];ch.region=rid;ch.species=sid;ch.before=n;
                const auto k=mulDiv(region.capacity.contains(sid)?region.capacity.at(sid):0,capFactor,1000);
                int64_t births=0,deaths=mulDiv(n,sp.mortalityMilli,1);
                if(k>0){auto g=mulDiv(n*sp.growthMilli,ratio(k-n,k),1000);if(sp.diet.empty())g=g>0?mulDiv(g,in.growthMilli,1000):g;if(g>0)births+=g;else deaths-=g;}
                else if(n>0)deaths+=mulDiv(n,sp.growthMilli,1); // no habitat: decline
                int64_t eaten=0;
                for(const auto& e:sp.diet){
                    const auto prey=ecologyCount(s,rid,e.prey);if(!prey||!n)continue;
                    auto want=mulDiv(n*e.attackMilli,ratio(prey,prey+e.halfSaturation),1000);auto got=std::min(want,left[e.prey]);
                    left[e.prey]-=got;delta[{rid,e.prey}]-=got;change[{rid,e.prey}].eaten+=got;eaten+=got;births+=mulDiv(got,e.conversionMilli,1000);
                }
                if(sp.foodNeedMilli&&n){const auto need=n*sp.foodNeedMilli;if(eaten<need)deaths+=mulDiv(n*sp.starvationMilli,ratio(need-eaten,need),1000);}
                if(births>0&&d.noiseMilli>0){auto noise=int64_t(rng.below(uint32_t(2*d.noiseMilli+1)))-d.noiseMilli;births=mulDiv(births,1000+noise,1000);}
                ch.births=births;ch.deaths=deaths;delta[{rid,sid}]+=births-deaths;
                if(sp.migrateMilli&&!region.neighbors.empty()&&n>k)emigrants[{rid,sid}]=mulDiv(n-k,sp.migrateMilli,1000);
            }
        }
        for(auto& [key,ch]:change){
            auto& p=s.populations[key];const auto before=p.count;auto total=p.carry+delta[key];auto whole=floorDiv(total,1000);
            p.carry=total-whole*1000;p.count=std::clamp<int64_t>(p.count+whole,0,kEcologyPopulationLimit);if(p.count==0)p.carry=std::max<int64_t>(p.carry,0);
            if(before>0&&p.count==0)rep.events.push_back({EcologyEventKind::Extinct,key.first,key.second,0});
        }
        for(auto [key,m]:emigrants){
            const auto& nb=d.regions.at(key.first).neighbors;auto& from=s.populations[key];m=std::min(m,from.count);if(m<=0)continue;
            from.count-=m;change[key].migrated-=m*1000;const auto each=m/int64_t(nb.size());auto extra=m%int64_t(nb.size());
            for(auto to:nb){
                auto moved=each+(extra-->0?1:0);if(!moved)continue;auto& dest=s.populations[{to,key.second}];
                if(dest.count==0)rep.events.push_back({EcologyEventKind::Arrived,to,key.second,moved});
                dest.count=std::min(kEcologyPopulationLimit,dest.count+moved);
                auto& c=change[{to,key.second}];c.region=to;c.species=key.second;c.migrated+=moved*1000;
            }
        }
        for(auto& [key,ch]:change){ch.after=ecologyCount(s,key.first,key.second);rep.changes.push_back(ch);}
    }
    s.lastTick=tick;s.initialized=true;state=std::move(s);return rep;
}
}
