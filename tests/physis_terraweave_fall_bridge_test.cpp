// Stage 87 optional REAL PHYSIS + TerraWeave + Box3D composition test.
// CMake registers this ONLY after TerraWeave headers are present and physics
// is enabled. It is not run on the PHYSIS-only branch.
#include "engine/world/PhysisTerraWeaveFallBridge.h"
#include <iostream>
#include <optional>
#include <stdexcept>

using namespace aiverse;
namespace pn = aiverse::physis;
namespace tw = aiverse::terraweave;
namespace host = aiverse::world;

static void require(bool b,const char* msg) {
    if(!b)throw std::runtime_error(msg);
}

int main() {
    World world;
    pn::Settings cfg;
    cfg.width=cfg.height=1;
    pn::initialize(world,cfg);
    world.setResource<host::NatureFallHostState>(host::NatureFallHostState{});
    tw::TerraWeaveAuthorityResource authority;
    tw::AnchoredPlant tree;
    tree.anchor.plantKey=551;
    tree.anchor.speciesId=14;
    tree.anchor.xCm=-500;
    tree.anchor.zCm=1200;
    tree.anchor.plantedHeightCm=200;
    tree.anchor.roots=tw::defaultRootProfile(tw::PlantHabit::Tree);
    authority.rooted.push_back(tree);
    world.setResource<tw::TerraWeaveAuthorityResource>(authority);

    const Entity proxy=world.create();
    require(host::bindNatureFallPlant(world,proxy,{14,551}),
            "failed authoritative Terra root binding");
    pn::NatureFractureOutboxComponent out;
    out.climateTick=17;
    out.eventKey=pn::natureFractureEventKey(proxy,17);
    out.delivery=pn::NatureFractureDelivery::Pending;
    world.add<pn::NatureFractureOutboxComponent>(proxy,out);
    const World preCommit=world;

    auto* saved=world.resource<tw::TerraWeaveAuthorityResource>();
    saved->earth.revision=47;
    const auto report=host::dispatchPhysisTerraWeaveFalls(world);
    require(report.committed==1 && report.pendingAtEnd==0,
            "PHYSIS event was not staged and acknowledged");
    const auto* ledger=world.resourceConst<host::NatureFallHostState>();
    require(ledger && ledger->records.size()==1 &&
            ledger->records[0].plant.plantKey==551 &&
            ledger->records[0].soilRevision==47,
            "Terra root identity or soil revision lost");

    physics::Scene scene;
    tw::FallenPlantPhysics bodies(scene);
    host::NatureFallBox3DReconciler reconciler(scene);
    auto spec=[](std::uint32_t species)
            ->std::optional<tw::FallenPlantBodySpec> {
        if(species!=14)return std::nullopt;
        return tw::FallenPlantBodySpec{};
    };
    const auto first=reconciler.reconcile(world,bodies,spec);
    require(first.spawned==1 && bodies.count()==1 && scene.bodyCount()==1,
            "Box3D falling tree was not created");
    const auto again=reconciler.reconcile(world,bodies,spec);
    require(again.reused==1 && bodies.count()==1 && scene.bodyCount()==1,
            "duplicate PHYSIS event created second Box3D body");
    scene.step();
    require(bodies.body({14,551})!=physics::kNoBody,
            "fallen tree body missing after physical step");

    world=preCommit; // authoritative rollback before event was staged
    const auto gone=reconciler.reconcile(world,bodies,spec);
    require(gone.removed==1 && bodies.count()==0 && scene.bodyCount()==0,
            "rollback retained non-authoritative Box3D body");

    // A replay with the exact same event ID must create ONE new derived body.
    world.resource<tw::TerraWeaveAuthorityResource>()->earth.revision=47;
    const auto replay=host::dispatchPhysisTerraWeaveFalls(world);
    require(replay.committed==1,"replayed PHYSIS fall did not stage");
    const auto resurrected=reconciler.reconcile(world,bodies,spec);
    require(resurrected.spawned==1 && scene.bodyCount()==1,
            "rollback replay failed to reconstruct one Box3D body");
    std::cout<<"PHYSIS/TerraWeave/Box3D real fall and rollback PASS\n";
}
