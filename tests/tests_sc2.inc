// StarCraft II-style group and production mechanics (agent-sc2): magic box moves, overkill-aware targeting,
// warp-in at a power field. Included after tests_mech.inc (uses its helpers). All are off unless the rules ask.

static std::vector<Entity> ownUnits(Simulation& sim, uint8_t player) {
    std::vector<Entity> us;
    sim.world().each<game::Unit, Owner>([&](Entity e, game::Unit&, Owner& o) { if (o.player == player) us.push_back(e); });
    return us;
}

TEST(sc2_magic_box) {
    auto rulesWith = [](const char* box) {
        return std::string(R"({"resources": {"gold": {}}, )") + kVic + R"(, "movement": {"magicBox": )" + box + R"(}, "types": {
            "foot": {"move": {"speed": 3, "radius": 0.3}, "health": {"max": 100}}}})";
    };
    const char* ents = R"(,"entities":[{"type":"foot","owner":1,"pos":[4,4]},{"type":"foot","owner":1,"pos":[7,4]},{"type":"foot","owner":1,"pos":[4,7]},{"type":"foot","owner":1,"pos":[7,7]}])";
    const std::string map = flatMap(48, 24, ents);
    auto spreadAfterMove = [&](const std::string& rules, Vec3 goal) {
        auto sim = makeSimJson(rules, map);
        auto us = ownUnits(*sim, 1);
        std::vector<Vec3> start;
        for (Entity e : us) start.push_back(posOf(*sim, e));
        sim->submit(mechCmd(1, game::kCmdMove, us, kNullEntity, goal));
        run(*sim, 15);
        // how far each unit ended from (goal + its starting offset from the group centre (5.5, 5.5))
        // how far the arrived shape is from the starting one (offsets from the first unit), and that it is at the goal
        Fixed worst{};
        Vec3 c{};
        for (size_t i = 0; i < us.size(); ++i) {
            c += posOf(*sim, us[i]);
            worst = fxMax(worst, dist2DTest(posOf(*sim, us[i]) - posOf(*sim, us[0]), start[i] - start[0]));
        }
        c = c / Fixed::fromInt(int64_t(us.size()));
        if (dist2DTest(c, goal) > Fixed::fromInt(2)) worst = Fixed::fromInt(99);
        return worst;
    };
    // on: the 3 x 3 square arrives as a 3 x 3 square
    CHECK(spreadAfterMove(rulesWith("true"), at(36, 12)) < Fixed::fromRatio(1, 2));
    // off (default) and a group wider than the radius: they converge on the point instead
    CHECK(spreadAfterMove(rulesWith("false"), at(36, 12)) > Fixed::fromInt(1));
    CHECK(spreadAfterMove(rulesWith("1"), at(36, 12)) > Fixed::fromInt(1));
    // a goal inside the group: no box (units gather at the point)
    {
        auto sim = makeSimJson(rulesWith("true"), map);
        auto us = ownUnits(*sim, 1);
        sim->submit(mechCmd(1, game::kCmdMove, us, kNullEntity, at(5, 5)));
        run(*sim, 6);
        for (Entity e : us) CHECK(dist2DTest(posOf(*sim, e), at(5, 5)) < Fixed::fromRatio(3, 2));
    }
    // save / load stays identical mid-move
    {
        auto sim = makeSimJson(rulesWith("true"), map);
        sim->submit(mechCmd(1, game::kCmdMove, ownUnits(*sim, 1), kNullEntity, at(36, 12)));
        run(*sim, 1);
        CHECK(mechRoundTrip(*sim, rulesWith("true"), map));
    }
    // bad value: readable error
    bool threw = false;
    try { auto s = makeSimJson(rulesWith("-2"), map); } catch (const std::exception& e) { threw = std::string(e.what()).find("magicBox") != std::string::npos; }
    CHECK(threw);
}

TEST(unit_type_selection_eligibility) {
    const std::string rules = R"({"types":{"unit":{},"scenery":{"selectable":false}}})";
    auto sim = makeSimJson(rules, flatMap(24, 24, R"(,"entities":[{"type":"scenery","owner":1,"pos":[5,5]}])"));
    const auto& types = mod(*sim).rules();
    game::SceneObject front, behind;
    front.entity = findType(*sim, "scenery", 1);
    front.type = types.findType("scenery");
    behind.type = types.findType("unit");
    CHECK(sim->world().alive(front.entity));
    CHECK(!front.selectable(types));
    CHECK(behind.selectable(types));
    behind.isProjectile = true;
    CHECK(!behind.selectable(types));
    behind.isProjectile = false;
    behind.visible = false;
    CHECK(!behind.selectable(types));
    bool rejected = false;
    try { makeSimJson(R"({"types":{"bad":{"selectable":"false"}}})", flatMap(12, 12, "")); }
    catch (const std::exception& error) { rejected = std::string(error.what()).find("selectable") != std::string::npos; }
    CHECK(rejected);
}

TEST(automatic_target_acquisition_order) {
    auto rulesWith = [](const std::string& order) {
        return std::string("{") + kVic + order + R"(,"types":{
            "gun":{"sight":20,"weapons":[{"range":20,"damage":1,"cooldown":5}]},
            "prop":{"selectable":false},"decoy":{"invulnerable":true}}})";
    };
    const std::string map = flatMap(32, 24, R"(,"entities":[
        {"type":"gun","owner":1,"pos":[8,10]},
        {"type":"gun","owner":2,"pos":[9,10]},
        {"type":"prop","owner":2,"pos":[4,2]},
        {"type":"decoy","owner":2,"pos":[5,2]},
        {"type":"gun","owner":2,"pos":[14,5]},
        {"type":"gun","owner":2,"pos":[12,5]}])");
    auto acquired = [&](const std::string& rules, int x, int z) {
        auto sim = makeSimJson(rules, map);
        run(*sim, 0.4);
        const Entity gun = findType(*sim, "gun", 1);
        const Entity target = sim->world().get<game::Combat>(gun).target;
        CHECK(sim->world().alive(target));
        if (sim->world().alive(target)) {
            const Vec3 pos = posOf(*sim, target);
            CHECK(pos.x.floorInt() == x);
            CHECK(pos.z.floorInt() == z);
        }
        CHECK(mechRoundTrip(*sim, rules, map));
    };
    acquired(rulesWith(""), 9, 10);
    acquired(rulesWith(R"(,"targeting":{"acquireOrder":"nearest"})"), 9, 10);
    acquired(rulesWith(R"(,"targeting":{"acquireOrder":"rowMajor"})"), 12, 5);
    bool rejected = false;
    try { makeSimJson(rulesWith(R"(,"targeting":{"acquireOrder":"random"})"), map); }
    catch (const std::exception& error) { rejected = std::string(error.what()).find("targeting.acquireOrder") != std::string::npos; }
    CHECK(rejected);
}

TEST(sc2_overkill_targeting) {
    // four slow-shot gunners against four 10 hp targets: one 12-damage shot kills a target.
    auto rulesWith = [](bool on) {
        return std::string(R"({"resources": {"gold": {}}, )") + kVic + (on ? R"(, "targeting": {"overkill": true})" : "") + R"(, "types": {
            "gun":  {"move": {"speed": 0}, "sight": 16, "health": {"max": 100},
                     "weapons": [{"range": 16, "damage": 12, "cooldown": 3, "projectileSpeed": 6}]},
            "dummy": {"move": {"speed": 0}, "health": {"max": 10}}}})";
    };
    const std::string map = flatMap(40, 20, R"(,"entities":[{"type":"gun","owner":1,"pos":[4,8]},{"type":"gun","owner":1,"pos":[4,9]},{"type":"gun","owner":1,"pos":[4,10]},{"type":"gun","owner":1,"pos":[4,11]},
        {"type":"dummy","owner":2,"pos":[12,10]},{"type":"dummy","owner":2,"pos":[13,10]},{"type":"dummy","owner":2,"pos":[14,10]},{"type":"dummy","owner":2,"pos":[15,10]}])");
    auto killedBy = [&](bool on, double seconds) {
        auto sim = makeSimJson(rulesWith(on), map);
        run(*sim, seconds);
        return 4 - countType(*sim, "dummy", 2);
    };
    // with overkill avoidance the first volley spreads over the four targets: all dead before the second volley (3 s)
    CHECK(killedBy(true, 2.8) == 4);
    // without it every gunner shoots the nearest target and three shots are wasted
    CHECK(killedBy(false, 2.8) == 1);
    auto sim = makeSimJson(rulesWith(true), map);
    run(*sim, 1);
    CHECK(mechRoundTrip(*sim, rulesWith(true), map));
}

TEST(sc2_warp_in) {
    std::string rules = std::string(R"({"resources": {"gold": {"start": 500}}, )") + kVic + R"(, "types": {
        "pylon": {"structure": {"footprint": [2, 2]}, "health": {"max": 200}, "powerField": {"id": 0, "radius": 6}},
        "gate":  {"structure": {"footprint": [3, 3]}, "health": {"max": 500}, "produces": ["zealot"],
                  "warpIn": {"field": 0, "time": 4}, "charges": {"max": 1, "interval": 20}},
        "zealot": {"move": {"speed": 3}, "health": {"max": 100}, "cost": {"gold": 100, "time": 27}}}})";
    const std::string map = flatMap(48, 24, R"(,"fogOfWar": {"mode": "none"},"entities":[{"type":"gate","owner":1,"pos":[5,5]},{"type":"pylon","owner":1,"pos":[30,12]}])");
    auto sim = makeSimJson(rules, map);
    World& w = sim->world();
    Entity gate = findType(*sim, "gate", 1);
    run(*sim, 21);   // the gate's charge is ready
    auto warp = [&](Vec3 p) { Command c = mechCmd(1, game::kCmdTrain, {gate}, kNullEntity, p, mod(*sim).rules().findType("zealot")); c.flags = kCmdAtPoint; sim->submit(c); };
    warp(at(40, 20));   // outside the field: ignored, nothing paid
    run(*sim, 0.2);
    CHECK(countType(*sim, "zealot", 1, false) == 0 && stockOf(*sim, 1, "gold") == Fixed::fromInt(500));
    warp(at(33, 13));   // inside: the zealot appears there at once, warping
    run(*sim, 0.2);
    Entity z = findType(*sim, "zealot", 1);
    CHECK(z.valid() && dist2DTest(posOf(*sim, z), at(33, 13)) < Fixed::fromRatio(1, 2));
    CHECK(z.valid() && (w.get<game::Unit>(z).flags & game::kUnitUnderConstruction));
    CHECK(stockOf(*sim, 1, "gold") == Fixed::fromInt(400));
    warp(at(32, 12));   // the charge is spent: a second warp waits for the cooldown
    run(*sim, 0.2);
    CHECK(countType(*sim, "zealot", 1, false) == 1);
    run(*sim, 4.2);     // warpIn.time, not the 27 s build time
    CHECK(z.valid() && !(w.get<game::Unit>(z).flags & game::kUnitUnderConstruction) && hpOf(*sim, z) >= Fixed::fromInt(99));
    // a plain train order (no kCmdAtPoint) still queues at the gate
    CHECK(mechRoundTrip(*sim, rules, map));
    // bad block: readable error
    bool threw = false;
    try { auto s = makeSimJson(R"({"resources": {"gold": {}}, "types": {"g": {"warpIn": 3}}})", map); } catch (const std::exception& e) { threw = std::string(e.what()).find("warpIn") != std::string::npos; }
    CHECK(threw);
}
