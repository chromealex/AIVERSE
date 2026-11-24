// Standalone real SpatialWorld physical door restoration, no genre module.
#include "engine/buildings/BuildingDualLeafOwner.h"
#include <cstdio>
#include <stdexcept>
using namespace aiverse;
using namespace aiverse::building;
#define REQUIRE(x) do {if(!(x)) throw std::runtime_error(\
    std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}while(false)

int main() {
    try {
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
        // the sweep. The physical progress remains at 600.
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
        std::puts("building dual-leaf authored/zero-revision rollback: PASS");
        return 0;
    }catch(const std::exception& e){
        std::fprintf(stderr,"building dual-leaf rollback: FAIL %s\n",e.what());
        return 1;
    }
}
