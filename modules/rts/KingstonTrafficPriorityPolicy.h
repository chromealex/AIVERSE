#pragma once
#include "modules/rts/RtsModule.h"
#include "modules/rts/RtsTraffic.h"
#include "modules/rts/RtsDriving.h"
namespace kingston {
inline bool activeResponse(aiverse::game::RtsModule& m,aiverse::Simulation& sim,aiverse::Entity e){
 if(auto*d=m.driving())if(auto*car=d->car(sim,e))if(car->player)return false;
 auto*t=m.traffic();auto*store=t?t->store(sim):nullptr;if(!store)return false;
 const auto& responseVars=m.state(sim).trig.vars;auto loaded=responseVars.find("ambulance_owned_"+std::to_string(e.packed()));
 const bool transporting=loaded!=responseVars.end()&&loaded->second.raw;
 for(const auto&c:store->cars)if(c.e==e)return c.kind==aiverse::game::kTrafficEmergency && (c.task==aiverse::game::kTaskNone||c.task==aiverse::game::kTaskGoing||(c.task==aiverse::game::kTaskHome&&transporting));
 return false;
}
inline bool beaconEnabled(aiverse::game::RtsModule& m,aiverse::Simulation& sim,aiverse::Entity e){
 if(!sim.world().alive(e)||sim.world().get<aiverse::game::Unit>(e).hp.raw<=0)return false;
 const auto&vars=m.state(sim).trig.vars;auto it=vars.find("vehicle_beacon_"+std::to_string(e.packed()));
 if(it!=vars.end()&&it->second.raw)return it->second.raw>0;
 return activeResponse(m,sim,e);
}
template<class Host> void syncTrafficSignals(aiverse::Simulation& sim,Host& host){
 auto&m=static_cast<aiverse::game::RtsModule&>(sim.module());auto*d=m.driving();auto*st=d?d->store(sim):nullptr;if(!st)return;
 const auto&vars=m.state(sim).trig.vars;
 for(const auto&c:st->cars){bool beacon=beaconEnabled(m,sim,c.e);auto it=vars.find("vehicle_siren_"+std::to_string(c.e.packed()));
  bool siren=beacon&&(it!=vars.end()&&it->second.raw?it->second.raw>0:activeResponse(m,sim,c.e));
  host.trafficSignals(c.e.packed(),beacon,siren);
 }
}
}
