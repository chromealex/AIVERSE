// NEREID stages 41-44 contract: JSON authoring, stable link IDs,
// ECS reference/indexed parity and read-only scene current projection.
// CPU-only test; does not require a graphics Device or window.
#include "engine/world/NereidConfluenceManifest.h"
#include "engine/world/NereidConfluenceEcsPresentation.h"
#include "engine/world/NereidEcsPresentation.h"
#include "engine/world/nereid/NereidQueryIndex.h"
#include "engine/world/nereid/NereidWaterSystems.h"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse;
namespace nw=aiverse::lib::nereid;

static void require(bool value,const char* why) {
    if(!value)throw std::runtime_error(why);
}
static Json baseManifest() {
    return Json::parse(R"json({
      "nereidWater": {
        "surfaces": [
          {"id":11,"kind":"river","shape":"box","center":[-2,5,0],
           "halfExtents":[10,10],"surfaceY":5,"bottomY":-2,"flow":[4,0,0]},
          {"id":12,"kind":"river","shape":"box","center":[2,5,0],
           "halfExtents":[10,10],"surfaceY":5,"bottomY":-2,"flow":[0,0,4]},
          {"id":13,"kind":"river","shape":"box","center":[0,5,2],
           "halfExtents":[10,10],"surfaceY":5,"bottomY":-2,
           "priority":10,"flow":[0,0,0]}
        ]
      },
      "nereidConfluences": {
        "sources": [{
          "id":101, "inletWaterIds":[11,12],"outletWaterId":13,
          "mouth":[0,5,0], "radius":8,"blend":0.75,
          "speedLimit":30,"enabled":true
        }]
      }
    })json");
}
static Json& firstJoin(Json& root) {
    return root.member("nereidConfluences").member("sources").at(0);
}
static bool invalid(const Json& root) {
    try {
        const auto water=world::parseNereidWaterManifest(root);
        (void)world::parseNereidConfluenceManifest(root,water.surfaces);
    }catch(const JsonError&) {return true;}
    return false;
}
static void checkManifest() {
    const Json root=baseManifest();
    const auto water=world::parseNereidWaterManifest(root);
    const auto parsed=world::parseNereidConfluenceManifest(root,water.surfaces);
    require(parsed.sources.size()==1,"valid JSON confluence not loaded");
    const auto& source=parsed.sources.front();
    require(source.id==101 && source.outletWaterId==13 &&
            source.inletWaterIds[0]==11 && source.inletWaterIds[1]==12 &&
            source.junction.inletCount==2,
            "confluence scene IDs or inlet count lost");
    require(source.junction.outlet==kNullEntity &&
            source.junction.inlets[0]==kNullEntity &&
            nw::validWaterConfluenceFlowPolicy(source.junction) &&
            !nw::validWaterConfluence(source.junction),
            "JSON scene links must never masquerade as ECS Entity handles");

    world::NereidWaterScene sceneWater;
    sceneWater.setSurfaces(water.surfaces);
    world::NereidConfluenceScene sceneJoin;
    sceneJoin.setConfluences(parsed.sources);
    const Vec3 probe{0_fx,4_fx,0_fx};
    const Vec3 expected{Fixed::fromRatio(3,2),0_fx,
                        Fixed::fromRatio(3,2)};
    require(sceneJoin.flowForWater(13,probe,{},sceneWater)==expected,
            "static manifest confluence blend mismatch");
    require(sceneJoin.flowForWater(13,{100_fx,4_fx,0_fx},{},sceneWater)==Vec3{},
            "out-of-radius confluence must preserve base flow");
    require(sceneJoin.flowForWater(11,probe,{},sceneWater)==Vec3{},
            "confluence must affect only linked outlet");
    require(world::parseNereidConfluenceManifest(
            Json::parse("{}"),water.surfaces).sources.empty(),
            "legacy manifest unexpectedly creates confluences");

    auto bad=baseManifest();
    firstJoin(bad).set("outletWaterId",Json::number(int64_t(11)));
    require(invalid(bad),"self-link must be rejected");
    bad=baseManifest();
    firstJoin(bad).set("outletWaterId",Json::number(int64_t(500)));
    require(invalid(bad),"unknown water ID must be rejected");
    bad=baseManifest();
    Json duplicate=Json::array();
    duplicate.push(Json::number(int64_t(11)));
    duplicate.push(Json::number(int64_t(11)));
    firstJoin(bad).set("inletWaterIds",duplicate);
    require(invalid(bad),"duplicate inlet IDs must be rejected");
    bad=baseManifest();
    Json mouth=Json::array();
    mouth.push(Json::number(int64_t(100)));
    mouth.push(Json::number(int64_t(5)));
    mouth.push(Json::number(int64_t(100)));
    firstJoin(bad).set("mouth",mouth);
    require(invalid(bad),"out-of-footprint mouth must be rejected");
    bad=baseManifest();
    firstJoin(bad).set("radius",Json::number(int64_t(0)));
    require(invalid(bad),"non-positive confluence radius must be rejected");
    bad=baseManifest();
    firstJoin(bad).set("enabled",Json::string("no"));
    require(invalid(bad),"nonboolean enabled must be rejected");
    bad=baseManifest();
    Json fractional=Json::array();
    fractional.push(Json::numberText("11.5"));
    fractional.push(Json::number(int64_t(12)));
    firstJoin(bad).set("inletWaterIds",fractional);
    require(invalid(bad),"fractional water ID must be rejected");
    bad=baseManifest();
    bad.member("nereidWater").member("surfaces").at(0).erase("id");
    require(invalid(bad),"referenced water must have explicit stable ID");
    bad=baseManifest();
    auto repeated=firstJoin(bad);
    bad.member("nereidConfluences").member("sources").push(repeated);
    require(invalid(bad),"duplicate confluence IDs must be rejected");
    bad=baseManifest();
    firstJoin(bad).set("enabled",Json::fromBool(false));
    const auto disabledWater=world::parseNereidWaterManifest(bad);
    require(world::parseNereidConfluenceManifest(
            bad,disabledWater.surfaces).sources.empty(),
            "disabled junction must not publish visual flow");
}
static void checkEcsAndSceneParity() {
    const Json raw=baseManifest();
    const auto water=world::parseNereidWaterManifest(raw);
    World ecs;
    std::vector<Entity> ids;
    for(const auto& source:water.surfaces) {
        const Entity id=ecs.create();
        ecs.add<nw::WaterBodyComponent>(id,source.body);
        ids.push_back(id);
    }
    nw::WaterConfluenceComponent join;
    join.inletCount=2;
    join.inlets[0]=ids[0];join.inlets[1]=ids[1];
    join.outlet=ids[2];
    join.mouth={0_fx,5_fx,0_fx};
    join.radius=8_fx;
    join.blend=Fixed::fromRatio(3,4);
    join.speedLimit=30_fx;
    require(nw::validWaterConfluence(join),"valid ECS junction rejected");
    const Entity junction=ecs.create();
    ecs.add<nw::WaterConfluenceComponent>(junction,join);
    const Vec3 probe{0_fx,4_fx,0_fx};
    const auto reference=nw::sampleWater(ecs,probe);
    const nw::WaterQueryIndex index(ecs);
    const auto indexed=index.sample(probe);
    require(reference.found && reference.submerged &&
            reference.body==ids[2] &&
            reference.flow==indexed.flow &&
            indexed.body==reference.body,
            "reference/indexed confluence contact parity failed");
    const Vec3 expected{Fixed::fromRatio(3,2),0_fx,
                        Fixed::fromRatio(3,2)};
    require(reference.flow==expected,"ECS confluence current mismatch");

    world::NereidWaterScene sceneWater;
    sceneWater.setSurfaces(world::snapshotNereidEcsWater(ecs));
    world::NereidConfluenceScene scene;
    scene.setConfluences(world::snapshotNereidEcsConfluences(ecs));
    require(scene.flowForWater(uint64_t(ids[2].packed())+1,
                              probe,{},sceneWater)==reference.flow,
            "ECS-to-scene flow projection differs from gameplay");
    auto& policy=ecs.get<nw::WaterConfluenceComponent>(junction);
    policy.enabled=false;
    require(nw::sampleWater(ecs,probe).flow==Vec3{},
            "disabled ECS confluence must preserve baseline flow");
}
static void checkMaterialKeys() {
    nw::WaterSurfaceStyle initial;
    auto updated=initial;
    updated.rapidFoamR=0.7f;
    updated.rapidDetail=nw::WaterRapidDetail::High;
    updated.refractionStrength=0.2f;
    const auto first=render::makeNereidSurfaceKey(1,false,{},initial);
    const auto second=render::makeNereidSurfaceKey(1,false,{},updated);
    require(!render::sameNereidSurfaceKey(first,second) &&
            render::sameNereidSurfaceUploadKey(first,second),
            "draw-only style must not invalidate GPU mesh");
    updated.foamStrength=0.2f;
    const auto baked=render::makeNereidSurfaceKey(1,false,{},updated);
    require(!render::sameNereidSurfaceUploadKey(first,baked),
            "baked shoreline tint must invalidate uploaded mesh");
}
int main() {
    checkManifest();
    checkEcsAndSceneParity();
    checkMaterialKeys();
    std::cout<<"PASS NEREID confluence JSON, ECS/indexed/scene and upload keys\n";
}
