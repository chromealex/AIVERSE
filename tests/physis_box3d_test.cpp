#include "engine/physics/PhysisBox3D.h"
#include "engine/world/TerraWeaveBox3DTerrain.h"
#include <cmath>
#include <cstdio>
using namespace aiverse;
using namespace aiverse::physics;
using namespace aiverse::physis;
namespace {
int failures=0;
#define CHECK(v) do {if(!(v)){std::printf("PHYSIS BOX3D FAIL %s:%d %s\n",__FILE__,__LINE__,#v);++failures;}}while(0)
BodyDesc box(float y=5.0f) {
    BodyDesc d;
    d.motion=Motion::Dynamic;
    d.shape=ShapeKind::Box;
    d.position={0,y,0};
    d.size={1,1,1};
    d.linearDamping=0.0f;
    d.angularDamping=0.0f;
    return d;
}
void testNoDoubleGravity() {
    aiverse::World physisWorld;
    Settings settings;
    settings.width=2;settings.height=2;
    initialize(physisWorld,settings);
    Scene noAdapter, withAdapter;
    auto one=noAdapter.create(box());
    auto two=withAdapter.create(box());
    CHECK(one!=kNoBody && two!=kNoBody);
    PhysisBodyMaterial material;material.dragCoefficient=Fixed{};
    for(int i=0;i<10;++i) {
        noAdapter.step();
        CHECK(applyPhysisToBody(withAdapter,physisWorld,two,material));
        withAdapter.step();
    }
    CHECK(std::abs(noAdapter.position(one).y-withAdapter.position(two).y)<0.01f);
    CHECK(std::abs(noAdapter.linearVelocity(one).y-withAdapter.linearVelocity(two).y)<0.01f);
    CHECK(!applyPhysisToBody(withAdapter,physisWorld,kNoBody,material));
}
void testZeroGAndWindAndVortex() {
    aiverse::World world;
    Settings settings;settings.width=2;settings.height=2;
    initialize(world,settings);
    GravitySettings zero;zero.baseline={};
    world.setResource<GravitySettings>(zero);
    Scene zeroGravity;
    auto still=zeroGravity.create(box());
    PhysisBodyMaterial material;material.dragCoefficient=Fixed{};
    for(int i=0;i<30;++i) {
        CHECK(applyPhysisToBody(zeroGravity,world,still,material));
        zeroGravity.step();
    }
    CHECK(std::abs(zeroGravity.position(still).y-5.0f)<0.05f);

    Scene windScene({.gravity={0,0,0}});
    auto windBody=windScene.create(box());
    WindZone wind;
    wind.center={0_fx,5_fx,0_fx};
    wind.radiusMeters=50_fx;
    wind.velocity={20_fx,0_fx,0_fx};
    world.add<WindZone>(world.create(),wind);
    material.dragCoefficient=1_fx;
    material.areaSqMeters=2_fx;
    for(int i=0;i<10;++i) {
        CHECK(applyPhysisToBody(windScene,world,windBody,material));
        windScene.step();
    }
    CHECK(windScene.linearVelocity(windBody).x>0.001f);

    Scene tornadoScene({.gravity={0,0,0}});
    auto lifted=tornadoScene.create(box(0));
    StormEvent storm;
    storm.kind=StormKind::Tornado;
    storm.center={0_fx,0_fx,0_fx};
    storm.eyeRadiusMeters=5_fx;
    storm.outerRadiusMeters=100_fx;
    storm.heightMeters=200_fx;
    storm.updraft=100_fx;
    storm.radialInflow=0_fx;
    world.add<StormEvent>(world.create(),storm);
    const auto before=tornadoScene.position(lifted).y;
    for(int i=0;i<20;++i) {
        CHECK(applyPhysisToBody(tornadoScene,world,lifted,material));
        tornadoScene.step();
    }
    CHECK(tornadoScene.position(lifted).y>before);
}
}
// Review fix: an older terrain bake must not replace a newer Box3D collider.
void testTerrainBox3DRevisionOrdering() {
    using namespace aiverse::terraweave;
    EarthworksLayer earth(100,[](int64_t,int64_t){return int32_t(0);});
    TerrainPatch older=bakeEarthworksTerrainPatch(earth,{1,0,2},16,3);
    TerrainPatch newer=older;
    older.sourceRevision=3;
    newer.sourceRevision=7;
    Scene scene;
    TerrainBox3DPublisher publisher;
    CHECK(publisher.publish(scene,newer));
    const auto body=publisher.body({1,0,2});
    CHECK(body!=kNoBody && publisher.revision({1,0,2})==7);
    CHECK(!publisher.publish(scene,older));      // stale: rejected
    CHECK(publisher.body({1,0,2})==body);        // scene untouched
    CHECK(publisher.revision({1,0,2})==7);
    CHECK(scene.valid(body));
    newer.sourceRevision=8;
    CHECK(publisher.publish(scene,newer));       // newer: replaces
    CHECK(publisher.revision({1,0,2})==8);
    CHECK(!scene.valid(body) && scene.valid(publisher.body({1,0,2})));
    publisher.unload(scene,{1,0,2});             // rollback path
    CHECK(publisher.publish(scene,older) && publisher.revision({1,0,2})==3);
    publisher.shutdown(scene);
}
int main(){testNoDoubleGravity();testZeroGAndWindAndVortex();
testTerrainBox3DRevisionOrdering();
std::printf("PHYSIS Box3D: %s\n",failures?"FAIL":"PASS");
return failures?1:0;}
