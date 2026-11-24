// Real native scenes, no GPU required for the baseline lifecycle.
#include "engine/world/TerraWeaveCavernsPlanetCollisionBinding.h"
#include "engine/world/TerraWeaveCavernsPlanetRuntimeFrame.h"
#include <stdexcept>
#include <iostream>
using namespace aiverse;
using namespace aiverse::terraweave::caverns;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
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
    candidate.generation=3;candidate.cut=cut;
    candidate.sources.emplace(2,freezePlanetCaveRegion(ecs,2));
    check(candidate.current(ecs,3),"candidate current");
    check(!candidate.current(ecs,4),"rollback generation fence");
    set.regions.front().authority.volume.settings.seed+=1;
    ecs.setResource<CavePlanetAuthoritySet>(set);
    check(!candidate.current(ecs,3),"equal numeric revision but changed snapshot");
}
int main(){
    checkCandidateIdentity();
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
