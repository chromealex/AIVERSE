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
        // The same manifest profile schema as the flat TerraWeave owner.
        const auto authored=Json::parse(R"({"species":[{"id":42,"preset":"oak","prototypeSeed":7},{"id":77,"shape":"columnar","minHeightCm":500,"maxHeightCm":500,"branches":5}]})");
        const auto models=game::buildPlanetForestSpecies(authored);
        check(models.size()==2&&models.contains(42)&&models.contains(77)&&!models.contains(1),"exact authored species IDs");
        check(models.at(42)->materials.size()==2&&models.at(77)->materials.size()==2,"bark foliage contract");
        bool duplicate=false;
        try{(void)game::buildPlanetForestSpecies(Json::parse(R"({"species":[{"id":42},{"id":42}]})"));}
        catch(const std::exception&){duplicate=true;}
        check(duplicate,"duplicate species rejected");
        const auto map=Json::parse(R"({"world":{"bodies":[{"id":"terra","faceSize":8}],"terraweaveForest":{"species":[{"id":42,"preset":"oak"}]}}})");
        const auto visualWorld=game::loadPlanetWorld(map);
        check(visualWorld.forestManifest["species"].size()==1,"map catalog preserved");
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
        // Consecutive confirmed physical poses, no extrapolation.
        game::PlanetForestMotionHistory history;
        physical.bodies.back().lastMotionTick=3;
        world.setResource<wp::PlanetConsequenceResource>(physical);
        check(game::collectPlanetForestVisuals(world,surface,snapshot),"first checkpoint");
        history.interpolate(snapshot,surface,&world,1,3,0,0.f,nullptr);
        const auto first=snapshot.plants[0].local;
        physical.bodies.back().lastMotionTick=4;
        physical.bodies.back().motion.position=ground+Vec3{1_fx,0_fx,0_fx};
        physical.bodies.back().motion.rotation=*fxQuaternionAxisAngle({0_fx,0_fx,1_fx},Fixed::fromRatio(1,4));
        world.setResource<wp::PlanetConsequenceResource>(physical);
        check(game::collectPlanetForestVisuals(world,surface,snapshot),"second checkpoint");
        const auto last=snapshot.plants[0].local;
        history.interpolate(snapshot,surface,&world,1,4,0,.5f,nullptr);
        check(std::abs(snapshot.plants[0].local.x-(first.x+last.x)*.5f)<.001f,"half tick position");
        check(std::abs(snapshot.plants[0].up.x)>.5f&&std::abs(snapshot.plants[0].up.y)>.5f,"half tick rotation");
        check(game::collectPlanetForestVisuals(world,surface,snapshot),"repeat current checkpoint");
        history.interpolate(snapshot,surface,&world,1,4,0,.75f,nullptr);
        check(std::abs(snapshot.plants[0].local.x-(first.x*.25f+last.x*.75f))<.001f,"same tick preserves endpoints");
        check(game::collectPlanetForestVisuals(world,surface,snapshot),"restore same tick checkpoint");
        history.interpolate(snapshot,surface,&world,2,4,0,0.f,nullptr);
        check(std::abs(snapshot.plants[0].local.x-last.x)<.001f,"restore serial snaps");
        // Restricted interpolation must never bridge cells through fog.
        game::PlanetForestMotionHistory restricted;
        std::vector<uint8_t> allVisible(size_t(surface.W)*size_t(surface.H),2);
        check(game::collectPlanetForestVisuals(world,surface,snapshot,&allVisible),"restricted first pose");
        restricted.interpolate(snapshot,surface,&world,2,4,1,1.f,&allVisible);
        physical.bodies.back().lastMotionTick=5;
        physical.bodies.back().motion.position=surface.cellCenter(surface.gridCell(0,1,3,3));
        world.setResource<wp::PlanetConsequenceResource>(physical);
        check(game::collectPlanetForestVisuals(world,surface,snapshot,&allVisible),"restricted next pose");
        const auto crossCell=snapshot.plants[0].local;
        restricted.interpolate(snapshot,surface,&world,2,5,1,0.f,&allVisible);
        check(std::abs(snapshot.plants[0].local.x-crossCell.x)<.001f&&
              std::abs(snapshot.plants[0].local.y-crossCell.y)<.001f&&
              std::abs(snapshot.plants[0].local.z-crossCell.z)<.001f,"restricted cross-cell snap");
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
