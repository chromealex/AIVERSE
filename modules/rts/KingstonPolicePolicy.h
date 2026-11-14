#pragma once
#include "modules/rts/RtsModule.h"
#include "modules/rts/RtsTraffic.h"
#include "modules/rts/RtsDriving.h"
#include "modules/rts/RtsArsenal.h"
#include "modules/rts/RtsTopDown.h"
namespace kingston {
inline bool policeOfficer(aiverse::Simulation& s,aiverse::Entity e){
 using namespace aiverse;using namespace game;auto&m=static_cast<RtsModule&>(s.module());auto*t=m.traffic();
 if(t)if(auto*st=t->store(s))for(const auto&c:st->cars)if(c.service>=0&&t->rules().services[size_t(c.service)].answers==kIncidentCrime)
  if(std::find(c.crew.begin(),c.crew.end(),e)!=c.crew.end())return true;
 const auto*u=s.world().tryGet<Unit>(e);if(!u)return false;const auto&id=m.rules().types[u->type].id;
 return id=="cop"||id=="city_officer";
}
inline bool policeArmed(aiverse::Simulation&s,aiverse::Entity e){
 using namespace aiverse;using namespace game;auto&m=static_cast<RtsModule&>(s.module());auto*u=s.world().tryGet<Unit>(e);if(!u)return false;
 auto unarmed=[](const std::string&id){return id=="fists"||id=="unarmed"||id=="punch";};
 if(auto*a=m.arsenal())if(auto*st=a->store(s))for(const auto&hand:st->hands)if(hand.hero==e){
  const auto&slots=a->rules().weapons;
  for(size_t i=0;i<slots.size()&&i<hand.ammo.size();++i)if(u->type<slots[i].index.size()&&slots[i].index[u->type]>=0&&!unarmed(slots[i].weapon)&&hand.ammo[i]!=0)return true;
  return false;
 }
 for(const auto&weapon:m.rules().types[u->type].weapons)if(!unarmed(weapon.id))return true;
 return false;
}
inline uint8_t policeVictimOwner(aiverse::Simulation&s,aiverse::Entity victim){
 auto&m=static_cast<aiverse::game::RtsModule&>(s.module());if(auto*t=m.topDown())if(auto*st=t->store(s))for(size_t i=1;i<st->pilots.size();++i)if(st->pilots[i].hero==victim)return uint8_t(i);return 0;
}
inline bool policeHeroVictim(aiverse::Simulation&s,aiverse::Entity victim){
 auto&m=static_cast<aiverse::game::RtsModule&>(s.module());return policeVictimOwner(s,victim)>0||m.state(s).trig.vars.count("police_body_for_"+std::to_string(victim.packed()));
}
inline aiverse::Entity policeBody(aiverse::Simulation&s,aiverse::Entity victim,uint16_t type,uint8_t owner,aiverse::Vec3 at){
 using namespace aiverse;using namespace game;auto&m=static_cast<RtsModule&>(s.module());auto&v=m.state(s).trig.vars;
 auto key="police_body_for_"+std::to_string(victim.packed());auto old=v.find(key);
 if(old!=v.end()&&old->second.raw){auto e=Entity::fromPacked(uint64_t(old->second.toInt()-1));if(s.world().alive(e))return e;}
 int proxy=m.rules().findType("city_rescue_patient");auto body=m.spawn(s,proxy>=0?uint16_t(proxy):type,owner,at);if(!body.valid())return body;
 auto suffix=std::to_string(body.packed());v[key]=Fixed::fromInt(int64_t(body.packed())+1);
 v["police_dead_"+suffix]=Fixed::fromInt(1);v["police_original_"+suffix]=Fixed::fromInt(type);
 v["police_victim_"+suffix]=Fixed::fromInt(int64_t(victim.packed())+1);
 v["resident_transport_"+suffix]=Fixed::fromInt(6);s.world().get<Unit>(body).flags|=kUnitDisabled;
 if(auto*td=m.topDown())if(auto*st=td->store(s))if(owner<st->pilots.size()&&(st->pilots[owner].hero==victim||(!s.world().alive(st->pilots[owner].hero)&&st->pilots[owner].heroType==type))){
  auto&p=st->pilots[owner];p.hero=body;p.respawnAt=0;p.vel={};p.input={};p.target=kNullEntity;
  v["police_"+std::to_string(owner)+"_phase"]=Fixed::fromInt(1);
 }
 return body;
}
inline void installPoliceGuard(aiverse::Simulation& sim){
 sim.insertSystem(0,"police_guard",[](aiverse::Simulation&s){
  using namespace aiverse;using namespace game;auto&m=static_cast<RtsModule&>(s.module());auto&vars=m.state(s).trig.vars;
  if(auto*td=m.topDown())if(auto*st=td->store(s))for(size_t i=1;i<st->pilots.size();++i){
   auto request=vars.find("police_request_"+std::to_string(i));if(request==vars.end()||!request->second.raw)continue;request->second={};
   auto hero=st->pilots[i].hero;if(s.world().alive(hero)&&m.traffic()){auto p=s.world().get<Transform>(hero).pos;m.traffic()->report(s,kIncidentCrime,{p.x,p.z},hero,hero,true);}
  }
  auto held=[&](Entity e){auto suffix=std::to_string(e.packed());for(auto key:{"police_custody_","police_dead_"}){auto i=vars.find(key+suffix);if(i!=vars.end()&&i->second.raw)return true;}return false;};
  s.world().each<Unit>([&](Entity e,Unit&u){auto suffix=std::to_string(e.packed());auto it=vars.find("police_custody_"+suffix);if(it==vars.end()||!it->second.raw)return;
   auto car=Entity::fromPacked(uint64_t(it->second.toInt()-1));auto*driver=s.world().tryGet<Unit>(car);if(driver&&driver->hp.raw>0)return;
   it->second={};u.flags&=~kUnitInCargo;vars["resident_transport_"+suffix]={};vars["resident_transport_aboard_"+suffix]={};
   auto dead=vars.find("police_dead_"+suffix);if(dead==vars.end()||!dead->second.raw){u.flags&=~kUnitDisabled;if(auto*td=m.topDown())if(auto*st=td->store(s))for(size_t i=1;i<st->pilots.size();++i)if(st->pilots[i].hero==e){u.flags|=kUnitPossessed;vars["police_"+std::to_string(i)+"_phase"]={};}}
  });
  if(auto*td=m.topDown())if(auto*st=td->store(s))for(auto&p:st->pilots)if(held(p.hero)){p.input={};p.vel={};p.target=kNullEntity;p.casting=-1;p.held=-1;p.respawnAt=0;}
 });
}
}
