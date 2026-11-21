// fps.vehicleTypes: typed vehicles over vehicle-controllers (controller, seats, collision, passengers firing, saves).
void typedVehicles(const std::string& root) {
    auto types=Json::parseFile(root+"/../../libraries/genre/vehicles-sports/vehicles.json");
    auto rules=Json::parse(R"({"fps":{"tickRate":20,"mode":"deathmatch","mechanics":{"enabled":true}}})");
    Json only=Json::object();Json list=Json::array();list.push(types["vehicles"][0]);only.set("vehicles",list);
    rules.member("fps").set("vehicleTypes",only);
    auto map=Json::parse(R"({"players":[{"team":1},{"team":1},{"team":2}],"spawns":[[-1.5,0.01,5],[1.5,0.01,5],[30,0.01,-30]],
      "colliders":[{"id":1,"min":[-200,-2,-200],"max":[200,0,200]},{"id":2,"min":[-20,0,80],"max":[20,4,82]}],
      "vehicles":[{"pos":[0,0.01,5],"type":1},{"pos":[40,0.01,-40]}]})");
    auto sim=simulation(rules,map),twin=simulation(rules,map);auto& s=state(*sim);
    CHECK(s.vehicleRuns.size()==1&&s.vehicles.size()==2);
    uint32_t seq=0;
    auto send=[&](uint8_t slot,fps::Input i){i.sequence=++seq;i.viewTick=sim->tick();auto c=fps::command(slot,i);c.tick=sim->tick();sim->inject(c);twin->inject(c);};
    auto step=[&](int n){for(int i=0;i<n;++i){sim->step();twin->step();CHECK(sim->checksum()==twin->checksum());}};
    fps::Input enter;enter.action=fps::Action::Interact;
    send(1,enter);step(1);CHECK(s.actors[0].vehicle==s.vehicles[0].id&&s.vehicles[0].driver==1);
    send(2,enter);step(1);CHECK(s.actors[1].vehicle==s.vehicles[0].id);
    auto start=s.vehicles[0].pos;
    for(int i=0;i<80;++i){fps::Input d;d.forward=1000;d.buttons=fps::Fire;send(1,d);fps::Input p;p.buttons=fps::Fire;p.yaw=Fixed::fromRatio(1,4);send(2,p);step(1);
        if(i==40){auto bytes=sim->serialize();auto copy=simulation(rules,map);CHECK(copy->deserialize(bytes)&&copy->checksum()==sim->checksum());}}
    CHECK(s.vehicles[0].pos.z>start.z+10_fx);                       // drove forward (+Z at yaw 0)
    CHECK(s.actors[0].shots==0&&s.actors[1].shots>0);               // the driver steers, the passenger shoots
    CHECK((s.actors[1].pos-s.vehicles[0].pos).length()<3_fx);       // the passenger rides along
    for(int i=0;i<400;++i){fps::Input d;d.forward=1000;send(1,d);step(1);}
    CHECK(s.vehicles[0].pos.z<80_fx);                               // the wall stops the car
    for(int i=0;i<120;++i){fps::Input d;d.forward=-1000;send(1,d);step(1);}
    for(int i=0;i<400;++i){auto vz=s.vehicles[0].velocity.z;if(fxAbs(vz)<Fixed::fromRatio(1,2))break;fps::Input d;d.forward=vz>Fixed{}?-1000:1000;send(1,d);step(1);}   // brake to a stop: exits need a slow car
    send(1,enter);step(1);CHECK(s.actors[0].vehicle==0&&(s.actors[0].pos-s.vehicles[0].pos).length()<4_fx);
    CHECK(s.vehicles[0].driver==0||s.vehicles[0].driver==2);
}
// A map "race" (templates/racing, shared format docs/races.md): grid + countdown, race bots drive the typed cars over the race session, the race
// decides the round; twins stay bit-identical and a mid-race save restores into the same continuation.
void raceMode(const std::string& root) {
    auto dir=root+"/../../../templates/racing/";
    auto project=fps::readProject([&](const std::string& f,std::string& text){std::ifstream in(dir+f);if(!in)return false;text.assign(std::istreambuf_iterator<char>(in),{});return true;});
    auto rules=project.rules,map=project.map;
    auto sim=simulation(rules,map),twin=simulation(rules,map);auto& s=state(*sim);
    CHECK(s.race.enabled&&s.race.vehicleOf.size()==4&&s.vehicleRuns.size()==4);
    for(const auto& a:s.actors)CHECK(a.vehicle!=0);                                  // everybody starts at the wheel
    CHECK(s.race.session.phase==lib::genre::RacePhase::Countdown);
    auto grid=s.vehicles[0].pos;uint32_t seq=0;
    auto send=[&](fps::Input i){i.sequence=++seq;i.viewTick=sim->tick();auto c=fps::command(1,i);c.tick=sim->tick();sim->inject(c);twin->inject(c);};
    // The countdown holds the brakes even with the throttle floored; Interact never leaves the car.
    for(int i=0;i<170;++i){fps::Input d;d.forward=1000;if(i==5)d.action=fps::Action::Interact;send(d);sim->step();twin->step();}
    CHECK((s.vehicles[0].pos-grid).length()<Fixed::fromRatio(1,2)&&s.actors[0].vehicle==s.vehicles[0].id);
    CHECK(sim->checksum()==twin->checksum());
    auto view=static_cast<fps::Module&>(sim->module()).raceView(*sim,1);
    CHECK(view.enabled&&view.racers==4&&view.laps==3&&view.countdownTicks>0&&view.countdownTicks<=10);
    // Race: player 1 holds the throttle for a while (then lets go), the bots run three laps.
    bool restored=false;uint32_t lapEvents=0,lastLap=0;
    while(!s.winner&&sim->tick()<20000){
        if(sim->tick()<400){fps::Input d;d.forward=1000;send(d);}
        sim->step();twin->step();
        if(sim->tick()%97==0)CHECK(sim->checksum()==twin->checksum());
        uint32_t lap=0;for(const auto& st:s.race.session.standings)lap=std::max(lap,st.lap);if(lap>lastLap){++lapEvents;lastLap=lap;}
        if(!restored&&sim->tick()==2400){
            auto bytes=sim->serialize();auto copy=simulation(rules,map);CHECK(copy->deserialize(bytes)&&copy->checksum()==sim->checksum());
            for(int i=0;i<120;++i){sim->step();copy->step();twin->step();}
            CHECK(copy->checksum()==sim->checksum());restored=true;
        }
    }
    CHECK(restored&&s.winner>1&&s.winner<=4);                                         // a bot wins (player 1 stopped)
    CHECK(s.race.session.phase==lib::genre::RacePhase::Finished&&lapEvents>=3);
    const auto& top=s.race.session.standings.front();CHECK(top.finished&&int32_t(top.racer)==s.winner);
    CHECK(sim->tick()<20000&&sim->checksum()==twin->checksum());
    CHECK(s.vehicles[0].pos.z>grid.z+5_fx||(s.vehicles[0].pos-grid).length()>5_fx);    // player 1 did drive off the grid
    // Data errors (shared race format, docs/races.md) and a map without a race keeping the old save version.
    auto bad=map;bad.member("race").member("checkpoints").at(0).set("width",Json::number(int64_t(0)));
    try{simulation(rules,bad);CHECK(false);}catch(const JsonError& e){CHECK(std::string(e.what()).find("map race.checkpoints[0].width: must be positive")!=std::string::npos);}
    auto knobs=rules;knobs.member("race").set("pits",Json::fromBool(true));
    try{simulation(knobs,map);CHECK(false);}catch(const JsonError& e){CHECK(std::string(e.what()).find("race.pits: unknown key")!=std::string::npos);}
    auto plain=map;plain.erase("race");auto free=simulation(rules,plain);CHECK(!state(*free).race.enabled&&!static_cast<fps::Module&>(free->module()).racing());
}
