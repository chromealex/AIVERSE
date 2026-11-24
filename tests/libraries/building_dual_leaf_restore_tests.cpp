// Standalone real SpatialWorld physical door restoration, no genre module.
#include "engine/buildings/BuildingDualLeafOwner.h"
#include <cstdio>
#include <stdexcept>
using namespace aiverse;
using namespace aiverse::building;
#define REQUIRE(x) do {if(!(x)) throw std::runtime_error(\
    std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}while(false)

// Minimal CPU geometry/socket fixture: exercise the real aperture owner
// and SpatialWorld without a style file or GPU dependency.
static void testAuthoredSingleLeaf() {
    using namespace aiverse::lib::systems;
    RuntimeBuildingResult generated;
    generated.building.mesh.vertices.emplace_back();
    generated.building.constructionSize.width=16.f;
    generated.building.constructionSize.height=8.f;
    generated.building.constructionSize.depth=12.f;
    BuildingGraphSocket socket;
    socket.id="entrance/0/0";
    socket.kind=BuildingFeatureKind::SmallGate;
    socket.side=BuildingFeatureSide::Front;
    socket.x=8.f;
    socket.width=4.f;
    socket.height=3.f;
    socket.open=0.5f;
    generated.structureGraph.sockets.push_back(socket);
    world::SpatialWorld spatial;
    BuildingApertureOwner owner;
    BuildingGraphChangeRouter graphs;
    const auto domains=buildingGraphBit(BuildingGraphDomain::Navigation);
    REQUIRE(graphs.registerConsumer(BuildingGraphDomain::Navigation,
        [](const BuildingGraphChange& c){return c.sceneId==45&&c.revision>0;}));
    const std::map<std::string,uint32_t> ids{{socket.id,10201}};
    bool blocked=false;
    const auto obstruction=[&](const world::Bounds&){return blocked;};
    const Vec3 origin{0_fx,0_fx,0_fx};
    std::string error;
    auto attach=[&](){return owner.attach(generated,spatial,45,origin,0,
        ids,obstruction,&error);};
    auto same=[](const world::Bounds& a,const world::Bounds& b){
        return a.min.x==b.min.x&&a.min.y==b.min.y&&a.min.z==b.min.z&&
               a.max.x==b.max.x&&a.max.y==b.max.y&&a.max.z==b.max.z;
    };
    REQUIRE(attach());
    const auto pose=owner.poses().at(0);
    REQUIRE(pose.physicalPermille==500&&!pose.open);
    const auto initial=spatial.colliders().at(10201).bounds;
    REQUIRE(initial.min.x==pose.bounds.min.x+2_fx);
    REQUIRE(same(initial,owner.closedColliderBounds().at(10201)));
    // An initially half-open door can close, not just open.
    REQUIRE(owner.beginMotion(socket.id,false,domains,&error));
    blocked=true;
    REQUIRE(!owner.advanceMotion(spatial,graphs,100).collisionCommitted);
    REQUIRE(same(initial,spatial.colliders().at(10201).bounds));
    blocked=false;
    for(int i=0;i<5;++i){
        const auto step=owner.advanceMotion(spatial,graphs,100);
        REQUIRE(step.collisionCommitted&&step.graphsAcknowledged);
    }
    REQUIRE(owner.poses().at(0).physicalPermille==0);
    REQUIRE(same(pose.bounds,spatial.colliders().at(10201).bounds));
    REQUIRE(owner.setOpen(socket.id,true,spatial,graphs,domains).graphsAcknowledged);
    REQUIRE(spatial.colliders().empty());
    owner.detach(spatial);
    graphs.forgetScene(45);
    REQUIRE(attach());
    blocked=true;
    REQUIRE(!owner.setOpen(socket.id,false,spatial,graphs,domains).collisionCommitted);
    REQUIRE(owner.poses().at(0).physicalPermille==500);
    blocked=false;
    const auto close=owner.setOpen(socket.id,false,spatial,graphs,domains);
    REQUIRE(close.collisionCommitted&&close.graphsAcknowledged);
    REQUIRE(owner.poses().at(0).physicalPermille==0);
    owner.detach(spatial);
    REQUIRE(spatial.colliders().empty());

    // An already-owned collider ID cannot be overwritten, even if its
    // requested initial aperture would be fully open.
    REQUIRE(spatial.add({10201,pose.bounds}));
    generated.structureGraph.sockets[0].open=1.f;
    REQUIRE(!attach());
    REQUIRE(!owner.attached());
    REQUIRE(same(pose.bounds,spatial.colliders().at(10201).bounds));
    spatial.remove(10201);
}

static void testGraphDiagnostics(){
    BuildingGraphChangeRouter router;
    const auto nav=buildingGraphBit(BuildingGraphDomain::Navigation);
    const auto spatial=buildingGraphBit(BuildingGraphDomain::Spatial);
    const auto audio=buildingGraphBit(BuildingGraphDomain::Audio);
    bool available=false;int navCalls=0;
    REQUIRE(router.registerConsumer(BuildingGraphDomain::Navigation,[&](const auto&){++navCalls;return true;}));
    REQUIRE(router.registerConsumer(BuildingGraphDomain::Spatial,[&](const auto&){return available;}));
    REQUIRE(!router.diagnostics(77).lastChange);
    const BuildingGraphChange change{77,1,9,BuildingGraphChangeKind::ApertureChanged,
        nav|spatial|audio,{{0_fx,0_fx,0_fx},{4_fx,3_fx,1_fx}},true};
    REQUIRE(!router.publish(change).complete);
    auto d=router.diagnostics(77);
    REQUIRE(d.lastChange&&d.lastChange->featureIndex==9&&d.lastChange->open);
    REQUIRE(d.registeredDomains==(nav|spatial)&&d.requestedDomains==(nav|spatial|audio));
    REQUIRE(d.acknowledgedDomains==nav&&d.pendingDomains==(spatial|audio));
    REQUIRE(d.refusedDomains==spatial&&d.missingOwners==audio&&d.attempts==1);
    available=true;
    REQUIRE(!router.publish(change).complete);
    d=router.diagnostics(77);
    REQUIRE(d.pendingDomains==audio&&d.acknowledgedDomains==(nav|spatial)&&d.attempts==2&&navCalls==1);
    REQUIRE(router.registerConsumer(BuildingGraphDomain::Audio,[](const auto&){return true;}));
    REQUIRE(router.publish(change).complete);
    d=router.diagnostics(77);
    REQUIRE(!d.pendingDomains&&d.acknowledgedDomains==change.domains&&d.attempts==3&&navCalls==1);
    router.forgetScene(77);
    REQUIRE(!router.diagnostics(77).lastChange);
    REQUIRE(router.diagnostics(77).registeredDomains==change.domains);
}

int main() {
    try {
        testGraphDiagnostics();
        testAuthoredSingleLeaf();
        world::SpatialWorld spatial;
        BuildingDualLeafOwner owner;
        BuildingAperturePose pose;
        pose.socketId="front-gate-0";
        pose.kind=lib::systems::BuildingFeatureKind::SmallGate;
        pose.bounds={{0_fx,0_fx,0_fx},{4_fx,3_fx,1_fx}};
        pose.open=false;
        pose.physicalPermille=500;
        const std::vector<BuildingAperturePose> poses{pose};
        const std::map<std::string,BuildingDualLeafIds> ids{
            {pose.socketId,{10101,10102}}};
        const auto noObstruction=[](const world::Bounds&,const world::Bounds&,
                                    uint32_t,uint32_t){return false;};
        std::string error;
        REQUIRE(owner.attach(poses,ids,spatial,44,noObstruction,&error));
        REQUIRE(owner.revision()==0);
        const auto first=owner.poses();
        REQUIRE(first.size()==1);
        REQUIRE(first[0].physicalPermille==500);
        REQUIRE(!first[0].open);
        REQUIRE(spatial.colliders().size()==2);
        owner.detach(spatial);
        REQUIRE(spatial.colliders().empty());

        // Revision zero is an authoritative snapshot before any movement.
        const std::map<std::string,uint16_t> restored{{pose.socketId,500}};
        REQUIRE(owner.attachRestored(poses,ids,restored,0,spatial,44,
                                     noObstruction,&error));
        REQUIRE(owner.revision()==0);
        REQUIRE(owner.poses()[0].physicalPermille==500);
        REQUIRE(spatial.colliders().size()==2);

        BuildingGraphChangeRouter graphs;
        int acknowledgements=0;
        REQUIRE(graphs.registerConsumer(BuildingGraphDomain::Navigation,
            [&](const BuildingGraphChange& change){
                ++acknowledgements;
                return change.sceneId==44&&change.revision>0;
            }));
        REQUIRE(owner.begin(pose.socketId,true,
            buildingGraphBit(BuildingGraphDomain::Navigation),&error));
        const auto step=owner.advance(spatial,graphs,100);
        REQUIRE(step.committed && step.graphsAcknowledged);
        REQUIRE(owner.poses()[0].physicalPermille==600);
        REQUIRE(acknowledgements==1);

        // The geometry must not be rewritten when an actor obstructs
        // the sweep. The newly restored physical progress remains at 500.
        owner.detach(spatial);
        const auto blocked=[](const world::Bounds&,const world::Bounds&,
                              uint32_t,uint32_t){return true;};
        REQUIRE(owner.attachRestored(poses,ids,restored,0,spatial,44,
                                     blocked,&error));
        REQUIRE(owner.begin(pose.socketId,true,
            buildingGraphBit(BuildingGraphDomain::Navigation),&error));
        const auto rejected=owner.advance(spatial,graphs,100);
        REQUIRE(!rejected.committed);
        REQUIRE(owner.poses()[0].physicalPermille==500);
        REQUIRE(spatial.colliders().size()==2);
        owner.detach(spatial);
        REQUIRE(spatial.colliders().empty());
        std::puts("building single/dual-leaf authored/zero-revision rollback: PASS");
        return 0;
    }catch(const std::exception& e){
        std::fprintf(stderr,"building dual-leaf rollback: FAIL %s\n",e.what());
        return 1;
    }
}
