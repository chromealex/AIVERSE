// NEREID sphere water: radial sampling, currents, priority, buoyancy and save/restore in a host module.
#include "engine/world/nereid/NereidNativeWater.h"
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace aiverse;
namespace nw = aiverse::lib::nereid;
static void check(bool v, const char* why) { if (!v) throw std::runtime_error(why); }

class Host final : public GameModule {
public:
    Entity boat = kNullEntity;
    std::string name() const override { return "nereid-sphere-host"; }
    void setup(Simulation& sim, const Json&, const Json&) override {
        auto& w = sim.world();
        const Entity sea = w.create();
        nw::WaterSphereComponent s;
        s.center = {1000_fx, Fixed{}, Fixed{}};
        s.surfaceRadius = 80_fx; s.floorRadius = 70_fx; s.currentSpeed = 2_fx;
        w.add<nw::WaterSphereComponent>(sea, s);
        boat = w.create();
        Transform t; t.pos = {1000_fx, Fixed{}, 79_fx};   // 1 m under the surface, on the +Z side
        w.add<Transform>(boat, t);
        w.add<nw::WaterProbeComponent>(boat);
        w.add<nw::WaterSphereContactComponent>(boat);
        nw::WaterBuoyancyComponent b; b.buoyancyAcceleration = 10_fx; b.immersionHeight = 2_fx; b.linearDrag = Fixed{};
        w.add<nw::WaterBuoyancyComponent>(boat, b);
        w.add<nw::WaterVelocityInputComponent>(boat);
        w.add<nw::WaterForceComponent>(boat);
        nw::SphereWaterActorOptions ao; ao.buoyancy = true; ao.buoyancyParams = b;
        nw::attachSphereWaterActor(w, boat, ao);   // keeps the components added above, adds events
        nw::NativeWaterPipelineConfig cfg; cfg.enableBuoyancy = true; cfg.enableInteractions = true; cfg.enableSphereWater = true;
        nw::registerNativeWaterPipeline(sim, "nereid", cfg);
    }
    void applyCommand(Simulation&, const Command&) override {}
    void hashState(Simulation&, Hasher&) override {}
    int result(Simulation&) override { return 0; }
    bool serializeState(Simulation&, Archive& a) override { return a.ok(); }
};

int main() {
    try {
        nw::WaterSphereComponent s;
        s.center = {}; s.surfaceRadius = 10_fx; s.floorRadius = 4_fx;
        const Entity e = Entity{};
        // below the surface along any axis: submerged, radial up, radial depth
        auto in = nw::sampleWaterSphere(e, s, {Fixed{}, -9_fx, Fixed{}});
        check(in.found && in.submerged && in.depth == 1_fx && in.up.y == -1_fx, "south pole sample");
        auto above = nw::sampleWaterSphere(e, s, {12_fx, Fixed{}, Fixed{}});
        check(above.found && !above.submerged && above.depth == Fixed{}, "above the surface");
        auto deep = nw::sampleWaterSphere(e, s, {3_fx, Fixed{}, Fixed{}});
        check(!deep.submerged, "below the floor");
        check(!nw::sampleWaterSphere(e, s, {}).found, "centre has no direction");
        // current: eastward about +Y; at +Z the east direction is +X
        s.currentSpeed = 3_fx;
        auto cur = nw::sampleWaterSphere(e, s, {Fixed{}, Fixed{}, 9_fx});
        check(cur.flow.x == 3_fx && cur.flow.y == Fixed{} && cur.flow.z == Fixed{}, "eastward current");
        // buoyancy points along up at the south pole
        nw::WaterBuoyancyComponent b; b.buoyancyAcceleration = 10_fx; b.immersionHeight = 2_fx; b.linearDrag = Fixed{};
        auto f = nw::computeSphereWaterForces(in, b, Vec3{});
        check(f.acceleration.y < Fixed{} && f.immersedFraction == Fixed::fromRatio(1, 2), "radial buoyancy");

        // surface point, waves
        check(in.surfacePoint.y == -10_fx && in.surfacePoint.x == Fixed{}, "surface point");
        nw::WaterSphereWavesComponent wv; wv.amplitude = 1_fx; wv.period = 4_fx; wv.cycles = Fixed{};
        auto crest = nw::sampleWaterSphere(e, s, {Fixed{}, Fixed{}, 10_fx + Fixed::fromRatio(1, 2)}, &wv, 1_fx);   // sin(1/4 turn) = 1
        check(crest.submerged && crest.surfaceRadius > 10_fx + Fixed::fromRatio(9, 10), "wave crest lifts the sea");
        auto calm = nw::sampleWaterSphere(e, s, {Fixed{}, Fixed{}, 10_fx + Fixed::fromRatio(1, 2)}, &wv, 2_fx);
        check(!calm.submerged, "between crests the point is dry");
        // JSON spec
        Json spec = Json::parse(R"({"depth": 3, "current": 1, "axis": [0, 0, 1], "kind": "lake",
                                     "waves": {"amplitude": 0.5, "period": 6, "cycles": 3}})");
        auto ws = nw::waterSphereFromJson(spec, {1_fx, 2_fx, 3_fx}, 20_fx, "test");
        check(ws.body.floorRadius == 17_fx && ws.body.kind == nw::WaterKind::Lake && ws.hasWaves &&
              ws.waves.cycles == 3_fx && ws.body.axis.z == 1_fx, "json spec");
        check(nw::waterSphereFromJson(Json::parse("true"), {}, 8_fx, "t").body.floorRadius == 6_fx, "json default depth");
        bool threw = false;
        try { nw::waterSphereFromJson(Json::parse(R"({"depth": 30})"), {}, 8_fx, "t"); } catch (const std::exception&) { threw = true; }
        check(threw, "json depth check");
        // interaction policy: init in water (no splash), wake after spacing, exit, enter
        nw::WaterInteractionComponent ic; nw::WaterInteractionStateComponent st;
        auto ev = [&](Vec3 p) { return nw::evaluateSphereWaterInteraction(nw::sampleWaterSphere(e, s, p), p, ic, st, 1).kind; };
        check(ev({Fixed{}, Fixed{}, 9_fx + Fixed::fromRatio(1, 2)}) == nw::WaterInteractionKind::None, "no splash at spawn");
        check(ev({Fixed{}, Fixed::fromRatio(1, 2), 9_fx + Fixed::fromRatio(1, 2)}) == nw::WaterInteractionKind::None, "short move: no wake");
        check(ev({Fixed{}, 3_fx, 9_fx}) == nw::WaterInteractionKind::SurfaceWake, "wake");
        check(ev({Fixed{}, Fixed{}, 11_fx}) == nw::WaterInteractionKind::Exit, "exit");
        check(ev({Fixed{}, Fixed{}, 9_fx}) == nw::WaterInteractionKind::Enter, "enter");

        // host: systems, determinism, save/restore
        auto run = [](int ticks, std::string* save) {
            auto mod = std::make_unique<Host>();
            Host* h = mod.get();
            Simulation sim(SimConfig{7, 20, 0, true}, std::move(mod));
            sim.setup(Json::object(), Json::object());
            for (int i = 0; i < ticks; ++i) sim.step();
            const auto* c = sim.world().tryGetConst<nw::WaterSphereContactComponent>(h->boat);
            const auto* fc = sim.world().tryGetConst<nw::WaterForceComponent>(h->boat);
            check(c && c->sample.submerged && c->sample.depth == 1_fx, "host contact");
            check(fc && fc->acceleration.z > Fixed{}, "host buoyancy along +Z");
            check(sim.world().has<nw::WaterSphereInteractionStateComponent>(h->boat), "actor events attached");
            if (save) *save = sim.serialize();
            return sim.checksum();
        };
        std::string a, b2;
        const uint64_t c1 = run(10, &a), c2 = run(10, &b2);
        check(c1 == c2 && a == b2, "deterministic");
        {   // save -> fresh sim -> same checksum
            auto mod = std::make_unique<Host>();
            Simulation sim(SimConfig{7, 20, 0, true}, std::move(mod));
            sim.setup(Json::object(), Json::object());
            std::string err;
            check(sim.deserialize(a, &err) && sim.checksum() == c1, "save/load");
        }
        std::cout << "nereid sphere water: ok\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "FAIL: " << ex.what() << "\n";
        return 1;
    }
}
