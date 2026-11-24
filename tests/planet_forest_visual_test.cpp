// CPU snapshot regression: no graphics device or simulation mutation.
#include "engine/view/game/GamePlanetFlora.h"
#include "aiverselib/world/PlanetConsequenceOwner.h"
#include "aiverselib/world/EvolvePlanetDeadwood.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
using namespace aiverse;
namespace tw=aiverse::terraweave;
namespace ev=aiverse::evolve;
namespace wp=aiverse::lib::worldprocess;
static void check(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}
int main(){
    try{
        game::Surface surface;
        game::BodyDef body;body.id="terra";body.radius=64_fx;body.faceSize=8;
        surface.bodies.push_back(body);surface.build();
        const auto ground=surface.cellCenter(surface.gridCell(0,0,3,3));
        World world;
        tw::TerraWeaveAuthorityResource authority;
        tw::AnchoredPlant plant;plant.anchor.speciesId=1;plant.anchor.plantKey=42;plant.fallen=true;
        authority.rooted.push_back(plant);world.setResource<tw::TerraWeaveAuthorityResource>(authority);
        ev::PlanetRootDeliveryResource delivery;
        const tw::AnchoredPlantId id{1,42};
        delivery.bindings.push_back({id,"terra",ground});
        ev::PlanetPlantFall oldFall;oldFall.event.id=id;oldFall.event.tick=1;oldFall.event.sourceSoilRevision=1;
        oldFall.bodyId="terra";oldFall.plantedPosition=ground;
        auto current=oldFall;current.event.tick=2;current.event.sourceSoilRevision=2;
        delivery.events={{oldFall,true},{current,false}};
        world.setResource<ev::PlanetRootDeliveryResource>(delivery);
        wp::PlanetConsequenceResource physical;
        wp::ConsequenceBody oldBody;oldBody.key=wp::consequenceKey(oldFall);oldBody.body="terra";
        oldBody.motion.position=ground;
        physical.bodies.push_back(oldBody);world.setResource<wp::PlanetConsequenceResource>(physical);
        ev::PlanetDeadwoodResource wood;
        tw::ForestDeadwoodRecord log;log.id=id;log.initialUnits=1000;log.remainingUnits=250;
        wood.ledger.logs.push_back(log);world.setResource<ev::PlanetDeadwoodResource>(wood);
        game::PlanetForestVisualSnapshot snapshot;
        check(game::collectPlanetForestVisuals(world,surface,snapshot),"pending snapshot");
        check(snapshot.plants.size()==1&&!snapshot.plants[0].physical,"old body cannot claim new fall");
        check(std::abs(snapshot.plants[0].thickness-.5f)<.0001f,"remaining area fraction");
        delivery.events.back().acknowledged=true;
        world.setResource<ev::PlanetRootDeliveryResource>(delivery);
        auto newBody=oldBody;newBody.key=wp::consequenceKey(current);
        physical.bodies.push_back(newBody);world.setResource<wp::PlanetConsequenceResource>(physical);
        check(game::collectPlanetForestVisuals(world,surface,snapshot)&&snapshot.plants.size()==1&&snapshot.plants[0].physical,"exact body receipt");
        // Raw visible cells, not explored cells or blurred GPU fog.
        std::vector<uint8_t> vision(size_t(surface.W)*size_t(surface.H),1);
        const int rootCell=surface.cellAt(ground);
        check(rootCell>=0,"root cell");
        const auto moved=surface.cellCenter(surface.gridCell(0,1,3,3));
        const int movedCell=surface.cellAt(moved);
        check(movedCell>=0&&movedCell!=rootCell,"separate motion cell");
        physical.bodies.back().motion.position=moved;
        world.setResource<wp::PlanetConsequenceResource>(physical);
        vision[size_t(rootCell)]=2;
        check(game::collectPlanetForestVisuals(world,surface,snapshot,&vision)&&snapshot.plants.empty(),
              "visible root cannot reveal physical tree in fog");
        vision[size_t(rootCell)]=0;vision[size_t(movedCell)]=2;
        check(game::collectPlanetForestVisuals(world,surface,snapshot,&vision)&&snapshot.plants.size()==1&&snapshot.plants[0].physical,
              "current visible physical position");
        vision[size_t(movedCell)]=1;
        check(game::collectPlanetForestVisuals(world,surface,snapshot,&vision)&&snapshot.plants.empty(),
              "explored fog does not retain live state");
        const std::vector<uint8_t> invalidVision{2};
        check(game::collectPlanetForestVisuals(world,surface,snapshot,&invalidVision)&&snapshot.plants.empty()&&snapshot.managedBodies[0],
              "malformed vision fails closed without cosmetic fallback");
        check(game::collectPlanetForestVisuals(world,surface,snapshot)&&snapshot.plants.size()==1,
              "spectator sees current state");
        physical.bodies.pop_back();world.setResource<wp::PlanetConsequenceResource>(physical);
        check(game::collectPlanetForestVisuals(world,surface,snapshot)&&snapshot.plants.empty(),"retired current fall does not resurrect old body");
        delivery.events.back().acknowledged=false;world.setResource<ev::PlanetRootDeliveryResource>(delivery);
        wood.ledger.logs[0].remainingUnits=0;world.setResource<ev::PlanetDeadwoodResource>(wood);
        check(game::collectPlanetForestVisuals(world,surface,snapshot)&&snapshot.plants.empty()&&snapshot.managedBodies[0],"spent tree hidden");
        authority.rooted.clear();delivery.bindings.clear();
        world.setResource<tw::TerraWeaveAuthorityResource>(authority);
        world.setResource<ev::PlanetRootDeliveryResource>(delivery);
        check(game::collectPlanetForestVisuals(world,surface,snapshot,&vision)&&snapshot.plants.empty()&&snapshot.managedBodies[0],
              "empty managed population cannot reveal hidden harvest via cosmetic scatter");
        std::puts("planet forest visual fixture PASS");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
