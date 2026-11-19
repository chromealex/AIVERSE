// Stage 87: PHYSIS-independent host-owned persisted fall ledger.
// This suite compiles on the PHYSIS branch before TerraWeave is integrated.
// Actual TerraWeave/Box3D end-to-end coverage is a separate optional target.
#include "engine/physis/PhysisIntegration.h"
#include "engine/world/PhysisNatureFallHost.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

using namespace aiverse;
namespace pn = aiverse::physis;
namespace host = aiverse::world;

static void require(bool ok, const char* why) {
    if (!ok) throw std::runtime_error(why);
}

static pn::NatureFractureEvent pending(World& w, Entity entity,
                                      std::uint64_t tick) {
    pn::NatureFractureOutboxComponent out{};
    out.climateTick = tick;
    out.eventKey = pn::natureFractureEventKey(entity,tick);
    out.delivery = pn::NatureFractureDelivery::Pending;
    w.add<pn::NatureFractureOutboxComponent>(entity,out);
    return {entity,out.eventKey,tick};
}

static host::NatureFallRecord fallRecord(
        const pn::NatureFractureEvent& event, host::NatureFallPlantId plant) {
    host::NatureFallRecord result;
    result.entityPacked = event.entity.packed();
    result.eventKey = event.eventKey;
    result.climateTick = event.climateTick;
    result.plant = plant;
    result.habit = 0;
    result.xCm = -15000;
    result.zCm = 23000;
    result.plantedHeightCm = 600;
    result.directionXPermille = 1000;
    result.directionZPermille = 200;
    result.supportPermille = 240;
    result.exposedCm = 90;
    result.soilRevision = 17;
    return result;
}

static void stageAndRetire() {
    World w;
    host::NatureFallHostState initial;
    w.setResource<host::NatureFallHostState>(initial);
    const Entity tree = w.create();
    const Entity shrub = w.create();
    const auto first = pending(w,tree,15);
    const auto second = pending(w,shrub,15);
    const host::NatureFallPlantId oak{17,1001}, bush{29,1002};

    require(host::bindNatureFallPlant(w,tree,oak),
            "tree binding missing");
    require(host::bindNatureFallPlant(w,shrub,bush),
            "shrub binding missing");
    require(host::bindNatureFallPlant(w,tree,oak),
            "binding idempotence failed");
    require(!host::bindNatureFallPlant(w,shrub,oak),
            "plant identity hijacked");
    const auto before = w;
    const auto result = pn::dispatchNatureFractures(
        w,[&](const pn::NatureFractureEvent& e) {
            auto record = fallRecord(e,e.entity == tree ? oak : bush);
            record.habit = e.entity == tree ? 0 : 1;
            return host::stageNatureFall(w,e,record);
        });
    require(result.committed == 2 && result.pendingAtEnd == 0,
            "host durable staging did not ack both event identities");
    const auto* ledger=w.resourceConst<host::NatureFallHostState>();
    require(ledger && ledger->records.size() == 2 &&
            ledger->records[0].plant == oak &&
            ledger->records[1].plant == bush &&
            ledger->records[0].active,
            "deterministic plant-fall journal mismatch");

    require(!host::retireNatureFall(w,tree,first.eventKey,15),
            "retired before repair rearm");
    auto& out = w.get<pn::NatureFractureOutboxComponent>(tree);
    out.delivery = pn::NatureFractureDelivery::Empty; // trusted repair precondition
    require(host::retireNatureFall(w,tree,first.eventKey,15),
            "repaired plant fall not retired");
    require(!host::retireNatureFall(w,tree,first.eventKey,15),
            "retirement was not idempotently rejected");
    require(!w.resourceConst<host::NatureFallHostState>()->records[0].active &&
            w.resourceConst<host::NatureFallHostState>()->records[1].active,
            "retired wrong plant event");
    require(!host::unbindNatureFallPlant(w,shrub),
            "active shrub fall was detached from its root identity");
    require(host::unbindNatureFallPlant(w,tree),
            "repaired tree identity could not be unbound");
    require(!host::unbindNatureFallPlant(w,tree),
            "duplicate unbind unexpectedly succeeded");
    require(host::pruneNatureFallHistory(w,16)==1 &&
            w.resourceConst<host::NatureFallHostState>()->records.size()==1,
            "retired fall history did not prune within rollback window");
    require(host::pruneNatureFallHistory(w,16)==0,
            "active shrub record was accidentally pruned");

    w = before; // rollback to a pending event and binding snapshot
    require(pn::pendingNatureFractures(w).size() == 2 &&
            w.resourceConst<host::NatureFallHostState>()->records.empty(),
            "rollback did not restore the pre-host-commit state");
    require(host::stageNatureFall(w,first,fallRecord(first,bush)) ==
                pn::NatureFractureDisposition::Retry,
            "mismatched root binding acknowledged");
    require(host::stageNatureFall(w,first,fallRecord(first,oak)) ==
                pn::NatureFractureDisposition::DurablyCommitted,
            "first replay did not stage");
    require(host::stageNatureFall(w,first,fallRecord(first,oak)) ==
                pn::NatureFractureDisposition::DurablyCommitted &&
            w.resourceConst<host::NatureFallHostState>()->records.size() == 1,
            "replayed identical event was duplicated");
    auto changed=fallRecord(first,oak);
    changed.soilRevision=123;
    require(host::stageNatureFall(w,first,changed) ==
                pn::NatureFractureDisposition::Retry,
            "conflicting replay payload consumed old event");
    // Import validation rejects multiple active fall identities for one
    // stable plant even if the entity generation/ticks differ.
    host::NatureFallHostState invalid;
    invalid.records.push_back(fallRecord(first,oak));
    invalid.records.push_back(fallRecord(second,oak));
    std::string saved;
    Archive archiveWriter(&saved);
    host::io(archiveWriter,invalid);
    host::NatureFallHostState decoded;
    Archive archiveReader(saved);
    host::io(archiveReader,decoded);
    require(!archiveReader.ok(),
            "ambiguous duplicate active plant journal accepted on load");
}

class NativeFallHost : public GameModule {
public:
    Entity tree = kNullEntity;
    std::string name() const override { return "physis-terra-fall-ledger"; }
    void setup(Simulation& sim, const Json&, const Json&) override {
        pn::InstallOptions options;
        options.climate.width=options.climate.height=1;
        options.enableForces=true;
        options.enableNatureContacts=true;
        options.enableNatureStress=true;
        options.enableFractureOutbox=true;
        pn::install(sim,options);
        host::installNatureFallHost(sim);
        tree=sim.world().create();
        sim.world().add<pn::NatureFractureOutboxComponent>(tree);
        require(host::bindNatureFallPlant(
            sim.world(),tree,{7,44}),"native binding");
    }
    void applyCommand(Simulation&, const Command&) override {}
    void hashState(Simulation&, Hasher&) override {}
    int result(Simulation&) override { return 0; }
    bool serializeState(Simulation&, Archive& a) override {return a.ok();}
};

static std::unique_ptr<Simulation> make(bool cow) {
    auto sim=std::make_unique<Simulation>(
        SimConfig{871,20,0,true},std::make_unique<NativeFallHost>());
    sim->setup(Json::object(),Json::object());
    if(cow)sim->world().enableCowSnapshots(true);
    return sim;
}

static std::uint64_t saveRollbackAndCow(bool cow) {
    auto sim=make(cow);
    auto& w=sim->world();
    const Entity tree = w.resourceConst<host::NatureFallHostState>()
            ->bindings.empty() ? kNullEntity :
        Entity::fromPacked(w.resourceConst<host::NatureFallHostState>()
            ->bindings.front().entityPacked);
    require(tree.valid() && w.alive(tree),"native root entity missing");
    const auto e=pending(w,tree,7);
    require(host::stageNatureFall(w,e,fallRecord(e,{7,44})) ==
                pn::NatureFractureDisposition::DurablyCommitted,
            "native pre-save fall staging");
    require(pn::acknowledgeNatureFracture(w,tree,e.eventKey,7),
            "native host acknowledgement");
    const auto checksum=sim->checksum();
    const std::string snapshot=sim->serialize();
    auto restored=make(cow);
    std::string error;
    require(restored->deserialize(snapshot,&error),
            "native fall ledger save/load rejected");
    require(restored->checksum()==checksum,
            "native fall ledger checksum mismatch");
    const auto* saved=restored->world().resourceConst<host::NatureFallHostState>();
    require(saved && saved->records.size()==1 &&
            saved->records[0].plant.plantKey==44,
            "native fall ledger not persisted");

    SimSnapshot rollback;
    sim->saveState(rollback);
    w.get<pn::NatureFractureOutboxComponent>(tree).delivery =
        pn::NatureFractureDelivery::Empty;
    require(host::retireNatureFall(w,tree,e.eventKey,7),
            "native fall retirement");
    const auto later=sim->checksum();
    require(later!=checksum,"retirement did not alter authoritative state");
    sim->loadState(rollback);
    require(sim->checksum()==checksum,
            "fall ledger rollback not restored");
    sim->world().get<pn::NatureFractureOutboxComponent>(tree).delivery =
        pn::NatureFractureDelivery::Empty;
    require(host::retireNatureFall(sim->world(),tree,e.eventKey,7),
            "fall retirement failed on replay");
    require(sim->checksum()==later,"COW rollback replay diverged");
    return later;
}

int main() {
    stageAndRetire();
    const auto deterministic=saveRollbackAndCow(false);
    require(deterministic==saveRollbackAndCow(true),
            "COW changed fall ledger save/replay state");
    std::cout<<"PHYSIS/Terra host fall ledger: binding, pending, "
                "ack, replay, save and rollback PASS\n";
}
