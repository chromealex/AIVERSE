#pragma once
#include "engine/mechanics/Namespace.h"
// transit-schedules: public transport and goods flows over a stop graph (buses, trams, metro, ferries, freight).
// - Lines: ordered stops with travel ticks between them, a fleet size and a headway; departures are generated from
//   the timetable (first departure + k * headway), vehicles have capacity.
// - Journeys: aggregated demand (origin stop, destination stop, passengers or cargo units per day) is assigned to the
//   fastest route with transfers by a deterministic earliest-arrival search over the timetable (RAPTOR-like, bounded
//   transfers), with a walking/transfer penalty.
// - Simulation: per tick, vehicles board waiting groups up to capacity in FIFO order, unload at destinations or
//   transfer stops; overloaded stops are reported (congestion diagnostics: waiting count, average wait, load factor).
// Integer ticks, ascending ids; record kind 215 (TransitState).
#include "PolicyCommon.h"
namespace aiverse::lib::mechanics {
struct TransitLine {int64_t id=0;std::vector<int64_t> stops;std::vector<uint32_t> travel;uint32_t firstDeparture=0,headway=600,lastDeparture=UINT32_MAX;int64_t capacity=60;bool loop=false;};
struct TransitNetwork {std::map<int64_t,TransitLine> lines;std::set<int64_t> stops;uint32_t transferTicks=60;uint32_t maxTransfers=3;};
struct TransitDemand {int64_t id=0,from=0,to=0,units=0;uint32_t spawnEvery=60;};          // units per spawn
struct Leg {int64_t line=0,board=0,alight=0;};
struct TransitRoute {std::vector<Leg> legs;uint32_t arrival=0;};
struct WaitingGroup {int64_t demand=0,units=0;uint32_t since=0;std::size_t leg=0;};        // leg index into the demand's route
struct TransitVehicle {int64_t line=0;uint32_t departure=0;std::size_t stopIndex=0;uint32_t nextArrival=0;std::vector<WaitingGroup> onboard;};
struct TransitState {std::map<int64_t,std::vector<WaitingGroup>> waiting;std::map<std::pair<int64_t,uint32_t>,TransitVehicle> vehicles;std::map<int64_t,TransitRoute> routes;
    int64_t delivered=0,totalWait=0;uint32_t lastTick=0;bool initialized=false;};
struct StopLoad {int64_t stop=0,waiting=0;uint32_t oldest=0;};
struct TransitReport {std::vector<StopLoad> stops;int64_t delivered=0,boarded=0,leftBehind=0;std::map<int64_t,int64_t> lineLoadMilli;};

inline bool valid(const TransitNetwork& n){if(n.maxTransfers>8)return false;for(auto& [id,l]:n.lines){if(id!=l.id||id<=0||l.stops.size()<2||l.travel.size()!=l.stops.size()-1||l.headway==0||l.capacity<=0)return false;
    for(auto s:l.stops)if(!n.stops.contains(s))return false;for(auto t:l.travel)if(t==0)return false;}return true;}
inline uint32_t arrivalAt(const TransitLine& l,uint32_t departure,std::size_t index){uint32_t t=departure;for(std::size_t i=0;i<index;++i)t+=l.travel[i];return t;}
// Next departure of `line` from stop index `i` at or after `time`; returns the line departure (from the first stop).
inline std::optional<uint32_t> nextDeparture(const TransitLine& l,std::size_t i,uint32_t time){
    auto offset=arrivalAt(l,0,i);if(time<l.firstDeparture+offset)return l.firstDeparture;
    uint64_t k=(uint64_t(time)-l.firstDeparture-offset+l.headway-1)/l.headway;uint64_t dep=l.firstDeparture+k*l.headway;if(dep>l.lastDeparture||dep>UINT32_MAX/2)return std::nullopt;return uint32_t(dep);}
// Earliest-arrival route with up to maxTransfers changes (rounds over lines in ascending id).
inline std::optional<TransitRoute> planTransit(const TransitNetwork& n,int64_t from,int64_t to,uint32_t time){
    if(!valid(n)||!n.stops.contains(from)||!n.stops.contains(to))return std::nullopt;if(from==to)return TransitRoute{{},time};
    std::map<int64_t,uint32_t> best{{from,time}};std::map<int64_t,std::vector<Leg>> how{{from,{}}};
    for(uint32_t round=0;round<=n.maxTransfers;++round){auto marked=best;bool changed=false;
        for(auto& [lid,l]:n.lines)for(std::size_t i=0;i<l.stops.size();++i){auto it=marked.find(l.stops[i]);if(it==marked.end())continue;
            auto ready=it->second+(how[l.stops[i]].empty()?0:n.transferTicks);auto dep=nextDeparture(l,i,ready);if(!dep)continue;
            for(std::size_t j=i+1;j<l.stops.size();++j){auto arr=arrivalAt(l,*dep,j);auto s=l.stops[j];
                if(!best.contains(s)||arr<best[s]){best[s]=arr;auto legs=how[l.stops[i]];legs.push_back({lid,l.stops[i],s});how[s]=legs;changed=true;}}}
        if(!changed)break;}
    if(!best.contains(to))return std::nullopt;return TransitRoute{how[to],best[to]};
}
inline std::optional<TransitReport> stepTransit(const TransitNetwork& n,std::span<const TransitDemand> demand,TransitState& state,uint32_t tick){
    if(!valid(n)||!policyTickOk(state.initialized,state.lastTick,tick))return std::nullopt;
    for(auto& d:demand)if(d.id<=0||!n.stops.contains(d.from)||!n.stops.contains(d.to)||d.units<0||d.spawnEvery==0)return std::nullopt;
    auto s=state;s.initialized=true;s.lastTick=tick;TransitReport r;
    for(auto& d:demand){if(tick%d.spawnEvery||!d.units||d.from==d.to)continue;
        if(!s.routes.contains(d.id)){auto route=planTransit(n,d.from,d.to,tick);if(!route||route->legs.empty())continue;s.routes[d.id]=*route;}
        s.waiting[d.from].push_back({d.id,d.units,tick,0});}
    // Spawn vehicles that depart now.
    for(auto& [lid,l]:n.lines)if(tick>=l.firstDeparture&&tick<=l.lastDeparture&&(tick-l.firstDeparture)%l.headway==0)s.vehicles[{lid,tick}]={lid,tick,0,tick,{}};
    std::vector<std::pair<int64_t,uint32_t>> finished;
    for(auto& [key,v]:s.vehicles){if(tick!=v.nextArrival)continue;const auto& l=n.lines.at(v.line);auto stop=l.stops[v.stopIndex];
        // Unload: groups whose current leg ends here.
        std::vector<WaitingGroup> stay;
        for(auto& g:v.onboard){auto& route=s.routes.at(g.demand);if(route.legs[g.leg].alight==stop){
            if(g.leg+1==route.legs.size()){s.delivered+=g.units;r.delivered+=g.units;}else{auto next=g;++next.leg;next.since=tick;s.waiting[stop].push_back(next);}}else stay.push_back(g);}
        v.onboard=std::move(stay);
        // Board FIFO groups whose current leg uses this line from this stop.
        int64_t load=0;for(auto& g:v.onboard)load+=g.units;
        if(v.stopIndex+1<l.stops.size()){auto& q=s.waiting[stop];std::vector<WaitingGroup> rest;
            for(auto& g:q){auto& leg=s.routes.at(g.demand).legs[g.leg];
                if(leg.line==v.line&&leg.board==stop&&load<l.capacity){auto take=std::min(g.units,l.capacity-load);auto part=g;part.units=take;v.onboard.push_back(part);load+=take;r.boarded+=take;
                    s.totalWait+=int64_t(tick-g.since)*take;if(take<g.units){auto left=g;left.units-=take;rest.push_back(left);r.leftBehind+=left.units;}}
                else rest.push_back(g);}
            q=std::move(rest);r.lineLoadMilli[v.line]=std::max(r.lineLoadMilli[v.line],load*1000/l.capacity);
            v.nextArrival=tick+l.travel[v.stopIndex];++v.stopIndex;}
        else finished.push_back(key);}
    for(auto& k:finished){for(auto& g:s.vehicles.at(k).onboard){auto& route=s.routes.at(g.demand);(void)route;s.waiting[n.lines.at(k.first).stops.back()].push_back(g);}s.vehicles.erase(k);}
    for(auto& [stop,q]:s.waiting){if(q.empty())continue;int64_t w=0;uint32_t old=tick;for(auto& g:q){w+=g.units;old=std::min(old,g.since);}r.stops.push_back({stop,w,tick-old});}
    std::erase_if(s.waiting,[](auto& e){return e.second.empty();});
    state=std::move(s);return r;
}
inline void io(Archive& a,Leg& l){a(l.line,l.board,l.alight);}
inline void io(Archive& a,TransitRoute& r){a(r.legs,r.arrival);}
inline void io(Archive& a,WaitingGroup& g){uint64_t leg=g.leg;a(g.demand,g.units,g.since,leg);g.leg=std::size_t(leg);}
inline void io(Archive& a,TransitVehicle& v){uint64_t i=v.stopIndex;a(v.line,v.departure,i,v.nextArrival,v.onboard);v.stopIndex=std::size_t(i);}
inline void io(Archive& a,TransitState& s){a(s.waiting,s.vehicles,s.routes,s.delivered,s.totalWait,s.lastTick,s.initialized);}
inline uint16_t stateKind(const TransitState&){return 215;}
inline bool valid(const TransitState& s){for(auto& [k,q]:s.waiting)for(auto& g:q){auto r=s.routes.find(g.demand);if(r==s.routes.end()||g.leg>=r->second.legs.size()||g.units<=0)return false;}
    for(auto& [k,v]:s.vehicles)for(auto& g:v.onboard){auto r=s.routes.find(g.demand);if(r==s.routes.end()||g.leg>=r->second.legs.size())return false;}return true;}
}
