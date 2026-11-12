#include <aiverselib/genre/Configured.h>
#include <aiverselib/genre/Persistence.h>
#include <aiverselib/genre/Data.h>
#include <array>
#include <iostream>
#include <stdexcept>
using namespace aiverse;using namespace aiverse::lib::genre;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}while(false)
template<class State> void roundtrip(const State& s){auto bytes=save(s);CHECK(!bytes.empty());State copy;CHECK(load(bytes,copy));CHECK(save(copy)==bytes);CHECK(!load(bytes+"x",copy));CHECK(!load(bytes.substr(0,bytes.size()-1),copy));CHECK(save(copy)==bytes);}
void movement(){
    MotionConfig c;MotionState s;auto dt=Fixed::fromRatio(1,60);
    CHECK(stepMotion(c,s,{{},true,false,false},0,dt));CHECK(stepMotion(c,s,{{},false,true,false},3,dt)->jumped);CHECK(!stepMotion(c,s,{{},false,true,false},4,dt)->jumped);
    CHECK(!stepMotion(c,s,{},4,dt));roundtrip(s);
    MotionState falling;CHECK(!stepMotion(c,falling,{{},false,true,false},0,dt)->jumped);CHECK(stepMotion(c,falling,{{},true,false,false},2,dt)->jumped);
    c.maxJumps=2;MotionState doubleJump;CHECK(stepMotion(c,doubleJump,{{},true,true,false},0,dt)->jumped);CHECK(stepMotion(c,doubleJump,{{},false,true,false},1,dt)->jumped);CHECK(!stepMotion(c,doubleJump,{{},false,true,false},2,dt)->jumped);
    MotionState a,b;Vec3 pa{},pb{};
    for(uint32_t t=0;t<240;++t){MotionInput in{{1_fx,{}},pa.y<=Fixed{},t%90==0,false};auto x=stepMotion(c,a,in,t,dt),y=stepMotion(c,b,in,t,dt);CHECK(x&&y&&x->velocity==y->velocity&&x->jumped==y->jumped);pa+=x->velocity*dt;pb+=y->velocity*dt;if(pa.y<Fixed{})pa.y={};if(pb.y<Fixed{})pb.y={};CHECK(pa==pb);CHECK(load(save(b),b));}
    CHECK(pa.x>10_fx);CHECK(!stepMotion(c,a,{},240,Fixed{}));
}
void combat(){
    AttackSet defs{{1,{1,2,2,4,4,8,{2}}},{2,{2,1,2,3,3,8,{1}}}};CHECK(valid(defs));MeleeState s;auto yes=[](auto){return true;};
    CHECK(stepMelee(defs,s,0,1,yes));CHECK(attackPhase(defs,s,1)==AttackPhase::Windup);CHECK(!recordMeleeHit(defs,s,1,10));CHECK(recordMeleeHit(defs,s,2,10));CHECK(!recordMeleeHit(defs,s,2,10));
    CHECK(stepMelee(defs,s,3,2,yes)&&s.attack==1);CHECK(stepMelee(defs,s,4,0,yes)&&s.attack==2&&s.hitTargets.empty());CHECK(recordMeleeHit(defs,s,5,10));roundtrip(s);
    CHECK(resolveMeleeContact(true,true,true,true)==MeleeContact::Evaded);CHECK(resolveMeleeContact(false,true,true,true)==MeleeContact::Parried);CHECK(resolveMeleeContact(false,false,true,false)==MeleeContact::Hit);
    MeleeState refused;CHECK(stepMelee(defs,refused,0,1,[](auto){return false;}));CHECK(stepMelee(defs,refused,9,0,yes));CHECK(refused.attack==0&&refused.queued==0);
}
void weapons(){
    RecoilState s;RecoilConfig c;CHECK(stepRecoil(c,s,true)&&s.pitch>Fixed{});for(int i=0;i<100;++i)CHECK(stepRecoil(c,s,false));CHECK(s.pitch==Fixed{});roundtrip(s);
    Random a(3),b(3);for(int i=0;i<100;++i){auto x=sampleSpread(a,Fixed::fromRatio(1,100)),y=sampleSpread(b,Fixed::fromRatio(1,100));CHECK(x&&y&&x->x==y->x&&x->y==y->y);}auto rng=a.state();CHECK(!sampleSpread(a,-1_fx)&&a.state()==rng);
    world::SpatialWorld world;CHECK(world.add({1,{{-5_fx,-5_fx,5_fx},{5_fx,5_fx,6_fx}}}));ProjectileConfig pc;pc.gravity={};pc.maxBounces=1;
    ProjectileState p;p.velocity={0_fx,0_fx,600_fx};auto hit=stepProjectile(pc,p,world,Fixed::fromRatio(1,60));CHECK(hit.accepted&&hit.impact&&hit.impact->bounced&&p.velocity.z<Fixed{}&&p.alive);roundtrip(p);
    CHECK(stepProjectile(pc,p,world,Fixed::fromRatio(1,60)).accepted&&p.position.z<4_fx);
    pc.lifetime=1;ProjectileState once;once.velocity={1_fx,{}, {}};CHECK(stepProjectile(pc,once,world,Fixed::fromRatio(1,60)).accepted&&!once.alive&&once.position.x>Fixed{});
}
void matches(){
    const std::array<int64_t,2> teams{1,2};auto state=makeMatch(teams);CHECK(state);MatchConfig c;c.warmupTicks=0;c.intermissionTicks=1;c.roundsToWin=2;c.scoreToWin=3;
    CHECK(stepMatch(c,*state,0)&&state->phase==MatchPhase::Live);CHECK(awardScore(*state,1,1,3));CHECK(!awardScore(*state,1,1,3));CHECK(stepMatch(c,*state,1)&&state->phase==MatchPhase::Intermission);CHECK(!awardScore(*state,1,2,3));
    CHECK(stepMatch(c,*state,2)&&state->phase==MatchPhase::Live);CHECK(!awardScore(*state,1,1,3));CHECK(awardScore(*state,1,2,3));CHECK(stepMatch(c,*state,3)&&state->phase==MatchPhase::Finished&&state->winner==1);roundtrip(*state);
    auto tie=makeMatch(teams);c.roundTicks=5;c.overtimeTicks=3;c.roundsToWin=1;c.maxRounds=1;CHECK(stepMatch(c,*tie,0));CHECK(stepMatch(c,*tie,5)&&tie->phase==MatchPhase::Overtime);CHECK(stepMatch(c,*tie,8)&&tie->winner==-1);roundtrip(*tie);
    CaptureState capture;CHECK(stepCapture(capture,{{1,1}},3,0));CHECK(stepCapture(capture,{{1,1},{2,1}},3,1)&&capture.progress==1);CHECK(stepCapture(capture,{{1,1}},3,2));auto taken=stepCapture(capture,{{1,1}},3,3);CHECK(taken&&*taken&&capture.owner==1);CHECK(!stepCapture(capture,{},3,3));roundtrip(capture);
}
void narrative(const std::string& root){
    auto graph=readStory(Json::parseFile(root+"/examples/libraries/genre/story.json"));auto s=beginStory(graph,1);CHECK(s&&storyChoices(graph,*s)==std::vector<int64_t>{2});CHECK(!chooseStory(graph,*s,1,1));
    s=beginStory(graph,1,{{1,1}});CHECK(s&&chooseStory(graph,*s,1,1)&&s->node==2&&s->facts.at(2)==1);CHECK(!chooseStory(graph,*s,1,1));roundtrip(*s);CHECK(chooseStory(graph,*s,1,2)&&s->complete);roundtrip(*s);
    auto bad=graph;bad[1].choices[0].next=99;CHECK(!valid(bad));
    auto overflow=graph;overflow[1].choices[0].changes={{2,1,true}};auto initial=beginStory(overflow,1,{{1,1},{2,INT64_MAX}});auto before=save(*initial);CHECK(!chooseStory(overflow,*initial,1,1)&&save(*initial)==before);
    auto settings=aiverse::lib::gameplay::readGameplaySettings(Json::parseFile(root+"/examples/libraries/genre/settings.json"));CHECK(settings.value&&motionConfig(*settings.value).maxJumps==2&&matchConfig(*settings.value).roundsToWin==2);
}
// ---- weapon-policies 0.2: penetration, explosions, feeds, charge, attachments, aiming ----
#include <aiverselib/genre/WeaponData.h>
#include <aiverselib/genre/WeaponPersistence.h>
bool weaponNear(Fixed a,Fixed b,Fixed eps){return fxAbs(a-b)<=eps;}
bool weaponNearV(Vec3 a,Vec3 b,Fixed eps){return weaponNear(a.x,b.x,eps)&&weaponNear(a.y,b.y,eps)&&weaponNear(a.z,b.z,eps);}
WeaponDefinitions weaponDefs(const std::string& root){return readWeaponDefinitions(Json::parseFile(root+"/examples/libraries/genre/weapons.json"));}
void weaponPenetration(const std::string& root){
    auto defs=weaponDefs(root);world::SpatialWorld world;
    CHECK(world.add({1,{{-1_fx,-1_fx,2_fx},{1_fx,1_fx,3_fx}}}));CHECK(world.add({2,{{-1_fx,-1_fx,5_fx},{1_fx,1_fx,Fixed::fromRatio(51,10)}}}));CHECK(world.add({3,{{-1_fx,-1_fx,8_fx},{1_fx,1_fx,10_fx}}}));
    auto material=[](uint32_t id){return int64_t(id);};
    auto layers=traceLayers(world,{},{{},{},20_fx},material);CHECK(layers&&layers->size()==3);
    CHECK((*layers)[0].collider==1&&(*layers)[1].collider==2&&(*layers)[2].collider==3);
    auto eps=Fixed::fromRatio(1,500);CHECK(weaponNear((*layers)[0].thickness,1_fx,eps)&&weaponNear((*layers)[1].thickness,Fixed::fromRatio(1,10),eps)&&weaponNear((*layers)[2].thickness,2_fx,eps));
    CHECK(weaponNearV((*layers)[1].point,{{},{},5_fx},eps)&&(*layers)[1].normal.z==-1_fx);
    PenetrationConfig c;auto r=resolvePenetration(defs.materials,c,*layers,100_fx,800_fx);
    CHECK(r&&r->stop==PenetrationStop::Material&&r->layers.size()==3&&r->damage==Fixed{});
    CHECK(weaponNear(r->layers[0].damageOut,90_fx,Fixed::fromRatio(1,10))&&weaponNear(r->layers[1].damageOut,72_fx,Fixed::fromRatio(1,10))&&weaponNear(r->layers[2].damageIn,72_fx,Fixed::fromRatio(1,10)));
    CHECK(weaponNear(r->layers[1].powerOut,Fixed::fromRatio(3,10),eps)&&r->layers[0].passed&&r->layers[1].passed&&!r->layers[2].passed);
    CHECK(weaponNear(r->layers[1].speedOut,648_fx,1_fx));
    c.power=Fixed::fromRatio(1,2);r=resolvePenetration(defs.materials,c,*layers,100_fx,800_fx);CHECK(r&&r->stop==PenetrationStop::Power&&r->layers.size()==2&&weaponNear(r->layers[1].damageIn,90_fx,Fixed::fromRatio(1,10)));
    c.power=1_fx;c.maxLayers=1;r=resolvePenetration(defs.materials,c,*layers,100_fx,800_fx);CHECK(r&&r->stop==PenetrationStop::MaxLayers&&r->layers.size()==1);
    c.maxLayers=4;c.minDamage=80_fx;r=resolvePenetration(defs.materials,c,*layers,100_fx,800_fx);CHECK(r&&r->stop==PenetrationStop::Damage&&r->layers.size()==2&&!r->layers[1].passed);
    c.minDamage={};c.minSpeed=700_fx;r=resolvePenetration(defs.materials,c,*layers,100_fx,800_fx);CHECK(r&&r->stop==PenetrationStop::Speed&&r->layers.size()==2);
    c.minSpeed={};auto two=std::vector<PenetrationLayer>(layers->begin(),layers->begin()+2);r=resolvePenetration(defs.materials,c,two,100_fx,800_fx);CHECK(r&&r->stop==PenetrationStop::None&&weaponNear(r->damage,72_fx,Fixed::fromRatio(1,10)));
    auto unknown=two;unknown[0].material=99;r=resolvePenetration(defs.materials,c,unknown,100_fx,800_fx);CHECK(r&&r->stop==PenetrationStop::Material&&r->layers.size()==1);
    auto thin=two;thin[1].thickness={};r=resolvePenetration(defs.materials,c,thin,100_fx,800_fx);CHECK(r&&weaponNear(r->layers[1].thickness,Fixed::fromRatio(1,100),eps));  // minThickness
    auto reversed=two;std::swap(reversed[0],reversed[1]);CHECK(!resolvePenetration(defs.materials,c,reversed,100_fx,800_fx));CHECK(!resolvePenetration(defs.materials,c,two,-1_fx,800_fx));
    auto a=resolvePenetration(defs.materials,c,*layers,100_fx,800_fx),b=resolvePenetration(defs.materials,c,*layers,100_fx,800_fx);CHECK(weaponHash(*a)==weaponHash(*b)&&weaponHash(*a)!=weaponHash(*r));
    auto partial=traceLayers(world,{},{{},{},9_fx},material);CHECK(partial&&partial->size()==3&&weaponNear((*partial)[2].thickness,1_fx,eps));  // ends inside the wall
    CHECK(traceLayers(world,{},{{},{},20_fx},material,1)->size()==1);CHECK(!traceLayers(world,{},{{},{},20_fx},material,0));
}
void weaponExplosions(){
    ExplosionConfig c;c.innerRadius=1_fx;c.outerRadius=5_fx;c.damage=100_fx;c.edgeScale=Fixed::fromRatio(1,5);c.impulse=10_fx;c.maxTargets=3;
    CHECK(explosionFalloff(c,Fixed::fromRatio(1,2))==1_fx&&explosionFalloff(c,1_fx)==1_fx&&explosionFalloff(c,5_fx)==Fixed::fromRatio(1,5)&&explosionFalloff(c,Fixed::fromRatio(501,100))==Fixed{});
    CHECK(weaponNear(explosionFalloff(c,3_fx),Fixed::fromRatio(3,5),Fixed::fromRaw(2)));
    std::vector<ExplosionTarget> targets{{9,{10_fx,{},{}},{}},{4,{6_fx,{},{}},Fixed::fromRatio(3,2)},{3,{3_fx,{},{}},{}},{5,{Fixed::fromRatio(1,2),{},{}},{}},{2,{{},3_fx,{}},{}}};
    std::vector<int64_t> calls;auto exposure=[&](const ExplosionTarget& t){calls.push_back(t.id);return t.id==2?Fixed{}:t.id==3?Fixed::fromRatio(1,2):1_fx;};
    auto hits=resolveExplosion(c,{},targets,exposure);CHECK(hits&&hits->size()==3);
    CHECK((calls==std::vector<int64_t>{5,2,3,4}));CHECK((*hits)[0].id==5&&(*hits)[1].id==3&&(*hits)[2].id==4);
    CHECK((*hits)[0].damage==100_fx&&weaponNear((*hits)[1].damage,30_fx,Fixed::fromRatio(1,100))&&weaponNear((*hits)[2].damage,30_fx,Fixed::fromRatio(1,100))&&weaponNear((*hits)[2].distance,Fixed::fromRatio(9,2),Fixed::fromRaw(4)));
    CHECK(((*hits)[1].direction==Vec3{1_fx,{},{}})&&weaponNear((*hits)[1].impulse,3_fx,Fixed::fromRatio(1,100))&&(*hits)[1].exposure==Fixed::fromRatio(1,2));
    calls.clear();c.maxTargets=2;hits=resolveExplosion(c,{},targets,exposure);CHECK(hits&&hits->size()==2&&(calls==std::vector<int64_t>{5,2,3}));
    auto shuffled=targets;std::reverse(shuffled.begin(),shuffled.end());calls.clear();auto again=resolveExplosion(c,{},shuffled,exposure);CHECK(again&&weaponHash(*again)==weaponHash(*hits)&&(calls==std::vector<int64_t>{5,2,3}));
    std::vector<ExplosionTarget> tie{{7,{{},{},2_fx},{}},{6,{-2_fx,{},{}},{}},{8,{},{}}};c.maxTargets=8;calls.clear();
    hits=resolveExplosion(c,{},tie,[&](const ExplosionTarget& t){calls.push_back(t.id);return 2_fx;});CHECK(hits&&(calls==std::vector<int64_t>{8,6,7})&&((*hits)[0].direction==Vec3{{},1_fx,{}})&&(*hits)[1].exposure==1_fx);
    auto dup=tie;dup[1].id=7;calls.clear();CHECK(!resolveExplosion(c,{},dup,exposure)&&calls.empty());
    c.innerRadius=6_fx;CHECK(!resolveExplosion(c,{},tie,exposure));
}
void weaponFeeds(){
    FeedConfig m;m.capacity=3;m.intervalTicks=2;m.reloadTicks=5;m.emptyReloadTicks=8;auto s=makeFeed(m);CHECK(s.loaded==3);
    uint32_t reserve=4;auto draw=[&](uint32_t n){auto g=std::min(n,reserve);reserve-=g;return g;};
    auto step=[&](uint32_t tick,FeedInput in){auto r=stepFeed(m,s,tick,in,draw);CHECK(r);return *r;};
    CHECK(step(0,{true}).fired&&s.loaded==2);CHECK(!step(1,{true}).fired);CHECK(step(2,{true}).fired);CHECK(step(4,{true}).consumed==1&&s.loaded==0);
    auto dry=step(6,{true});CHECK(!dry.fired&&dry.dry&&dry.reloadStarted&&s.phase==FeedPhase::Reloading&&s.nextInsert==14);
    for(uint32_t t=7;t<14;++t)CHECK(!step(t,{true}).fired);CHECK(s.phase==FeedPhase::Reloading);
    auto done=step(14,{});CHECK(done.reloadCompleted&&done.loadedAdded==3&&s.loaded==3&&reserve==1);
    CHECK(step(15,{true}).fired);CHECK(step(16,{false,true}).reloadStarted&&s.nextInsert==21);
    auto interrupt=step(18,{true});CHECK(interrupt.reloadInterrupted&&interrupt.fired&&s.loaded==1&&s.phase==FeedPhase::Ready);
    CHECK(step(19,{false,true}).reloadStarted);auto cancel=step(20,{false,false,true});CHECK(cancel.reloadInterrupted&&s.phase==FeedPhase::Ready);
    CHECK(step(21,{false,true}).reloadStarted);CHECK(step(26,{}).loadedAdded==1&&s.loaded==2&&reserve==0);
    auto before=save(s);CHECK(!stepFeed(m,s,26,{true},draw)&&save(s)==before);
    m.interruptible=false;CHECK(step(27,{false,true}).reloadStarted);CHECK(!step(28,{true}).fired&&s.phase==FeedPhase::Reloading);
    m.interruptible=true;m.autoReload=false;s=makeFeed(m);s.loaded=0;CHECK(step(29,{true}).dry&&s.phase==FeedPhase::Ready);

    FeedConfig shell;shell.kind=FeedKind::PerShell;shell.capacity=4;shell.reloadStartTicks=2;shell.shellTicks=3;FeedState p;p.loaded=1;uint32_t shells=10;
    auto sdraw=[&](uint32_t n){auto g=std::min(n,shells);shells-=g;return g;};auto pstep=[&](uint32_t tick,FeedInput in){auto r=stepFeed(shell,p,tick,in,sdraw);CHECK(r);return *r;};
    CHECK(pstep(0,{false,true}).reloadStarted&&p.nextInsert==5);CHECK(pstep(4,{}).loadedAdded==0);CHECK(pstep(5,{}).loadedAdded==1&&p.loaded==2);CHECK(pstep(8,{}).loadedAdded==1);
    auto shot=pstep(9,{true});CHECK(shot.reloadInterrupted&&shot.fired&&p.loaded==2&&shells==8);
    CHECK(pstep(10,{false,true}).reloadStarted);auto burst=pstep(30,{});CHECK(burst.loadedAdded==2&&burst.reloadCompleted&&p.loaded==4);  // skipped ticks insert in order, bounded by capacity
    shells=0;p.loaded=2;CHECK(pstep(31,{false,true}).reloadStarted);auto empty=pstep(36,{});CHECK(empty.reloadCompleted&&empty.loadedAdded==0&&p.loaded==2);

    FeedConfig belt;belt.kind=FeedKind::Belt;belt.spinupTicks=3;FeedState b;uint32_t links=2;auto bdraw=[&](uint32_t n){auto g=std::min(n,links);links-=g;return g;};
    auto bstep=[&](uint32_t tick,bool fire){auto r=stepFeed(belt,b,tick,{fire},bdraw);CHECK(r);return *r;};
    CHECK(!bstep(0,true).fired&&!bstep(1,true).fired&&!bstep(2,true).fired);CHECK(bstep(3,true).fired&&bstep(4,true).consumed==1&&links==0);CHECK(bstep(5,true).dry);
    CHECK(!bstep(6,false).fired);CHECK(!bstep(7,true).fired&&b.spin==1);CHECK(!stepFeed(belt,b,8,{true},[](uint32_t){return 0u;})->fired);

    FeedConfig e;e.kind=FeedKind::Energy;e.intervalTicks=1;e.heatPerShot=400;e.heatCapacity=1000;e.coolPerTick=50;e.overheatTicks=10;e.recoverHeat=200;FeedState h;
    auto nodraw=[](uint32_t){return 0u;};std::optional<FeedStep> r;
    CHECK(stepFeed(e,h,0,{true},nodraw)->fired&&h.heat==400);CHECK(stepFeed(e,h,1,{true},nodraw)->fired&&h.heat==750);r=stepFeed(e,h,2,{true},nodraw);CHECK(r->overheated&&h.heat==1000&&h.phase==FeedPhase::Overheated);
    for(uint32_t t=3;t<18;++t){r=stepFeed(e,h,t,{true},nodraw);CHECK(r&&!r->fired&&!r->recovered);}
    r=stepFeed(e,h,18,{true},nodraw);CHECK(r->recovered&&r->fired&&h.heat==600);
    FeedConfig bad=e;bad.recoverHeat=1000;CHECK(!valid(bad));bad=m;bad.ammoPerShot=4;CHECK(!valid(bad));FeedState over;over.loaded=40;CHECK(!stepFeed(m,over,0,{true},draw));
}
void weaponCharge(){
    ChargeConfig c;ChargeState s;auto step=[&](uint32_t tick,bool held,bool cancel=false){auto r=stepCharge(c,s,tick,{held,cancel});CHECK(r);return *r;};
    CHECK(step(0,true).started);auto early=step(5,false);CHECK(early.cancelled&&!early.release);
    CHECK(step(6,true).started);auto mid=step(46,false);CHECK(mid.release&&mid.release->tier==2&&mid.release->scale==Fixed::fromRatio(3,2)&&mid.release->fraction==Fixed::fromRatio(3,5)&&!mid.release->automatic);
    CHECK(step(50,true).started);auto full=step(200,false);CHECK(full.release&&full.release->tier==3&&full.release->heldTicks==60&&full.release->fraction==1_fx&&full.release->scale==2_fx);
    c.fireBelowMin=true;CHECK(step(201,true).started);auto tap=step(203,false);CHECK(tap.release&&tap.release->belowMin&&tap.release->tier==0&&tap.release->scale==1_fx&&tap.release->fraction==Fixed{});
    c.autoReleaseTicks=70;CHECK(step(210,true).started);CHECK(!step(279,true).release);auto autoFire=step(280,true);CHECK(autoFire.release&&autoFire.release->automatic&&autoFire.release->tier==3);
    CHECK(!step(281,true).started&&s.latched);CHECK(!step(282,false).release&&!s.latched);CHECK(step(283,true).started);
    auto cancelled=step(290,true,true);CHECK(cancelled.cancelled&&!s.charging&&s.latched);CHECK(!step(291,true).started);
    auto before=save(s);CHECK(!stepCharge(c,s,291,{true,false})&&save(s)==before);
    ChargeConfig bad;bad.tiers={{30,1_fx},{20,2_fx}};CHECK(!valid(bad));bad=ChargeConfig{};bad.autoReleaseTicks=5;CHECK(!valid(bad));bad=ChargeConfig{};bad.minTicks=70;CHECK(!valid(bad));
}
void weaponAttachments(const std::string& root){
    auto d=weaponDefs(root);CHECK(valid(d.frame,d.attachments));LoadoutState l;
    CHECK(equipAttachment(d.frame,d.attachments,l,30)==0);CHECK(equipAttachment(d.frame,d.attachments,l,20)==0);CHECK(equipAttachment(d.frame,d.attachments,l,10)==0);
    auto stats=resolveStats(d.frame,d.attachments,l);CHECK(stats);auto eps=Fixed::fromRatio(1,1000);
    CHECK(weaponNear(stats->at(1),Fixed::fromRatio(324,10),eps)&&weaponNear(stats->at(2),Fixed::fromRatio(3,2),eps)&&stats->at(3)==45_fx);
    LoadoutState other;for(auto id:{10,30,20})CHECK(equipAttachment(d.frame,d.attachments,other,id));CHECK(*resolveStats(d.frame,d.attachments,other)==*stats);
    CHECK(save(other)==save(l)&&weaponHash(l)==weaponHash(other));
    CHECK(equipAttachment(d.frame,d.attachments,l,11)==10);stats=resolveStats(d.frame,d.attachments,l);CHECK(weaponNear(stats->at(2),Fixed::fromRatio(3,4),eps)&&weaponNear(stats->at(1),Fixed::fromRatio(45,2),eps));
    CHECK(equipAttachment(d.frame,d.attachments,l,31)==30);CHECK(resolveStats(d.frame,d.attachments,l)->at(3)==100_fx);  // 300 clamped to maximum
    CHECK(removeAttachment(d.frame,d.attachments,l,3)==31);CHECK(!removeAttachment(d.frame,d.attachments,l,3));CHECK(!equipAttachment(d.frame,d.attachments,l,99));
    LoadoutState wrong;wrong.slots[2]=10;CHECK(!valid(d.frame,d.attachments,wrong)&&!resolveStats(d.frame,d.attachments,wrong)&&!equipAttachment(d.frame,d.attachments,wrong,20)&&wrong.slots.size()==1);
    auto broken=d.attachments;broken[10].modifiers.push_back({77,StatOp::Add,1_fx});CHECK(!valid(d.frame,broken));broken=d.attachments;broken[10].slot=9;CHECK(!valid(d.frame,broken));
    roundtrip(l);
}
void weaponAiming(){
    auto eps=Fixed::fromRatio(1,20);auto g=10_fx,v=50_fx;
    auto at=[&](const LaunchSolution& s,Fixed gravity){return s.velocity*s.time-Vec3{{},gravity*s.time*s.time/2,{}};};
    for(Vec3 d:{Vec3{30_fx,{},{}},Vec3{20_fx,5_fx,10_fx},Vec3{{},15_fx,{}},Vec3{100_fx,-20_fx,50_fx},Vec3{{},-10_fx,{}},Vec3{-120_fx,30_fx,-90_fx}}){
        auto r=solveLaunch(d,v,g);CHECK(r&&r->count==2);
        for(const auto& s:{r->low,r->high}){CHECK(weaponNearV(at(s,g),d,eps));CHECK(weaponNear(s.velocity.length(),v,Fixed::fromRatio(1,10)));}
        CHECK(r->low.elevation<=r->high.elevation&&r->low.time<r->high.time);
    }
    CHECK(solveLaunch({300_fx,{},{}},v,g)->count==0);CHECK(solveLaunch({{},200_fx,{}},v,g)->count==0);CHECK(solveLaunch({},v,g)->count==0);
    CHECK(!solveLaunch({1_fx,{},{}},{},g)&&!solveLaunch({1_fx,{},{}},v,-1_fx)&&!solveLaunch({200000_fx,{},{}},v,g));
    auto straight=solveLaunch({3_fx,4_fx,{}},5_fx,{});CHECK(straight&&straight->count==1&&weaponNear(straight->low.time,1_fx,Fixed::fromRaw(4))&&weaponNearV(straight->low.velocity,{3_fx,4_fx,{}},Fixed::fromRaw(4)));
    auto far=Vec3{40000_fx,-20000_fx,{}};auto big=solveLaunch(far,1000_fx,Fixed::fromRatio(98,10));CHECK(big&&big->count==2);  // exercises the scaled discriminant
    for(const auto& s:{big->low,big->high})CHECK(weaponNearV(at(s,Fixed::fromRatio(98,10)),far,2_fx)&&weaponNear(s.velocity.length(),1000_fx,1_fx));
    AimConfig c;Vec3 target{60_fx,{},{}},vel{{},{},5_fx};auto lead=solveIntercept(c,{},target,vel,v,g);CHECK(lead&&lead->found&&lead->converged&&lead->iterations<=c.maxIterations);
    CHECK(weaponNearV(at(lead->launch,g),target+vel*lead->launch.time,Fixed::fromRatio(1,5)));
    c.maxIterations=1;auto once=solveIntercept(c,{},target,vel,v,g);CHECK(once&&once->found&&!once->converged&&once->iterations==1);
    auto still=solveIntercept(c,{},target,{},v,g);CHECK(still&&still->converged&&still->iterations==1);
    c=AimConfig{};c.preferHigh=true;auto high=solveIntercept(c,{},target,vel,v,g);CHECK(high&&high->found&&high->launch.time>lead->launch.time);
    auto away=solveIntercept(AimConfig{},{},{240_fx,{},{}},{40_fx,{},{}},v,g);CHECK(away&&!away->found);
    CHECK(weaponHash(*lead)==weaponHash(*solveIntercept(AimConfig{},{},target,vel,v,g)));
}
void weaponRestore(){
    FeedConfig c;c.kind=FeedKind::PerShell;c.capacity=5;c.reloadStartTicks=3;c.shellTicks=4;c.intervalTicks=3;c.heatPerShot=300;c.heatCapacity=1000;c.coolPerTick=20;c.overheatTicks=5;c.recoverHeat=100;
    auto input=[](uint32_t t){return FeedInput{t%7<4,t%23==0,false};};
    FeedState a=makeFeed(c),b;uint32_t ra=40,rb=0;std::string saved;
    auto da=[&](uint32_t n){auto g=std::min(n,ra);ra-=g;return g;};auto db=[&](uint32_t n){auto g=std::min(n,rb);rb-=g;return g;};
    Hasher ha,hb;for(uint32_t t=0;t<120;++t){
        auto x=stepFeed(c,a,t,input(t),da);CHECK(x);hashWeapon(ha,*x);
        if(t==50){saved=save(a);CHECK(load(saved,b));rb=ra;hb=ha;}
        if(t>50){auto y=stepFeed(c,b,t,input(t),db);CHECK(y&&weaponHash(*x)==weaponHash(*y));hashWeapon(hb,*y);CHECK(save(a)==save(b));}
    }
    CHECK(ha.value()==hb.value()&&weaponHash(a)==weaponHash(b));roundtrip(a);
    ChargeState charge;CHECK(stepCharge(ChargeConfig{},charge,4,{true,false}));roundtrip(charge);ChargeState wrongKind;CHECK(!load(saved,wrongKind));
    FeedState corrupt;auto bytes=saved;bytes[bytes.size()-1]=char(2);CHECK(!load(bytes,corrupt));  // non-canonical bool
    FeedState invalid=a;invalid.phase=FeedPhase(9);CHECK(save(invalid).empty());
}
void weaponData(const std::string& root){
    auto expectError=[](const char* text,const std::string& message){
        try{readWeaponDefinitions(Json::parse(text));}catch(const JsonError& e){if(std::string(e.what()).find(message)!=std::string::npos)return;throw std::runtime_error(std::string("unexpected error: ")+e.what());}
        throw std::runtime_error("expected error: "+message);
    };
    expectError(R"({"materials":[{"id":1,"hardness":2}]})","weapons.materials[0]: unknown key 'hardness'");
    expectError(R"({"materials":[{"id":1},{"id":1}]})","weapons.materials[1].id: duplicate material id 1");
    expectError(R"({"materials":[{"id":1,"resistanceMilli":-5}]})","weapons.materials[0].resistanceMilli: integer out of range");
    expectError(R"({"materials":[{"id":1,"resistanceMilli":1.5}]})","weapons.materials[0].resistanceMilli: expected integer");
    expectError(R"({"attachments":[]})","weapons.attachments: requires a frame");
    expectError(R"({"frame":{"slots":[1],"stats":[{"id":1,"baseMilli":5,"minimumMilli":10}]}})","requires minimumMilli <= baseMilli <= maximumMilli");
    expectError(R"({"frame":{"slots":[1,1],"stats":[{"id":1,"baseMilli":5}]}})","weapons.frame.slots: duplicate slot id");
    expectError(R"({"frame":{"slots":[1],"stats":[{"id":1,"baseMilli":5}]},"attachments":[{"id":2,"slot":3}]})","weapons.attachments[0].slot: unknown frame slot 3");
    expectError(R"({"frame":{"slots":[1],"stats":[{"id":1,"baseMilli":5}]},"attachments":[{"id":2,"slot":1,"modifiers":[{"stat":4,"op":"add","valueMilli":1}]}]})","modifiers[0].stat: unknown frame stat 4");
    expectError(R"({"frame":{"slots":[1],"stats":[{"id":1,"baseMilli":5}]},"attachments":[{"id":2,"slot":1,"modifiers":[{"stat":1,"op":"pow","valueMilli":1}]}]})","modifiers[0].op: expected \"add\" or \"mul\"");
    expectError(R"({"frame":{"slots":[1],"stats":[{"id":1,"baseMilli":5}]},"attachments":[{"id":2,"slot":1,"modifiers":[{"stat":1,"op":"mul","valueMilli":-1}]}]})","valueMilli: integer out of range");
    auto parsed=aiverse::lib::gameplay::readGameplaySettings(Json::parseFile(root+"/examples/libraries/genre/weapon-settings.json"));CHECK(parsed.value&&weaponSettingsErrors(*parsed.value).empty());
    auto feed=feedConfig(*parsed.value);auto charge=chargeConfig(*parsed.value);auto blast=explosionConfig(*parsed.value);
    CHECK(feed.kind==FeedKind::PerShell&&feed.capacity==6&&valid(feed)&&charge.tiers.size()==3&&charge.tiers[1].ticks==35&&charge.tiers[1].scale==Fixed::fromRatio(3,2)&&valid(charge));
    CHECK(blast.edgeScale==Fixed::fromRatio(1,5)&&valid(blast)&&valid(penetrationConfig(*parsed.value))&&aimConfig(*parsed.value).tolerance==Fixed::fromRatio(5,1000));
    auto settings=[](const char* text){return aiverse::lib::gameplay::readGameplaySettings(Json::parse(text));};
    auto r=settings(R"({"schemaVersion":1,"ammoFeed":{"capacity":-1}})");CHECK(!r.value&&r.errors.at(0)=="ammoFeed.capacity: integer out of range");
    r=settings(R"({"schemaVersion":1,"explosion":{"innerRadiusMilli":6000}})");CHECK(!r.value&&r.errors.at(0)=="explosion.innerRadiusMilli: must not exceed outerRadiusMilli");
    r=settings(R"({"schemaVersion":1,"charge":{"blast":1}})");CHECK(!r.value&&r.errors.at(0)=="charge.blast: unknown field");
    r=settings(R"({"schemaVersion":1,"charge":{"autoReleaseTicks":5},"ammoFeed":{"kind":2,"shellTicks":0}})");CHECK(r.value);
    auto errors=weaponSettingsErrors(*r.value);CHECK(errors.size()==2&&errors[0]=="ammoFeed.shellTicks: perShell feeds need at least 1 tick per shell"&&errors[1]=="charge.autoReleaseTicks: must be 0 or at least minTicks");
    r=settings(R"({"schemaVersion":1,"charge":{"minTicks":10,"maxTicks":11,"tiers":5}})");CHECK(r.value&&weaponSettingsErrors(*r.value).at(0)=="charge.tiers: maxTicks - minTicks must be at least tiers - 1");
}
void weaponBallistics(const std::string& root){weaponPenetration(root);weaponExplosions();weaponFeeds();weaponCharge();weaponAttachments(root);weaponAiming();weaponRestore();weaponData(root);}
int main(int argc,char** argv){try{CHECK(argc==2);movement();combat();weapons();matches();narrative(argv[1]);weaponBallistics(argv[1]);std::cout<<"Genre policies: locomotion, melee, projectiles, rounds/capture, story and save/restore: OK\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
