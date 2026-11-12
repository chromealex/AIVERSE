// Real RTS command, movement, damage, hash and save paths for opt-in projectile programs.
namespace {
std::string programRules(const std::string& flight, int speed=200, bool weapon=false) {
    std::string attack=weapon ? "\"weapons\":[{\"range\":30,\"cooldown\":100,\"damage\":10,\"projectileSpeed\":"+std::to_string(speed)+",\"flight\":"+flight+"}]" :
        "\"abilities\":[{\"id\":\"shot\",\"target\":\"point\",\"range\":30,\"cooldown\":100,\"autocast\":false,\"delivery\":{\"type\":\"projectile\",\"speed\":"+std::to_string(speed)+",\"flight\":"+flight+"},\"effects\":[{\"type\":\"damage\",\"amount\":10}]}]";
    return "{\"victory\":\"noUnits\",\"types\":{\"mage\":{\"health\":{\"max\":1000},"+attack+"},\"dummy\":{\"radius\":0.25,\"health\":{\"max\":1000}}}}";
}
std::unique_ptr<Simulation> programSim(const std::string& flight,const std::string& targets,int speed=200,bool weapon=false) {
    auto rules=programRules(flight,speed,weapon);
    return fxSim(rules.c_str(),targets);
}
void fireProgram(Simulation& s,bool weapon=false) {
    Entity caster=findType(s,"mage",1);
    if (!weapon) castAbility(s,caster,0,kNullEntity,P(30,10));
    else { Command c; c.player=1;c.type=game::kCmdAttack;c.units={caster};c.target=unitAt(s,8_fx,10_fx);s.submit(c); }
}
std::vector<Entity> programEntities(Simulation& s) {
    std::vector<Entity> result;
    s.world().each<game::Projectile>([&](Entity e,game::Projectile& p){if(p.program.enabled) result.push_back(e);});
    return result;
}
}
TEST(projectile_program_parser_rejects_invalid_data) {
    const std::vector<std::string> bad={
        "null", "[]", R"({"radius":-1})", R"({"maxDistance":0})", R"({"lifetime":121})",
        R"({"gravity":-1})", R"({"verticalSpeed":1025})", R"({"homing":1})", R"({"typo":1})", R"({"modifiers":{}})",
        R"({"modifiers":[{"kind":"unknown"}]})", R"({"modifiers":[{"kind":"split","count":1}]})",
        R"({"modifiers":[{"kind":"split","count":2.5}]})", R"({"modifiers":[{"kind":"split","after":1}]})",
        R"({"modifiers":[{"kind":"split","trigger":"wall"}]})", R"({"modifiers":[{"kind":"flyThrough","falloff":2}]})",
        R"({"modifiers":[{"kind":"ricochet","count":33}]})", R"({"modifiers":[{"kind":"speed","factor":0}]})"
    };
    for (const auto& j:bad) {
        bool rejected=false;try { game::loadRules(Json::parse(programRules(j))); } catch(const std::exception& e) { rejected=std::string(e.what()).find("flight")!=std::string::npos; }
        CHECK(rejected);
    }
    bool rejected=false;try {game::loadRules(Json::parse(programRules("{}",0,true)));}catch(const std::exception&){rejected=true;}
    CHECK(rejected);
    auto r=game::loadRules(Json::parse(programRules(R"({"modifiers":[{"kind":"split"},{"kind":"damage","factor":0.5}]})")));
    CHECK(!r.payloads.empty());
}
TEST(projectile_program_swept_flythrough_order_and_wall) {
    auto f=R"({"modifiers":[{"kind":"flyThrough","count":2,"falloff":0.5}]})";
    // Reverse entity order: collision order must follow entry distance, not ECS insertion order.
    auto s=programSim(f,dummy(12,10)+dummy(10,10)+dummy(8,10));
    fireProgram(*s);run(*s,0.2);
    CHECK(hpAt(*s,8,10)==990_fx);CHECK(hpAt(*s,10,10)==995_fx);CHECK(hpAt(*s,12,10)==Fixed::parse("997.5"));
    CHECK(programEntities(*s).empty());
    auto b=programSim(f,dummy(12,10)+dummy(8,10));
    auto& grid=mod(*b).state(*b).grid;grid.flags[size_t(grid.index(10,10))]=0;
    fireProgram(*b);run(*b,0.2);
    CHECK(hpAt(*b,8,10)==990_fx);CHECK(hpAt(*b,12,10)==1000_fx);
    CHECK(programEntities(*b).empty());
}
TEST(projectile_program_split_preserves_prefix_and_inherits_history) {
    auto s=programSim(R"({"modifiers":[{"kind":"split","count":2,"angle":0},{"kind":"damage","factor":0.5}]})",dummy(8,10)+dummy(12,10));
    fireProgram(*s);run(*s,0.3);
    CHECK(hpAt(*s,8,10)==990_fx); // children do not hit their parent's contact again
    CHECK(hpAt(*s,12,10)==990_fx); // two children, five damage each
    CHECK(programEntities(*s).empty());
}
TEST(projectile_program_ricochet_replacement_and_suffix_once) {
    auto s=programSim(R"({"modifiers":[{"kind":"ricochet","count":2,"radius":4},{"kind":"damage","factor":0.5}]})",dummy(8,10)+dummy(8,13)+dummy(5,13));
    fireProgram(*s);run(*s,0.5);
    CHECK(hpAt(*s,8,10)==990_fx);CHECK(hpAt(*s,8,13)==995_fx);CHECK(hpAt(*s,5,13)==995_fx);
    CHECK(programEntities(*s).empty());
}
TEST(projectile_program_ricochet_tie_break_and_no_target_fallback) {
    auto s=programSim(R"({"modifiers":[{"kind":"ricochet","count":1,"radius":4}]})",dummy(8,10)+dummy(8,13)+dummy(8,7));
    fireProgram(*s);run(*s,0.5);
    CHECK(hpAt(*s,8,13)==990_fx);CHECK(hpAt(*s,8,7)==1000_fx);
    auto b=programSim(R"({"modifiers":[{"kind":"ricochet","radius":0.1},{"kind":"split","angle":0},{"kind":"damage","factor":0.5}]})",dummy(8,10)+dummy(12,10));
    fireProgram(*b);run(*b,0.5);
    CHECK(hpAt(*b,8,10)==990_fx);CHECK(hpAt(*b,12,10)==990_fx);
}
TEST(projectile_program_timed_split_budget_range_and_lifetime) {
    auto f=R"({"splitBudget":2,"maxDistance":20,"lifetime":0.5,"modifiers":[{"kind":"split","trigger":"time","after":0.1,"angle":60},{"kind":"split","trigger":"time","after":0,"angle":60}]})";
    auto s=programSim(f,dummy(30,15),10);
    fireProgram(*s);run(*s,0.25);
    auto es=programEntities(*s);CHECK(es.size()==3);
    for(auto e:es) CHECK(s->world().get<game::Projectile>(e).program.splitBudget==0);
    run(*s,0.7);CHECK(programEntities(*s).empty());
    auto b=programSim(R"({"maxDistance":1})",dummy(8,10),200);
    fireProgram(*b);run(*b,0.2);CHECK(hpAt(*b,8,10)==1000_fx);CHECK(programEntities(*b).empty());
}
TEST(projectile_program_weapon_and_filters) {
    auto s=programSim(R"({"modifiers":[{"kind":"flyThrough","count":2}]})",dummy(8,10)+dummy(10,10,1)+dummy(12,10),200,true);
    fireProgram(*s,true);run(*s,0.2);
    CHECK(hpAt(*s,8,10)==990_fx);CHECK(hpAt(*s,10,10)==1000_fx);CHECK(hpAt(*s,12,10)==990_fx);
}
TEST(projectile_program_restore_midflight_and_hash) {
    auto f=R"({"homing":true,"modifiers":[{"kind":"flyThrough","count":2,"falloff":0.8},{"kind":"split","angle":0},{"kind":"ricochet","count":2,"radius":5},{"kind":"damage","factor":0.5}]})";
    auto targets=dummy(8,10)+dummy(12,10)+dummy(12,13);
    auto a=programSim(f,targets,10), b=programSim(f,targets,10);
    fireProgram(*a);run(*a,0.4); // save after contact and branching, with inherited hit history
    auto es=programEntities(*a);CHECK(!es.empty());
    SimSnapshot snapshot; a->saveState(snapshot);
    auto bytes=a->serialize();CHECK(!bytes.empty());std::string error;
    CHECK(b->deserialize(bytes,&error));CHECK(a->checksum()==b->checksum());
    for(int i=0;i<80;++i){a->step();b->step();CHECK(a->checksum()==b->checksum());}
    a->loadState(snapshot); run(*a,4); CHECK(a->checksum()==b->checksum());
    auto c=programSim(f,targets,10);fireProgram(*c);run(*c,0.2);
    auto ids=programEntities(*c);CHECK(!ids.empty());
    if(!ids.empty()){
        auto before=c->checksum(); auto& p=c->world().get<game::Projectile>(ids.front());
        p.program.splitBudget--;CHECK(c->checksum()!=before);
        before=c->checksum();p.program.hit.push_back(findType(*c,"dummy",2));CHECK(c->checksum()!=before);
        before=c->checksum();p.program.remaining[0]--;CHECK(c->checksum()!=before);
    }
}
TEST(projectile_program_endpoint_overlap_and_source_death) {
    auto s=programSim(R"({"maxDistance":2.8})",dummy(8,10));
    fireProgram(*s);run(*s,0.3);CHECK(hpAt(*s,8,10)==990_fx);
    auto b=programSim(R"({"modifiers":[{"kind":"split","angle":0},{"kind":"damage","factor":0.5}]})",dummy(8,10)+dummy(12,10)+dummy(3,15,1),10);
    fireProgram(*b);run(*b,0.2);
    b->world().get<game::Unit>(findType(*b,"mage",1)).hp=Fixed{};
    run(*b,1.5);CHECK(hpAt(*b,8,10)==990_fx);CHECK(hpAt(*b,12,10)==990_fx);
}
TEST(projectile_program_incompatible_delivery_and_changed_config_save) {
    auto json=Json::parse(programRules("{}"));
    auto& delivery=json.member("types").member("mage").member("abilities").at(0).member("delivery");
    delivery.set("arc",Json::fromBool(true));
    bool rejected=false;try{game::loadRules(json);}catch(const std::exception&){rejected=true;}CHECK(!rejected);
    delivery.set("arc",Json::fromBool(false));
    delivery.set("type",Json::string("instant"));
    rejected=false;try{game::loadRules(json);}catch(const std::exception&){rejected=true;}CHECK(rejected);
    auto a=programSim(R"({"modifiers":[{"kind":"split","angle":30}]})",dummy(15,10),10);
    fireProgram(*a);run(*a,0.2);CHECK(!programEntities(*a).empty());
    auto b=programSim(R"({"modifiers":[{"kind":"split","angle":60}]})",dummy(15,10),10);
    std::string error;CHECK(!b->deserialize(a->serialize(),&error));
    CHECK(error.find("checksum")!=std::string::npos);
}
TEST(projectile_program_loads_real_v4_projectile_save) {
    const std::string dir=std::string(AIVERSE_SOURCE_DIR)+"/tests/data/projectiles/";
    auto rules=Json::parseFile(dir+"v4-rules.json");auto map=Json::parseFile(dir+"v4-map.json");
    SimConfig cfg;cfg.seed=1;
    Simulation restored(cfg,std::make_unique<game::RtsModule>());restored.setup(rules,map);
    std::ifstream in(dir+"v4-shot.bin",std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(in)),{}),error;
    CHECK(!bytes.empty());CHECK(restored.deserialize(bytes,&error));
    CHECK(restored.world().count<game::Projectile>()==1);
    CHECK(programEntities(restored).empty());
    run(restored,10);CHECK(hpAt(restored,15,10)==990_fx);
    auto upgraded=restored.serialize();CHECK(!upgraded.empty());
    Simulation copy(cfg,std::make_unique<game::RtsModule>());copy.setup(rules,map);
    CHECK(copy.deserialize(upgraded,&error));CHECK(copy.checksum()==restored.checksum());
}

TEST(projectile_program_ricochet_overlapping_targets) {
    auto s=programSim(R"({"homing":true,"modifiers":[{"kind":"ricochet","count":1,"radius":4}]})",dummy(8,10)+dummy(8,10));
    fireProgram(*s);run(*s,0.4);
    int damaged=0;
    s->world().each<game::Unit,Owner>([&](Entity,game::Unit& u,Owner& o){if(o.player==2 && u.hp==990_fx) ++damaged;});
    CHECK(damaged==2);
}

TEST(projectile_program_ballistic_arc_and_restore) {
    auto json=Json::parse(programRules(R"({"modifiers":[{"kind":"split","trigger":"time","after":0.25,"angle":0},{"kind":"flyThrough","count":2}]})",10));
    auto& d=json.member("types").member("mage").member("abilities").at(0).member("delivery");
    d.set("arc",Json::fromBool(true));d.set("arcHeight",Json::number(int64_t(3)));
    auto make=[&](){return fxSim(json.dump().c_str(),dummy(10,10)+dummy(15,10));};
    auto a=make(),b=make();castAbility(*a,findType(*a,"mage",1),0,kNullEntity,P(15,10));run(*a,0.5);
    auto ids=programEntities(*a);CHECK(ids.size()==3);
    for(auto id:ids){auto& p=a->world().get<game::Projectile>(id);CHECK(p.program.ballistic);CHECK(p.program.gravity>Fixed{});CHECK(a->world().get<Transform>(id).pos.y>2_fx);}
    CHECK(hpAt(*a,10,10)==1000_fx); // shot passes above the midpoint unit
    CHECK(b->deserialize(a->serialize()));CHECK(a->checksum()==b->checksum());
    for(int i=0;i<50;++i){a->step();b->step();CHECK(a->checksum()==b->checksum());}
    CHECK(hpAt(*a,15,10)<1000_fx);CHECK(programEntities(*a).empty());
}
TEST(projectile_program_planet_seam_chains_and_save) {
    const auto map=R"({"players":[{"start":{"body":"p","face":0,"x":5,"y":5}},{"start":{"body":"p","face":4,"x":5,"y":5}}],
      "world":{"start":"p","bodies":[{"id":"p","kind":"planet","radius":20,"faceSize":16},{"id":"q","kind":"moon","radius":20,"faceSize":16}]},
      "entities":[{"type":"mage","owner":1,"at":{"body":"p","face":0,"x":5,"y":5}},
      {"type":"dummy","owner":2,"at":{"body":"p","face":4,"x":5,"y":5}},
      {"type":"dummy","owner":2,"at":{"body":"p","face":4,"x":6,"y":5}},
      {"type":"dummy","owner":2,"at":{"body":"q","face":4,"x":5,"y":5}}]})";
    for(const char* flight:{R"({"modifiers":[{"kind":"flyThrough","count":2}]})",
                            R"({"modifiers":[{"kind":"split","angle":0}]})",
                            R"({"modifiers":[{"kind":"ricochet","count":2,"radius":8}]})",
                            R"({"gravity":0.1,"verticalSpeed":0.1,"modifiers":[{"kind":"ricochet","count":2,"radius":8},{"kind":"flyThrough","count":2}]})"}) {
        auto rules=programRules(flight,10);auto a=makeSimJson(rules,map,2),b=makeSimJson(rules,map,2);
        const auto* surface=mod(*a).surface(*a);
        auto point=[](Fixed x,Fixed z,int body=0){return game::Surface::anchor(body)+fxNormalize(Vec3{x,Fixed{},z})*20_fx;};
        Entity caster=findType(*a,"mage",1);std::vector<Entity> targets;
        a->world().each<game::Unit,Owner>([&](Entity e,game::Unit&,Owner& o){if(o.player==2)targets.push_back(e);});
        auto put=[&](Entity e,Vec3 pos){a->world().get<Transform>(e).pos=pos;a->world().get<Transform>(e).prevPos=pos;};
        put(caster,point(1_fx,Fixed::fromRatio(7,10)));put(targets[0],point(1_fx,1_fx));put(targets[1],point(Fixed::fromRatio(7,10),1_fx));put(targets[2],point(1_fx,1_fx,1));
        castAbility(*a,caster,0,kNullEntity,point(Fixed::fromRatio(4,10),1_fx));run(*a,0.25);
        CHECK(!programEntities(*a).empty());
        for(auto id:programEntities(*a)){auto pos=a->world().get<Transform>(id).pos;CHECK(surface->bodyAt(pos)==0);CHECK(fxAbs(surface->altitude(pos)-Fixed::fromRatio(6,10))<(Json::parse(flight).has("gravity")?Fixed::fromRatio(1,10):Fixed::fromRatio(1,50)));}
        CHECK(b->deserialize(a->serialize()));
        for(int i=0;i<40;++i){a->step();b->step();CHECK(a->checksum()==b->checksum());}
        CHECK(a->world().get<game::Unit>(targets[0]).hp<1000_fx);CHECK(a->world().get<game::Unit>(targets[1]).hp<1000_fx);
        CHECK(a->world().get<game::Unit>(targets[2]).hp==1000_fx);
    }
}
