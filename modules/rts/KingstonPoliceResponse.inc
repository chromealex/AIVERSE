if(sv.answers==kIncidentCrime && inc && (c.task==kTaskCrewOut||c.task==kTaskWorking||gs.trig.vars["police_owned_"+std::to_string(c.e.packed())].raw)){
 auto&vars=gs.trig.vars;const auto key="police_car_"+std::to_string(c.e.packed())+"_";
 auto get=[&](const std::string&name){auto it=vars.find(key+name);return it==vars.end()?Fixed{}:it->second;};
 auto set=[&](const std::string&name,int64_t value){vars[key+name]=Fixed::fromInt(value);};
 auto phase=[&](int value){set("phase",value);set("since",sim.tick());};
 const auto owned="police_owned_"+std::to_string(c.e.packed());auto prisoner=inc->offender;
 auto alive=[&](Entity e){return living(w,e);};
 int state=int(get("phase").toInt());const auto suffix=std::to_string(prisoner.packed());
 const bool dead=vars["police_dead_"+suffix].raw>0;
 auto restoreCrew=[&](){for(auto e:c.crew)if(alive(e)){auto k=key+"disabled_"+std::to_string(e.packed());if(vars[k].toInt()==1)w.get<Unit>(e).flags&=~kUnitDisabled;vars[k]={};}};
 auto release=[&](){if(alive(prisoner)){w.get<Unit>(prisoner).flags&=~kUnitInCargo;if(get("was_possessed").raw)w.get<Unit>(prisoner).flags|=kUnitPossessed;if(!get("was_disabled").raw&&!dead)w.get<Unit>(prisoner).flags&=~kUnitDisabled;}
  vars["police_custody_"+suffix]={};vars["resident_transport_"+suffix]={};vars["resident_transport_aboard_"+suffix]={};
  for(unsigned pl=1;pl<gs.players.size();++pl){auto k="police_"+std::to_string(pl)+"_";if(vars[k+"car"].toInt()==int64_t(c.e.packed())+1)vars[k+"phase"]={};}
  vars[owned]={};phase(0);restoreCrew();};
 if(!alive(prisoner)||c.crew.empty()) {release();finish(sim,st,c,false);continue;}
 if(!state&&!dead&&kingston::policeArmed(sim,prisoner)){
  restoreCrew();
  // Armed suspects with ammunition cannot be handcuffed. Keep the combat call open.
  inc->assigned=sim.tick();inc->pos=v2(w.get<Transform>(prisoner).pos);c.taskUntil=sim.tick()+sim.tickRate()*3;
  if(sim.tick()%sim.tickRate()==0)for(auto e:c.crew){Order order;order.type=OrderType::Attack;order.target=prisoner;order.point=w.get<Transform>(prisoner).pos;m_->issue(sim,e,order,false);}
 }else{
  const auto claim=vars["police_custody_"+suffix];
  if(claim.raw&&claim.toInt()!=int64_t(c.e.packed())+1){resolve(sim,st,*inc);finish(sim,st,c,true);continue;}
  inc->assigned=sim.tick();inc->born=sim.tick();inc->state=2;
  auto lead=c.crew.front();auto at=w.get<Transform>(prisoner).pos;
  auto close=[&](Entity e,Vec3 q,Fixed r){return (w.get<Transform>(e).pos-q).lengthSq()<=r*r;};
  auto move=[&](Entity e,Vec3 q){if(auto*o=w.tryGet<Orders>(e))o->queue.clear();if(sim.tick()%10==0||!w.get<Mobile>(e).moving)m_->moveTo(sim,e,q,Fixed::fromRatio(12,100));};
  for(auto e:c.crew){auto&u=w.get<Unit>(e);auto k=key+"disabled_"+std::to_string(e.packed());if(!vars[k].raw)vars[k]=Fixed::fromInt((u.flags&kUnitDisabled)?2:1);u.flags|=kUnitDisabled;if(auto*a=w.tryGet<Combat>(e)){a->target=kNullEntity;a->chargeTarget=kNullEntity;}if(auto*o=w.tryGet<Orders>(e))o->queue.clear();}
  uint8_t player=0;if(auto*td=m_->topDown())if(auto*store=td->store(sim))for(size_t p=1;p<store->pilots.size();++p)if(store->pilots[p].hero==prisoner)player=uint8_t(p);
  auto playerPhase=[&](int p){if(player){vars["police_"+std::to_string(player)+"_phase"]=Fixed::fromInt(p);vars["police_"+std::to_string(player)+"_car"]=Fixed::fromInt(int64_t(c.e.packed())+1);}};
  if(!state){
   c.task=kTaskCrewOut;
   if(!close(lead,at,Fixed::fromRatio(9,10))){move(lead,at);continue;}
   if(!dead&&drv->isDriving(sim,prisoner)&&!drv->policeExit(sim,prisoner)){restoreCrew();continue;}
   if(!dead&&kingston::policeArmed(sim,prisoner)){restoreCrew();continue;}
   vars[owned]=kOne;set("prisoner",prisoner.packed());set("was_disabled",(w.get<Unit>(prisoner).flags&kUnitDisabled)!=0);set("was_possessed",(w.get<Unit>(prisoner).flags&kUnitPossessed)!=0);
   vars["police_custody_"+suffix]=Fixed::fromInt(int64_t(c.e.packed())+1);vars["resident_transport_"+suffix]=Fixed::fromInt(6);
   w.get<Unit>(prisoner).flags=(w.get<Unit>(prisoner).flags|kUnitDisabled)&~kUnitPossessed;m_->stopMoving(sim,prisoner);m_->stopMoving(sim,lead);phase(1);playerPhase(1);continue;
  }
  if(auto*o=w.tryGet<Orders>(prisoner))o->queue.clear();
  if(auto*a=w.tryGet<Combat>(prisoner)){a->target=kNullEntity;a->chargeTarget=kNullEntity;}
  const auto elapsed=int64_t(sim.tick())-get("since").toInt();
  if(state==1){c.task=kTaskWorking;m_->stopMoving(sim,lead);m_->stopMoving(sim,prisoner);
   if(elapsed>=sim.tickRate()*2){auto f=w.get<Unit>(c.e).facing;Vec3 side{-f.z,{},f.x};if((at-pos3).dot(side).raw<0)side=side*Fixed::fromInt(-1);
    Fixed length=Fixed::fromRatio(6,5),width=Fixed::fromRatio(1,2);for(const auto&v:drv->rules().vehicles)if(v.type==w.get<Unit>(c.e).type){length=v.length;width=v.width;}
    auto door=pos3+side*(width/2+Fixed::fromRatio(35,100))-f*(length/5);
    vars[key+"door_x"]=door.x;vars[key+"door_z"]=door.z;phase(2);playerPhase(2);
   }
  }else if(state==2||state==6){
   c.task=kTaskWorking;Vec3 goal=state==2?Vec3{get("door_x"),{},get("door_z")}:Vec3{get("entry_x"),{},get("entry_z")};
   move(lead,goal);auto leader=w.get<Transform>(lead).pos;
   if(dead){auto&tr=w.get<Transform>(prisoner);tr.prevPos=tr.pos;tr.pos=leader;w.get<Unit>(prisoner).facing=w.get<Unit>(lead).facing;}
   else move(prisoner,leader-w.get<Unit>(lead).facing*Fixed::fromRatio(2,5));
   for(size_t i=1;i<c.crew.size();++i)move(c.crew[i],leader-w.get<Unit>(lead).facing*Fixed::fromRatio(4,5));
   if(close(lead,goal,Fixed::fromRatio(3,10))&&close(prisoner,goal,Fixed::fromRatio(9,10))){m_->stopMoving(sim,lead);m_->stopMoving(sim,prisoner);phase(state==2?3:7);playerPhase(state==2?3:5);}
  }else if(state==3){
   c.task=kTaskWorking;vars["police_door_"+std::to_string(c.e.packed())]=kOne;
   if(elapsed>=sim.tickRate()*2){
    if(c.homePiece<0)continue;
    c.destPiece=c.homePiece;c.destAlong=c.homeAlong;c.task=kTaskHome;auto f=w.get<Unit>(c.e).facing;
    if(!reroute(sim,c,pos,{f.x,f.z})){c.task=kTaskWorking;continue;}
    w.get<Unit>(prisoner).flags|=kUnitInCargo;vars["resident_transport_aboard_"+suffix]=kOne;
    for(auto e:c.crew){m_->stopMoving(sim,e);w.get<Unit>(e).flags|=kUnitInCargo;}
    vars["police_door_"+std::to_string(c.e.packed())]={};phase(5);playerPhase(4);
   }
  }else if(state==5){
   c.task=kTaskHome;w.get<Transform>(prisoner).pos=pos3;for(auto e:c.crew){w.get<Transform>(e).pos=pos3;w.get<Unit>(e).flags|=kUnitInCargo;}
   if(slowNow&&atAddress()){
    Vec3 station=pos3;Fixed nearest=Fixed::fromInt(1000000);
    for(const auto&base:stations_[size_t(c.service)]){auto distance=(base.pos-pos).lengthSq();if(distance<nearest){nearest=distance;station=v3(base.pos);}}
    Vec3 entry{};bool found=false;nearest=Fixed::fromInt(1000000);
    for(int dz=-6;dz<=6;++dz)for(int dx=-6;dx<=6;++dx){Vec3 q{Fixed::fromInt(pos.x.floorInt()+dx)+Fixed::fromRatio(1,2),{},Fixed::fromInt(pos.y.floorInt()+dz)+Fixed::fromRatio(1,2)};
     int cell=gs.grid.cellOf(q);if(cell<0||!side_[size_t(cell)]||!gs.grid.passableAt(q,kLayerGround))continue;
     if((station-pos3).lengthSq().raw&&(q-pos3).dot(station-pos3).raw<=0)continue;
     auto distance=(q-station).lengthSq()+(q-pos3).lengthSq();if(distance<nearest){nearest=distance;entry=q;found=true;}}
    if(!found)continue;vars[key+"entry_x"]=entry.x;vars[key+"entry_z"]=entry.z;
    auto f=w.get<Unit>(c.e).facing;Vec3 side{-f.z,{},f.x};if((entry-pos3).dot(side).raw<0)side=side*Fixed::fromInt(-1);
    auto exit=pos3+side*(m_->rules().types[w.get<Unit>(c.e).type].radius+Fixed::fromRatio(2,5));if(!gs.grid.passableAt(exit,kLayerGround))continue;
    w.get<Unit>(prisoner).flags&=~kUnitInCargo;vars["resident_transport_aboard_"+suffix]={};w.get<Transform>(prisoner).pos=exit;
    for(size_t i=0;i<c.crew.size();++i){auto e=c.crew[i];w.get<Unit>(e).flags&=~kUnitInCargo;w.get<Transform>(e).pos=exit+f*Fixed::fromRatio(int(i)+1,3);}
    vars["police_door_"+std::to_string(c.e.packed())]=kOne;phase(6);playerPhase(5);
   }
  }else if(state==7){
   c.task=kTaskWorking;
   if(elapsed>=sim.tickRate()*3){
    const auto exit=w.get<Transform>(prisoner).pos;
    release();vars["police_door_"+std::to_string(c.e.packed())]={};
    if(player){
     if(dead){auto type=uint16_t(vars["police_original_"+suffix].toInt());auto replacement=m_->spawn(sim,type,player,exit);if(replacement.valid()){w.get<Unit>(replacement).flags|=kUnitPossessed;auto&p=m_->topDown()->store(sim)->pilots[player];p.hero=replacement;p.heroType=type;p.home=exit;p.input={};p.vel={};silentRemove(sim,prisoner);}}
     playerPhase(0);vars["police_"+std::to_string(player)+"_deliveries"]+=kOne;
    }else silentRemove(sim,prisoner);
    vars["police_dead_"+suffix]={};vars["police_delivered"]+=kOne;resolve(sim,st,*inc);finish(sim,st,c,true);
   }
  }
  continue;
 }
}
