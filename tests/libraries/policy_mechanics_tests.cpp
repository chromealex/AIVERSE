// Tests for the policy packages added on 2026-11-22: follow-camera, nav-links, volume-nav,
// structure-destruction, portals-rewind (batch 1) and the later batches appended below.
#include <aiverselib/gameplay/ThirdPersonCamera.h>
#include <aiverselib/bots/NavLinks.h>
#include <aiverselib/bots/VolumeNav.h>
#include <aiverselib/mechanics/StructureDestruction.h>
#include <aiverselib/mechanics/PortalsRewind.h>
#include <iostream>
#include <stdexcept>
using namespace aiverse;using namespace aiverse::lib::mechanics;
#define CHECK(...) do{if(!(__VA_ARGS__))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #__VA_ARGS__);}while(false)
template<class State> void roundtrip(const State& s){auto bytes=save(s);CHECK(!bytes.empty());State copy;CHECK(load(bytes,copy));CHECK(save(copy)==bytes);
    CHECK(!load(bytes+"x",copy));CHECK(!load(bytes.substr(0,bytes.size()-1),copy));CHECK(policyHash(copy)==policyHash(s));}
const Fixed dt=Fixed::fromRatio(1,60);
#include "policy_mechanics_batch2.inc"

void camera(){
    namespace cam=aiverse::lib::gameplay::camera;
    cam::BoomProfile base;cam::ProfileStack stack;cam::BoomState s;cam::BoomPose pose;
    auto clear=[](cam::V3,cam::V3,float max,float){return max;};
    cam::BoomInput in{{0,0,0},0,0,0,1.0f/60};
    for(int i=0;i<300;++i)CHECK(cam::updateBoom(s,base,stack,in,clear,pose));
    CHECK(std::abs(pose.distance-base.distance)<0.05f&&!pose.occluded&&pose.eye.z<0);   // behind the target (yaw 0 looks along +Z)
    auto wall=[](cam::V3,cam::V3 dir,float max,float){return dir.z<-0.5f?1.5f:max;};      // obstacle 1.5 m behind
    CHECK(cam::updateBoom(s,base,stack,in,wall,pose));CHECK(pose.occluded&&pose.distance<=1.5f+1e-3f);
    for(int i=0;i<30;++i)cam::updateBoom(s,base,stack,in,clear,pose);CHECK(pose.distance>1.5f&&pose.distance<base.distance);   // eases back out
    cam::BoomProfile aim=base;aim.distance=1.8f;aim.shoulder=0.8f;aim.fov=55;CHECK(cam::pushProfile(stack,s,base,1,aim));CHECK(!cam::pushProfile(stack,s,base,1,aim));
    for(int i=0;i<120;++i)cam::updateBoom(s,base,stack,in,clear,pose);CHECK(std::abs(pose.fov-55)<0.01f&&pose.distance<2.0f);
    CHECK(cam::popProfile(stack,s,base,1));CHECK(!cam::popProfile(stack,s,base,1));
    auto before=s;cam::BoomInput bad=in;bad.dt=-1;CHECK(!cam::updateBoom(s,base,stack,bad,clear,pose));CHECK(s.distance==before.distance);
}

void navLinks(){
    using namespace aiverse::lib::bots;
    // Two islands: walking works inside |x|<=10 and inside x in [20,30]; a jump link and a ladder join them.
    auto island=[](Vec3 p){return p.x<=10_fx?0:p.x>=20_fx?1:-1;};
    auto mesh=[&](Vec3 a,Vec3 b)->std::optional<Fixed>{if(island(a)<0||island(a)!=island(b))return std::nullopt;return (b-a).length();};
    NavLinkSet links;links[1]={1,{10_fx,{},{}},{20_fx,{},{}},LinkKind::Jump,{},false,true,20,2_fx};
    links[2]={2,{10_fx,{},5_fx},{20_fx,{},5_fx},LinkKind::Ladder,40_fx,true,true,30};
    auto p=planWithLinks(links,{0_fx,{},{}},{25_fx,{},{}},mesh);CHECK(p&&p->legs.size()==3&&p->legs[1].kind==LegKind::Link&&p->legs[1].link==1&&p->legs.back().target==Vec3{25_fx,{},{}});
    CHECK(!planWithLinks(links,{25_fx,{},{}},{0_fx,{},{}},mesh)||planWithLinks(links,{25_fx,{},{}},{0_fx,{},{}},mesh)->legs[1].link==2);   // back only over the two-way ladder
    links[1].enabled=false;p=planWithLinks(links,{0_fx,{},{}},{25_fx,{},{}},mesh);CHECK(p&&p->legs[1].link==2);
    links[2].enabled=false;CHECK(!planWithLinks(links,{0_fx,{},{}},{25_fx,{},{}},mesh));links[1].enabled=true;
    LinkTraversal t;CHECK(beginLink(t,links,1,false,10));CHECK(!beginLink(t,links,1,false,11));
    auto st=stepLinkTraversal(links,t,20);CHECK(st&&!st->finished&&st->position.y>Fixed{});    // mid-jump arc
    for(uint32_t k=21;k<=30;++k)st=stepLinkTraversal(links,t,k);CHECK(st->finished&&st->position==links[1].to&&!t.active);
    roundtrip(t);
    links[3]={3,{0_fx,{},{}},{0_fx,{},10_fx},LinkKind::Door,{},true,true,10};LinkTraversal door;CHECK(beginLink(door,links,3,false,1));
    CHECK(stepLinkTraversal(links,door,2,false)->waiting);CHECK(stepLinkTraversal(links,door,3,false)->waiting);
    for(uint32_t k=4;k<20;++k)st=stepLinkTraversal(links,door,k,true);CHECK(st->finished);
    std::vector<world::Bounds> boxes{{{0_fx,{},0_fx},{2_fx,Fixed::fromRatio(12,10),2_fx}},{{10_fx,{},0_fx},{12_fx,3_fx,4_fx}},{{20_fx,{},{}},{21_fx,Fixed::fromRatio(2,10),1_fx}}};
    auto cover=generateCover(boxes,CoverGenConfig{},[](Vec3){return true;});
    CHECK(!cover.empty());bool half=false,full=false;for(auto& g:cover){CHECK(g.box!=3);half|=g.protectionMilli==500&&g.box==1;full|=g.protectionMilli==1000&&g.box==2;}CHECK(half&&full);
    CHECK(cover.front().id==1&&cover.back().id==int64_t(cover.size()));
    CHECK(generateCover(boxes,CoverGenConfig{},[](Vec3){return false;}).empty());
}

void volumeNav(){
    using namespace aiverse::lib::bots;
    // A wall across the volume with a hole high up: the path must climb through the hole.
    std::vector<world::Bounds> walls{{{10_fx,0_fx,0_fx},{12_fx,16_fx,20_fx}},{{10_fx,16_fx,0_fx},{12_fx,20_fx,8_fx}},{{10_fx,16_fx,12_fx},{12_fx,20_fx,20_fx}}};
    auto g=buildVolumeGrid({{0_fx,0_fx,0_fx},{24_fx,20_fx,20_fx}},1_fx,{},walls);CHECK(g&&g->nx==24&&g->ny==20&&g->nz==20);
    auto p=findVolumePath(*g,{2_fx,2_fx,10_fx},{20_fx,2_fx,10_fx});CHECK(p&&!p->partial&&p->points.front()==Vec3{2_fx,2_fx,10_fx}&&p->points.back()==Vec3{20_fx,2_fx,10_fx});
    bool high=false;for(auto& q:p->points)high|=q.y>=16_fx;CHECK(high);
    for(std::size_t i=1;i<p->points.size();++i)CHECK(volumeLineClear(*g,p->points[i-1],p->points[i]));
    auto again=findVolumePath(*g,{2_fx,2_fx,10_fx},{20_fx,2_fx,10_fx});CHECK(again->points==p->points);    // deterministic
    auto cut=findVolumePath(*g,{2_fx,2_fx,10_fx},{20_fx,2_fx,10_fx},20);CHECK(cut&&cut->partial);
    CHECK(!findVolumePath(*g,{11_fx,2_fx,10_fx},{20_fx,2_fx,10_fx}));            // start inside the wall
    FlockConfig fc;std::vector<FlockAgent> flock{{1,{0_fx,{},{}},{}},{2,{1_fx,{},{}},{}},{3,{0_fx,{},1_fx},{}}};
    auto none=[](Vec3){return Vec3{};};auto a=flockSteer(fc,flock[0],flock,{0_fx,{},50_fx},none);CHECK(a.x<Fixed{}&&a.lengthSq()<=fc.maxAccel*fc.maxAccel+1_fx);
    auto slot=formationSlot3D({},Fixed::fromRatio(1,4),{},{{},{},-2_fx});CHECK(fxAbs(slot.x+2_fx)<Fixed::fromRatio(1,100)&&fxAbs(slot.z)<Fixed::fromRatio(1,100));
    auto up=formationSlot3D({},{},Fixed::fromRatio(1,4),{{},{},2_fx});CHECK(fxAbs(up.y-2_fx)<Fixed::fromRatio(1,100));
}

void destruction(){
    StructureConfig c;StructureState s;
    // A floor slab on two pillars, the right pillar is breakable with a fracture grid.
    CHECK(addPiece(s,{1,{{0_fx,0_fx,0_fx},{1_fx,3_fx,1_fx}},100,0,true}));
    CHECK(addPiece(s,{2,{{4_fx,0_fx,0_fx},{5_fx,3_fx,1_fx}},100,0,true,1,3,1,60}));
    CHECK(addPiece(s,{3,{{0_fx,3_fx,0_fx},{5_fx,Fixed::fromRatio(7,2),1_fx}},200,0,false}));
    CHECK(addPiece(s,{4,{{8_fx,0_fx,0_fx},{9_fx,1_fx,1_fx}},50,0,false}));     // free piece, collapses on first change
    CHECK(!addPiece(s,{1,{{0_fx,0_fx,0_fx},{1_fx,1_fx,1_fx}}}));
    auto r=stepStructure(c,s,1,dt,{});CHECK(r&&r->removed.empty());
    StructureDamage small{1,{Fixed::fromRatio(9,2),1_fx,Fixed::fromRatio(1,2)},2_fx,30,{}};
    r=stepStructure(c,s,2,dt,std::span(&small,1));CHECK(r&&s.pieces.at(2).health<100&&!s.pieces.contains(4)==false);
    StructureDamage big{2,{Fixed::fromRatio(9,2),Fixed::fromRatio(1,2),Fixed::fromRatio(1,2)},Fixed::fromRatio(3,2),200,{1_fx,{},{}}};
    r=stepStructure(c,s,3,dt,std::span(&big,1));CHECK(r);
    bool fractured=false;for(auto& e:r->events)fractured|=e.kind==StructureEventKind::Fractured&&e.piece==2;CHECK(fractured);
    CHECK(r->dirty&&!r->removed.empty());CHECK(s.pieces.contains(3));             // slab still held by the left pillar
    CHECK(!s.pieces.contains(4));                                                // the free piece fell when support was recomputed
    StructureDamage kill{3,{Fixed::fromRatio(1,2),Fixed::fromRatio(3,2),Fixed::fromRatio(1,2)},2_fx,1000,{}};
    r=stepStructure(c,s,4,dt,std::span(&kill,1));bool collapsed=false;for(auto& e:r->events)collapsed|=e.kind==StructureEventKind::Collapsed&&e.piece==3;
    CHECK(!s.pieces.contains(1)&&!s.debris.empty());
    (void)collapsed;
    bool rest=false;for(uint32_t t=5;t<400&&!rest;++t){auto x=stepStructure(c,s,t,dt,{});for(auto& e:x->events)rest|=e.kind==StructureEventKind::DebrisRested;}
    CHECK(rest);for(auto& [id,d]:s.debris)CHECK(d.box.min.y>=c.groundY);
    roundtrip(s);auto copy=s;auto a=stepStructure(c,s,500,dt,{}),b=stepStructure(c,copy,500,dt,{});CHECK(a&&b&&policyHash(s)==policyHash(copy));
    CHECK(!stepStructure(c,s,500,dt,{}));
}

void portalsRewind(){
    PortalSet ps;ps[1]={1,2,{0_fx,1_fx,0_fx},{{},{},1_fx},{{},1_fx,{}}};ps[2]={2,1,{50_fx,1_fx,0_fx},{1_fx,{},{}},{{},1_fx,{}}};CHECK(valid(ps));
    PortalCooldowns cd;PortalBody before{1,{0_fx,1_fx,1_fx},{{},{},-5_fx},{{},{},-1_fx}},after{1,{0_fx,1_fx,-1_fx},{{},{},-5_fx},{{},{},-1_fx}};
    auto x=crossPortals(ps,cd,1,std::span(&before,1),std::span(&after,1));CHECK(x&&x->size()==1);
    auto& c=x->front();CHECK(c.to==2&&c.velocity.x>Fixed{}&&fxAbs(c.velocity.x-5_fx)<Fixed::fromRatio(1,10)&&c.position.x>50_fx);
    auto y=crossPortals(ps,cd,2,std::span(&before,1),std::span(&after,1));CHECK(y&&y->empty());            // cooldown
    PortalBody back1{1,{0_fx,1_fx,-1_fx},{},{}},back2{1,{0_fx,1_fx,1_fx},{},{}};
    auto z=crossPortals(ps,cd,10,std::span(&back1,1),std::span(&back2,1));CHECK(z&&z->empty());             // from behind: no crossing
    PortalBody wide1{1,{5_fx,1_fx,1_fx},{},{}},wide2{1,{5_fx,1_fx,-1_fx},{},{}};CHECK(crossPortals(ps,cd,11,std::span(&wide1,1),std::span(&wide2,1))->empty());
    auto p=portalTransformPoint(ps[1],ps[2],{0_fx,1_fx,-1_fx});CHECK(fxAbs(p.x-51_fx)<Fixed::fromRatio(1,50));
    roundtrip(cd);
    auto broken=ps;broken[2].link=3;CHECK(!valid(broken));
    RewindConfig rc;rc.capacityTicks=100;rc.maxEnergy=50;RewindHistory h;
    for(uint32_t t=1;t<=120;++t)CHECK(recordRewind(rc,h,t,"state"+std::to_string(t)));CHECK(h.snapshots.size()==100&&h.snapshots.begin()->first==21);
    CHECK(!recordRewind(rc,h,120,"dup"));
    auto rw=rewindTo(h,100);CHECK(rw&&rw->tick==100&&rw->snapshot=="state100"&&rw->spent==20&&h.energy==30&&h.lastTick==100&&h.snapshots.rbegin()->first==100);
    CHECK(!rewindTo(h,40));                                                      // needs 60 energy
    CHECK(recordRewind(rc,h,101,"new101"));roundtrip(h);
}

int main(){
    try{camera();navLinks();volumeNav();destruction();portalsRewind();policyBatch2();}
    catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
    std::cout<<"policy mechanics tests passed\n";return 0;
}
