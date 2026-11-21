// NEREID review-fix regressions (2026-11-21): rock shores, flood flux
// carries through partial obstructions, flood flow units, dead inflow ports,
// bucketed flood obstruction lookup.
#include <aiverselib/water/NereidErosion.h>
#include <aiverselib/water/NereidFloodRouting.h>
#include "engine/world/NereidRipples.h"
#include "engine/world/NereidCascadeScene.h"
#include "engine/world/TerraWeaveSedimentEcs.h"
#include "TestCheck.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
using namespace aiverse;
namespace nw=aiverse::lib::nereid;

static void rockIsNotErodible(){
    nw::ShoreErosionCell rock;
    rock.soil=nw::ShoreSoil::Rock;
    rock.groundCm=500;
    rock.bedrockCm=-500;
    rock.suspendedCm=200;
    nw::ShoreErosionForcing violent;
    violent.flow={Fixed::fromInt(40),Fixed{},Fixed::fromInt(40)};
    violent.waveEnergyPermille=1000;
    violent.wetContactPermille=1000;
    violent.bankSlopePermille=8000;
    for(int i=0;i<100;++i){
        const auto out=nw::erodeShore(rock,violent,1000);
        AIVERSE_TEST_CHECK(!out.changed && out.removedCm==0 &&
                           out.depositedCm==0 && out.cell.groundCm==500 &&
                           out.cell.suspendedCm==200);
    }
    nw::ShoreErosionForcing quiet;
    quiet.wetContactPermille=1000; // still water: formerly max deposition
    nw::ShoreErosionSettings fast;fast.ratePermille=10000;
    const auto still=nw::erodeShore(rock,quiet,1000,fast);
    AIVERSE_TEST_CHECK(!still.changed && still.depositedCm==0 &&
                       still.cell.groundCm==500);
    // Erodible soils keep their behaviour.
    nw::ShoreErosionCell sand=rock;
    sand.soil=nw::ShoreSoil::Sand;
    AIVERSE_TEST_CHECK(nw::erodeShore(sand,violent,1000).removedCm>0);
    AIVERSE_TEST_CHECK(nw::erodeShore(sand,quiet,1000,fast).depositedCm>0);
}

static nw::WaterFloodResource twoCells(uint32_t wetCm,Entity body){
    nw::WaterFloodResource r;
    r.gridCm=100;
    nw::WaterFloodCell a;a.x=0;a.z=0;a.volumeCm=wetCm;a.waterBody=body;
    nw::WaterFloodCell b;b.x=1;b.z=0;
    r.cells={a,b};
    return r;
}

static void partialObstructionPassesWater(){
    const Entity body{3,1};
    auto r=twoCells(10,body); // head 10 cm: quantized base flux 1 cm/tick
    const nw::WaterFloodPermeability log=
        [](Entity,int64_t,int64_t,int32_t){return 250u;};
    uint64_t moved=0;
    for(uint32_t t=0;t<8;++t)
        moved+=nw::stepNereidFlood(r,t,16,{},log).movedCm;
    // 25% permeability: 1 cm every 4 ticks (formerly 0: watertight).
    AIVERSE_TEST_CHECK(moved==2);
    AIVERSE_TEST_CHECK(r.cells[0].volumeCm==8 && r.cells[1].volumeCm==2);
    const auto r9=[&]{auto c=r;(void)nw::stepNereidFlood(c,8,16,{},log);return c;}();
    AIVERSE_TEST_CHECK(r9.cells[0].carryXMilliCm==250);

    // Carries are persisted in schema v2 and absent from v1 payloads.
    auto saved=r9;
    std::string bytes;
    {Archive out(&bytes);out.formatVersion=2;nw::io(out,saved);
     AIVERSE_TEST_CHECK(out.ok());}
    nw::WaterFloodResource loaded;
    {Archive in(bytes);in.formatVersion=2;nw::io(in,loaded);
     AIVERSE_TEST_CHECK(in.ok() && in.remaining()==0);}
    AIVERSE_TEST_CHECK(loaded.cells==saved.cells);
    std::string legacy;
    {Archive out(&legacy);out.formatVersion=1;nw::io(out,saved);}
    AIVERSE_TEST_CHECK(legacy.size()+2*4==bytes.size());
    nw::WaterFloodResource old;
    {Archive in(legacy);in.formatVersion=1;nw::io(in,old);
     AIVERSE_TEST_CHECK(in.ok() && in.remaining()==0);}
    AIVERSE_TEST_CHECK(old.cells[0].carryXMilliCm==0 &&
                       old.cells[0].volumeCm==saved.cells[0].volumeCm);

    // Unobstructed routing is unchanged (no carry accumulates).
    auto open=twoCells(10,body);
    (void)nw::stepNereidFlood(open,0,16);
    AIVERSE_TEST_CHECK(open.cells[1].volumeCm==1 &&
                       open.cells[0].carryXMilliCm==0);
    // A solid obstruction stays watertight.
    auto solid=twoCells(10,body);
    const nw::WaterFloodPermeability wall=
        [](Entity,int64_t,int64_t,int32_t){return 0u;};
    for(uint32_t t=0;t<20;++t)
        AIVERSE_TEST_CHECK(nw::stepNereidFlood(solid,t,16,{},wall).movedCm==0);
}

static void floodFlowHasVelocityUnits(){
    const Entity body{3,1};
    auto r=twoCells(100,body);
    const auto tick=nw::stepNereidFlood(r,0,16);
    AIVERSE_TEST_CHECK(tick.movedCm==1);
    // 1 column-cm across a 100 cm cell in 16 ms over a 99 cm deep column:
    // 1*100*1000/(99*16) = 63 cm/s (formerly 0: units were cells/s).
    AIVERSE_TEST_CHECK(r.cells[0].flowXcmPerSec==63);
    // 1 cm deep destination: 1*100*1000/(1*16)=6250 cm/s.
    AIVERSE_TEST_CHECK(r.cells[1].flowXcmPerSec==6250);
    AIVERSE_TEST_CHECK(r.cells[0].flowZcmPerSec==0);
}

static void portsMustBeInsideGrid(){
    auto r=twoCells(10,Entity{3,1});
    nw::WaterFloodPort outside;
    outside.x=5;outside.z=5;
    outside.kind=nw::WaterFloodPortKind::Outflow;
    outside.maxCmPerTick=1;
    r.ports={outside};
    bool threw=false;
    try{nw::validateNereidFlood(r);}catch(const std::invalid_argument&){threw=true;}
    AIVERSE_TEST_CHECK(threw);
    // ...so a save carrying it is rejected at load, not on every tick.
    std::string bytes;
    {Archive out(&bytes);out.formatVersion=2;nw::io(out,r);}
    nw::WaterFloodResource loaded;
    Archive in(bytes);in.formatVersion=2;nw::io(in,loaded);
    AIVERSE_TEST_CHECK(!in.ok());
}

static void obstructionIndexMatchesScan(){
    World w;
    const Entity bodyA=w.create(),bodyB=w.create();
    for(int i=0;i<12;++i){
        const Entity e=w.create();
        nw::WaterFlowObstructionComponent o;
        o.waterBody=i%3==2?bodyB:bodyA;
        o.center={Fixed::fromRatio(i*37,10),Fixed::fromRatio(i*11-40,10)};
        o.radius=Fixed::fromRatio(15+i*5,10);
        o.bottomY=Fixed::fromRatio(-100+i*10,100);
        o.topY=o.bottomY+Fixed::fromRatio(150+i*20,100);
        o.permeabilityPermille=uint32_t(50+i*70);
        o.enabled=i%5!=4;
        w.add<nw::WaterFlowObstructionComponent>(e,o);
    }
    const nw::NereidFloodObstructionIndex index(w);
    // Reference: the former per-call ECS scan of addNereidFloodRoutingSystem.
    const auto scan=[&](Entity body,int64_t x,int64_t z,int32_t surface){
        uint32_t strength=1000u;
        w.eachExistingConst<nw::WaterFlowObstructionComponent>(
            [&](Entity id,const nw::WaterFlowObstructionComponent& obs){
                if(!w.alive(id) || !obs.enabled || obs.waterBody!=body)return;
                const Fixed fx=Fixed::fromRatio(x,100);
                const Fixed fz=Fixed::fromRatio(z,100);
                const Fixed top=Fixed::fromRatio(surface,100);
                if(obs.topY<=top-Fixed::fromInt(1) || obs.bottomY>=top ||
                   fxAbs(fx-obs.center.x)>=obs.radius ||
                   fxAbs(fz-obs.center.y)>=obs.radius)return;
                strength=std::min(strength,obs.permeabilityPermille);
            });
        return strength;
    };
    uint32_t blocked=0;
    for(const Entity body:{bodyA,bodyB,Entity{999,1}})
        for(int64_t x=-200;x<=4600;x+=50)
            for(int64_t z=-800;z<=800;z+=50)
                for(int32_t s=-150;s<=300;s+=30){
                    const uint32_t want=scan(body,x,z,s);
                    AIVERSE_TEST_CHECK(index.permeability(body,x,z,s)==want);
                    blocked+=want<1000u;
                }
    AIVERSE_TEST_CHECK(blocked>0);
}

static void wakeTileCullMatchesShaderEllipse(){
    const Entity water{4,1};
    const auto event=[&](nw::WaterInteractionKind kind){
        nw::WaterPresentationEvent e;
        e.source=Entity{9,1};
        e.event.kind=kind;
        e.event.water=water;
        e.event.radius=Fixed::fromInt(2);
        e.event.strength=Fixed::fromInt(4);
        e.event.tick=1;
        e.movementDirection={Fixed::fromInt(1),Fixed{}};
        return std::vector<nw::WaterPresentationEvent>{e};
    };
    const uint64_t bodyId=water.packed()+1;
    // ring 2 m + 2.5 m band: the wake shader ellipse reaches 4.5/0.75 = 6 m
    // ahead; a tile at 5..5.5 m used to be culled by the circular test.
    world::NereidRippleField wake;
    wake.ingest(event(nw::WaterInteractionKind::SurfaceWake),1);
    const auto ahead=wake.shaderPacketForTile(bodyId,0,0,5.0,5.5,-0.1,0.1);
    AIVERSE_TEST_CHECK(ahead.rings[0].w>0.f && ahead.shapes[0].w>0.5f &&
                       ahead.shapes[0].z==1.35f);
    AIVERSE_TEST_CHECK(wake.shaderPacketForTile(
        bodyId,0,0,6.5,7.0,-0.1,0.1).rings[0].w==0.f);
    // A plain ring keeps its circular bound.
    world::NereidRippleField ring;
    ring.ingest(event(nw::WaterInteractionKind::Enter),1);
    AIVERSE_TEST_CHECK(ring.shaderPacketForTile(
        bodyId,0,0,5.0,5.5,-0.1,0.1).rings[0].w==0.f);
    AIVERSE_TEST_CHECK(ring.shaderPacketForTile(
        bodyId,0,0,4.0,4.4,-0.1,0.1).rings[0].w>0.f);
}

static void cascadeMeshAddressSurvivesSetCascades(){
    world::NereidCascadeScene scene;
    world::NereidCascadeSource a;a.id=7;a.cascade.enabled=false;
    world::NereidCascadeSource b;b.id=8;b.cascade.enabled=false;
    scene.setCascades({a});
    const render::Mesh* before=scene.meshFor(7);
    AIVERSE_TEST_CHECK(before!=nullptr);
    // Renderer3D items_ keep &mesh until render: the object must not move.
    scene.setCascades({a,b});
    AIVERSE_TEST_CHECK(scene.meshFor(7)==before && scene.meshFor(8)!=nullptr);
    scene.setCascades({b});
    AIVERSE_TEST_CHECK(scene.meshFor(7)==nullptr && scene.count()==1);
}

static void reAddedSurfaceGetsFreshRevision(){
    world::NereidWaterScene scene;
    world::NereidSceneSurface lake;
    lake.id=10;lake.body.center.x=4_fx;
    std::vector<uint64_t> seen;
    scene.setSurfaces({lake});
    seen.push_back(scene.surfaceRevision(10));
    lake.body.center.x=8_fx;          // changed: new revision
    scene.setSurfaces({lake});
    seen.push_back(scene.surfaceRevision(10));
    scene.setSurfaces({});            // removed (a job could be pending)
    scene.setSurfaces({lake});        // re-added
    const uint64_t again=scene.surfaceRevision(10);
    for(const uint64_t old:seen)AIVERSE_TEST_CHECK(again!=old);
    AIVERSE_TEST_CHECK(seen[0]!=seen[1] && again!=0);
}

static void duplicateSedimentLinksDoNotThrow(){
    World w;
    const auto ocean=[&]{
        const Entity e=w.create();
        nw::WaterBodyComponent body;
        body.kind=nw::WaterKind::Ocean;
        body.footprint=nw::WaterFootprint::Infinite;
        body.surfaceY=10_fx;body.bottomY=-100_fx;
        w.add<nw::WaterBodyComponent>(e,body);
        return e;
    };
    const Entity a=ocean(),b=ocean(),c=ocean();
    // 600 identical junctions = 1200 raw links (> the 1024 kernel budget)
    // but only two distinct body pairs.
    for(int i=0;i<600;++i){
        nw::WaterConfluenceComponent j;
        j.inletCount=2;j.inlets[0]=a;j.inlets[1]=b;j.outlet=c;
        AIVERSE_TEST_CHECK(nw::validWaterConfluence(j));
        w.add<nw::WaterConfluenceComponent>(w.create(),j);
    }
    const auto result=terraweave::transportNereidBankSediment(w,1,100,16);
    AIVERSE_TEST_CHECK(result.visitedBanks==0 && result.transferCount==0);
}

int main(){
    rockIsNotErodible();
    partialObstructionPassesWater();
    floodFlowHasVelocityUnits();
    portsMustBeInsideGrid();
    obstructionIndexMatchesScan();
    wakeTileCullMatchesShaderEllipse();
    cascadeMeshAddressSurvivesSetCascades();
    reAddedSurfaceGetsFreshRevision();
    duplicateSedimentLinksDoNotThrow();
    std::cout<<"PASS: NEREID review fixes (rock, flood carry/flow/ports/index, wake cull, cascade mesh, surface revision, sediment links)\n";
}
