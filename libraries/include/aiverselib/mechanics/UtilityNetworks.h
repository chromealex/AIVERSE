#pragma once
#include "engine/mechanics/Namespace.h"
// utility-networks: pressure-driven fluid networks and heat exchange between rooms, next to ColonyPower.h (electric and
// simple fluid balance) and production-networks (item transport).
// - Fluid: nodes are sources (fixed pressure head, max output), sinks (demand per tick), tanks (capacity, level; pressure
//   rises with fill) and junctions; pipes have conductance and a flow cap, pumps add head along a pipe, valves close it.
//   Each tick: junction pressures relax for a fixed number of Gauss-Seidel sweeps, pipe flows follow the pressure
//   difference (clamped), then volumes move with exact conservation (sources pay what leaves them, sinks receive at most
//   their demand, tanks never go negative or overflow; the rest is reported as shortfall/spill).
// - Heat: rooms with heat capacity and temperature, links with conductance (walls, doors open/closed), an outdoor
//   boundary, heaters/coolers with power and an optional thermostat target. Explicit integration with symmetric
//   exchanges (energy conserved up to the outdoor boundary and devices), clamped for stability.
// Integer milli units; ascending ids; record kinds 211 (FluidState) and 212 (HeatState).
#include "PolicyCommon.h"
namespace aiverse::lib::mechanics {
enum class FluidNodeKind:uint8_t {Source,Sink,Tank,Junction};
struct FluidNode {int64_t id=0;FluidNodeKind kind=FluidNodeKind::Junction;int64_t head=0,maxOutput=0,demand=0,capacity=0,elevation=0;};   // pressures/volumes in milli
struct FluidPipe {int64_t id=0,a=0,b=0;int64_t conductanceMilli=1000,maxFlow=1000,pumpHead=0;bool open=true;};   // pump pushes a -> b
struct FluidNetwork {std::map<int64_t,FluidNode> nodes;std::map<int64_t,FluidPipe> pipes;uint32_t sweeps=16;};
struct FluidState {std::map<int64_t,int64_t> tankLevel,pressure;std::map<int64_t,bool> valves;uint32_t lastTick=0;bool initialized=false;};   // valves override pipe.open
struct FluidReport {std::map<int64_t,int64_t> flow,delivered,shortfall,spilled,pressure;};
inline bool valid(const FluidNetwork& n){
    if(n.sweeps==0||n.sweeps>256||n.nodes.size()>kPolicyMaxObjects||n.pipes.size()>kPolicyMaxObjects)return false;
    for(auto& [id,x]:n.nodes)if(id!=x.id||id<=0||uint8_t(x.kind)>3||x.maxOutput<0||x.demand<0||x.capacity<0||(x.kind==FluidNodeKind::Tank&&x.capacity==0))return false;
    for(auto& [id,p]:n.pipes)if(id!=p.id||id<=0||p.a==p.b||!n.nodes.contains(p.a)||!n.nodes.contains(p.b)||p.conductanceMilli<0||p.maxFlow<0)return false;
    return true;
}
inline int64_t nodePressure(const FluidNetwork& n,const FluidState& s,const FluidNode& x){
    switch(x.kind){case FluidNodeKind::Source:return x.head+x.elevation;
    case FluidNodeKind::Tank:{auto it=s.tankLevel.find(x.id);int64_t lvl=it==s.tankLevel.end()?0:it->second;return x.head*lvl/x.capacity+x.elevation;}
    case FluidNodeKind::Sink:return x.elevation;default:{auto it=s.pressure.find(x.id);return it==s.pressure.end()?x.elevation:it->second;}}
    (void)n;
}
inline std::optional<FluidReport> stepFluid(const FluidNetwork& n,FluidState& state,uint32_t tick){
    if(!valid(n)||!policyTickOk(state.initialized,state.lastTick,tick))return std::nullopt;
    for(auto& [id,l]:state.tankLevel){auto it=n.nodes.find(id);if(it==n.nodes.end()||it->second.kind!=FluidNodeKind::Tank||l<0||l>it->second.capacity)return std::nullopt;}
    auto s=state;s.initialized=true;s.lastTick=tick;FluidReport r;
    auto open=[&](const FluidPipe& p){auto v=s.valves.find(p.id);return v==s.valves.end()?p.open:v->second;};
    std::map<int64_t,std::vector<const FluidPipe*>> adj;for(auto& [id,p]:n.pipes)if(open(p)){adj[p.a].push_back(&p);adj[p.b].push_back(&p);}
    for(uint32_t k=0;k<n.sweeps;++k)for(auto& [id,x]:n.nodes)if(x.kind==FluidNodeKind::Junction){int64_t num=0,den=0;
        for(auto* p:adj[id]){auto other=p->a==id?p->b:p->a;auto po=nodePressure(n,s,n.nodes.at(other))+(p->a==id?-p->pumpHead:p->pumpHead);num+=po*p->conductanceMilli;den+=p->conductanceMilli;}
        s.pressure[id]=den?num/den:x.elevation;}
    // Desired flows a -> b.
    std::map<int64_t,int64_t> want;
    for(auto& [id,p]:n.pipes){if(!open(p)){r.flow[id]=0;continue;}auto pa=nodePressure(n,s,n.nodes.at(p.a)),pb=nodePressure(n,s,n.nodes.at(p.b));
        auto f=std::clamp<int64_t>((pa+p.pumpHead-pb)*p.conductanceMilli/1000,-p.maxFlow,p.maxFlow);want[id]=f;}
    // Supply limits: scale outflows of sources/tanks that cannot provide them (deterministic, in id order, one pass).
    std::map<int64_t,int64_t> outflow;for(auto& [id,f]:want){auto& p=n.pipes.at(id);if(f>0)outflow[p.a]+=f;else if(f<0)outflow[p.b]-=f;}
    std::map<int64_t,std::pair<int64_t,int64_t>> scale;   // node -> (num, den)
    for(auto& [node,out]:outflow){auto& x=n.nodes.at(node);int64_t avail=out;
        if(x.kind==FluidNodeKind::Source)avail=x.maxOutput;else if(x.kind==FluidNodeKind::Tank)avail=s.tankLevel[node];else if(x.kind==FluidNodeKind::Sink)avail=0;
        if(x.kind!=FluidNodeKind::Junction&&avail<out)scale[node]={avail,out};}
    for(auto& [id,f]:want){auto& p=n.pipes.at(id);auto from=f>=0?p.a:p.b;if(auto it=scale.find(from);it!=scale.end())f=f*it->second.first/it->second.second;r.flow[id]=f;}
    // Apply with conservation.
    std::map<int64_t,int64_t> net;for(auto& [id,f]:r.flow){auto& p=n.pipes.at(id);net[p.b]+=f;net[p.a]-=f;}
    for(auto& [id,x]:n.nodes){auto in=net[id];
        if(x.kind==FluidNodeKind::Tank){auto& lvl=s.tankLevel[id];lvl+=in;if(lvl>x.capacity){r.spilled[id]=lvl-x.capacity;lvl=x.capacity;}if(lvl<0)lvl=0;}
        else if(x.kind==FluidNodeKind::Sink){auto got=std::clamp<int64_t>(in,0,x.demand);r.delivered[id]=got;r.shortfall[id]=x.demand-got;if(in>x.demand)r.spilled[id]=in-x.demand;}
        else if(x.kind==FluidNodeKind::Junction&&in!=0)r.spilled[id]=in;               // junctions hold nothing: imbalance is reported
        r.pressure[id]=nodePressure(n,s,x);}
    std::erase_if(s.tankLevel,[](auto& e){return e.second==0;});
    state=std::move(s);return r;
}
inline void io(Archive& a,FluidState& s){a(s.tankLevel,s.pressure,s.valves,s.lastTick,s.initialized);}
inline uint16_t stateKind(const FluidState&){return 211;}
inline bool valid(const FluidState& s){for(auto& [id,l]:s.tankLevel)if(id<=0||l<0)return false;return policyPositiveKeys(s.pressure)&&policyPositiveKeys(s.valves);}

// ---- Heat.
struct HeatRoom {int64_t id=0;int64_t capacity=1000;};                         // energy (milli) per milli-degree
struct HeatLink {int64_t id=0,a=0,b=0;int64_t conductance=10;bool open=true;int64_t openConductance=0;};   // b = 0: outdoors; openConductance used when a door link is open
struct HeatDevice {int64_t id=0,room=0;int64_t power=0;int64_t target=INT64_MIN;};   // power >0 heats, <0 cools (energy per tick); target: thermostat in milli-degrees
struct HeatNetwork {std::map<int64_t,HeatRoom> rooms;std::map<int64_t,HeatLink> links;std::map<int64_t,HeatDevice> devices;};
struct HeatState {std::map<int64_t,int64_t> temperature;std::map<int64_t,bool> doorOpen;std::map<int64_t,bool> deviceOn;uint32_t lastTick=0;bool initialized=false;};
struct HeatReport {std::map<int64_t,int64_t> temperature,deviceEnergy;int64_t outdoorExchange=0;};
inline bool valid(const HeatNetwork& n){for(auto& [id,r]:n.rooms)if(id!=r.id||id<=0||r.capacity<=0)return false;
    for(auto& [id,l]:n.links)if(id!=l.id||id<=0||!n.rooms.contains(l.a)||(l.b&&!n.rooms.contains(l.b))||l.a==l.b||l.conductance<0||l.openConductance<0)return false;
    for(auto& [id,d]:n.devices)if(id!=d.id||id<=0||!n.rooms.contains(d.room))return false;return true;}
inline std::optional<HeatReport> stepHeat(const HeatNetwork& n,HeatState& state,uint32_t tick,int64_t outdoorMilli){
    if(!valid(n)||!policyTickOk(state.initialized,state.lastTick,tick))return std::nullopt;
    auto s=state;s.initialized=true;s.lastTick=tick;HeatReport r;
    auto T=[&](int64_t room){auto it=s.temperature.find(room);return it==s.temperature.end()?outdoorMilli:it->second;};
    std::map<int64_t,int64_t> energy;
    for(auto& [id,l]:n.links){auto open=s.doorOpen.contains(id)?s.doorOpen.at(id):l.open;auto k=open&&l.openConductance?l.openConductance:l.conductance;
        auto ta=T(l.a),tb=l.b?T(l.b):outdoorMilli;
        int64_t cap=l.b?std::min(n.rooms.at(l.a).capacity,n.rooms.at(l.b).capacity):n.rooms.at(l.a).capacity;
        int64_t q=(tb-ta)*k/1000;int64_t limit=std::abs(tb-ta)*cap/4;q=std::clamp(q,-limit,limit);   // never overshoot equilibrium
        energy[l.a]+=q;if(l.b)energy[l.b]-=q;else r.outdoorExchange-=q;}
    for(auto& [id,d]:n.devices){bool on=s.deviceOn.contains(id)?s.deviceOn.at(id):true;if(!on)continue;auto t=T(d.room);
        if(d.target!=INT64_MIN&&((d.power>0&&t>=d.target)||(d.power<0&&t<=d.target)))continue;energy[d.room]+=d.power;r.deviceEnergy[id]=d.power;}
    for(auto& [id,room]:n.rooms){auto t=T(id)+energy[id]/room.capacity;s.temperature[id]=std::clamp<int64_t>(t,-273150,10000000);r.temperature[id]=s.temperature[id];}
    state=std::move(s);return r;
}
inline void io(Archive& a,HeatState& s){a(s.temperature,s.doorOpen,s.deviceOn,s.lastTick,s.initialized);}
inline uint16_t stateKind(const HeatState&){return 212;}
inline bool valid(const HeatState& s){return policyPositiveKeys(s.temperature)&&policyPositiveKeys(s.doorOpen)&&policyPositiveKeys(s.deviceOn);}
}
