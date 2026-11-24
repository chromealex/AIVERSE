// Real native scenes, no GPU required for the baseline lifecycle.
#include "engine/world/TerraWeaveCavernsPlanetCollisionBinding.h"
#include "engine/world/TerraWeaveCavernsPlanetRuntimeFrame.h"
#include "engine/physics/TerraWeaveCavernsPlanetMotorBridge.h"
#include "engine/world/TerraWeaveCavernsPlanetGiPublication.h"
#include "engine/world/TerraWeaveCavernsPlanetNativeHost.h"
#include "engine/physics/TerraWeaveCavernsNativeCheckpoint.h"
#include <stdexcept>
#include <iostream>
using namespace aiverse;
using namespace aiverse::terraweave::caverns;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void checkBaselineTransfer(){
    physics::Scene box;world::SpatialWorld spatial;CaveColliderLeasePool pool(20000,100);
    CavePlanetBaselineCollision exterior(box,spatial,pool),interior(box,spatial,pool);
    const auto p=[](int x,int z){return Vec3{Fixed::fromInt(x),10_fx,Fixed::fromInt(z)};};
    CavePlanetSurfaceSlot slot{{2,0,0,1,0},{{p(0,0),p(0,2),p(2,0)}}};
    const auto exclusive=[](){return true;};
    check(exterior.prepare(0,7,std::span<const CavePlanetSurfaceSlot>(&slot,1),
        [](const auto&){return world::Cell{0,100,0};},[](const auto&){return true;},exclusive)&&
        exterior.publish(exclusive),"exterior baseline published");
    const auto mutation=spatial.mutationVersion();
    physics::Scene foreignBox;CavePlanetBaselineCollision foreign(foreignBox,spatial,pool);
    check(!exterior.transferSolidLeafTo(slot.key,foreign,exclusive),"foreign physics owner refused");
    check(!exterior.transferSolidLeafTo(slot.key,interior,[]{return false;}),"transfer requires barrier");
    check(exterior.transferSolidLeafTo(slot.key,interior,exclusive)&&interior.solidReady()&&
        exterior.phase()==CavePlanetBaselineCollision::Phase::Empty,"transfer actual solid leaf");
    check(box.bodyCount()==1&&pool.allocations()==1&&spatial.mutationVersion()==mutation,"no native duplicate or rebuild");
    check(interior.retireForCut(exclusive)&&interior.prepareRestore(exclusive)&&interior.publish(exclusive),"restore borrowed baseline");
    check(interior.transferSolidLeafTo(slot.key,exterior,exclusive)&&exterior.solidReady(),"return restored leaf");
    check(exterior.shutdown(exclusive)&&box.bodyCount()==0&&pool.allocations()==0,"exterior owns final retirement");
}
static void checkCandidateIdentity(){
    World ecs;
    CavePlanetAuthoritySet set;
    CavePlanetAuthorityEntry e;
    e.id=2;e.region.anchor={"planet",0,{}, {},800};
    e.authority.recipeId=planetCaveConfig(2,1).recipeId;
    e.authority.volume.settings.minYcm=-600;
    e.authority.volume.settings.maxYcm=-100;
    set.regions.push_back(e);ecs.setResource<CavePlanetAuthoritySet>(set);
    auto cut=std::make_shared<CavePlanetBodyCutPlan>();
    auto region=std::make_shared<CavePlanetCutPlan>();region->anchor=e.region.anchor;
    cut->regions.emplace(2,region);
    CavePlanetRuntimeCandidate candidate;
    candidate.generation=3;candidate.nativeRevision=5;candidate.cut=cut;
    candidate.sources.emplace(2,freezePlanetCaveRegion(ecs,2));
    check(candidate.current(ecs,3),"candidate current");
    check(!candidate.current(ecs,4),"rollback generation fence");
    auto surface=std::make_shared<game::Surface>();
    game::BodyDef body;body.id="planet";body.radius=100_fx;body.faceSize=16;
    surface->bodies.push_back(body);surface->build();
    CavePlanetDemandConfig cfg;
    const auto keys=planetInteriorKeys(e.region.anchor,cfg);
    candidate.navigationCoverage.emplace(2,CavePlanetNavigationCoverage::certify(e.region.anchor,cfg,keys));
    uint64_t epoch=7;
    const CavePlanetNavigationInputSource inputs=[&](const World&,uint64_t id)
        ->std::optional<CavePlanetNavigationInputs>{
        if(id!=2||!epoch)return {};
        CavePlanetNavigationInputs input;input.environmentRevision=epoch;
        input.physisCell=[](PointCm)->std::optional<std::pair<int32_t,int32_t>>{return {};};
        return input;
    };
    auto frozen=std::make_shared<const World>(ecs);
    auto first=candidate.prepareNavigationLeases(surface,frozen,inputs);
    check(first&&first->size()==1&&candidate.navigationLeases.empty(),"preparation is private");
    candidate.navigationLeases=std::move(*first);
    const auto old=candidate.navigationLeases.at(2);
    check(old.matches(2,3,5,1,7),"prepared identity");
    auto same=candidate.prepareNavigationLeases(surface,frozen,inputs);
    check(same&&same->at(2).provider==old.provider,"unchanged epoch reuses provider");
    epoch=8;
    auto next=candidate.prepareNavigationLeases(surface,frozen,inputs);
    check(next&&next->at(2).provider!=old.provider,"environment refresh creates provider");
    check(candidate.navigationLeases.at(2).provider==old.provider,"refresh not published during prepare");
    candidate.navigationLeases.swap(*next);
    check(candidate.navigationLeases.at(2).matches(2,3,5,1,8),"refresh keeps native epoch");
    epoch=7;
    check(!candidate.prepareNavigationLeases(surface,frozen,inputs),"regressing epoch refused");
    epoch=0;
    check(!candidate.prepareNavigationLeases(surface,frozen,inputs),"missing environment refused");
    check(candidate.navigationLeases.at(2).provider->environmentRevision()==8,"failed prepare preserves active map");
    set.regions.front().authority.volume.settings.seed+=1;
    ecs.setResource<CavePlanetAuthoritySet>(set);
    check(!candidate.current(ecs,3),"equal numeric revision but changed snapshot");
}
static void checkOrientedMotor(){
    physics::Scene scene;
    physics::BodyDesc floor;floor.motion=physics::Motion::Static;
    floor.shape=physics::ShapeKind::Box;floor.size={1,20,20};floor.position={-.5f,0,0};
    check(scene.create(floor)!=physics::kNoBody,"side support");
    physics::CharacterParams params;physics::CharacterState state;
    state.position={2,0,0};
    const auto q=b3MakeQuatFromAxisAngle({0,0,1},-1.57079632679f);
    const auto proxy=scene.createCharacter(params,state.position);
    check(proxy!=physics::kNoBody&&scene.moveCharacterOriented(proxy,state,q,true),"oriented proxy");
    for(int i=0;i<90;++i){
        check(physics::stepCharacterOriented(scene.world(),params,state,{},scene.dt(),q,nullptr,proxy),"oriented fall");
        check(scene.moveCharacterOriented(proxy,state,q),"oriented proxy movement");scene.step();
    }
    check(state.grounded&&std::abs(state.position.x)<.15f,"landed on +X support");
    physics::CharacterInput jump;jump.jump=true;
    check(physics::stepCharacterOriented(scene.world(),params,state,jump,scene.dt(),q,nullptr,proxy),"oriented jump");
    check(state.velocity.x>0,"jump follows support up");
    const auto before=state.position;
    check(!physics::stepCharacterOriented(scene.world(),params,state,{},scene.dt(),{{0,0,0},0}),"invalid quaternion refused");
    check(state.position.x==before.x&&state.position.y==before.y&&state.position.z==before.z,"invalid orientation moved character");
}
static void checkTravelPins(){
    World world;
    const auto actor=world.create();
    world.add<CavePlanetTravel>(actor,CavePlanetTravel{9,0,0,0});
    check(cavePlanetTravelPins(world).contains(9),"stationary cave occupant pinned without route");
    check(cavePlanetInteriorMotorAllowed(world,actor,9),"occupied chart motor");
    world.get<CavePlanetTravel>(actor)={9,9,7,1};
    check(cavePlanetTravelMotor(world,actor)==CavePlanetTravelMotor::Crossing&&
          !cavePlanetInteriorMotorAllowed(world,actor,9),"crossing excludes regular motor");
    check(!cancelCavePlanetCrossing(world,actor,[](const auto&,auto,const auto&){return false;})&&
          cavePlanetTravelPins(world).contains(9),"unsafe cancel preserves pin");
    check(cancelCavePlanetCrossing(world,actor,[](const auto&,auto,const auto&){return true;})&&
          cavePlanetTravelPins(world).contains(9),"exit cancel preserves occupancy");
}
static void checkExactGiBaselineRemap(){
    const CavePlanetGiProject old=[](const std::string&,Vec3 p){
        return render::float3{float(p.x.toDouble()),float(p.y.toDouble()),float(p.z.toDouble())};
    };
    const CavePlanetGiProject next=[old](const std::string& b,Vec3 p){
        const auto q=old(b,p);return render::float3{q.z+1,q.y-2,-q.x};
    };
    const world::Triangle triangle{{0_fx,0_fx,0_fx},{1_fx,0_fx,0_fx},{0_fx,0_fx,1_fx}};
    const auto remap=exactCavePlanetGiBaselineRemap(old,next,{{7,{"planet",{triangle}}}});
    const auto p=remap(7,old("planet",triangle.b)),expected=next("planet",triangle.b);
    check(p.x==expected.x&&p.y==expected.y&&p.z==expected.z,"exact canonical GI rebase");
    bool refused=false;try{(void)remap(8,{});}catch(const std::out_of_range&){refused=true;}
    check(refused,"unowned GI chunk refused");
    refused=false;try{(void)remap(7,{20,20,20});}catch(const std::out_of_range&){refused=true;}
    check(refused,"unknown baseline vertex refused");
}
static void checkNativeCheckpointCodec(){
    CaveNativeAirborneCheckpoint c;c.actor={7,2};c.simulationTick=21;c.checksum=123;
    c.controller.position={1,2,3};c.controller.velocity={.25f,0,-.5f};c.simulation="fixture";
    const auto bytes=encodeCaveNativeCheckpoint(c);
    auto restored=decodeCaveNativeCheckpoint(bytes);
    check(restored&&restored->actor==c.actor&&restored->simulationTick==21&&
        restored->controller.velocity.z==-.5f&&restored->simulation=="fixture","native checkpoint roundtrip");
    check(!decodeCaveNativeCheckpoint(bytes.substr(0,bytes.size()-1)),"truncated checkpoint refused");
    check(!decodeCaveNativeCheckpoint(bytes+"x"),"trailing checkpoint bytes refused");
    c.controller.grounded=true;check(encodeCaveNativeCheckpoint(c).empty(),"unsupported moving ground refused");
    c.controller.grounded=false;
    physics::Scene scene;physics::CharacterParams params;physics::CharacterState state;
    const auto proxy=scene.createCharacter(params,{0,0,0});
    check(proxy!=physics::kNoBody&&restoreCaveNativeAirborneProxy(scene,proxy,c,state)&&
        caveNativeProxyPositionMatches(scene,proxy,state)&&state.velocity.z==-.5f,"actual native proxy restore");
    check(scene.destroy(proxy),"destroy restored proxy");
}
static void checkNativeDispatchGuard(){
    World world;const auto a=world.create(),b=world.create();
    CavePlanetMotorTickGuard guard;
    check(guard.claim(4,10,a,1),"first motor dispatch");
    check(!guard.claim(4,10,a,2),"same entity duplicate");
    check(!guard.claim(4,10,b,1),"same proxy duplicate");
    check(!guard.claim(4,11,b,2),"simulation advanced without physics");
    check(guard.claim(4,10,b,2),"distinct actor same tick");
    check(!guard.claim(5,10,a,1),"physics advanced without simulation");
    check(guard.claim(5,11,a,1),"next physics tick");
    check(!guard.claim(4,10,a,1),"old physics tick refused");
    guard.reset();
    check(guard.claim(1,2,a,1),"synchronized restore clears derived receipt");
}
static void checkTravelModeSelection(){
    CavePlanetTraversalCapabilities caps;
    auto choose=[&](CaveTraversal current,bool support,bool ground,bool swim,bool fly,bool wet,std::optional<Fixed> depth){
        return selectCavePlanetTraversal(caps,current,support,ground,swim,fly,wet,depth,
            Fixed::fromRatio(12,10),1_fx);
    };
    check(choose(CaveTraversal::Ground,true,true,false,true,false,{})==CaveTraversal::Ground,"dry support selects ground");
    check(!choose(CaveTraversal::Ground,false,false,false,true,false,{}),"no granted flight");
    caps.swim=true;
    check(choose(CaveTraversal::Ground,true,true,true,false,true,2_fx)==CaveTraversal::Swimming,"deep water selects swim");
    check(choose(CaveTraversal::Swimming,true,true,true,false,true,Fixed::fromRatio(11,10))==CaveTraversal::Swimming,"swim hysteresis");
    caps.flight=true;
    check(choose(CaveTraversal::Ground,false,false,false,true,false,{})==CaveTraversal::Flying,"capable zero-G flight");
    check(!choose(CaveTraversal::Ground,false,false,false,true,true,{}),"underwater flight requires explicit capability");
}
static void checkTravelReceipts(){
    using P=CavePlanetCrossingPhase;using S=CavePlanetCapsuleSide;
    const CavePlanetTravel start{0,9,7,10};
    check(!advanceCavePlanetTravelReceipt(start,10,3,false,false,S::Destination),"no cap bypass");
    auto approach=advanceCavePlanetTravelReceipt(start,10,3,false,false,S::Original);
    check(approach&&approach->phase==P::Approach,"approach receipt");
    auto cross=advanceCavePlanetTravelReceipt(*approach,11,3,true,false,S::Straddling);
    check(cross&&cross->phase==P::Clearance&&cross->crossedSourceRevision==3,"partial crossing receipt");
    check(!advanceCavePlanetTravelReceipt(*cross,11,3,false,false,S::Destination),"duplicate tick refused");
    check(!advanceCavePlanetTravelReceipt(*cross,12,4,false,false,S::Destination),"edited cap receipt refused");
    std::string bytes;Archive writer(&bytes);writer(*cross);
    CavePlanetTravel restored;Archive reader(static_cast<const std::string&>(bytes));reader(restored);
    check(writer.ok()&&reader.ok()&&!reader.remaining()&&restored.phase==P::Clearance&&
          restored.lastReceiptTick==11&&restored.crossedSourceRevision==3,"crossing receipt archive roundtrip");
    auto invalid=restored;invalid.crossedSourceRevision=0;
    std::string corrupt;Archive badWriter(&corrupt);badWriter(invalid);
    Archive badReader(static_cast<const std::string&>(corrupt));badReader(restored);
    check(!badReader.ok(),"malformed clearance restore refused");
    auto clear=advanceCavePlanetTravelReceipt(*cross,12,3,false,false,S::Destination);
    check(clear&&clear->pendingRegion==9&&clear->phase==P::Clearance,"clearance retains pin until motor handoff");
    auto back=advanceCavePlanetTravelReceipt(*cross,12,3,false,true,S::Original);
    check(back&&back->phase==P::Approach&&!back->crossedSourceRevision,"reverse crossing resets receipt");
}
static void checkTravelRim(){
    Entrance mouth;mouth.id=7;mouth.radiusCm=200;
    CaveSurfaceRim rim;rim.entranceId=7;
    for(const Vec3 p:std::array<Vec3,4>{{{-1_fx,0_fx,-1_fx},{1_fx,0_fx,-1_fx},
                {1_fx,0_fx,1_fx},{-1_fx,0_fx,1_fx}}})rim.ring.push_back(surfaceVertexKey(p));
    check(crossesCavePlanetRim(rim,mouth,{0_fx,1_fx,0_fx},{0_fx,-1_fx,0_fx},
        Fixed::fromRatio(1,4),true),"entry crosses committed rim");
    check(crossesCavePlanetRim(rim,mouth,{0_fx,-1_fx,0_fx},{0_fx,1_fx,0_fx},
        Fixed::fromRatio(1,4),false),"exit crosses committed rim");
    check(!crossesCavePlanetRim(rim,mouth,{0_fx,1_fx,0_fx},{0_fx,0_fx,0_fx},
        Fixed::fromRatio(1,4),true),"cap endpoint does not double count");
    check(crossesCavePlanetRim(rim,mouth,{0_fx,0_fx,0_fx},{0_fx,-1_fx,0_fx},
        Fixed::fromRatio(1,4),true),"cap endpoint continues inward");
    check(!crossesCavePlanetRim(rim,mouth,{1_fx,1_fx,0_fx},{1_fx,-1_fx,0_fx},
        Fixed::fromRatio(1,4),true),"rim clearance refusal");
    check(!crossesCavePlanetRim(rim,mouth,{0_fx,-1_fx,0_fx},{0_fx,1_fx,0_fx},
        Fixed::fromRatio(1,4),true),"wrong crossing direction");
}
static void checkVolumeSweep(){
    physics::Scene scene;
    physics::BodyDesc ceiling;ceiling.motion=physics::Motion::Static;
    ceiling.shape=physics::ShapeKind::Box;ceiling.size={10,1,10};ceiling.position={0,3,0};
    check(scene.create(ceiling)!=physics::kNoBody,"volume ceiling");
    physics::CharacterParams params;physics::CharacterState state;
    const auto proxy=scene.createCharacter(params,state.position);
    check(proxy!=physics::kNoBody,"volume proxy");
    for(int i=0;i<40;++i){
        check(physics::sweepCharacterVelocityOriented(scene.world(),params,state,{0,3,0},scene.dt(),
            b3Quat_identity,proxy),"vertical volume sweep");
        check(scene.moveCharacterOriented(proxy,state,b3Quat_identity),"volume proxy update");
        scene.step();
    }
    check(state.position.y>.1f&&state.position.y<.8f&&!state.grounded,"ceiling blocks volume sweep");
    CavePlanetMotorIntent intent;intent.traversal=CaveTraversal::Flying;
    intent.speedCmPerTick=10;intent.simulationDestination={0_fx,1_fx,0_fx};
    const auto v=cavePlanetVolumeVelocity(intent,{}, {0_fx,10_fx,0_fx},Fixed::fromRatio(1,10),10_fx);
    check(v&&v->y<=1_fx&&v->y>Fixed::fromRatio(9,10),"volume route speed cap");
    intent.traversal=CaveTraversal::Ground;
    check(!cavePlanetVolumeVelocity(intent,{}, {},Fixed::fromRatio(1,10),10_fx),"ground refuses volume adapter");
}
static void checkDrive(){
    CavePlanetMotorIntent intent;
    intent.speedCmPerTick=10;
    intent.basis.groundedCompatible=true;
    intent.basis.right={0_fx,1_fx,0_fx};intent.basis.up={1_fx,0_fx,0_fx};
    intent.basis.back={0_fx,0_fx,-1_fx};
    intent.simulationDirection=intent.basis.back*Fixed::fromInt(-1);
    intent.simulationDestination=intent.simulationDirection;
    const auto drive=cavePlanetCharacterDrive(intent,{}, {},Fixed::fromRatio(1,10));
    check(drive&&std::abs(drive->input.moveZ-1)<.001f&&
        std::abs(drive->input.moveX)<.001f,"oriented route input");
    check(std::abs(drive->params.walkSpeed-1)<.001f,"route speed cap");
    intent.traversal=CaveTraversal::Flying;
    check(!cavePlanetCharacterDrive(intent,{}, {},Fixed::fromRatio(1,10)),"flight has its own motor");
}
// Real shared GI scene/BVH, recording target exercises publication refusal.
// This is NOT GPU atlas acceptance; native GPU bridge is verified separately.
struct GiFixtureTarget {
    bool accept=true;
    uint64_t revision=0;
    size_t fullPublications=0;
    std::shared_ptr<const render::GiChunkedGeometrySnapshot> snapshot;
    bool publishGeometry(std::shared_ptr<const render::GiChunkedGeometrySnapshot> s,uint64_t r){
        if(!accept||r<=revision)return false;
        snapshot=std::move(s);revision=r;++fullPublications;return true;
    }
    bool publishGeometryRegion(std::shared_ptr<const render::GiChunkedGeometrySnapshot> s,
            uint64_t r,render::GiBounds){
        if(!accept||r<=revision)return false;
        snapshot=std::move(s);revision=r;return true;
    }
};
static void checkGiViewProjection(){
    game::PlanetWorld world;
    game::PlanetBody body;body.id="planet";body.radius=100;world.bodies.push_back(body);
    render::SphereViewFrame a,b;
    a.origin={0,0,0};b.origin={10,0,0};
    const auto before=CavePlanetGiViewFrame::capture(world,a,0);
    const auto after=CavePlanetGiViewFrame::capture(world,b,0);
    const Vec3 local{2_fx,3_fx,4_fx};
    const auto first=before.projector()("planet",local);
    const auto last=after.projector()("planet",local);
    const auto remap=after.remapFrom(before,[](uint64_t id)->std::optional<std::string>{
        if(id==1)return "planet";
        return {};
    });
    const auto mapped=remap(1,first);
    check(std::abs(mapped.x-last.x)<.001f&&std::abs(mapped.y-last.y)<.001f&&
          std::abs(mapped.z-last.z)<.001f,"planet GI view frame remap");
    check(std::abs(remap(2,{15,0,0}).x-5)<.001f,"static GI world remap");
}
static void checkGiPublication(){
    render::GiGeometryScene scene;
    GiFixtureTarget target;
    const auto point=[](int x,int y,int z){return Vec3{Fixed::fromInt(x),Fixed::fromInt(y),Fixed::fromInt(z)};};
    const auto project=[](const std::string&,Vec3 p){return render::float3{float(p.x.toDouble()),float(p.y.toDouble()),float(p.z.toDouble())};};
    const world::Triangle original{point(0,0,0),point(0,0,2),point(2,0,0)};
    const render::float3 originalColor{.2f,.3f,.4f};
    scene.replaceChunk(10,{{project("",original.a),project("",original.b),project("",original.c),0}},{originalColor});
    scene.replaceChunk(99,{{{10,0,0},{10,0,2},{12,0,0},0}},{{.8f,.8f,.8f}});
    check(scene.publish(target).published,"initial GI scene");
    const auto oldScene=scene.currentShared();
    const auto unrelated=oldScene->findChunk(99);
    CavePlanetGiBindings bindings;
    bindings.surfaceId=[](const auto&,const auto&)->std::optional<uint64_t>{return 10;};
    bindings.volumeId=[](const auto&,uint64_t,ChunkKey3)->std::optional<uint64_t>{return 20;};
    bindings.project=project;
    bindings.albedo=[](const auto&,const auto&,const auto&){return render::float3{.5f,.5f,.5f};};
    bindings.ready=[](){return true;};
    CavePlanetGiPublication<GiFixtureTarget> publication(scene,target,std::move(bindings));
    auto body=std::make_shared<CavePlanetBodyCutPlan>();
    auto region=std::make_shared<CavePlanetCutPlan>();
    body->bodyId="planet";body->surfaceRevision=7;body->surfaceEpoch=1;
    region->anchor.bodyId=body->bodyId;region->surfaceRevision=7;region->surfaceEpoch=1;region->authorityRevision=1;
    CavePlanetCutSlot slot;slot.key={2,0,0,1,0};slot.originalBody={original};
    region->slots.emplace(slot.key,slot);body->regions.emplace(1,region);
    CavePlanetRuntimeCandidate candidate;candidate.cut=body;candidate.nativeRevision=3;
    CavePlanetNativeSlot volume;volume.id.region=1;volume.payload.body=0;
    volume.payload.anchor=region->anchor;volume.payload.collision.revision=3;
    const auto anchor=game::Surface::anchor(0);
    volume.payload.collision.triangles.push_back({point(0,-2,0)+anchor,point(0,-2,2)+anchor,point(2,-2,0)+anchor});
    candidate.volume.push_back(std::move(volume));
    check(publication.prepare(candidate)&&scene.currentShared()==oldScene,"GI prepare private");
    target.accept=false;
    check(!publication.commit("planet",3)&&scene.currentShared()==oldScene,"GI target refusal");
    check(publication.abort("planet",3)&&!publication.staged(),"GI abort");
    target.accept=true;
    check(publication.prepare(candidate)&&publication.commit("planet",3),"GI publish exact replacement");
    check(publication.revision("planet")==3&&!scene.current()->findChunk(10)&&scene.current()->findChunk(20),"GI removes uncut leaf");
    check(scene.current()->findChunk(99)==unrelated,"GI preserves unrelated owner");
    const auto oldCut=scene.currentShared();
    const auto fullBefore=target.fullPublications;
    const CavePlanetGiProject moved=[project](const std::string& b,Vec3 p){
        auto v=project(b,p);v.x+=100;return v;
    };
    const CavePlanetGiOtherProject other=[](uint64_t,render::float3 v){v.z+=5;return v;};
    const CavePlanetGiProject collapsed=[](const std::string&,Vec3){return render::float3{};};
    check(!publication.reproject(collapsed,other)&&scene.currentShared()==oldCut,"degenerate coordinate refused");
    target.accept=false;
    check(!publication.reproject(moved,other)&&scene.currentShared()==oldCut&&publication.current(),"reproject target refusal atomic");
    target.accept=true;
    check(publication.reproject(moved,other)&&publication.current(),"reproject accepted");
    check(target.fullPublications==fullBefore+1&&publication.revision("planet")==3&&
          scene.current()->findChunk(20)->triangles[0].a.x==100,"full GI invalidation, native revision preserved");
    check(scene.current()->findChunk(99)->triangles[0].a.z==5&&
          oldCut->findChunk(20)->triangles[0].a.x==0,"other owner remap and immutable old snapshot");
    check(publication.prepareRetire("planet",3)&&scene.current()->findChunk(20),"GI retire staging private");
    check(publication.retire("planet",3)&&!publication.activeBodies(),"GI retirement");
    const auto* restored=scene.current()->findChunk(10);
    check(restored&&restored->triangles.size()==1&&restored->albedos.size()==1&&
          restored->albedos[0].x==originalColor.x&&restored->triangles[0].a.x==100&&!scene.current()->findChunk(20),"GI restore baseline materials");
    check(scene.current()->findChunk(99)->triangles[0].a.z==5&&target.snapshot==scene.currentShared(),"GI shared owner identity");
}
int main(){
    CavePlanetRestoreNotice notice;
    check(!notice.pending()&&!notice.consume(),"initial restore notice");
    notice.mark();notice.mark();
    check(notice.pending()&&notice.consume()&&!notice.pending(),"coalesced native restore notice");
    static_assert(!std::is_copy_constructible_v<CavePlanetCpuNativeHost>);
    static_assert(!std::is_copy_constructible_v<CavePlanetGpuNativeHost>);
    checkGiViewProjection();
    checkGiPublication();
    checkDrive();
    checkVolumeSweep();
    checkTravelRim();
    checkTravelReceipts();
    checkTravelModeSelection();
    checkNativeDispatchGuard();
    checkNativeCheckpointCodec();
    checkExactGiBaselineRemap();
    checkTravelPins();
    checkOrientedMotor();
    checkCandidateIdentity();
    checkBaselineTransfer();
    check(!CavePlanetRuntimeHooks{}.valid(),"missing host hooks must refuse");
    physics::Scene box;
    world::SpatialWorld spatial;
    CaveColliderLeasePool pool(10000,100);
    CavePlanetBaselineCollision baseline(box,spatial,pool);
    using Phase=CavePlanetBaselineCollision::Phase;
    const auto p=[](int x,int z){return Vec3{Fixed::fromInt(x),Fixed::fromInt(10),Fixed::fromInt(z)};};
    CavePlanetSurfaceSlot a{{2,0,0,1,0},{{p(0,0),p(0,2),p(2,0)}}};
    CavePlanetSurfaceSlot b{{2,1,0,1,0},{{p(2,0),p(2,2),p(4,0)}}};
    std::vector<CavePlanetSurfaceSlot> slots{a,b};
    const auto cells=[](const CavePlanetSurfaceSlotKey& k){return world::Cell{k.x,100,k.z};};
    bool paused=true;
    const auto exclusive=[&]{return paused;};
    const auto unowned=[](const CavePlanetSurfaceSlotKey&){return true;}; // fixture owns initially empty leaves
    check(!baseline.prepare(0,7,slots,cells,[](const auto&){return false;},exclusive),"foreign leaf accepted");
    check(baseline.phase()==Phase::Empty&&pool.allocations()==0,"refusal leaked leases");
    check(baseline.prepare(0,7,slots,cells,unowned,exclusive),"prepare original collision");
    check(box.bodyCount()==0&&spatial.colliders().empty(),"prepare changed live physics");
    paused=false;check(!baseline.publish(exclusive),"published without exclusion");paused=true;
    check(baseline.publish(exclusive)&&baseline.solidReady(),"publish baseline");
    check(box.bodyCount()==2&&spatial.colliders().size()==2,"baseline ownership count");
    const float x=float(game::Surface::kSphereOrigin)+.25f;
    const Vec3 start{Fixed::fromRaw(int64_t(x*Fixed::kOne)),Fixed::fromInt(12),Fixed::fromRatio(1,4)};
    const Vec3 down{{},Fixed::fromInt(-4),{}};
    check(spatial.cast(start,down).hit,"baseline spatial ray");
    check(box.raycast({x,12,.25f},{0,-4,0}).body!=physics::kNoBody,"baseline Box3D ray");
    check(baseline.retireForCut(exclusive)&&baseline.cutReady(),"retire originals");
    check(box.bodyCount()==0&&spatial.colliders().empty(),"duplicate baseline remained");
    check(!spatial.cast(start,down).hit,"old spatial hit remained");
    check(box.raycast({x,12,.25f},{0,-4,0}).body==physics::kNoBody,"old Box3D hit remained");
    check(pool.allocations()==2,"restoration namespace released early");
    check(baseline.prepareRestore(exclusive),"stage restore");
    check(baseline.cancelRestore()&&baseline.cutReady(),"cancel restore");
    check(baseline.prepareRestore(exclusive)&&baseline.publish(exclusive),"restore originals");
    check(baseline.solidReady()&&box.bodyCount()==2&&spatial.colliders().size()==2,"restore count");
    check(spatial.cast(start,down).hit&&box.raycast({x,12,.25f},{0,-4,0}).body!=physics::kNoBody,"restored ray");
    check(baseline.shutdown(exclusive),"shutdown baseline");
    check(box.bodyCount()==0&&spatial.colliders().empty()&&pool.allocations()==0,"shutdown leaked resources");
    std::cout<<"PASS planetary baseline Box3D / SpatialWorld replacement lifecycle\n";
}
