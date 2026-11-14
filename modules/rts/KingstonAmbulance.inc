// Included inside Traffic::responders: uses the existing saved incident and lane route.
if (sv.carry && sv.answers == kIncidentInjured && inc &&
    (c.task==kTaskCrewOut || c.task==kTaskWorking || gs.trig.vars["ambulance_owned_"+std::to_string(c.e.packed())].raw)) {
    auto& vars=gs.trig.vars;const auto key="ambulance_"+std::to_string(c.e.packed())+"_";
    auto get=[&](const std::string& name){auto it=vars.find(key+name);return it==vars.end()?Fixed{}:it->second;};
    auto set=[&](const std::string& name,int64_t value){vars[key+name]=Fixed::fromInt(value);};
    auto phase=[&](int value){set("phase",value);set("since",sim.tick());};
    const auto owned="ambulance_owned_"+std::to_string(c.e.packed());
    int state=int(get("phase").toInt());
    if(!state){phase(1);state=1;set("patient",inc->subject.packed());vars[owned]=kOne;if(living(w,inc->subject))w.get<Unit>(inc->subject).flags&=~kUnitInCargo;}
    inc->born=sim.tick();inc->assigned=sim.tick(); // A reserved patient never expires during transport.
    auto patient=inc->subject;
    auto alive=[&](Entity e){return living(w,e);};
    if(!alive(patient)||c.crew.size()<2){
        if(alive(patient)){w.get<Unit>(patient).flags&=~kUnitInCargo;inc->pos=v2(w.get<Transform>(patient).pos);}
        for(unsigned pl=1;pl<gs.players.size();++pl){auto pk="medical_"+std::to_string(pl)+"_";
            auto it=vars.find(pk+"car");if(it==vars.end()||it->second.toInt()!=int64_t(c.e.packed())+1)continue;
            vars[pk+"phase"]=kOne;vars[pk+"medics_arrived"]={};vars[pk+"car"]={};
            if(alive(patient)){const auto p=w.get<Transform>(patient).pos;vars[pk+"ground_x"]=p.x;vars[pk+"ground_y"]={};vars[pk+"ground_z"]=p.z;}
        }
        vars["drive_door_"+std::to_string(c.e.packed())]={};
        vars[owned]={};phase(0);finish(sim,st,c,false);continue;
    }
    // Player recovery observes the same transport; its patient proxy is kept separate from the incident marker.
    std::string playerKey;
    for(unsigned pl=1;pl<gs.players.size();++pl){auto pk="medical_"+std::to_string(pl)+"_";
        auto it=vars.find(pk+"car");if(it!=vars.end()&&it->second.toInt()==int64_t(c.e.packed())+1){playerKey=pk;break;}}
    auto heroPatient=[&](){return playerKey.empty()?kNullEntity:Entity::fromPacked(uint64_t(vars[playerKey+"patient"].toInt()-1));};
    auto syncPlayer=[&](int p){if(playerKey.empty())return;vars[playerKey+"phase"]=Fixed::fromInt(p);
        auto e=heroPatient();if(alive(e)){w.get<Transform>(e).pos=w.get<Transform>(patient).pos;w.get<Unit>(e).flags|=kUnitInCargo;}};
    auto medic=c.crew[0],other=c.crew[1];
    auto close=[&](Entity e,Vec3 goal,Fixed radius){return (w.get<Transform>(e).pos-goal).lengthSq()<=radius*radius;};
    auto move=[&](Entity e,Vec3 goal){if(sim.tick()%10==0||!w.get<Mobile>(e).moving)m_->moveTo(sim,e,goal,Fixed::fromRatio(12,100));};
    auto stop=[&](){for(auto e:c.crew)m_->stopMoving(sim,e);};
    const auto elapsed=int64_t(sim.tick())-get("since").toInt();
    if(state==1){
        c.task=kTaskCrewOut;
        const auto at=w.get<Transform>(patient).pos;
        // Work from distinct sides of the patient, within arm reach rather than a full cell away.
        const auto facing=w.get<Unit>(patient).facing;Vec3 side{facing.z,{},-facing.x};
        const auto first=at+side*Fixed::fromRatio(8,25),second=at-side*Fixed::fromRatio(8,25);
        if(close(medic,first,Fixed::fromRatio(9,50))&&close(other,second,Fixed::fromRatio(9,50))){stop();phase(2);c.task=kTaskWorking;}
        else {move(medic,first);move(other,second);}
    }else if(state==2){
        c.task=kTaskWorking;stop();
        for(auto e:c.crew){auto toward=w.get<Transform>(patient).pos-w.get<Transform>(e).pos;if(toward.length().raw>0)w.get<Unit>(e).facing=toward/toward.length();}
        if(!close(medic,w.get<Transform>(patient).pos,Fixed::fromInt(1))||!close(other,w.get<Transform>(patient).pos,Fixed::fromInt(1))){phase(1);continue;}
        if(elapsed>=std::max(sv.workTicks,sim.tickRate()*4u)){
            w.get<Unit>(patient).flags|=kUnitInCargo;phase(3);syncPlayer(2);
        }
    }else if(state==3 || state==6){
        c.task=kTaskWorking;
        Vec3 goal;
        if(state==3){
            Fixed length=Fixed::fromRatio(3,2);
            for(const auto& def:drv->rules().vehicles)if(def.type==w.get<Unit>(c.e).type)length=def.length;
            goal=pos3-w.get<Unit>(c.e).facing*(length/2+Fixed::fromRatio(2,5));
        }else goal={get("entrance_x"),{},get("entrance_z")};
        move(medic,goal);
        const auto lead=w.get<Transform>(medic).pos;
        move(other,lead-w.get<Unit>(medic).facing*Fixed::fromRatio(45,100));
        auto& tr=w.get<Transform>(patient);tr.prevPos=tr.pos;tr.pos=lead-w.get<Unit>(medic).facing*Fixed::fromRatio(23,100);tr.pos.y+=Fixed::fromRatio(1,5);
        vars[key+"x"]=tr.pos.x;vars[key+"z"]=tr.pos.z;
        auto direction=w.get<Unit>(medic).facing;vars[key+"fx"]=direction.x;vars[key+"fz"]=direction.z;
        syncPlayer(2);
        if(close(medic,goal,Fixed::fromRatio(3,10))&&close(other,goal,Fixed::fromRatio(9,10))){stop();phase(state==3?4:7);}
    }else if(state==4){
        c.task=kTaskWorking;stop();
        if(elapsed>=sim.tickRate()*3){
            // Prefer a hospital ward over the clinic from which the crew dispatched.
            const Station* ward=nullptr;Fixed nearest=Fixed::fromInt(1000000);
            for(const auto& station:stations_[size_t(c.service)]){
                bool hospital=false;
                w.each<Unit,Transform>([&](Entity,const Unit& unit,const Transform& tr){
                    if(m_->rules().types[unit.type].id.find("hospital")!=std::string::npos && (v2(tr.pos)-station.pos).lengthSq()<Fixed::fromInt(1))hospital=true;
                });
                const auto distance=(station.pos-pos).lengthSq();if(hospital&&distance<nearest){nearest=distance;ward=&station;}
            }
            if(ward){c.homePiece=ward->piece;c.homeAlong=ward->along;}
            set("hospital_selected",ward?1:0);
            if(c.homePiece>=0){auto goal=tl::trafficLanePoint(net_,net_.pieces[size_t(c.homePiece)],1,0,c.homeAlong);vars[key+"destination_x"]=goal.x;vars[key+"destination_z"]=goal.y;}
            if(c.homePiece<0){phase(3);continue;} // A real medical facility is required.
            c.destPiece=c.homePiece;c.destAlong=c.homeAlong;c.task=kTaskHome;
            const auto facing=w.get<Unit>(c.e).facing;
            if(!reroute(sim,c,pos,{facing.x,facing.z})){c.task=kTaskWorking;continue;}
            for(auto e:c.crew)w.get<Unit>(e).flags|=kUnitInCargo;
            vars["drive_door_"+std::to_string(c.e.packed())]={};phase(5);syncPlayer(3);
        }
    }else if(state==5){
        c.task=kTaskHome;
        w.get<Transform>(patient).pos=pos3;
        for(auto e:c.crew){w.get<Transform>(e).pos=pos3;w.get<Unit>(e).flags|=kUnitInCargo;}
        syncPlayer(3);
        if(slowNow&&atAddress()){
            Vec3 hospital=pos3;Fixed best=Fixed::fromInt(1000000);
            for(const auto& station:stations_[size_t(c.service)]){auto d=(station.pos-pos).lengthSq();if(d<best){best=d;hospital=v3(station.pos);}}
            Vec3 entrance{};bool found=false;best=Fixed::fromInt(1000000);
            for(int dz=-6;dz<=6;++dz)for(int dx=-6;dx<=6;++dx){
                Vec3 p{Fixed::fromInt(pos3.x.floorInt()+dx)+Fixed::fromRatio(1,2),{},Fixed::fromInt(pos3.z.floorInt()+dz)+Fixed::fromRatio(1,2)};
                int cell=gs.grid.cellOf(p);if(cell<0||!side_[size_t(cell)]||!gs.grid.passableAt(p,kLayerGround))continue;
                if((hospital-pos3).lengthSq().raw&&(p-pos3).dot(hospital-pos3).raw<=0)continue;
                auto d=(p-hospital).lengthSq()+(p-pos3).lengthSq();if(d<best){best=d;entrance=p;found=true;}
            }
            if(!found)continue;
            vars[key+"entrance_x"]=entrance.x;vars[key+"entrance_z"]=entrance.z;
            auto f=w.get<Unit>(c.e).facing;Vec3 right{-f.z,{},f.x};if((entrance-pos3).dot(right).raw<0)right=right*Fixed::fromInt(-1);
            const auto clearance=m_->rules().types[w.get<Unit>(c.e).type].radius+Fixed::fromRatio(2,5);
            Vec3 exit=pos3+right*clearance;
            if(!gs.grid.passableAt(exit,kLayerGround))continue;
            for(size_t i=0;i<c.crew.size();++i){auto e=c.crew[i];w.get<Unit>(e).flags&=~kUnitInCargo;
                auto& tr=w.get<Transform>(e);tr.pos=exit+f*Fixed::fromRatio(int(i),3);tr.prevPos=tr.pos;}
            c.task=kTaskWorking;phase(6);
        }
    }else if(state==7){
        c.task=kTaskWorking;stop();
        if(elapsed>=sim.tickRate()*2){
            if(!playerKey.empty()){
                vars[playerKey+"phase"]=Fixed::fromInt(4);vars[playerKey+"delivered"]=kOne;
                vars[playerKey+"release"]=Fixed::fromInt(sim.tick()+sim.tickRate()*8);
                vars[playerKey+"exit_x"]=get("entrance_x");vars[playerKey+"exit_z"]=get("entrance_z");
            }
            resolve(sim,st,*inc);vars[owned]={};phase(0);
            vars["drive_door_"+std::to_string(c.e.packed())]={};
            vars["ambulance_delivered"]+=kOne;finish(sim,st,c,true);
        }
    }
    continue;
}
