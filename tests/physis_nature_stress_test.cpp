// PHYSIS Stage 79: deterministic nature stress/structural fracture policy.
// No real Box3D or terrain mutation; native hosts own those side effects.
#include "engine/physis/PhysisIntegration.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

using namespace aiverse;
namespace pn = aiverse::physis;
static void require(bool ok, const char* reason) {
    if (!ok) throw std::runtime_error(reason); // enabled in Release
}

static void tick(World& world) {
    pn::advanceEnvironmentWithDisasters(world, pn::FloodMode::ExternalAuthority);
    pn::advanceForces(world);
    pn::advanceNatureContacts(world, 0);
    pn::advanceNatureStress(world);
}

static void windFracturesAProxyExactlyOnce() {
    World world;
    pn::Settings settings;
    settings.width = settings.height = 1;
    initialize(world, settings);
    pn::initializeDisasters(world);
    const Entity zone = world.create();
    pn::WindZone wind;
    wind.center = {};
    wind.radiusMeters = 100_fx;
    wind.velocity = {20_fx, 0_fx, 0_fx};
    world.add<pn::WindZone>(zone, wind);

    const Entity tree = world.create();
    pn::ForceReceiver forces;
    forces.massKg = 10_fx;
    forces.dragCoefficient = 1_fx;
    forces.areaSqMeters = 2_fx;
    forces.airDensity = 1_fx;
    world.add<pn::ForceReceiver>(tree, forces);
    world.add<pn::ForceOutput>(tree, pn::ForceOutput{});
    world.add<pn::NatureProbeComponent>(tree, pn::NatureProbeComponent{});
    world.add<pn::NatureContactComponent>(tree, pn::NatureContactComponent{});
    pn::NatureStressReceiverComponent resistance;
    resistance.leverArmMeters = 2_fx;
    resistance.forceLimitN = 100_fx;
    resistance.bendingLimitNm = 100_fx;
    resistance.catastrophicLoadRatio = 2_fx;
    resistance.fatigueRate = 0_fx;
    world.add<pn::NatureStressReceiverComponent>(tree, resistance);
    world.add<pn::NatureStressOutputComponent>(tree, pn::NatureStressOutputComponent{});

    tick(world);
    const auto first = world.get<pn::NatureStressOutputComponent>(tree);
    require(first.valid && first.sampledClimateTick == 1,
            "stress not sampled on authoritative climate tick");
    require(first.windLoadN > 100_fx &&
            first.bendingMomentNm > 100_fx &&
            first.aerodynamicForceN.x > Fixed{},
            "wind force or bending moment missing");
    require(first.fractured && first.newlyFractured &&
            first.accumulatedDamage == 1_fx,
            "extreme wind must break low-strength proxy");

    pn::advanceNatureStress(world); // same tick: no double-damage/event
    const auto again = world.get<pn::NatureStressOutputComponent>(tree);
    require(again.accumulatedDamage == first.accumulatedDamage &&
            again.newlyFractured && again.sampledClimateTick == 1,
            "second scheduling changed resolved climate tick");

    const World snapshot = world;
    tick(world); // second tick: fracture must remain latched
    const auto next = world.get<pn::NatureStressOutputComponent>(tree);
    require(next.valid && next.fractured && !next.newlyFractured,
            "fracture should produce one transition only");
    world = snapshot;
    tick(world);
    const auto replay = world.get<pn::NatureStressOutputComponent>(tree);
    require(replay.valid && replay.fractured && !replay.newlyFractured &&
            replay.accumulatedDamage == next.accumulatedDamage,
            "world-copy/COW replay diverged after fracture");

    // Changing the force proxy location without updating the NatureProbe
    // must NOT emit an ungrounded load or reset prior damage.
    world.get<pn::ForceReceiver>(tree).position = {50_fx, 0_fx, 0_fx};
    tick(world);
    const auto stale = world.get<pn::NatureStressOutputComponent>(tree);
    require(!stale.valid && stale.fractured &&
            stale.accumulatedDamage == 1_fx,
            "stale force location was accepted or fracture got reset");
}

static void fireExposureUsesNatureContactNotWater() {
    World world;
    pn::Settings settings;
    settings.width = settings.height = 1;
    initialize(world, settings);
    pn::initializeDisasters(world);
    require(pn::ignite(world, 0, 0), "fire fixture was not initialized");
    const Entity shrub = world.create();
    pn::ForceReceiver still;
    still.areaSqMeters = 0_fx; // no aerodynamic load
    world.add<pn::ForceReceiver>(shrub, still);
    world.add<pn::ForceOutput>(shrub, pn::ForceOutput{});
    world.add<pn::NatureProbeComponent>(shrub, pn::NatureProbeComponent{});
    world.add<pn::NatureContactComponent>(shrub, pn::NatureContactComponent{});
    pn::NatureStressReceiverComponent material;
    material.forceLimitN = 100000_fx;
    material.bendingLimitNm = 100000_fx;
    material.fireDamageRate = 1_fx;
    material.quakeDamageRate = 0_fx;
    material.fatigueRate = 0_fx;
    material.recoveryRate = 0_fx;
    material.catastrophicLoadRatio = 16_fx;
    world.add<pn::NatureStressReceiverComponent>(shrub, material);
    world.add<pn::NatureStressOutputComponent>(shrub, pn::NatureStressOutputComponent{});

    for (int i = 0; i < 3; ++i) {
        tick(world);
        require(world.get<pn::NatureStressOutputComponent>(shrub).valid,
                "fire exposure could not read nature contact");
    }
    const auto exposed = world.get<pn::NatureStressOutputComponent>(shrub);
    require(exposed.windLoadN == Fixed{} && exposed.fractured,
            "fire should cause persistent damage without wind");
    require(exposed.accumulatedDamage == 1_fx,
            "fire should reach bounded fatigue threshold");
}

// One opt-in Simulation host proving v1 persistence, rollback and single
// authoritative writer across both force and nature-contact systems.
class StressHost final : public GameModule {
public:
    Entity object = kNullEntity;
    std::string name() const override { return "physis-nature-stress"; }
    void setup(Simulation& simulation, const Json&, const Json&) override {
        pn::InstallOptions settings;
        settings.climate.width = settings.climate.height = 1;
        settings.climate.seed = 17;
        settings.enableDisasters = true;
        settings.enableForces = true;
        settings.enableNatureContacts = true;
        settings.enableNatureStress = true;
        settings.enableWindZones = true;
        settings.floodMode = pn::FloodMode::ExternalAuthority;
        pn::install(simulation, settings);
        const Entity zone = simulation.world().create();
        pn::WindZone wind;
        wind.velocity = {20_fx, 0_fx, 0_fx};
        wind.radiusMeters = 100_fx;
        simulation.world().add<pn::WindZone>(zone, wind);
        object = simulation.world().create();
        pn::ForceReceiver receiver;
        receiver.areaSqMeters = 2_fx;
        receiver.airDensity = 1_fx;
        receiver.dragCoefficient = 1_fx;
        simulation.world().add<pn::ForceReceiver>(object, receiver);
        simulation.world().add<pn::ForceOutput>(object, pn::ForceOutput{});
        simulation.world().add<pn::NatureProbeComponent>(object, pn::NatureProbeComponent{});
        simulation.world().add<pn::NatureContactComponent>(object, pn::NatureContactComponent{});
        pn::NatureStressReceiverComponent strength;
        strength.forceLimitN = 10_fx;
        strength.bendingLimitNm = 10_fx;
        simulation.world().add<pn::NatureStressReceiverComponent>(object, strength);
        simulation.world().add<pn::NatureStressOutputComponent>(object, pn::NatureStressOutputComponent{});
    }
    void applyCommand(Simulation&, const Command&) override {}
    void hashState(Simulation&, Hasher&) override {}
    int result(Simulation&) override { return 0; }
    bool serializeState(Simulation&, Archive& a) override { return a.ok(); }
};

static std::unique_ptr<Simulation> make(bool cow = false) {
    auto sim = std::make_unique<Simulation>(
        SimConfig{415, 20, 0, true}, std::make_unique<StressHost>());
    sim->setup(Json::object(), Json::object());
    if (cow) sim->world().enableCowSnapshots(true);
    return sim;
}

static std::uint64_t stressPersistenceAndRollback(bool cow) {
    auto world = make(cow);
    world->step();
    const std::string save = world->serialize();
    const auto before = world->checksum();
    auto loaded = make(cow);
    std::string error;
    require(loaded->deserialize(save, &error), "native stress save/load failed");
    require(loaded->checksum() == before, "stress persistence checksum mismatch");
    world->step();
    loaded->step();
    require(loaded->checksum() == world->checksum(),
            "stress replay after file restore diverged");

    SimSnapshot saved;
    world->saveState(saved);
    const auto checksum = world->checksum();
    world->step();
    const auto later = world->checksum();
    world->loadState(saved);
    require(world->checksum() == checksum, "stress snapshot failed");
    world->step();
    require(world->checksum() == later, "stress rollback replay diverged");
    return later;
}

int main() {
    windFracturesAProxyExactlyOnce();
    fireExposureUsesNatureContactNotWater();
    require(stressPersistenceAndRollback(false) ==
            stressPersistenceAndRollback(true),
            "COW mode changed deterministic nature stress state");
    std::cout << "PHYSIS nature stress: loads, fire, fracture, "
                 "persistence and rollback PASS\n";
}
