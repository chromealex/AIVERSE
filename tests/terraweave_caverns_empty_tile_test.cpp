// Stage28: empty cave surface must REMOVE old physics, never retain a ghost.
#include "engine/world/TerraWeaveCavernsVolumePhysicsStage.h"
#include <iostream>
#include <stdexcept>
using namespace aiverse;
using namespace aiverse::terraweave::caverns;
static void need(bool yes,const char* message) {
    if(!yes)throw std::runtime_error(message);
}
static CaveMesh triangle(){
    CaveMesh mesh;mesh.key={0,-1,0};
    mesh.vertices={{0,0,0,0,1,0},{1,0,0,0,1,0},{0,0,1,0,1,0}};
    mesh.indices={0,1,2};return mesh;
}
int main() {
    CaveMesh empty;empty.key={0,-1,0};
    auto gone=prepareCaveVolumeCollision(empty,4,100,12);
    need(gone.revision==12 && gone.triangles.empty() &&
         gone.body.indices.empty() && gone.body.vertices.empty(),
         "empty cave geometry should encode collider retirement");
    bool incompleteRejected=false;
    try{
        auto malformed=empty;malformed.vertices.push_back({0,0,0,0,1,0});
        (void)prepareCaveVolumeCollision(malformed,4,100,12);
    }catch(const std::invalid_argument&){incompleteRejected=true;}
    need(incompleteRejected,"invalid half-empty source accepted");

    world::SpatialWorld scene;
    CaveSpatialShadow shadow;
    const auto base=prepareCaveVolumeCollision(triangle(),4,100,11);
    need(shadow.prepare(base,120000,scene),"initial SpatialWorld stage failed");
    need(scene.colliders().empty(),"prepare mutated live SpatialWorld");
    need(shadow.commit(scene,11),"first spatial commit failed");
    need(shadow.activeCount()==1 && scene.colliders().size()==1,
         "first resident missing");
    need(shadow.prepare(gone,0,scene),"empty spatial retirement stage refused");
    need(scene.colliders().size()==1,"empty prepare removed live geometry");
    need(shadow.commit(scene,12),"empty spatial commit failed");
    need(shadow.activeRevision()==12 && shadow.activeCount()==0 &&
         scene.colliders().empty(),"old colliders remained after Fill");

    CaveBox3DShadow box;
    need(box.prepare(base),"initial Box3D descriptor stage failed");
    need(box.abort(11),"Box3D descriptor abort failed");
    need(box.prepare(gone),"empty Box3D retirement stage refused");
    need(box.stagedRevision()==12 && box.abort(12),
         "empty Box3D abort failed");

    CaveVolumePhysicsStage both;
    need(both.prepare(triangle(),4,100,13,140000,scene),
         "parallel CPU owner stage failed");
    need(both.prepared(13),"both owners did not stage same revision");
    need(both.abort(13),"paired abort failed");
    need(scene.colliders().empty(),"paired abort had live side effects");
    need(both.prepare(empty,4,100,14,0,scene),
         "paired empty retirement staging failed");
    need(both.prepared(14) && both.abort(14),
         "paired empty stage abort failed");

    std::cout<<"caverns stage28 empty tile physics staging OK\n";
}
