// PHYSIS Stage 82: persistent host fracture outbox and deterministic delivery.
#include "engine/physis/PhysisIntegration.h"
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse;
namespace pn = aiverse::physis;
static void require(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason); // also active in Release
}

static void stableOrderAndAcknowledgement() {
    World w;
    pn::Settings settings;
    settings.width = settings.height = 1;
    pn::initialize(w, settings);
    w.resource<pn::State>()->tick = 11;
    const Entity a = w.create();
    const Entity b = w.create();
    // Source/pool insertion order should not affect event ordering.
    for (Entity entity : std::vector<Entity>{b, a}) {
        w.add<pn::NatureFractureOutboxComponent>(
            entity, pn::NatureFractureOutboxComponent{});
        pn::NatureStressOutputComponent stress;
        stress.fractured = true;
        stress.newlyFractured = true;
        stress.valid = true;
        stress.sampledClimateTick = 11;
        stress.accumulatedDamage = 1_fx;
        stress.aerodynamicForceN = {10_fx, 0_fx, 4_fx};
        stress.bendingMomentNm = 20_fx;
        w.add<pn::NatureStressOutputComponent>(entity, stress);
    }
    pn::advanceNatureFractureOutbox(w);
    auto events = pn::pendingNatureFractures(w);
    require(events.size() == 2, "newly fractured entities not enqueued");
    require(events[0].entity.packed() < events[1].entity.packed(),
            "event delivery is not generation-stable and sorted");
    require(events[0].eventKey ==
            pn::natureFractureEventKey(events[0].entity, 11),
            "non-deterministic fracture identity");
    require(events[0].aerodynamicForceN.x == 10_fx &&
            events[0].bendingMomentNm == 20_fx,
            "event dropped canonical stress load");
    pn::advanceNatureFractureOutbox(w);
    require(pn::pendingNatureFractures(w).size() == 2,
            "duplicate publisher call changed pending events");

    const World beforeAck = w;
    require(!pn::acknowledgeNatureFracture(
                w, a, 42, 11), "wrong event key was accepted");
    require(!pn::acknowledgeNatureFracture(
                w, a, pn::natureFractureEventKey(a, 11), 12),
            "stale acknowledgement timestamp was accepted");
    require(pn::pendingNatureFractures(w).size() == 2,
            "rejected ack removed a pending fracture");
    require(pn::acknowledgeNatureFracture(
                w, a, pn::natureFractureEventKey(a, 11), 11),
            "valid acknowledgement rejected");
    require(!pn::acknowledgeNatureFracture(
                w, a, pn::natureFractureEventKey(a, 11), 11),
            "duplicate acknowledgement accepted");
    require(pn::pendingNatureFractures(w).size() == 1,
            "acknowledged event still pending");
    w.resource<pn::State>()->tick = 12;
    pn::advanceNatureFractureOutbox(w);
    require(pn::pendingNatureFractures(w).size() == 1,
            "unacknowledged event was lost after climate advanced");
    w = beforeAck;
    pn::advanceNatureFractureOutbox(w);
    require(pn::pendingNatureFractures(w).size() == 2,
            "rollback did not reconstruct pending events");
}

class FractureHost final : public GameModule {
public:
    std::string name() const override { return "physis-fracture-outbox"; }
    void setup(Simulation& sim, const Json&, const Json&) override {
        pn::InstallOptions options;
        options.climate.width = options.climate.height = 1;
        options.climate.seed = 41;
        options.enableForces = true;
        options.enableWindZones = true;
        options.enableNatureContacts = true;
        options.enableNatureStress = true;
        options.enableFractureOutbox = true;
        pn::install(sim, options);
        World& world = sim.world();
        const Entity zone = world.create();
        pn::WindZone wind;
        wind.radiusMeters = 100_fx;
        wind.velocity = {20_fx, 0_fx, 0_fx};
        world.add<pn::WindZone>(zone, wind);
        const Entity tree = world.create();
        pn::ForceReceiver receiver;
        receiver.areaSqMeters = 2_fx;
        receiver.airDensity = 1_fx;
        receiver.dragCoefficient = 1_fx;
        world.add<pn::ForceReceiver>(tree, receiver);
        world.add<pn::ForceOutput>(tree, pn::ForceOutput{});
        world.add<pn::NatureProbeComponent>(tree, pn::NatureProbeComponent{});
        world.add<pn::NatureContactComponent>(tree, pn::NatureContactComponent{});
        pn::NatureStressReceiverComponent strength;
        strength.forceLimitN = 10_fx;
        strength.bendingLimitNm = 10_fx;
        world.add<pn::NatureStressReceiverComponent>(tree, strength);
        world.add<pn::NatureStressOutputComponent>(
            tree, pn::NatureStressOutputComponent{});
        world.add<pn::NatureFractureOutboxComponent>(
            tree, pn::NatureFractureOutboxComponent{});
    }
    void applyCommand(Simulation&, const Command&) override {}
    void hashState(Simulation&, Hasher&) override {}
    int result(Simulation&) override { return 0; }
    bool serializeState(Simulation&, Archive& archive) override {
        return archive.ok();
    }
};

static std::unique_ptr<Simulation> make(bool cow) {
    auto simulation = std::make_unique<Simulation>(
        SimConfig{821, 20, 0, true}, std::make_unique<FractureHost>());
    simulation->setup(Json::object(), Json::object());
    if (cow) simulation->world().enableCowSnapshots(true);
    return simulation;
}

static std::uint64_t nativePersistenceAndRollback(bool cow) {
    auto sim = make(cow);
    require(pn::pendingNatureFractures(sim->world()).empty(),
            "fracture already pending before sampling");
    sim->step();
    auto events = pn::pendingNatureFractures(sim->world());
    require(events.size() == 1 && events[0].climateTick == 1,
            "host did not publish first-tick fracture");
    const auto event = events[0];
    SimSnapshot checkpoint;
    sim->saveState(checkpoint);
    const auto original = sim->checksum();
    // A late host consumer can acknowledge after several weather ticks.
    sim->step();
    require(pn::pendingNatureFractures(sim->world()).size() == 1,
            "unacknowledged fracture lost on later tick");

    auto unloaded = make(cow);
    const std::string savedPending = sim->serialize();
    std::string error;
    require(unloaded->deserialize(savedPending, &error),
            "persistent outbox load failed");
    require(unloaded->checksum() == sim->checksum(),
            "pending outbox checksum changed on native restore");
    require(pn::pendingNatureFractures(unloaded->world()).size() == 1,
            "saved event not pending after restore");

    require(pn::acknowledgeNatureFracture(
                sim->world(), event.entity, event.eventKey, event.climateTick),
            "host acknowledgement failed");
    require(pn::acknowledgeNatureFracture(
                unloaded->world(), event.entity, event.eventKey, event.climateTick),
            "restored host acknowledgement failed");
    const std::string ackSave = sim->serialize();
    auto ackLoaded = make(cow);
    require(ackLoaded->deserialize(ackSave, &error),
            "acknowledged outbox load failed");
    require(pn::pendingNatureFractures(ackLoaded->world()).empty(),
            "acknowledged fracture was redelivered from save");
    sim->step();
    unloaded->step();
    ackLoaded->step();
    require(sim->checksum() == unloaded->checksum() &&
            sim->checksum() == ackLoaded->checksum(),
            "saved outbox state diverged after authoritative step");

    sim->loadState(checkpoint);
    require(sim->checksum() == original,
            "rollback to pre-acknowledgement checkpoint failed");
    events = pn::pendingNatureFractures(sim->world());
    require(events.size() == 1 && events[0].eventKey == event.eventKey,
            "rollback did not restore original pending event identity");
    require(pn::acknowledgeNatureFracture(
                sim->world(), event.entity, event.eventKey, event.climateTick),
            "replayed authoritative ack failed");
    sim->step();
    return sim->checksum();
}


static void explicitRepairRearmsOnlyLaterFractures() {
    World world;
    pn::Settings settings;
    settings.width = settings.height = 1;
    pn::initialize(world, settings);
    auto* climate = world.resource<pn::State>();
    climate->tick = 21;
    const Entity plant = world.create();
    world.add<pn::NatureFractureOutboxComponent>(
        plant, pn::NatureFractureOutboxComponent{});
    pn::NatureStressOutputComponent stress;
    stress.valid = true;
    stress.fractured = true;
    stress.newlyFractured = true;
    stress.sampledClimateTick = 21;
    stress.accumulatedDamage = 1_fx;
    world.add<pn::NatureStressOutputComponent>(plant, stress);
    pn::advanceNatureFractureOutbox(world);
    const auto first = pn::pendingNatureFractures(world);
    require(first.size() == 1, "first fracture missing before repair");
    require(!pn::rearmNatureFractureAfterRepair(
                world, plant, first[0].eventKey, 21),
            "pending fracture must not be rearmed");
    require(pn::acknowledgeNatureFracture(
                world, plant, first[0].eventKey, 21),
            "initial fracture acknowledgement failed");
    require(!pn::rearmNatureFractureAfterRepair(
                world, plant, first[0].eventKey, 21),
            "damaged/fractured stress was accepted as repaired");

    auto& repaired = world.get<pn::NatureStressOutputComponent>(plant);
    repaired.fractured = false;
    repaired.newlyFractured = false;
    repaired.accumulatedDamage = Fixed{};
    require(!pn::rearmNatureFractureAfterRepair(
                world, plant, first[0].eventKey, 21),
            "same-tick repair reused the original event identity");
    ++climate->tick;
    require(!pn::rearmNatureFractureAfterRepair(
                world, plant, first[0].eventKey ^ 1u, 21),
            "wrong repair key accepted");
    require(!pn::rearmNatureFractureAfterRepair(
                world, plant, first[0].eventKey, 20),
            "wrong repair tick accepted");
    const World afterRepairBeforeRearm = world;
    require(pn::rearmNatureFractureAfterRepair(
                world, plant, first[0].eventKey, 21),
            "authoritative repaired object could not rearm");
    require(!pn::rearmNatureFractureAfterRepair(
                world, plant, first[0].eventKey, 21),
            "duplicate repair rearm was accepted");
    pn::advanceNatureFractureOutbox(world);
    require(pn::pendingNatureFractures(world).empty(),
            "rearm emitted an old fracture without a fresh stress sample");

    auto& nextStress = world.get<pn::NatureStressOutputComponent>(plant);
    nextStress.valid = true;
    nextStress.fractured = true;
    nextStress.newlyFractured = true;
    nextStress.accumulatedDamage = 1_fx;
    nextStress.sampledClimateTick = climate->tick;
    pn::advanceNatureFractureOutbox(world);
    const auto second = pn::pendingNatureFractures(world);
    require(second.size() == 1 && second[0].climateTick == 22 &&
            second[0].eventKey != first[0].eventKey,
            "second fracture reused original event identity");
    require(!pn::acknowledgeNatureFracture(
                world, plant, first[0].eventKey, 21),
            "stale first-cycle acknowledgement consumed second fracture");
    require(pn::acknowledgeNatureFracture(
                world, plant, second[0].eventKey, 22),
            "second-cycle acknowledgement was rejected");

    world = afterRepairBeforeRearm;
    require(pn::rearmNatureFractureAfterRepair(
                world, plant, first[0].eventKey, 21),
            "rollback failed to restore rearm eligibility");
}

static void durableDeliveryBudgetRetryAndReplay() {
    World world;
    pn::Settings settings;
    settings.width = settings.height = 1;
    pn::initialize(world, settings);
    world.resource<pn::State>()->tick = 7;
    const Entity a = world.create();
    const Entity b = world.create();
    const Entity c = world.create();
    for (Entity entity : std::vector<Entity>{c, b, a}) {
        world.add<pn::NatureFractureOutboxComponent>(
            entity, pn::NatureFractureOutboxComponent{});
        pn::NatureStressOutputComponent stress;
        stress.valid = true;
        stress.fractured = true;
        stress.newlyFractured = true;
        stress.sampledClimateTick = 7;
        stress.accumulatedDamage = 1_fx;
        world.add<pn::NatureStressOutputComponent>(entity, stress);
    }
    pn::advanceNatureFractureOutbox(world);
    const World beforeDelivery = world;
    const auto first = pn::dispatchNatureFractures(
        world, [&](const pn::NatureFractureEvent& event) {
            return event.entity == a ? pn::NatureFractureDisposition::Retry
                                     : pn::NatureFractureDisposition::DurablyCommitted;
        }, 2);
    require(first.pendingAtStart == 3 && first.visited == 2 &&
            first.retry == 1 && first.committed == 1 &&
            first.pendingAtEnd == 2,
            "delivery budget/retry counts incorrect");
    require(pn::pendingNatureFractures(world).size() == 2,
            "retry event unexpectedly acknowledged");

    try {
        pn::dispatchNatureFractures(world,
            [](const pn::NatureFractureEvent&) {
                return pn::NatureFractureDisposition::Retry;
            }, 0);
        require(false, "zero delivery budget accepted");
    } catch (const std::invalid_argument&) {}
    try {
        pn::dispatchNatureFractures(world,
            [](const pn::NatureFractureEvent&) {
                return pn::NatureFractureDisposition::Retry;
            }, 4097);
        require(false, "oversize delivery budget accepted");
    } catch (const std::invalid_argument&) {}
    bool sinkThrew = false;
    try {
        pn::dispatchNatureFractures(world,
            [](const pn::NatureFractureEvent&) -> pn::NatureFractureDisposition {
                throw std::runtime_error("temporary durable sink failure");
            }, 1);
    } catch (const std::runtime_error&) {
        sinkThrew = true;
    }
    require(sinkThrew, "sink exception unexpectedly swallowed");
    require(pn::pendingNatureFractures(world).size() == 2,
            "failed delivery callback lost a pending event");

    // Simulate the host's *own* deduplicating durable event journal, kept
    // independently from the ECS snapshot. Journal storage is only mocked:
    // the test does NOT prove actual filesystem durability or Box3D rollback.
    std::vector<std::uint64_t> journal;
    std::size_t physicalEffects = 0;
    auto durable = [&](const pn::NatureFractureEvent& event) {
        if (std::find(journal.begin(), journal.end(), event.eventKey) ==
                journal.end()) {
            journal.push_back(event.eventKey);
            ++physicalEffects;
        }
        return pn::NatureFractureDisposition::DurablyCommitted;
    };
    const auto delivered = pn::dispatchNatureFractures(world, durable);
    require(delivered.committed == 2 && delivered.pendingAtEnd == 0 &&
            physicalEffects == 2, "remaining pending fractures not committed");
    world = beforeDelivery;
    const auto replay = pn::dispatchNatureFractures(world, durable);
    require(replay.committed == 3 && replay.pendingAtEnd == 0 &&
            physicalEffects == 3,
            "rollback delivery duplicated prior host effects");
    require(pn::pendingNatureFractures(world).empty(),
            "rollback replay failed to acknowledge existing durable entries");
}

int main() {
    stableOrderAndAcknowledgement();
    explicitRepairRearmsOnlyLaterFractures();
    durableDeliveryBudgetRetryAndReplay();
    const auto native = nativePersistenceAndRollback(false);
    require(nativePersistenceAndRollback(true) == native,
            "COW mode changed fracture outbox replay identity");
    std::cout << "PHYSIS fracture outbox: stable identity, pending, ack, "
                 "native persistence and rollback PASS\n";
}
