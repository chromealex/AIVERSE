// PHYSIS Stage 78: genre-neutral ECS nature contacts + opt-in NEREID join.
// This is a real same-World Simulation fixture, not two independent worlds.
#include "engine/physis/PhysisIntegration.h"
#include "engine/world/PhysisNereidNatureBridge.h"
#include <aiverselib/water/NereidQueryIndex.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

using namespace aiverse;
namespace pn = aiverse::physis;
namespace nw = aiverse::lib::nereid;
namespace bridge = aiverse::world;

static void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message); // always active in Release
}

class NatureWaterHost final : public GameModule {
public:
    Entity observer = kNullEntity;
    Entity lake = kNullEntity;
    std::string name() const override { return "physis-nereid-nature-contact"; }
    void setup(Simulation& sim, const Json&, const Json&) override {
        pn::InstallOptions options;
        options.climate.originX = -1;
        options.climate.originZ = -1;
        options.climate.width = 2;
        options.climate.height = 2;
        options.climate.cellSizeMeters = 100;
        options.climate.seed = 63;
        options.enableDisasters = true;
        options.enableWindZones = true;
        options.enableNatureContacts = true;
        options.floodMode = pn::FloodMode::ExternalAuthority;
        pn::install(sim, options);
        nw::registerNereidWaterPersistence(sim);

        World& world = sim.world();
        require(pn::ignite(world, -1, -1), "PHYSIS fire fixture");
        // A stale local shallow flood must be cleared in ExternalAuthority.
        world.get<pn::HazardCell>(pn::cellAt(world, -1, -1)).waterDepth = 3_fx;

        Entity zone = world.create();
        pn::WindZone wind;
        wind.center = {-50_fx, 0_fx, -50_fx};
        wind.radiusMeters = 75_fx;
        wind.velocity = {12_fx, 0_fx, 0_fx};
        world.add<pn::WindZone>(zone, wind);

        lake = world.create();
        nw::WaterBodyComponent water;
        water.kind = nw::WaterKind::Lake;
        water.center = {-50_fx, 0_fx, -50_fx};
        water.footprint = nw::WaterFootprint::Box;
        water.halfExtents = {50_fx, 50_fx};
        water.surfaceY = 2_fx;
        water.bottomY = -10_fx;
        water.flow = {3_fx, 0_fx, 0_fx};
        world.add<nw::WaterBodyComponent>(lake, water);

        observer = world.create();
        pn::NatureProbeComponent probe;
        probe.position = {-50_fx, 0_fx, -50_fx};
        world.add<pn::NatureProbeComponent>(observer, probe);
        world.add<pn::NatureContactComponent>(observer);
        nw::WaterProbeComponent waterProbe;
        waterProbe.position = probe.position;
        world.add<nw::WaterProbeComponent>(observer, waterProbe);
        world.add<nw::WaterContactComponent>(observer);

        // PHYSIS's climate and nature contacts are already registered first.
        // NEREID gets its own later read-only sampling system in the same World.
        nw::addIndexedWaterSamplingSystem(sim, "nereid.water_indexed_contacts");
    }
    void applyCommand(Simulation&, const Command&) override {}
    void hashState(Simulation&, Hasher&) override {}
    int result(Simulation&) override { return 0; }
    bool serializeState(Simulation&, Archive& a) override { return a.ok(); }
};

static std::unique_ptr<Simulation> makeSim(NatureWaterHost** host = nullptr,
                                            bool cow = false) {
    auto module = std::make_unique<NatureWaterHost>();
    NatureWaterHost* pointer = module.get();
    auto sim = std::make_unique<Simulation>(
        SimConfig{313, 20, 0, true}, std::move(module));
    sim->setup(Json::object(), Json::object());
    if (cow) sim->world().enableCowSnapshots(true);
    if (host) *host = pointer;
    return sim;
}

static std::uint64_t run(bool cow) {
    NatureWaterHost* host = nullptr;
    auto sim = makeSim(&host, cow);
    const auto initial = sim->world().get<pn::NatureContactComponent>(host->observer);
    require(!initial.valid, "contact must be opt-in and initially invalid");

    sim->step();
    const auto& world = sim->world();
    const auto* naturePtr = world.tryGetConst<pn::NatureContactComponent>(host->observer);
    const auto* waterPtr = world.tryGetConst<nw::WaterContactComponent>(host->observer);
    require(naturePtr && waterPtr, "PHYSIS/NEREID contact components missing");
    const auto nature = *naturePtr;
    const auto water = *waterPtr;
    require(nature.valid && nature.hasAtmosphere && nature.hasHazards,
            "PHYSIS nature contact missing");
    require(nature.sampledSimulationTick == 0 && nature.climateTick == 1,
            "PHYSIS sample was not taken after the climate barrier");
    require(nature.position == Vec3{-50_fx, 0_fx, -50_fx},
            "PHYSIS negative-grid probe position");
    require(nature.windVelocity.x > 8_fx, "PHYSIS local wind was not sampled");
    require(water.sampledTick == 0 && water.sample.body == host->lake &&
            water.sample.submerged && water.sample.depth == 2_fx,
            "NEREID native contact failed");
    require(water.sample.flow.x == 3_fx, "NEREID current changed unexpectedly");
    pn::HazardCell fire{};
    require(pn::hazardAt(world, -1, -1, fire) &&
            fire.waterDepth == Fixed{}, "PHYSIS double shallow-flood authority");

    bridge::NatureWaterContactView both;
    require(bridge::readNatureWaterContact(world, host->observer, both) &&
            both.hasWater && both.nature.climateTick == 1 &&
            both.sampledSimulationTick == 0,
            "composed PHYSIS/NEREID contact must be coherent");

    // Simulate an authoritative climate advance before contacts have been
    // resampled. Equal old contact stamps alone are not proof of freshness.
    auto* climateClock = sim->world().resource<pn::State>();
    ++climateClock->tick;
    require(!bridge::readNatureWaterContact(sim->world(), host->observer, both),
            "bridge accepted old contacts during the next climate tick");
    --climateClock->tick;

    // After input moves, the old derived result must not be accepted before
    // the next committed PHYSIS/NEREID sampling pass.
    sim->world().get<pn::NatureProbeComponent>(host->observer).position =
        {-40_fx, 0_fx, -50_fx};
    require(!bridge::readNatureWaterContact(sim->world(), host->observer, both),
            "bridge accepted a stale nature position before sampling");
    sim->world().get<pn::NatureProbeComponent>(host->observer).position =
        {-50_fx, 0_fx, -50_fx};

    const std::string saved = sim->serialize();
    const auto initialChecksum = sim->checksum();
    auto restored = makeSim();
    std::string error;
    require(restored->deserialize(saved, &error), "PHYSIS/NEREID native save/load");
    require(restored->checksum() == initialChecksum, "saved ECS checksum mismatch");
    sim->step();
    restored->step();
    require(restored->checksum() == sim->checksum(), "restored same-World replay diverged");

    SimSnapshot checkpoint;
    sim->saveState(checkpoint);
    const Vec3 moved{10000_fx, 0_fx, 10000_fx};
    sim->world().get<pn::NatureProbeComponent>(host->observer).position = moved;
    sim->world().get<nw::WaterProbeComponent>(host->observer).position = moved;
    sim->step();
    const auto outside = sim->world().get<pn::NatureContactComponent>(host->observer);
    require(outside.valid && !outside.hasAtmosphere,
            "global-only nature sampling outside grid");
    bridge::NatureWaterContactView dry;
    require(bridge::readNatureWaterContact(sim->world(), host->observer, dry) &&
            !dry.hasWater, "dry composed nature/water snapshot");
    const auto movedChecksum = sim->checksum();
    sim->loadState(checkpoint);
    sim->world().get<pn::NatureProbeComponent>(host->observer).position = moved;
    sim->world().get<nw::WaterProbeComponent>(host->observer).position = moved;
    sim->step();
    require(sim->checksum() == movedChecksum, "rollback/COW replay mismatch");

    // A disabled PHYSIS receiver must invalidate a formerly published contact
    // without changing NEREID water state or the climate tick.
    sim->world().get<pn::NatureProbeComponent>(host->observer).enabled = false;
    sim->step();
    require(!sim->world().get<pn::NatureContactComponent>(host->observer).valid,
            "disabled probe retained stale derived nature result");
    require(!bridge::readNatureWaterContact(sim->world(), host->observer, dry),
            "bridge accepted a disabled nature probe");
    return movedChecksum;
}

int main() {
    const auto reference = run(false);
    require(run(true) == reference, "COW snapshot mode changed deterministic output");
    std::cout << "PHYSIS/NEREID: same World, opt-in nature contacts, "
                 "external flood authority, save/load, rollback and COW PASS\n";
}
