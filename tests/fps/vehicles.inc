// fps.vehicleTypes: typed vehicles over vehicle-controllers (controller, seats, collision, passengers firing, saves).
void typedVehicles(const std::string& root) {
    auto types=Json::parseFile(root+"/../../libraries/mechanics/vehicles-sports/vehicles.json");
    auto rules=Json::parse(R"({"actor":{"tickRate":20,"mode":"deathmatch","mechanics":{"enabled":true}}})");
    Json only=Json::object();Json list=Json::array();list.push(types["vehicles"][0]);only.set("vehicles",list);
    rules.member("actor").set("vehicleTypes",only);
    auto map=Json::parse(R"({"players":[{"team":1},{"team":1},{"team":2}],"spawns":[[-1.5,0.01,5],[1.5,0.01,5],[30,0.01,-30]],
      "colliders":[{"id":1,"min":[-200,-2,-200],"max":[200,0,200]},{"id":2,"min":[-20,0,80],"max":[20,4,82]}],
      "vehicles":[{"pos":[0,0.01,5],"type":1},{"pos":[40,0.01,-40]}]})");
    auto sim=simulation(rules,map),twin=simulation(rules,map);
    CHECK(sim->world().countExisting<vehicles::VehicleDrive>()==1&&vehiclesOf(*sim).size()==2);
    uint32_t seq=0;
    auto send=[&](uint8_t slot,actor::Input i){i.sequence=++seq;i.viewTick=sim->tick();auto c=actor::command(slot,i);c.tick=sim->tick();sim->inject(c);twin->inject(c);};
    auto step=[&](int n){for(int i=0;i<n;++i){sim->step();twin->step();CHECK(sim->checksum()==twin->checksum());}};
    actor::Input enter;enter.action=actor::Action::Interact;
    send(1,enter);step(1);CHECK(character(*sim,1).vehicle==vehiclesOf(*sim)[0].id&&vehiclesOf(*sim)[0].driver==1);
    send(2,enter);step(1);CHECK(character(*sim,2).vehicle==vehiclesOf(*sim)[0].id);
    auto start=vehiclesOf(*sim)[0].pos;
    for(int i=0;i<80;++i){actor::Input d;d.forward=1000;d.buttons=actor::Fire;send(1,d);actor::Input p;p.buttons=actor::Fire;p.yaw=Fixed::fromRatio(1,4);send(2,p);step(1);
        if(i==40){auto bytes=sim->serialize();auto copy=simulation(rules,map);CHECK(copy->deserialize(bytes)&&copy->checksum()==sim->checksum());}}
    CHECK(vehiclesOf(*sim)[0].pos.z>start.z+10_fx);                       // drove forward (+Z at yaw 0)
    CHECK(character(*sim,1).shots==0&&character(*sim,2).shots>0);               // the driver steers, the passenger shoots
    CHECK((character(*sim,2).pos-vehiclesOf(*sim)[0].pos).length()<3_fx);       // the passenger rides along
    for(int i=0;i<400;++i){actor::Input d;d.forward=1000;send(1,d);step(1);}
    CHECK(vehiclesOf(*sim)[0].pos.z<80_fx);                               // the wall stops the car
    for(int i=0;i<120;++i){actor::Input d;d.forward=-1000;send(1,d);step(1);}
    for(int i=0;i<400;++i){auto vz=vehiclesOf(*sim)[0].velocity.z;if(fxAbs(vz)<Fixed::fromRatio(1,2))break;actor::Input d;d.forward=vz>Fixed{}?-1000:1000;send(1,d);step(1);}   // brake to a stop: exits need a slow car
    send(1,enter);step(1);CHECK(character(*sim,1).vehicle==0&&(character(*sim,1).pos-vehiclesOf(*sim)[0].pos).length()<4_fx);
    CHECK(vehiclesOf(*sim)[0].driver==0||vehiclesOf(*sim)[0].driver==2);
}
// A map "race" (templates/racing, shared format docs/races.md): grid + countdown, race bots drive the typed cars over the race session, the race
// decides the round; twins stay bit-identical and a mid-race save restores into the same continuation.
// ECS shape (one vehicle system, engine/vehicles): the vehicles::RaceState resource holds the session and racer i's car
// (racer i = slot i), the cars are Vehicle + vehicles::VehicleDrive entities driven by their character.
void raceMode(const std::string& root) {
    auto dir=std::string(AIVERSE_TEMPLATES_DIR)+"/racing/";
    auto project=actor::readProject([&](const std::string& f,std::string& text){std::ifstream in(dir+f);if(!in)return false;text.assign(std::istreambuf_iterator<char>(in),{});return true;});
    auto rules=project.rules,map=project.map;
    auto sim=simulation(rules,map),twin=simulation(rules,map);auto S=[&]()->actor::State&{return state(*sim);};auto R=[&]()->const vehicles::RaceState&{return *vehicles::VehicleRace::state(static_cast<const World&>(sim->world()));};
    const auto& module=fpsModule(*sim);
    CHECK(module.racing()&&R().started&&module.characterCount(*sim)==4&&sim->world().countExisting<vehicles::VehicleDrive>()==4);
    for(uint8_t k=1;k<=4;++k){                                                        // everybody starts at the wheel of its own car
        const Entity car=R().racers.at(k-1);const auto* drive=vehicles::driveOf(static_cast<const World&>(sim->world()),car);
        CHECK(drive&&drive->driver==module.characterEntity(*sim,k)&&drive->locked&&character(*sim,k).vehicle==sim->world().tryGetConst<actor::Vehicle>(car)->id);
    }
    CHECK(R().session.phase==lib::mechanics::RacePhase::Countdown);
    auto grid=vehiclesOf(*sim)[0].pos;uint32_t seq=0;
    auto send=[&](actor::Input i){i.sequence=++seq;i.viewTick=sim->tick();auto c=actor::command(1,i);c.tick=sim->tick();sim->inject(c);twin->inject(c);};
    // The countdown holds the brakes even with the throttle floored; Interact never leaves the car.
    for(int i=0;i<170;++i){actor::Input d;d.forward=1000;if(i==5)d.action=actor::Action::Interact;send(d);sim->step();twin->step();}
    CHECK((vehiclesOf(*sim)[0].pos-grid).length()<Fixed::fromRatio(1,2)&&character(*sim,1).vehicle==vehiclesOf(*sim)[0].id);
    CHECK(sim->checksum()==twin->checksum());
    auto view=fpsModule(*sim).raceView(*sim,1);
    CHECK(view.enabled&&view.racers==4&&view.laps==3&&view.countdownTicks>0&&view.countdownTicks<=10);
    // Race: player 1 holds the throttle for a while (then lets go), the bots run three laps.
    bool restored=false;uint32_t lapEvents=0,lastLap=0;
    while(!S().winner&&sim->tick()<20000){
        if(sim->tick()<400){actor::Input d;d.forward=1000;send(d);}
        sim->step();twin->step();
        if(sim->tick()%97==0)CHECK(sim->checksum()==twin->checksum());
        uint32_t lap=0;for(const auto& st:R().session.standings)lap=std::max(lap,st.lap);if(lap>lastLap){++lapEvents;lastLap=lap;}
        if(!restored&&sim->tick()==2400){
            auto bytes=sim->serialize();auto copy=simulation(rules,map);CHECK(copy->deserialize(bytes)&&copy->checksum()==sim->checksum());
            for(int i=0;i<120;++i){sim->step();copy->step();twin->step();}
            CHECK(copy->checksum()==sim->checksum());restored=true;
        }
    }
    CHECK(restored&&S().winner>1&&S().winner<=4);                                     // a bot wins (player 1 stopped)
    CHECK(R().session.phase==lib::mechanics::RacePhase::Finished&&lapEvents>=3);
    const auto& top=R().session.standings.front();CHECK(top.finished&&int32_t(top.racer)==S().winner);
    CHECK(sim->tick()<20000&&sim->checksum()==twin->checksum());
    CHECK(vehiclesOf(*sim)[0].pos.z>grid.z+5_fx||(vehiclesOf(*sim)[0].pos-grid).length()>5_fx);   // player 1 did drive off the grid
    // Data errors (shared race format, docs/races.md) and a map without a race (no RaceState).
    auto bad=map;bad.member("race").member("checkpoints").at(0).set("width",Json::number(int64_t(0)));
    try{simulation(rules,bad);CHECK(false);}catch(const JsonError& e){CHECK(std::string(e.what()).find("map race.checkpoints[0].width: must be positive")!=std::string::npos);}
    auto knobs=rules;knobs.member("race").set("pits",Json::fromBool(true));
    try{simulation(knobs,map);CHECK(false);}catch(const JsonError& e){CHECK(std::string(e.what()).find("race.pits: unknown key")!=std::string::npos);}
    auto plain=map;plain.erase("race");auto free=simulation(rules,plain);
    CHECK(!fpsModule(*free).racing()&&!vehicles::VehicleRace::state(static_cast<const World&>(free->world())));
}
