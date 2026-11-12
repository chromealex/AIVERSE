#include <aiverselib/genre/Configured.h>
#include <aiverselib/genre/Persistence.h>
#include <aiverselib/genre/Data.h>
#include <aiverselib/genre/CombatConfigured.h>
#include <aiverselib/genre/CombatData.h>
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
// ---- combat-actions: hit/hurt shapes per frame, iframes, guard/parry, poise, lock-on, persistence, invalid data.
CombatShape sphereAt(Vec3 c,Fixed r){CombatShape s;s.a=c;s.b=c;s.radius=r;return s;}
CombatShape capsuleOf(Vec3 a,Vec3 b,Fixed r){CombatShape s;s.kind=ShapeKind::Capsule;s.a=a;s.b=b;s.radius=r;return s;}
CombatShape boxAt(Vec3 c,Vec3 half,Fixed yaw={}){CombatShape s;s.kind=ShapeKind::Box;s.a=c;s.b=c;s.half=half;s.yaw=yaw;return s;}
Fixed milli(int64_t v){return Fixed::fromRatio(v,1000);}
CombatBody bodyAt(int64_t id,int64_t team,Vec3 at,Fixed yaw={}){return {id,team,{at,yaw},{{1,capsuleOf({},{0_fx,2_fx,0_fx},milli(400))}}};}
void combatShapes(){
    CHECK(overlaps(sphereAt({},1_fx),sphereAt({2_fx,{},{}},1_fx)));CHECK(!overlaps(sphereAt({},1_fx),sphereAt({milli(2010),{},{}},1_fx)));
    auto thin=boxAt({},{1_fx,1_fx,milli(100)});auto probe=sphereAt({milli(500),{},milli(-500)},milli(50));
    CHECK(!overlaps(thin,probe));CHECK(overlaps(boxAt({},{1_fx,1_fx,milli(100)},Fixed::fromRatio(1,8)),probe));
    auto cube=boxAt({},{1_fx,1_fx,1_fx});CHECK(!overlaps(cube,boxAt({milli(2300),{},{}},{1_fx,1_fx,1_fx})));
    CHECK(overlaps(cube,boxAt({milli(2300),{},{}},{1_fx,1_fx,1_fx},Fixed::fromRatio(1,8))));CHECK(!overlaps(cube,boxAt({{},milli(2100),{}},{1_fx,1_fx,1_fx})));
    auto rod=capsuleOf({-3_fx,{},{}},{3_fx,{},{}},milli(100));
    CHECK(!overlaps(rod,boxAt({{},{},milli(500)},{milli(200),milli(200),milli(350)})));CHECK(overlaps(rod,boxAt({{},{},milli(500)},{milli(200),milli(200),milli(450)})));
    CHECK(overlaps(capsuleOf({-1_fx,{},{}},{1_fx,{},{}},milli(100)),capsuleOf({{},milli(150),-1_fx},{{},milli(150),1_fx},milli(100))));
    CHECK(!overlaps(capsuleOf({-1_fx,{},{}},{1_fx,{},{}},milli(100)),capsuleOf({{},milli(250),-1_fx},{{},milli(250),1_fx},milli(100))));
    auto bad=sphereAt({},-1_fx);CHECK(!valid(bad)&&!overlaps(bad,bad));auto roundBox=cube;roundBox.radius=1_fx;CHECK(!valid(roundBox));
}
void combatHitboxes(){
    AttackSet defs{{1,{1,2,4,4,6,8,{}}}};auto yes=[](auto){return true;};
    HitboxSet boxes{{1,{{1,2,4,sphereAt({{},1_fx,1_fx},milli(500))},{2,4,6,capsuleOf({{},1_fx,2_fx},{{},1_fx,3_fx},milli(250))}}}};CHECK(valid(defs,boxes));
    auto attacker=bodyAt(1,1,{});std::vector<CombatBody> bodies{bodyAt(11,2,{{},{},3_fx}),attacker,bodyAt(10,2,{{},{},milli(1200)}),bodyAt(12,1,{milli(200),{},1_fx})};
    MeleeState s;CHECK(stepMelee(defs,s,0,1,yes));std::vector<std::vector<int64_t>> perTick;
    for(uint32_t t=1;t<=7;++t){CHECK(stepMelee(defs,s,t,0,yes));auto hits=collectMeleeHits(defs,boxes,s,t,attacker,bodies);CHECK(hits);std::vector<int64_t> ids;for(auto h:*hits)ids.push_back(h.target*100+h.hitbox*10+h.hurtbox);perTick.push_back(ids);}
    CHECK(perTick[0].empty());CHECK(perTick[1]==std::vector<int64_t>{1011});CHECK(perTick[2].empty());CHECK(perTick[3]==std::vector<int64_t>{1121});
    CHECK(perTick[4].empty()&&perTick[5].empty()&&perTick[6].empty());CHECK((s.hitTargets==std::vector<int64_t>{10,11}));roundtrip(s);
    MeleeState ff;CHECK(stepMelee(defs,ff,0,1,yes));auto all=collectMeleeHits(defs,boxes,ff,2,attacker,bodies,true);CHECK(all&&all->size()==2&&(*all)[0].target==10&&(*all)[1].target==12);
    MeleeState turned;CHECK(stepMelee(defs,turned,0,1,yes));auto side=bodyAt(1,1,{},Fixed::fromRatio(1,4));std::vector<CombatBody> right{bodyAt(20,0,{milli(1200),{},{}}),bodyAt(10,2,{{},{},milli(1200)})};
    auto rotated=collectMeleeHits(defs,boxes,turned,2,side,right);CHECK(rotated&&rotated->size()==1&&rotated->front().target==20);
    MeleeState untouched;CHECK(stepMelee(defs,untouched,0,1,yes));auto before=save(untouched);std::vector<CombatBody> twins{bodyAt(10,2,{}),bodyAt(10,2,{})};
    CHECK(!collectMeleeHits(defs,boxes,untouched,2,attacker,twins)&&save(untouched)==before);
    auto broken=boxes;broken[1][0].to=7;CHECK(!valid(defs,broken)&&!collectMeleeHits(defs,broken,untouched,2,attacker,bodies));
    auto active=activeHitboxes(defs,boxes,s,4,attacker.pose);CHECK(active.size()==1&&active[0].first==2&&activeHitboxes(defs,boxes,s,1,attacker.pose).empty()&&activeHitboxes(defs,boxes,s,7,attacker.pose).empty());
}
void combatDefense(){
    DodgeConfig dc{12,2,8,20};DodgeState ds;auto yes=[]{return true;};
    CHECK(stepDodge(dc,ds,9,false,yes)==std::optional<bool>(false));CHECK(stepDodge(dc,ds,10,true,yes)==std::optional<bool>(true)&&ds.readyAt==42);
    CHECK(!stepDodge(dc,ds,10,true,yes));CHECK(!stepDodge(dc,ds,5,true,yes));
    CHECK(!dodgeInvulnerable(dc,ds,11)&&dodgeInvulnerable(dc,ds,12)&&dodgeInvulnerable(dc,ds,17)&&!dodgeInvulnerable(dc,ds,18)&&dodging(dc,ds,21)&&!dodging(dc,ds,22));
    GuardConfig gc{Fixed::fromRatio(1,8),6,20};GuardState down;CombatPose me{};
    CHECK(resolveCombatContact(dc,ds,gc,down,me,{{},{},2_fx},12)==MeleeContact::Evaded);CHECK(resolveCombatContact(dc,ds,gc,down,me,{{},{},2_fx},18)==MeleeContact::Hit);
    CHECK(stepDodge(dc,ds,25,true,yes)==std::optional<bool>(false));CHECK(stepDodge(dc,ds,41,true,yes)==std::optional<bool>(false));CHECK(stepDodge(dc,ds,42,true,yes)==std::optional<bool>(true));
    CHECK(stepDodge(dc,ds,80,true,[]{return false;})==std::optional<bool>(false)&&ds.started==42);roundtrip(ds);
    auto badDodge=dc;badDodge.invulnTo=13;DodgeState fresh;CHECK(!stepDodge(badDodge,fresh,0,true,yes)&&!fresh.initialized);
    GuardState g;CHECK(stepGuard(gc,g,100,true)&&g.parryArmed);Vec3 front{{},{},2_fx},behind{{},{},-2_fx};
    CHECK(resolveCombatContact(dc,ds,gc,g,me,front,102)==MeleeContact::Parried);CHECK(resolveCombatContact(dc,ds,gc,g,me,front,106)==MeleeContact::Blocked);
    CHECK(resolveCombatContact(dc,ds,gc,g,me,behind,102)==MeleeContact::Hit);CHECK(resolveCombatContact(dc,ds,gc,g,me,{1_fx,{},2_fx},106)==MeleeContact::Blocked);
    CHECK(resolveCombatContact(dc,ds,gc,g,me,{2_fx,{},1_fx},106)==MeleeContact::Hit);CHECK(resolveCombatContact(dc,ds,gc,g,me,front,102,true)==MeleeContact::Hit);
    CHECK(resolveCombatContact(dc,ds,gc,g,{{},Fixed::fromRatio(1,2)},behind,102)==MeleeContact::Parried);
    CHECK(stepGuard(gc,g,111,false)&&stepGuard(gc,g,112,true)&&!g.parryArmed);CHECK(resolveCombatContact(dc,ds,gc,g,me,front,112)==MeleeContact::Blocked);
    CHECK(stepGuard(gc,g,113,false)&&stepGuard(gc,g,140,true)&&g.parryArmed);CHECK(!stepGuard(gc,g,140,true));CHECK(resolveCombatContact(dc,ds,gc,g,me,front,141)==MeleeContact::Parried);roundtrip(g);
}
void combatPoise(){
    PoiseConfig pc{1000,100,500,10,5,3,20,15};PoiseState p;CHECK(stepPoise(pc,p,0)&&p.poise==1000);
    CHECK(stepPoise(pc,p,1));auto r=applyPoiseHit(pc,p,1,400,MeleeContact::Hit);CHECK(r&&r->poiseDamage==400&&!r->broke&&p.poise==600&&staggered(p,5)&&!staggered(p,6));
    CHECK(stepPoise(pc,p,2)&&p.poise==600);r=applyPoiseHit(pc,p,2,400,MeleeContact::Blocked);CHECK(r&&r->poiseDamage==200&&p.poise==400&&p.stunUntil==6);
    r=applyPoiseHit(pc,p,2,999,MeleeContact::Parried);CHECK(r&&r->poiseDamage==0&&p.poise==400);r=applyPoiseHit(pc,p,2,999,MeleeContact::Evaded);CHECK(r&&r->poiseDamage==0);
    CHECK(stepPoise(pc,p,3));r=applyPoiseHit(pc,p,3,500,MeleeContact::Hit);CHECK(r&&r->broke&&p.broken&&p.poise==0&&p.stunUntil==23);roundtrip(p);
    CHECK(stepPoise(pc,p,4));r=applyPoiseHit(pc,p,4,100,MeleeContact::Hit);CHECK(r&&r->poiseDamage==0&&!r->broke&&r->stunUntil==23);
    CHECK(stepPoise(pc,p,22)&&p.broken);CHECK(stepPoise(pc,p,23)&&!p.broken&&p.poise==1000&&!staggered(p,23));
    CHECK(stepPoise(pc,p,30)&&applyPoiseHit(pc,p,30,300,MeleeContact::Hit)&&p.poise==700);CHECK(stepPoise(pc,p,35)&&p.poise==700);CHECK(stepPoise(pc,p,41)&&p.poise==800);CHECK(stepPoise(pc,p,45)&&p.poise==1000);
    CHECK(!stepPoise(pc,p,45));CHECK(!applyPoiseHit(pc,p,46,10,MeleeContact::Hit));CHECK(!applyPoiseHit(pc,p,45,-1,MeleeContact::Hit));
    PoiseState attacker;CHECK(stepPoise(pc,attacker,141)&&applyParryStagger(pc,attacker,141)&&staggered(attacker,155)&&!staggered(attacker,156));CHECK(!applyParryStagger(pc,attacker,142));
    MeleeState swing;swing.attack=1;swing.queued=1;swing.hitTargets={4};interruptMelee(swing);CHECK(swing.attack==0&&swing.queued==0&&swing.hitTargets.empty());
    auto badPoise=pc;badPoise.blockScaleMilli=2000;PoiseState unused;CHECK(!stepPoise(badPoise,unused,0));PoiseState lie;lie.initialized=true;lie.broken=true;lie.poise=5;CHECK(save(lie).empty());
}
void combatLockOn(){
    LockOnConfig lc{15_fx,20_fx,Fixed::fromRatio(1,4),1000,1000};CombatPose me{};LockOnState s;
    std::vector<LockCandidate> c{{5,{{},{},5_fx}},{3,{3_fx,{},5_fx}},{7,{-3_fx,{},5_fx}},{9,{{},{},-5_fx}},{8,{{},{},4_fx},false}};
    CHECK(selectLockTarget(lc,me,c)==5);auto r=stepLockOn(lc,s,1,me,c,{true,0});CHECK(r&&r->target==5&&r->changed);
    r=stepLockOn(lc,s,2,me,c,{false,1});CHECK(r&&r->target==3&&r->changed);r=stepLockOn(lc,s,3,me,c,{false,1});CHECK(r&&r->target==3&&!r->changed);
    r=stepLockOn(lc,s,4,me,c,{false,-1});CHECK(r&&r->target==5);r=stepLockOn(lc,s,5,me,c,{false,-1});CHECK(r&&r->target==7);CHECK(!stepLockOn(lc,s,5,me,c,{}));roundtrip(s);
    auto far=c;far[2].position={-30_fx,{},5_fx};r=stepLockOn(lc,s,7,me,far,{});CHECK(r&&r->broken&&r->target==0);
    r=stepLockOn(lc,s,8,me,c,{true,0});CHECK(r&&r->target==5);auto hidden=c;hidden[0].targetable=false;r=stepLockOn(lc,s,9,me,hidden,{});CHECK(r&&r->broken&&!r->target);
    r=stepLockOn(lc,s,10,me,c,{true,0});CHECK(r&&r->target==5);r=stepLockOn(lc,s,11,me,c,{true,0});CHECK(r&&r->target==0&&r->changed&&!r->broken);
    std::vector<LockCandidate> tie{{21,{{},{},4_fx}},{20,{{},{},4_fx}}};CHECK(selectLockTarget(lc,me,tie)==20);
    CombatPose back{{},Fixed::fromRatio(1,2)};CHECK(selectLockTarget(lc,back,c)==9);
    std::vector<LockCandidate> twins{{4,{}},{4,{}}};CHECK(!stepLockOn(lc,s,12,me,twins,{true,0}));CHECK(!stepLockOn(lc,s,12,me,c,{false,2}));
    auto badLock=lc;badLock.breakRange=10_fx;CHECK(!stepLockOn(badLock,s,12,me,c,{true,0})&&s.lastTick==11);
}
void combatPersistence(){
    CombatActorState actor;DodgeConfig dc;GuardConfig gc;PoiseConfig pc;LockOnConfig lc;std::vector<LockCandidate> c{{5,{{},{},5_fx}}};
    auto play=[&](CombatActorState& a,uint32_t from,uint32_t to){for(auto t=from;t<to;++t){CHECK(stepDodge(dc,a.dodge,t,t%17==3,[]{return true;}));CHECK(stepGuard(gc,a.guard,t,t%11<4));CHECK(stepPoise(pc,a.poise,t));
        if(t%7==0)CHECK(applyPoiseHit(pc,a.poise,t,9000,resolveCombatContact(dc,a.dodge,gc,a.guard,{},{{},{},1_fx},t)));CHECK(stepLockOn(lc,a.lock,t,{},c,{t%13==0,0}));}};
    play(actor,0,60);roundtrip(actor);auto bytes=save(actor);CombatActorState restored;CHECK(load(bytes,restored)&&combatHash(restored)==combatHash(actor));
    CombatActorState straight=actor;play(straight,60,120);play(restored,60,120);CHECK(save(straight)==save(restored)&&combatHash(straight)==combatHash(restored));
    auto changed=restored;changed.poise.stunUntil+=1;CHECK(combatHash(changed)!=combatHash(restored));
    GuardState wrongKind;CHECK(!load(save(actor.dodge),wrongKind));auto tampered=bytes;tampered[4]=char(99);CHECK(!load(tampered,restored)&&save(restored)==save(straight));
    roundtrip(actor.dodge);roundtrip(actor.poise);roundtrip(actor.lock);
}
void combatData(const std::string& root){
    auto settings=aiverse::lib::gameplay::readGameplaySettings(Json::parseFile(root+"/examples/libraries/genre/settings.json"));CHECK(settings.value);
    CHECK(dodgeConfig(*settings.value).cooldown==24&&guardConfig(*settings.value).parryTicks==5&&lockOnConfig(*settings.value).range==12_fx);
    CHECK(valid(dodgeConfig(*settings.value))&&valid(guardConfig(*settings.value))&&valid(poiseConfig(*settings.value))&&valid(lockOnConfig(*settings.value)));
    auto defaults=aiverse::lib::gameplay::readGameplaySettings(Json::parse(R"({"schemaVersion":1})"));CHECK(defaults.value&&poiseConfig(*defaults.value).maximum==100000&&lockOnConfig(*defaults.value).halfAngle==Fixed::fromRatio(1,4));
    auto error=[&](const char* text,const std::string& expected){auto r=aiverse::lib::gameplay::readGameplaySettings(Json::parse(text));if(r.value)return false;for(const auto& e:r.errors)if(e.find(expected)!=std::string::npos)return true;return false;};
    CHECK(error(R"({"schemaVersion":1,"combat":{"dodgeTicks":4,"dodgeInvulnTo":8}})","combat.dodgeInvulnTo: must not exceed dodgeTicks"));
    CHECK(error(R"({"schemaVersion":1,"combat":{"parryTicks":5000}})","combat.parryTicks: integer out of range"));
    CHECK(error(R"({"schemaVersion":1,"combat":{"stamina":1}})","combat.stamina: unknown field"));
    CHECK(error(R"({"schemaVersion":1,"combat":{"lockRangeMilli":30000,"lockBreakMilli":20000}})","combat.lockRangeMilli: must not exceed lockBreakMilli"));
    AttackSet defs{{1,attackConfig(*settings.value,1)}};auto boxes=readHitboxes(Json::parseFile(root+"/examples/libraries/genre/hitboxes.json"),defs);
    CHECK(boxes.at(1).size()==2&&boxes.at(1)[1].shape.kind==ShapeKind::Box&&boxes.at(1)[0].shape.radius==milli(250)&&valid(defs,boxes));
    auto rejects=[&](const std::string& text,const std::string& expected){try{readHitboxes(Json::parse(text),defs);}catch(const JsonError& e){return std::string(e.what()).find(expected)!=std::string::npos;}return false;};
    CHECK(rejects(R"({"attacks":[{"attack":9,"hitboxes":[]}]})","hitboxes.attack 9: unknown attack"));
    CHECK(rejects(R"({"attacks":[{"attack":1,"hitboxes":[{"id":1,"from":0,"to":2,"shape":"sphere","a":[0,0,0],"radiusMilli":1}]}]})","frames must lie inside the active window 5..8"));
    CHECK(rejects(R"({"attacks":[{"attack":1,"hitboxes":[{"id":1,"from":5,"to":6,"shape":"cone","a":[0,0,0],"radiusMilli":1}]}]})","expected sphere, capsule or box"));
    CHECK(rejects(R"({"attacks":[{"attack":1,"hitboxes":[{"id":1,"from":5,"to":6,"shape":"sphere","a":[0,0,0],"radiusMilli":1,"damage":3}]}]})","unknown key 'damage'"));
    CHECK(rejects(R"({"attacks":[{"attack":1,"hitboxes":[{"id":1,"from":5,"to":6,"shape":"box","a":[0,0,0],"half":[1,1,1],"radiusMilli":1}]}]})","boxes have no radius"));
    CHECK(rejects(R"({"attacks":[{"attack":1,"hitboxes":[{"id":2,"from":5,"to":6,"shape":"sphere","a":[0,0,0],"radiusMilli":1},{"id":2,"from":5,"to":6,"shape":"sphere","a":[0,0,0],"radiusMilli":1}]}]})","ids must be unique and ascending"));
    CHECK(rejects(R"({"attacks":[{"attack":1,"hitboxes":[{"id":1,"from":5,"to":6,"shape":"sphere","a":[0,0],"radiusMilli":1}]}]})","expected [x,y,z]"));
}
void combatActions(const std::string& root){combatShapes();combatHitboxes();combatDefense();combatPoise();combatLockOn();combatPersistence();combatData(root);}
int main(int argc,char** argv){try{CHECK(argc==2);movement();combat();weapons();matches();narrative(argv[1]);combatActions(argv[1]);std::cout<<"Genre policies: locomotion, melee, combat actions, projectiles, rounds/capture, story and save/restore: OK\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
