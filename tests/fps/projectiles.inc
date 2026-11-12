void projectilePrograms() {
    auto base=Json::parse(R"({"fps":{"tickRate":20,"mode":"deathmatch","weapon":{"damage":10,"intervalTicks":1000,"bulletSpeed":200,"bulletGravity":0,"flight":{}},"motor":{"gravity":1}}})");
    auto map=Json::parse(R"({"players":[{"team":1},{"team":2},{"team":2},{"team":2}],"spawns":[[0,0.01,0],[0,0.01,4],[0,0.01,8],[0,0.01,12]],"colliders":[{"id":1,"min":[-100,-2,-100],"max":[100,0,100]}]})");
    auto make=[&](const char* flight,Json m){auto r=base;r.member("fps").member("weapon").set("flight",Json::parse(flight));return simulation(r,m);};
    auto fire=[](Simulation& sim){fps::Input in;in.buttons=fps::Fire;in.sequence=1;auto cmd=fps::command(1,in);cmd.tick=sim.tick();sim.inject(cmd);sim.step();};
    auto step=[](Simulation& sim,int n){while(n--)sim.step();};
    auto sim=make(R"({"modifiers":[{"kind":"flyThrough","count":2,"falloff":0.5}]})",map);
    fire(*sim);step(*sim,4);CHECK(state(*sim).actors[1].health==90);CHECK(state(*sim).actors[2].health==95);CHECK(state(*sim).actors[3].health==98);
    CHECK(state(*sim).actors[0].ammo==29);CHECK(state(*sim).projectiles.empty());
    sim=make(R"({"modifiers":[{"kind":"split","count":2,"angle":0},{"kind":"damage","factor":0.5},{"kind":"flyThrough","count":1}]})",map);
    fire(*sim);step(*sim,8);CHECK(state(*sim).actors[1].health==90);CHECK(state(*sim).actors[2].health==90);CHECK(state(*sim).actors[3].health==90);
    auto corner=map;corner.set("spawns",Json::parse("[[0,0.01,0],[0,0.01,4],[3,0.01,4],[6,0.01,4]]"));
    sim=make(R"({"gravity":2,"modifiers":[{"kind":"ricochet","count":2,"radius":5},{"kind":"damage","factor":0.5}]})",corner);
    fire(*sim);step(*sim,1);CHECK(!state(*sim).projectiles.empty());CHECK(state(*sim).projectiles.front().program.verticalSpeed<Fixed{});step(*sim,7);CHECK(state(*sim).actors[1].health==90);CHECK(state(*sim).actors[2].health==95);CHECK(state(*sim).actors[3].health==95);
    auto cover=map;cover.member("colliders").push(Json::parse(R"({"id":2,"min":[-1,0,6],"max":[1,5,6.01]})"));
    sim=make(R"({"modifiers":[{"kind":"flyThrough","count":3}]})",cover);fire(*sim);step(*sim,4);
    CHECK(state(*sim).actors[1].health==90);CHECK(state(*sim).actors[2].health==100);
    // Ballistic branches retain vertical velocity; state restores after a timed split.
    auto ballistic=base;ballistic.member("fps").member("weapon").set("bulletSpeed",Json::number(int64_t(10)));
    ballistic.member("fps").member("weapon").set("flight",Json::parse(R"({"gravity":4,"verticalSpeed":4,"splitBudget":2,"modifiers":[{"kind":"split","trigger":"time","after":0.1,"angle":45},{"kind":"flyThrough","count":2}]})"));
    sim=simulation(ballistic,map);fire(*sim);step(*sim,3);CHECK(state(*sim).projectiles.size()==3);
    auto saved=sim->serialize();auto copy=simulation(ballistic,map);CHECK(copy->deserialize(saved));CHECK(copy->checksum()==sim->checksum());
    for(const auto& p:state(*sim).projectiles){CHECK(p.pos.y>1_fx);CHECK(p.program.verticalSpeed>3_fx);CHECK(p.program.splitBudget==0);}
    net::authority::Snapshot active;active.localSlot=1;active.actors=state(*sim).actors;active.projectiles=state(*sim).projectiles;
    net::authority::Snapshot view;CHECK(net::authority::decodeSnapshot(net::authority::encodeSnapshot(active),view));
    CHECK(view.projectiles.size()==3);CHECK(view.projectiles.front().pos==active.projectiles.front().pos);CHECK(!view.projectiles.front().program.enabled);
    for(int i=0;i<80;++i){sim->step();copy->step();CHECK(sim->checksum()==copy->checksum());}
    // Fully vertical swept shot hits an actor above, never a target merely aligned in XZ below.
    sim=make("{}",map);fire(*sim);auto& shot=state(*sim).projectiles.front();shot.pos={0_fx,5_fx,4_fx};shot.program.direction={0_fx,1_fx,0_fx};
    state(*sim).actors[2].pos={0_fx,8_fx,4_fx};step(*sim,1);
    CHECK(state(*sim).actors[1].health==100);CHECK(state(*sim).actors[2].health==90);
    // Network presentation keeps the existing wire format; private history is server-only.
    net::authority::Snapshot snapshot;snapshot.localSlot=1;snapshot.actors=state(*sim).actors;snapshot.projectiles=state(*sim).projectiles;
    net::authority::Snapshot decoded;CHECK(net::authority::decodeSnapshot(net::authority::encodeSnapshot(snapshot),decoded));
    bool rejected=false;try{auto bad=base;bad.member("fps").member("weapon").set("bulletSpeed",Json::number(int64_t(0)));fps::readConfig(bad);}catch(const std::exception&){rejected=true;}CHECK(rejected);
}
