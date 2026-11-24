// Opt-in rules `locomotion` (character-locomotion modes in the actor host): dash, wall run, grapple, save/restore,
// client prediction. Without the block the plain motor runs and no locomotion state or bytes exist.
void locomotionModes() {
    using lib::mechanics::LocomotionMode;
    auto open=Json::parse(R"({"players":[{"team":1},{"team":2}],"spawns":[[0,0.01,0],[-40,0.01,-40]],
        "colliders":[{"id":1,"min":[-100,-2,-100],"max":[100,0,100]},{"id":2,"min":[-2,10,8],"max":[2,12,12]}]})");
    auto wall=Json::parse(R"({"players":[{"team":1},{"team":2}],"spawns":[[2.6,0.01,-30],[-40,0.01,-40]],
        "colliders":[{"id":1,"min":[-100,-2,-100],"max":[100,0,100]},{"id":2,"min":[3,0,-40],"max":[4,8,40]}]})");
    auto plain=Json::parse(R"({"actor":{"tickRate":60,"mode":"deathmatch"}})");
    auto modes=Json::parse(R"({"actor":{"tickRate":60,"mode":"deathmatch"},"locomotion":{"airDashes":1}})");
    auto step=[](Simulation& sim,actor::Input i){i.sequence=sim.tick()+1;i.viewTick=sim.tick();auto c=actor::command(1,i);c.tick=sim.tick();sim.inject(c);sim.step();};
    auto mode=[](Simulation& sim){return fpsModule(sim).locomotion(sim,1)->state.mode;};
    actor::Input idle,dash;dash.buttons=actor::Dash;
    // Without the block: no state, Dash does nothing, the plain motor keeps the body still.
    auto sim=simulation(plain,open);CHECK(!fpsModule(*sim).locomotion(*sim,1));
    for(int i=0;i<10;++i)step(*sim,idle);
    auto before=character(*sim,1).pos;step(*sim,dash);CHECK(character(*sim,1).pos==before);
    // Invalid settings fail with the rules path.
    bool threw=false;
    try{actor::readConfig(Json::parse(R"({"actor":{},"locomotion":{"dashEndSpeedMilli":20000}})"));}
    catch(const JsonError& e){threw=std::string(e.what()).find("rules.locomotion.dashEndSpeedMilli")!=std::string::npos;}
    CHECK(threw);
    // Dash: one per press (held button), 14 m/s for 10 ticks along the view (yaw 0 = +z), the body stays on the ground.
    sim=simulation(modes,open);CHECK(fpsModule(*sim).locomotion(*sim,1));
    for(int i=0;i<10;++i)step(*sim,idle);CHECK(character(*sim,1).grounded);
    const auto start=character(*sim,1).pos;step(*sim,dash);CHECK(mode(*sim)==LocomotionMode::Dash);
    for(int i=0;i<9;++i)step(*sim,dash);
    auto moved=character(*sim,1).pos-start;
    CHECK(moved.z>2_fx&&moved.z<Fixed::fromRatio(26,10)&&fxAbs(moved.x)<Fixed::fromRatio(1,100)&&fxAbs(moved.y)<Fixed::fromRatio(1,10));
    for(int i=0;i<5;++i)step(*sim,dash);CHECK(mode(*sim)==LocomotionMode::Ground&&character(*sim,1).grounded);
    // Save mid-dash and restore: same checksum, same locomotion state.
    sim=simulation(modes,open);for(int i=0;i<10;++i)step(*sim,idle);step(*sim,dash);step(*sim,dash);
    auto bytes=sim->serialize();auto restored=simulation(modes,open);CHECK(restored->deserialize(bytes)&&restored->checksum()==sim->checksum());
    CHECK(mode(*restored)==LocomotionMode::Dash);
    for(int i=0;i<12;++i){step(*sim,idle);step(*restored,idle);CHECK(restored->checksum()==sim->checksum());}
    auto plainSim=simulation(plain,open);auto plainBytes=plainSim->serialize();CHECK(bytes.size()>plainBytes.size()); // only the block appends bytes
    // Wall run: sprint along the wall (x = 3), jump; the body runs along it and stays up far longer than a jump.
    sim=simulation(modes,wall);for(int i=0;i<10;++i)step(*sim,idle);
    actor::Input run;run.forward=1000;run.buttons=actor::Sprint;for(int i=0;i<40;++i)step(*sim,run);
    auto jump=run;jump.buttons|=actor::Jump;step(*sim,jump);
    bool ran=false;for(int i=0;i<5;++i){step(*sim,run);ran|=mode(*sim)==LocomotionMode::WallRun;}
    CHECK(ran);for(int i=0;i<40;++i)step(*sim,run);
    CHECK(mode(*sim)==LocomotionMode::WallRun&&character(*sim,1).pos.y>Fixed::fromRatio(1,2)&&!character(*sim,1).grounded);
    // Grapple (pull): aim up at the block, hold the button; the body is pulled up to the hook and lets go there.
    sim=simulation(modes,open);for(int i=0;i<10;++i)step(*sim,idle);
    actor::Input hook;hook.pitch=Fixed::fromRatio(12,100);hook.buttons=actor::Grapple;
    step(*sim,hook);CHECK(mode(*sim)==LocomotionMode::Grapple&&fpsModule(*sim).locomotion(*sim,1)->anchored);
    const auto anchor=fpsModule(*sim).locomotion(*sim,1)->anchor;CHECK(anchor.y>9_fx&&anchor.y<=10_fx);
    Fixed highest=character(*sim,1).pos.y;
    for(int i=0;i<90&&mode(*sim)==LocomotionMode::Grapple;++i){step(*sim,hook);highest=fxMax(highest,character(*sim,1).pos.y);}
    CHECK(mode(*sim)!=LocomotionMode::Grapple&&highest>6_fx&&!fpsModule(*sim).locomotion(*sim,1)->anchored);
    // Releasing the button lets go at once.
    sim=simulation(modes,open);for(int i=0;i<10;++i)step(*sim,idle);step(*sim,hook);step(*sim,hook);step(*sim,idle);
    CHECK(mode(*sim)!=LocomotionMode::Grapple);
    // Client prediction: the server sends the own locomotion bytes; the predicted pose and mode follow the server.
    for(const auto* rules:{&plain,&modes}){
        net::authority::Server server(*rules,open);auto pair=net::makeLoopbackPair({30,8,42});
        CHECK(server.attach(1,std::move(pair.first)));net::authority::Client client(*rules,open,std::move(pair.second));
        for(uint64_t now=0;now<4000;now+=5){
            client.poll(now);server.poll(now);
            if(now%20==0){
                actor::Input i;i.forward=now<2400?1000:0;
                if(now>=1000&&now<1100)i.buttons=actor::Dash;
                if(now>=1600&&now<1640)i.buttons|=actor::Jump;
                client.submit(i);server.step();
            }
        }
        for(uint64_t now=4000;now<4600;now+=5){client.poll(now);server.poll(now);if(now%20==0){client.submit({});server.step();}}
        CHECK(client.ready()&&server.rejected()==0);
        const auto& own=character(server.simulation(),1);
        CHECK((client.predicted().pos-own.pos).length()<Fixed::fromRatio(1,10));
        if(rules==&modes){
            CHECK(!client.snapshot().locomotion.empty());
            CHECK(client.predictedLocomotion().state.mode==fpsModule(server.simulation()).locomotion(server.simulation(),1)->state.mode);
            CHECK(own.pos.z>10_fx);
        }else CHECK(client.snapshot().locomotion.empty());
    }
}
