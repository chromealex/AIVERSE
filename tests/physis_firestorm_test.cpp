// PHYSIS tornado nature (2026-11-22): fire whirl (fire drawn into the vortex
// -> fireness, little fire = weak effect, decay, ignition on the path, embers,
// heat damage), water (land tornado weakens and dissipates over water,
// waterspouts, fire quenched over water), ground scour, determinism, map data,
// save / load / continue checksums.
#include "engine/physis/PhysisAutoInstall.h"
#include "engine/physis/PhysisProjectConfig.h"
#include "engine/physis/PhysisTornadoAssets.h"
#include <cstdio>
#include <map>
#include <memory>
#include <stdexcept>

using namespace aiverse;
using namespace aiverse::physis;
namespace {
int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("PHYSIS FIRESTORM FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

Settings grid() {
    Settings s;
    s.width = 16; s.height = 12; s.cellSizeMeters = 4; s.seed = 5;
    s.baseTemperature = Fixed::fromInt(24); s.baseHumidity = Fixed::fromRatio(3, 10);
    return s;
}

// Fire changes only through the tornado (no spread / growth / burn-out).
DisasterConfig frozenFire() {
    DisasterConfig c;
    c.fireSpreadRate = Fixed{}; c.fireGrowthRate = Fixed{}; c.fuelConsumptionRate = Fixed{};
    c.rainSuppressionRate = Fixed{}; c.wetnessSuppressionRate = Fixed{};
    return c;
}

StormEvent vortex(Vec3 at) {
    StormEvent e;
    e.kind = StormKind::Tornado;
    e.center = at;
    e.eyeRadiusMeters = Fixed::fromInt(3);
    e.outerRadiusMeters = Fixed::fromInt(14);
    e.heightMeters = Fixed::fromInt(60);
    e.tangentialWind = Fixed::fromInt(45);
    e.updraft = Fixed::fromInt(18);
    e.radialInflow = Fixed::fromInt(5);
    return e;
}

TornadoTrack straight(Fixed toX, Fixed z, Fixed speedPerTick) {
    TornadoTrack t;
    t.mode = TornadoPathMode::Waypoints;
    t.speedPerTick = speedPerTick;
    t.waypoints = {{toX, Fixed{}, z}};
    t.rampTicks = 0;
    return t;
}

Vec3 at(int x, int z) { return {Fixed::fromInt(x), Fixed{}, Fixed::fromInt(z)}; }

struct Water {   // open water for x >= edge
    Fixed edge = Fixed::fromInt(1000000);
    static Fixed depth(void* user, Fixed x, Fixed) {
        return x >= static_cast<Water*>(user)->edge ? Fixed::fromInt(2) : Fixed{};
    }
};

struct Damage {
    std::map<std::uint64_t, Fixed> total;
    static void hit(void* user, Entity target, Fixed amount, Entity) {
        static_cast<Damage*>(user)->total[target.packed()] += amount;
    }
};

struct Hosts { Water* water = nullptr; Damage* damage = nullptr; };

TornadoTickStats step(World& w, Water* water = nullptr, Damage* damage = nullptr) {
    advanceEnvironmentWithStormsAndDisasters(w);
    Hosts hosts{water, damage};
    TornadoHooks h;
    h.user = &hosts;
    if (water)
        h.waterDepth = [](void* u, Fixed x, Fixed z) { return Water::depth(static_cast<Hosts*>(u)->water, x, z); };
    if (damage)
        h.damage = [](void* u, Entity t, Fixed a, Entity s) { Damage::hit(static_cast<Hosts*>(u)->damage, t, a, s); };
    return advanceTornadoes(w, h);
}

const TornadoNature& natureOf(const World& w) {
    const TornadoNature* n = nullptr;
    w.eachExistingConst<TornadoNature>([&](Entity, const TornadoNature& v) { n = &v; });
    if (!n) throw std::logic_error("no nature");
    return *n;
}
TornadoNature& natureOf(World& w) { return const_cast<TornadoNature&>(natureOf(static_cast<const World&>(w))); }
const TornadoTrack& trackOf(const World& w) {
    const TornadoTrack* t = nullptr;
    w.eachExistingConst<TornadoTrack>([&](Entity, const TornadoTrack& v) { t = &v; });
    return *t;
}
const StormEvent& stormOf(const World& w) {
    const StormEvent* t = nullptr;
    w.eachExistingConst<StormEvent>([&](Entity, const StormEvent& v) { t = &v; });
    return *t;
}

void burn(World& w, int x0, int z0, int x1, int z1, Fixed strength) {
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) (void)ignite(w, x, z, strength);
}

Fixed totalFire(const World& w) {
    Fixed s{};
    w.eachExistingConst<HazardCell>([&](Entity, const HazardCell& h) { s += h.fire; });
    return s;
}

// More fire under the vortex -> more fireness; a little fire -> a weak effect.
void growthByAmount() {
    World big, small, none;
    for (World* w : {&big, &small, &none}) {
        initialize(*w, grid());
        initializeDisasters(*w, frozenFire());
        TornadoTrack still;   // drift mode, no drift: stays in place
        (void)spawnTornado(*w, vortex(at(32, 24)), still, defaultTornadoNature(20));
    }
    burn(big, 4, 2, 11, 9, Fixed::fromInt(1));             // the whole area under the vortex
    burn(small, 8, 6, 8, 6, Fixed::fromRatio(1, 5));       // one weakly burning cell under the core
    Fixed peakBig{}, peakSmall{};
    for (int i = 0; i < 200; ++i) {
        step(big); step(small); step(none);
        peakBig = fxMax(peakBig, natureOf(big).fireness);
        peakSmall = fxMax(peakSmall, natureOf(small).fireness);
    }
    std::printf("fireness peak: big %.3f small %.3f\n", peakBig.toDouble(), peakSmall.toDouble());
    CHECK(peakBig > Fixed::fromRatio(1, 2));
    CHECK(peakSmall > Fixed{});
    CHECK(peakSmall < Fixed::fromRatio(1, 10));
    CHECK(peakBig > peakSmall * 5);
    CHECK(natureOf(none).fireness == Fixed{});
    // The drawn fire left the ground (the vortex took it in).
    CHECK(totalFire(big) < Fixed::fromInt(64));
}

// No more fire under it -> heat decays and fireness falls to ~0.
void decay() {
    World w;
    initialize(w, grid());
    initializeDisasters(w, frozenFire());
    (void)spawnTornado(w, vortex(at(32, 24)), TornadoTrack{}, defaultTornadoNature(20));
    burn(w, 4, 2, 11, 9, Fixed::fromInt(1));
    for (int i = 0; i < 60; ++i) step(w);
    // Burned out: no fire and no fuel left (a fire whirl over fuel would keep
    // igniting it and stay fiery).
    w.eachExisting<HazardCell>([&](Entity, HazardCell& h) { h.fire = Fixed{}; h.fuel = Fixed{}; });
    if (auto* embers = w.resource<TornadoEmberField>()) embers->embers.clear();
    const Fixed start = natureOf(w).fireness;
    CHECK(start > Fixed::fromRatio(1, 3));
    Fixed prev = start;
    bool monotonic = true;
    for (int i = 0; i < 600; ++i) {
        step(w);
        if (natureOf(w).fireness > prev) monotonic = false;
        prev = natureOf(w).fireness;
    }
    CHECK(monotonic);
    CHECK(prev < start / 8);
}

// A fire whirl sets fuel on fire along its path; a dust tornado does not.
void ignitionOnPath() {
    World hot, cold, hot2;
    for (World* w : {&hot, &cold, &hot2}) {
        initialize(*w, grid());
        initializeDisasters(*w, frozenFire());
        TornadoNature n = defaultTornadoNature(20);
        n.heatDecayPerTick = Fixed{};                   // keep the heat for the test
        if (w != &cold) n.heat = Fixed::fromInt(600);   // fireness ~0.9
        (void)spawnTornado(*w, vortex(at(6, 24)), straight(Fixed::fromInt(58), Fixed::fromInt(24),
                                                           Fixed::fromRatio(1, 10)), n);
    }
    std::uint32_t embersSeen = 0;
    for (int i = 0; i < 600; ++i) {
        const auto s = step(hot); step(cold); step(hot2);
        embersSeen = std::max(embersSeen, s.embers);
    }
    int burning = 0, coldBurning = 0;
    for (int x = 2; x < 14; ++x) {
        HazardCell h;
        if (hazardAt(hot, x, 6, h) && h.fire > Fixed{}) ++burning;
        if (hazardAt(cold, x, 6, h) && h.fire > Fixed{}) ++coldBurning;
    }
    std::printf("ignited on path: %d, embers in air max %u, ember ignitions %u\n", burning, embersSeen,
                hot.resourceConst<TornadoEmberField>()->ignitions);
    CHECK(burning >= 6);
    CHECK(coldBurning == 0);
    CHECK(embersSeen > 0);
    CHECK(totalFire(cold) == Fixed{});
    // Deterministic: the same world twice gives the same fire map and embers.
    bool same = true;
    for (Entity c : hot.resourceConst<State>()->cells) {
        const auto* a = hot.tryGetConst<HazardCell>(c);
        const auto* b = hot2.tryGetConst<HazardCell>(Entity{c});
        if (!a || !b || !(a->fire == b->fire)) same = false;
    }
    CHECK(same);
    CHECK(natureOf(hot).rng == natureOf(hot2).rng);
}

// Heat burns entities in the ring through the damage hook (not only the core).
void heatDamage() {
    World w;
    initialize(w, grid());
    initializeDisasters(w, frozenFire());
    TornadoNature n = defaultTornadoNature(20);
    n.heat = Fixed::fromInt(600);
    n.heatDecayPerTick = Fixed{};
    (void)spawnTornado(w, vortex(at(32, 24)), TornadoTrack{}, n);   // wind damage 0
    const Entity nearE = w.create();
    w.add<Transform>(nearE, Transform{at(40, 24), {}, Fixed{}});    // 8 m: ring
    const Entity farE = w.create();
    w.add<Transform>(farE, Transform{at(60, 44), {}, Fixed{}});
    Damage d;
    for (int i = 0; i < 40; ++i) step(w, nullptr, &d);
    CHECK(d.total[nearE.packed()] > Fixed{});
    CHECK(d.total.count(farE.packed()) == 0);
}

// Land tornado onto water: loses vigor, dissipates; fire is quenched.
void landTornadoOverWater() {
    World w;
    initialize(w, grid());
    initializeDisasters(w, frozenFire());
    TornadoNature n = defaultTornadoNature(20);
    n.heat = Fixed::fromInt(600);
    (void)spawnTornado(w, vortex(at(16, 24)), straight(Fixed::fromInt(56), Fixed::fromInt(24),
                                                       Fixed::fromRatio(1, 10)), n);
    Water water; water.edge = Fixed::fromInt(40);
    Fixed landIntensity{}, firenessAtShore{};
    bool sawWater = false, spoutOnLand = false;
    std::uint32_t overWaterTicks = 0;
    for (int i = 0; i < 2000 && !natureOf(w).dissipated; ++i) {
        step(w, &water);
        const auto& nat = natureOf(w);
        if (!nat.overWater) { landIntensity = trackOf(w).intensity; firenessAtShore = nat.fireness; }
        else { sawWater = true; ++overWaterTicks; }
        if (nat.waterspout) spoutOnLand = true;
        if (overWaterTicks == 40) {
            CHECK(nat.fireness < firenessAtShore / 3);     // quenched
            CHECK(trackOf(w).intensity < landIntensity);
            CHECK(nat.spray > Fixed{});
        }
    }
    CHECK(sawWater);
    CHECK(!spoutOnLand);
    CHECK(natureOf(w).dissipated);
    CHECK(trackOf(w).intensity == Fixed{});
    CHECK(!stormOf(w).enabled);
    std::printf("land tornado over water: dissipated after %u ticks\n", overWaterTicks);
    CHECK(overWaterTicks > 150 && overWaterTicks < 400);   // ~12.5 s at the default rate
}

// Waterspout: born over water (auto), weaker, spray; dies fast on land.
void waterspout() {
    World w;
    initialize(w, grid());
    initializeDisasters(w, frozenFire());
    (void)spawnTornado(w, vortex(at(56, 24)), straight(Fixed::fromInt(8), Fixed::fromInt(24),
                                                       Fixed::fromRatio(1, 10)), defaultTornadoNature(20));
    Water water; water.edge = Fixed::fromInt(30);
    step(w, &water);
    CHECK(natureOf(w).waterspout);
    CHECK(natureOf(w).spray > Fixed{});
    CHECK(stormOf(w).tangentialWind <= Fixed::fromInt(45) * Fixed::fromRatio(11, 20) + Fixed::fromRatio(1, 100));
    CHECK(stormOf(w).tangentialWind > Fixed::fromInt(20));
    std::uint32_t landTicks = 0;
    for (int i = 0; i < 2000 && !natureOf(w).dissipated; ++i) {
        step(w, &water);
        if (!natureOf(w).overWater) ++landTicks;
    }
    CHECK(natureOf(w).dissipated);
    std::printf("waterspout on land: dissipated after %u ticks\n", landTicks);
    CHECK(landTicks > 10 && landTicks < 80);              // ~2 s
    // Forced land type over water behaves as a land tornado (no spray column).
    World f;
    initialize(f, grid());
    TornadoNature n = defaultTornadoNature(20);
    n.kind = TornadoSurfaceKind::Land;
    (void)spawnTornado(f, vortex(at(56, 24)), TornadoTrack{}, n);
    advanceEnvironmentWithStorms(f);
    TornadoHooks h; h.user = &water; h.waterDepth = &Water::depth;
    (void)advanceTornadoes(f, h);
    CHECK(!natureOf(f).waterspout && natureOf(f).overWater);
}

// Ground scour along the path, deposit around it, none over water; same twice.
void erosion() {
    World a, b;
    Water water; water.edge = Fixed::fromInt(44);
    for (World* w : {&a, &b}) {
        initialize(*w, grid());
        initializeDisasters(*w, frozenFire());
        TornadoNature n = defaultTornadoNature(20);
        n.waterDecayPerTick = Fixed{};                   // keep it alive over water
        (void)spawnTornado(*w, vortex(at(6, 24)), straight(Fixed::fromInt(58), Fixed::fromInt(24),
                                                           Fixed::fromRatio(1, 20)), n);
    }
    std::uint32_t touched = 0;
    for (int i = 0; i < 1200; ++i) { touched += step(a, &water).scoured; step(b, &water); }
    const auto& fa = *a.resourceConst<GroundScourField>();
    const auto& fb = *b.resourceConst<GroundScourField>();
    CHECK(touched > 0);
    CHECK(fa.cells.size() == fb.cells.size());
    bool same = fa.revision == fb.revision;
    for (std::size_t i = 0; i < fa.cells.size() && i < fb.cells.size(); ++i)
        if (fa.cells[i].x != fb.cells[i].x || fa.cells[i].z != fb.cells[i].z || !(fa.cells[i].delta == fb.cells[i].delta))
            same = false;
    CHECK(same);
    const Fixed track = groundScourAt(a, Fixed::fromInt(24), Fixed::fromRatio(49, 2));
    const Fixed sea = groundScourAt(a, Fixed::fromInt(52), Fixed::fromRatio(49, 2));
    bool deposit = false;
    for (const auto& c : fa.cells) if (c.delta > Fixed{}) deposit = true;
    std::printf("scour: %zu cells, track %.3f m, over water %.3f m\n", fa.cells.size(),
                track.toDouble(), sea.toDouble());
    CHECK(track < -Fixed::fromRatio(1, 20));
    CHECK(sea >= Fixed{});
    CHECK(deposit);
    CHECK(track >= -natureOf(a).maxScourMeters);
}

// Simulation host: water x >= 48 (east lake), damage recorded.
struct HostModule final : GameModule {
    std::string name() const override { return "host"; }
    void setup(Simulation& sim, const Json&, const Json&) override {
        for (int i = 0; i < 5; ++i) {
            const Entity e = sim.world().create();
            sim.world().add<Transform>(e, Transform{at(10 + 8 * i, 24), {}, Fixed{}});
        }
    }
    void applyCommand(Simulation&, const Command&) override {}
    void hashState(Simulation&, Hasher& h) override {
        for (const auto& [k, v] : damage) { h.u64(k); h.u64(std::uint64_t(v.raw)); }
    }
    int result(Simulation&) override { return 0; }
    bool serializeState(Simulation&, Archive& a) override { a(damage); return a.ok(); }
    void environmentDamage(Simulation&, Entity target, Fixed amount, Entity) override {
        damage[target.packed()] += amount;
    }
    Fixed environmentWaterDepth(Simulation&, Fixed x, Fixed) override {
        return x >= Fixed::fromInt(48) ? Fixed::fromInt(3) : Fixed{};
    }
    std::map<std::uint64_t, Fixed> damage;
};

const char* kMap = R"({"physis":{
  "grid":{"originX":0,"originZ":0,"width":16,"height":12,"cellSizeMeters":4,"seed":7},
  "climate":{"baseTemperature":26,"baseHumidity":0.25},
  "enableDisasters":true, "floodMode":"ExternalAuthority", "scourCellMeters":1,
  "ignitions":[{"center":[22,24],"radiusMeters":7,"strength":1,"startSeconds":0.5},
               {"center":[34,16],"radiusMeters":3,"startTick":30}],
  "tornadoes":[
    {"id":"firestorm","center":[6,24],"coreRadiusMeters":3,"outerRadiusMeters":12,
     "heightMeters":50,"tangentialWind":40,"updraft":16,"radialInflow":6,
     "startSeconds":1,"durationSeconds":60,"rampSeconds":1,
     "path":{"mode":"waypoints","speed":1.5,"points":[[20,24],[30,24],[60,24]]},
     "damagePerSecond":2,"damageRadiusMeters":5,
     "fire":{"suppression":0.6,"fanning":0.4,"intake":0.6,"halfHeat":40,"heatDamagePerSecond":5},
     "surface":{"kind":"auto","waterDecay":0.2},
     "erosion":{"rate":0.05,"maxDepthMeters":0.5},
     "debris":{"max":16,"perSecond":6}}
  ]}})";

std::unique_ptr<Simulation> make(const Json& map) {
    SimConfig cfg; cfg.seed = 3;
    auto sim = std::make_unique<Simulation>(cfg, std::make_unique<HostModule>());
    sim->setup(Json::object(), map);
    return sim;
}

void mapSavesAndDeterminism() {
    const Json map = Json::parse(kMap);
    auto a = make(map), b = make(map);
    CHECK(a->world().countExisting<TornadoNature>() == 1);
    CHECK(a->world().resourceConst<GroundScourField>() != nullptr);
    CHECK(a->world().resourceConst<TornadoEmberField>() != nullptr);
    CHECK(a->systemCount() == 3);                      // environment + tornadoes + ignitions
    Fixed peak{};
    for (int i = 0; i < 300; ++i) {
        a->step(); b->step();
        peak = fxMax(peak, natureOf(a->world()).fireness);
    }
    CHECK(a->checksum() == b->checksum());
    CHECK(peak > Fixed::fromRatio(1, 5));
    const auto bytes = a->serialize();
    auto c = make(map);
    std::string error;
    CHECK(c->deserialize(bytes, &error));
    if (!error.empty()) std::printf("deserialize: %s\n", error.c_str());
    CHECK(c->checksum() == a->checksum());
    CHECK(natureOf(c->world()).heat == natureOf(a->world()).heat);
    for (int i = 0; i < 900; ++i) { a->step(); b->step(); c->step(); }
    CHECK(a->checksum() == b->checksum());
    CHECK(c->checksum() == a->checksum());
    const auto& nat = natureOf(a->world());
    std::printf("map run: peak fireness %.3f, waterspout %d, over water %d, dissipated %d, scour cells %zu\n",
                peak.toDouble(), int(nat.waterspout), int(nat.overWater), int(nat.dissipated),
                a->world().resourceConst<GroundScourField>()->cells.size());
    CHECK(!nat.waterspout);
    CHECK(nat.dissipated);                              // reached the lake (x >= 48) and died there
    CHECK(!a->world().resourceConst<GroundScourField>()->cells.empty());
    const auto& host = static_cast<const HostModule&>(a->module());
    std::size_t hit = 0;
    for (const auto& [k, v] : host.damage) if (v > Fixed{}) ++hit;
    CHECK(hit >= 3);
    // Errors: ignitions without disasters; bad surface kind.
    std::string noDisasters = kMap;
    noDisasters.replace(noDisasters.find("\"enableDisasters\":true"), 22, "\"enableDisasters\":false");
    bool threw = false;
    try { (void)make(Json::parse(noDisasters)); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try {
        (void)parseTornadoes(Json::parse(R"({"tornadoes":[{"id":"a","center":[0,0],"surface":"lava"}]})"), 20);
    } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    const auto off = parseTornadoes(Json::parse(
        R"({"tornadoes":[{"id":"a","center":[0,0],"erosion":false,"fire":{"whirl":false},"surface":"water"}]})"), 20);
    CHECK(off.size() == 1 && off[0].nature.scourPerTick == Fixed{} && !off[0].nature.fire &&
          off[0].nature.kind == TornadoSurfaceKind::Water);
}

// A tornado without TornadoNature (C++ spawnTornado) keeps the old rules:
// the fire in the core is blown out, nothing is drawn into the vortex.
void withoutNatureUnchanged() {
    World w;
    initialize(w, grid());
    initializeDisasters(w, frozenFire());
    TornadoTrack t; t.fireSuppression = Fixed::fromRatio(1, 10);
    (void)spawnTornado(w, vortex(at(34, 26)), t);
    (void)ignite(w, 8, 6, Fixed::fromInt(1));
    step(w);
    HazardCell h;
    CHECK(hazardAt(w, 8, 6, h));
    CHECK(h.fire < Fixed::fromInt(1));
    CHECK(w.countExisting<TornadoNature>() == 0);
    CHECK(!w.resourceConst<GroundScourField>());
}
} // namespace

int main() {
    try {
        growthByAmount();
        decay();
        ignitionOnPath();
        heatDamage();
        landTornadoOverWater();
        waterspout();
        erosion();
        mapSavesAndDeterminism();
        withoutNatureUnchanged();
    } catch (const std::exception& e) {
        std::printf("PHYSIS FIRESTORM: exception %s\n", e.what());
        return 1;
    }
    if (failures) { std::printf("PHYSIS FIRESTORM: %d failures\n", failures); return 1; }
    std::printf("PHYSIS FIRESTORM: OK\n");
    return 0;
}
