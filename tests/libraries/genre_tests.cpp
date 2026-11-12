#include <aiverselib/genre/Configured.h>
#include <aiverselib/genre/Persistence.h>
#include <aiverselib/genre/Data.h>
#include <aiverselib/genre/LocomotionModes.h>
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
// Locomotion mode machine: each mode's trajectory, save/restore and invalid-data rejection.
namespace loco {
using M=LocomotionMode;
const Fixed dt=Fixed::fromRatio(1,60);
bool near(Fixed a,Fixed b,Fixed eps){return fxAbs(a-b)<=eps;}
LocomotionResult step(const LocomotionModeConfig& c,LocomotionState& s,const LocomotionInput& in,uint32_t t){auto r=stepLocomotion(c,s,in,t,dt);CHECK(r);return *r;}
LocomotionInput grounded(){LocomotionInput in;in.env.grounded=true;return in;}
void compatibilityAndSlopes(){
    LocomotionModeConfig c;CHECK(valid(c));
    MotionState a;LocomotionState b;Vec3 p{};
    for(uint32_t t=0;t<240;++t){
        MotionInput mi{{1_fx,{}},p.y<=Fixed{},t%90==0,false};LocomotionInput li;li.wish=mi.wish;li.jumpPressed=mi.jumpPressed;li.env.grounded=mi.grounded;
        auto x=stepMotion(c.motion,a,mi,t,dt);auto y=stepLocomotion(c,b,li,t,dt);CHECK(x&&y&&x->velocity==y->velocity&&x->jumped==y->jumped);
        CHECK(y->mode==(mi.grounded&&!x->jumped?M::Ground:M::Air));p+=x->velocity*dt;if(p.y<Fixed{})p.y={};
    }
    // 30 degrees: walkable, velocity follows the slope plane.
    const Vec3 gentle{fxSinTurns(Fixed::fromRatio(1,12)),fxCosTurns(Fixed::fromRatio(1,12)),{}};
    LocomotionState s;auto in=grounded();in.env.groundNormal=gentle;in.wish={1_fx,{}};LocomotionResult r;
    for(uint32_t t=0;t<60;++t)r=step(c,s,in,t);
    CHECK(r.mode==M::Ground&&!r.sliding&&r.velocity.x>5_fx&&r.velocity.y<Fixed{}&&fxAbs(r.velocity.dot(fxNormalize(gentle)))<Fixed::fromRatio(1,100));
    // 60 degrees: slides downhill, cannot jump, speed capped.
    in.env.groundNormal={fxSinTurns(Fixed::fromRatio(1,6)),fxCosTurns(Fixed::fromRatio(1,6)),{}};in.wish={};in.jumpPressed=true;
    r=step(c,s,in,60);CHECK(r.sliding&&(r.events&LocoSlideStart)&&!r.jumped&&r.mode==M::Ground);in.jumpPressed=false;
    for(uint32_t t=61;t<400;++t){r=step(c,s,in,t);CHECK(!r.jumped&&r.velocity.length()<=c.slideSpeed+Fixed::fromRatio(1,100));}
    CHECK(r.velocity.y< -5_fx&&r.velocity.x>Fixed{});roundtrip(s);
    r=step(c,s,grounded(),400);CHECK(!r.sliding&&(r.events&LocoSlideEnd)&&r.velocity.y==Fixed{});
}
void ledges(){
    LocomotionModeConfig c;LocomotionState s;LocomotionInput in;in.env.ledge=true;in.env.ledgeHeight=Fixed::fromRatio(3,2);in.env.ledgeNormal={{},{},-1_fx};
    auto r=step(c,s,in,0);CHECK(r.mode==M::Air); // first airborne tick: was grounded
    r=step(c,s,in,1);CHECK(r.mode==M::Ledge&&(r.events&LocoLedgeGrab)&&r.velocity==Vec3{});roundtrip(s);
    in.wish={1_fx,{}};r=step(c,s,in,2);CHECK(r.mode==M::Ledge&&near(r.velocity.x,-c.shimmySpeed,Fixed::fromRaw(4))&&r.velocity.y==Fixed{});
    in.wish={};in.jumpPressed=true;Vec3 p{};r=step(c,s,in,3);CHECK(r.mode==M::Mantle&&(r.events&LocoLedgeClimb));in.jumpPressed=false;
    uint32_t t=3;while(r.mode==M::Mantle){p+=r.velocity*dt;in.env.grounded=t+1-3>=c.ledgeClimbTicks;r=step(c,s,in,++t);if(r.mode==M::Mantle)roundtrip(s);}
    CHECK(t==3+c.ledgeClimbTicks&&(r.events&LocoMantleEnd)&&r.mode==M::Ground);
    CHECK(near(p.y,Fixed::fromRatio(3,2),Fixed::fromRatio(1,100))&&near(p.z,c.ledgeClimbDepth,Fixed::fromRatio(1,100))&&p.x==Fixed{});
    // Drop, regrab cooldown, missing ledge and out-of-range heights.
    LocomotionState d;in=LocomotionInput{};in.env.ledge=true;in.env.ledgeHeight=Fixed::fromRatio(3,2);in.env.ledgeNormal={{},{},-1_fx};step(c,d,in,0);CHECK(step(c,d,in,1).mode==M::Ledge);
    in.dropPressed=true;r=step(c,d,in,2);CHECK(r.mode==M::Air&&(r.events&LocoLedgeRelease));in.dropPressed=false;
    for(uint32_t k=3;k<=2+c.ledgeRegrabTicks;++k)CHECK(step(c,d,in,k).mode==M::Air);CHECK(step(c,d,in,3+c.ledgeRegrabTicks).mode==M::Ledge);
    in.env.ledge=false;r=step(c,d,in,20);CHECK(r.mode==M::Air&&(r.events&LocoLedgeRelease));
    LocomotionState high;in.env.ledge=true;in.env.ledgeHeight=3_fx;step(c,high,in,0);CHECK(step(c,high,in,1).mode==M::Air);
}
void ladders(){
    LocomotionModeConfig c;LocomotionState s;auto in=grounded();in.env.ladder=true;in.env.ladderNormal={{},{},-1_fx};in.env.ladderBottom=true;in.grabPressed=true;
    auto r=step(c,s,in,0);CHECK(r.mode==M::Ladder&&(r.events&LocoLadderAttach));in.grabPressed=false;in.env.grounded=false;in.env.ladderBottom=false;
    in.vertical=1_fx;Vec3 p{};uint32_t t=1;for(;t<61;++t){r=step(c,s,in,t);CHECK((r.mode==M::Ladder&&r.velocity==Vec3{{},c.ladderSpeed,{}}));p+=r.velocity*dt;}
    CHECK(near(p.y,c.ladderSpeed,Fixed::fromRatio(1,100)));roundtrip(s);
    in.env.ladderTop=true;r=step(c,s,in,t);CHECK(r.mode==M::Mantle&&(r.events&LocoLadderExitTop)&&r.velocity.y>Fixed{});
    const uint32_t start=t;Vec3 exit{};while(r.mode==M::Mantle){exit+=r.velocity*dt;in.env.grounded=t+1-start>=c.ladderExitTicks;r=step(c,s,in,++t);}
    CHECK(r.mode==M::Ground&&near(exit.y,c.ladderExitRise,Fixed::fromRatio(1,100))&&near(exit.z,c.ladderExitDepth,Fixed::fromRatio(1,100)));
    // Bottom exit and jump-off.
    LocomotionState b;in=grounded();in.env.ladder=true;in.env.ladderNormal={{},{},-1_fx};in.grabPressed=true;step(c,b,in,0);in.grabPressed=false;
    in.vertical=-1_fx;in.env.ladderBottom=true;r=step(c,b,in,1);CHECK(r.mode==M::Ground&&(r.events&LocoLadderExitBottom));
    LocomotionState j;in=LocomotionInput{};in.env.ladder=true;in.env.ladderNormal={{},{},-1_fx};in.grabPressed=true;CHECK(step(c,j,in,0).mode==M::Ladder);in.grabPressed=false;in.jumpPressed=true;
    r=step(c,j,in,1);CHECK(r.mode==M::Air&&r.jumped&&(r.events&LocoLadderDetach)&&r.velocity.y==c.motion.jumpSpeed&&r.velocity.z<Fixed{});
    in.jumpPressed=false;in.env.ladder=false;r=step(c,j,in,2);CHECK(r.mode==M::Air&&r.velocity.y<c.motion.jumpSpeed);
}
void swimming(){
    LocomotionModeConfig c;LocomotionState s;const Fixed surface=0_fx;Vec3 p{{},-3_fx,{}};LocomotionInput in;uint32_t events=0;LocomotionResult r;
    auto depth=[&]{return fxMax(surface-p.y,Fixed{});};
    for(uint32_t t=0;t<600;++t){in.env.waterDepth=depth();r=step(c,s,in,t);events|=r.events;p+=r.velocity*dt;if(t==0)CHECK((r.events&LocoWaterEnter)&&(r.events&LocoDived)&&r.mode==M::Swim);CHECK(r.mode==M::Swim);}
    CHECK((events&LocoSurfaced)&&near(depth(),c.floatDepth,Fixed::fromRatio(1,20))&&!s.submerged);roundtrip(s);
    in.vertical=-1_fx;events=0;for(uint32_t t=600;t<700;++t){in.env.waterDepth=depth();r=step(c,s,in,t);events|=r.events;p+=r.velocity*dt;}
    CHECK((events&LocoDived)&&r.velocity.y==-c.swimVerticalSpeed&&s.submerged);
    in.vertical={};in.wish={{},1_fx};for(uint32_t t=700;t<1300;++t){in.env.waterDepth=depth();r=step(c,s,in,t);p+=r.velocity*dt;}
    CHECK(near(r.velocity.z,c.swimSpeed,Fixed::fromRaw(8))&&near(depth(),c.floatDepth,Fixed::fromRatio(1,20)));
    in.jumpPressed=true;in.env.waterDepth=depth();r=step(c,s,in,1300);CHECK(r.mode==M::Air&&r.jumped&&(r.events&LocoWaterExit)&&r.velocity.y==c.waterJumpSpeed);
    in.jumpPressed=false;r=step(c,s,in,1301);CHECK(r.mode==M::Air); // rising: no immediate re-entry
    // Wading out: shallow water exits to ground with the same tick's ground step.
    LocomotionState w;auto g=grounded();g.env.waterDepth=Fixed::fromRatio(13,10);CHECK(step(c,w,g,0).mode==M::Swim);
    g.env.waterDepth=Fixed::fromRatio(1,2);g.wish={1_fx,{}};r=step(c,w,g,1);CHECK(r.mode==M::Ground&&(r.events&LocoWaterExit)&&r.velocity.x>Fixed{});
}
void climbing(){
    LocomotionModeConfig c;c.staminaMax=10;LocomotionState s;auto in=grounded();in.env.climbable=true;in.env.wallNormal={{},{},-1_fx};in.grabPressed=true;
    auto r=step(c,s,in,0);CHECK(r.mode==M::Climb&&(r.events&LocoClimbStart)&&s.stamina==10);in.grabPressed=false;in.env.grounded=false;in.vertical=1_fx;in.wish={1_fx,{}};
    for(uint32_t t=1;t<=10;++t){r=step(c,s,in,t);CHECK(r.mode==M::Climb&&r.velocity.y==c.climbSpeed&&near(r.velocity.x,-c.climbSpeed,Fixed::fromRaw(4)));}
    CHECK(s.stamina==0);roundtrip(s);r=step(c,s,in,11);CHECK(r.mode==M::Air&&(r.events&LocoStaminaDepleted)&&(r.events&LocoClimbEnd));
    in.grabPressed=true;r=step(c,s,in,12);CHECK(r.mode==M::Air); // exhausted: cannot reattach
    in=grounded();for(uint32_t t=13;t<20;++t)step(c,s,in,t);CHECK(s.stamina==10); // regen 2/tick, capped
    // Wall top -> ledge, wall jump, owner-managed stamina hook.
    LocomotionState top;in=grounded();in.env.climbable=true;in.env.wallNormal={{},{},-1_fx};in.grabPressed=true;step(c,top,in,0);in.grabPressed=false;in.env.grounded=false;
    in.vertical=1_fx;in.env.ledge=true;in.env.ledgeNormal={{},{},-1_fx};in.env.ledgeHeight=2_fx;r=step(c,top,in,1);CHECK(r.mode==M::Ledge&&(r.events&LocoLedgeGrab));
    LocomotionState jump;in=grounded();in.env.climbable=true;in.env.wallNormal={{},{},-1_fx};in.grabPressed=true;step(c,jump,in,0);in.grabPressed=false;in.jumpPressed=true;
    r=step(c,jump,in,1);CHECK(r.mode==M::Air&&(r.events&LocoWallJump)&&r.velocity.z==-c.wallJumpPush&&r.velocity.y==c.motion.jumpSpeed);
    int64_t pool=3,calls=0;auto spend=[&](int64_t cost){++calls;if(pool<cost)return false;pool-=cost;return true;};
    LocomotionState hook;in=grounded();in.env.climbable=true;in.env.wallNormal={{},{},-1_fx};in.grabPressed=true;CHECK(stepLocomotionWithStamina(c,hook,in,0,dt,spend)->mode==M::Climb);in.grabPressed=false;in.env.grounded=false;
    for(uint32_t t=1;t<=3;++t)CHECK(stepLocomotionWithStamina(c,hook,in,t,dt,spend)->mode==M::Climb);
    r=*stepLocomotionWithStamina(c,hook,in,4,dt,spend);CHECK(r.mode==M::Air&&(r.events&LocoStaminaDepleted)&&calls==4&&pool==0&&hook.stamina==10);
    LocomotionModeConfig off=c;off.climbEnabled=false;LocomotionState none;in=grounded();in.env.climbable=true;in.env.wallNormal={{},{},-1_fx};in.grabPressed=true;CHECK(step(off,none,in,0).mode==M::Ground);
}
void mantles(){
    LocomotionModeConfig c;LocomotionState s;auto in=grounded();in.wish={1_fx,{}};LocomotionResult r;
    for(uint32_t t=0;t<60;++t)r=step(c,s,in,t);CHECK(r.velocity.x==c.motion.groundSpeed);
    in.jumpPressed=true;in.env.obstacle=true;in.env.obstacleHeight=Fixed::fromRatio(4,5);in.env.obstacleDepth=Fixed::fromRatio(3,5);in.env.obstacleNormal={-1_fx,{},{}};
    r=step(c,s,in,60);CHECK(r.mode==M::Mantle&&(r.events&LocoVaultStart)&&s.mantle==MantleKind::Vault&&r.velocity.x==Fixed{}&&r.velocity.y>Fixed{});in.jumpPressed=false;in.env.grounded=false;
    Vec3 p{};uint32_t t=60;while(r.mode==M::Mantle){p+=r.velocity*dt;CHECK(t-60<c.vaultTicks);if(t-60>=c.vaultTicks/2)CHECK(r.velocity.x==c.motion.groundSpeed);in.env.grounded=t+1-60>=c.vaultTicks;r=step(c,s,in,++t);}
    CHECK(t==60+c.vaultTicks&&(r.events&LocoMantleEnd)&&r.mode==M::Ground&&r.velocity.x==c.motion.groundSpeed&&near(p.y,Fixed::fromRatio(4,5),Fixed::fromRatio(1,100)));
    // From rest: a full mantle; too high: an ordinary jump; ceiling aborts the rise.
    LocomotionState slow;in=grounded();in.jumpPressed=true;in.env.obstacle=true;in.env.obstacleHeight=Fixed::fromRatio(6,5);in.env.obstacleNormal={-1_fx,{},{}};
    r=step(c,slow,in,0);CHECK(r.mode==M::Mantle&&(r.events&LocoMantleStart)&&slow.mantleTicks==c.mantleTicks);roundtrip(slow);
    in.jumpPressed=false;in.env.ceiling=true;r=step(c,slow,in,1);CHECK(r.mode==M::Air&&(r.events&LocoMantleAbort)&&slow.mantle==MantleKind::None);
    LocomotionState tall;in=grounded();in.jumpPressed=true;in.env.obstacle=true;in.env.obstacleHeight=2_fx;in.env.obstacleNormal={-1_fx,{},{}};
    r=step(c,tall,in,0);CHECK(r.mode==M::Air&&r.jumped&&(r.events&LocoJumped));
}
// Scripted course mixing every mode; returns the owner's input for tick t.
LocomotionInput course(uint32_t t,Vec3 p){
    LocomotionInput in;in.wish={1_fx,{}};in.env.grounded=p.y<=Fixed{};
    if(t>=40&&t<45){in.jumpPressed=t==40;in.env.obstacle=true;in.env.obstacleHeight=Fixed::fromRatio(7,10);in.env.obstacleDepth=Fixed::fromRatio(1,2);in.env.obstacleNormal={-1_fx,{},{}};}
    if(t>=80&&t<160){in.env.ladder=true;in.env.ladderNormal={-1_fx,{},{}};in.grabPressed=t==80;in.vertical=1_fx;in.env.ladderTop=t>=140;}
    if(t>=200&&t<320){in.env.waterDepth=fxMax(Fixed::fromRatio(3,2)-p.y,Fixed{});in.vertical=t<240?-1_fx:Fixed{};}
    if(t>=340&&t<380){in.env.climbable=true;in.env.wallNormal={-1_fx,{},{}};in.grabPressed=t==340;in.vertical=1_fx;in.jumpPressed=t==370;}
    if(t>=400&&t<460){in.env.grounded=p.y<=Fixed{};in.env.groundNormal={fxSinTurns(Fixed::fromRatio(1,6)),fxCosTurns(Fixed::fromRatio(1,6)),{}};}
    return in;
}
void determinismAndRestore(){
    LocomotionModeConfig c;LocomotionState a,b,restored;Vec3 pa{},pb{},pr{};uint32_t modes=0;
    for(uint32_t t=0;t<480;++t){
        auto x=stepLocomotion(c,a,course(t,pa),t,dt),y=stepLocomotion(c,b,course(t,pb),t,dt);CHECK(x&&y&&x->velocity==y->velocity&&x->events==y->events&&x->mode==y->mode);
        modes|=1u<<uint32_t(x->mode);pa+=x->velocity*dt;pb+=y->velocity*dt;if(pa.y<Fixed{})pa.y={};if(pb.y<Fixed{})pb.y={};CHECK(locomotionHash(a)==locomotionHash(b));
        if(t==150){CHECK(load(save(a),restored));pr=pa;}
        if(t>150){auto z=stepLocomotion(c,restored,course(t,pr),t,dt);CHECK(z&&z->velocity==x->velocity);pr+=z->velocity*dt;if(pr.y<Fixed{})pr.y={};CHECK(locomotionHash(restored)==locomotionHash(a)&&pr==pa);}
        roundtrip(a);
    }
    CHECK(modes==0x7Bu); // every mode except Ledge, which ledges() covers
}
void rejection(const std::string& root){
    LocomotionModeConfig c;LocomotionState s;auto in=grounded();step(c,s,in,5);const auto before=save(s);
    CHECK(!stepLocomotion(c,s,in,5,dt)&&!stepLocomotion(c,s,in,4,dt)&&save(s)==before);
    in.wish={2_fx,{}};CHECK(!stepLocomotion(c,s,in,6,dt));in=grounded();in.env.groundNormal={};CHECK(!stepLocomotion(c,s,in,6,dt));
    in=grounded();in.env.ladder=true;CHECK(!stepLocomotion(c,s,in,6,dt)); // ladder without a horizontal normal
    in=grounded();in.env.waterDepth=-1_fx;CHECK(!stepLocomotion(c,s,in,6,dt));CHECK(!stepLocomotion(c,s,grounded(),6,Fixed{}));CHECK(save(s)==before);
    auto bad=c;bad.swimExitDepth=2_fx;CHECK(locomotionModeError(bad).find("swimExitDepth")!=std::string::npos&&!stepLocomotion(bad,s,grounded(),6,dt));
    bad=c;bad.mantleTicks=1;CHECK(!valid(bad));bad=c;bad.vaultMaxHeight=2_fx;CHECK(locomotionModeError(bad).find("vaultMaxHeight")!=std::string::npos);
    LocomotionState broken=s;broken.mode=LocomotionMode(9);CHECK(save(broken).empty());broken=s;broken.mode=M::Mantle;CHECK(save(broken).empty());
    auto bytes=before;CHECK(bytes.size()>49);bytes[49]=char(9);LocomotionState copy;CHECK(!load(bytes,copy));MotionState other;CHECK(!load(before,other));
    auto parse=[](const char* text){return aiverse::lib::gameplay::readGameplaySettings(Json::parse(text));};
    auto defaults=parse("{\"schemaVersion\":1}");CHECK(defaults.value&&locomotionConfigHash(locomotionModeConfig(*defaults.value))==locomotionConfigHash(c));
    auto relation=parse("{\"schemaVersion\":1,\"locomotion\":{\"swimExitDepthMilli\":1500}}");CHECK(!relation.value&&relation.errors.at(0)=="locomotion.swimExitDepthMilli: must not exceed swimEnterDepthMilli");
    auto unknown=parse("{\"schemaVersion\":1,\"locomotion\":{\"ladderSpeed\":3}}");CHECK(!unknown.value&&unknown.errors.at(0)=="locomotion.ladderSpeed: unknown field");
    auto range=parse("{\"schemaVersion\":1,\"locomotion\":{\"mantleTicks\":1}}");CHECK(!range.value&&range.errors.at(0)=="locomotion.mantleTicks: integer out of range");
    auto type=parse("{\"schemaVersion\":1,\"locomotion\":{\"climbEnabled\":1}}");CHECK(!type.value&&type.errors.at(0)=="locomotion.climbEnabled: expected boolean");
    auto example=aiverse::lib::gameplay::readGameplaySettings(Json::parseFile(root+"/examples/libraries/genre/settings.json"));CHECK(example.value);
    auto configured=locomotionModeConfig(*example.value);CHECK(valid(configured)&&configured.motion.maxJumps==2&&configured.staminaMax==240&&configured.maxSlope==Fixed::fromRatio(50000,360000)&&!configured.autoLedgeGrab);
}
}
void locomotionModes(const std::string& root){loco::compatibilityAndSlopes();loco::ledges();loco::ladders();loco::swimming();loco::climbing();loco::mantles();loco::determinismAndRestore();loco::rejection(root);}
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
// Match objective modes: zones/KotH, payload, elimination, extraction, bomb, route, clock, spectator,
// scoring through the match owner, save/restore mid-match and invalid data.
#include <aiverselib/genre/MatchObjectivesConfigured.h>
#include <aiverselib/genre/MatchObjectivesData.h>
namespace objectives {
using K=ObjectiveEventKind;
template<class F> ObjectiveEvents ok(F&& f){auto r=f();CHECK(r);return *r;}
bool has(const ObjectiveEvents& e,K kind,int64_t team=-1,int64_t subject=-1){for(const auto& x:e)if(x.kind==kind&&(team<0||x.team==team)&&(subject<0||x.subject==subject))return true;return false;}
void zones(){
    ZoneConfig c;c.captureTicks=3;ZoneState s;
    auto step=[&](const ZoneConfig& cfg,ZoneState& st,Occupants o,uint32_t t){return ok([&]{return stepZone(cfg,st,7,o,t);});};
    CHECK(step(c,s,{{1,1}},0).empty()&&s.capturer==1&&s.progress==1);
    auto e=step(c,s,{{1,1},{2,1}},1);CHECK(e.size()==1&&e[0]==(ObjectiveEvent{K::ZoneContested,0,7,2})&&s.progress==1&&s.contested);
    CHECK(step(c,s,{},2).empty()&&s.progress==0&&s.capturer==0&&!s.contested);
    step(c,s,{{1,1}},3);step(c,s,{{1,1}},4);e=step(c,s,{{1,1}},5);CHECK(e.size()==1&&e[0]==(ObjectiveEvent{K::ZoneCaptured,1,7,0})&&s.owner==1&&!s.progress);
    step(c,s,{{2,2}},6);CHECK(s.capturer==2&&s.progress==1);step(c,s,{{1,1}},7);CHECK(s.capturer==0&&s.progress==0&&s.owner==1);  // owner reverts
    auto before=save(s);CHECK(!stepZone(c,s,7,{{1,1}},7)&&!stepZone(c,s,7,{{0,1}},8)&&save(s)==before);roundtrip(s);
    // Neutralize first, then capture.
    ZoneConfig n;n.captureTicks=2;n.neutralizeFirst=true;ZoneState owned;owned.owner=1;
    step(n,owned,{{2,1}},0);e=step(n,owned,{{2,1}},1);CHECK(e.size()==1&&e[0]==(ObjectiveEvent{K::ZoneNeutralized,2,7,1})&&owned.owner==0);
    step(n,owned,{{2,1}},2);e=step(n,owned,{{2,1}},3);CHECK(has(e,K::ZoneCaptured,2)&&owned.owner==2);
    // Majority contest with a capture-rate cap.
    ZoneConfig m;m.captureTicks=6;m.maxCapturers=4;m.contest=ContestRule::Majority;ZoneState ms;
    step(m,ms,{{1,3},{2,1}},0);CHECK(ms.progress==2&&ms.contested);step(m,ms,{{1,2},{2,2}},1);CHECK(ms.progress==2);
    e=step(m,ms,{{1,4}},2);CHECK(has(e,K::ZoneCaptured,1)&&ms.owner==1);
    // Another team drains existing progress before building its own.
    ZoneConfig d;d.captureTicks=10;d.maxCapturers=2;ZoneState ds;step(d,ds,{{1,2}},0);CHECK(ds.progress==2);
    step(d,ds,{{2,1}},1);CHECK(ds.capturer==1&&ds.progress==1);step(d,ds,{{2,1}},2);CHECK(!ds.capturer&&!ds.progress);step(d,ds,{{2,1}},3);CHECK(ds.capturer==2&&ds.progress==1);
    // King of the hill: score every 3 controlled ticks; contested or empty ticks do not count.
    auto koth=kingOfTheHill(2,3);ZoneState h;int holds=0;
    const Occupants plan[]={{{1,1}},{{1,1}},{{1,1}},{{1,1}},{{1,1},{2,1}},{},{{1,1}},{{1,1}}};
    for(uint32_t t=0;t<8;++t){e=step(koth,h,plan[t],t);if(has(e,K::ZoneHoldTick,1))++holds;}
    CHECK(h.owner==1&&holds==1&&h.holdTicks==2);  // capture tick + 2, then contested/empty, then 2 more
    koth.holdRequiresPresence=false;ZoneState own;own.owner=1;holds=0;for(uint32_t t=0;t<6;++t)holds+=has(step(koth,own,{},t),K::ZoneHoldTick);CHECK(holds==2);
    HillRotation r{100,3};CHECK(activeHill(r,10,10)==0u&&activeHill(r,10,109)==0u&&activeHill(r,10,110)==1u&&activeHill(r,10,310)==0u&&!activeHill(r,10,9)&&!activeHill({0,0},0,0));
    ZoneConfig bad=c;bad.decayPerTick=4;CHECK(!valid(bad)&&!stepZone(bad,s,7,{},50));
}
PayloadTrack track(){auto t=makePayloadTrack({{},{10_fx,{},{}},{10_fx,{},10_fx}},{10_fx});CHECK(t);return *t;}
PayloadConfig payloadRules(){PayloadConfig c;c.pushSpeed=1_fx;c.extraPusher=Fixed::fromRatio(1,2);c.reverseSpeed=Fixed::fromRatio(1,4);c.maxPushers=2;c.reverseDelayTicks=2;c.checkpointBonusTicks=100;return c;}
void payload(){
    auto t=track();auto c=payloadRules();auto s=makePayload(1);CHECK(t.length==20_fx&&*payloadPoint(t,15_fx)==(Vec3{10_fx,{},5_fx}));
    auto step=[&](PayloadState& st,uint32_t a,uint32_t d,uint32_t tick){return ok([&]{return stepPayload(c,t,st,a,d,tick);});};
    step(s,1,0,0);CHECK(s.progress==1_fx);step(s,3,0,1);CHECK(s.progress==Fixed::fromRatio(5,2));
    step(s,1,1,2);CHECK(s.contested&&s.progress==Fixed::fromRatio(5,2));step(s,0,0,3);CHECK(s.progress==Fixed::fromRatio(5,2));
    step(s,0,0,4);CHECK(s.progress==Fixed::fromRatio(9,4));
    ObjectiveEvents e;for(uint32_t tick=5;tick<=10;++tick)e=step(s,2,0,tick);
    CHECK(e.size()==1&&e[0]==(ObjectiveEvent{K::PayloadCheckpoint,1,1,100})&&s.reached==1&&s.progress==Fixed::fromRatio(45,4));
    auto mid=save(s);
    for(uint32_t tick=11;tick<=30;++tick)step(s,0,0,tick);CHECK(s.progress==10_fx);  // locked at the checkpoint
    for(uint32_t tick=31;tick<=37;++tick)e=step(s,2,0,tick);CHECK(s.delivered&&s.progress==20_fx&&has(e,K::PayloadDelivered,1));
    CHECK(step(s,2,0,38).empty());roundtrip(s);
    PayloadState restored;CHECK(load(mid,restored)&&restored.progress==Fixed::fromRatio(45,4));
    c.checkpointLock=false;for(uint32_t tick=11;tick<=80;++tick)step(restored,0,0,tick);CHECK(restored.progress==Fixed{});
    CHECK(!stepPayload(c,t,restored,1,0,80)&&!makePayloadTrack({{},{}},{})&&!makePayloadTrack({{},{1_fx,{},{}}},{1_fx}));
    auto broken=t;broken.length=19_fx;CHECK(!valid(broken)&&!stepPayload(c,broken,restored,1,0,81));
}
void elimination(){
    EliminationConfig c{2,5,10,false};const std::array<std::pair<int64_t,int64_t>,3> roster{{{1,1},{2,1},{3,2}}};
    auto s=makeElimination(c,roster);CHECK(s);
    auto step=[&](std::vector<int64_t> d,uint32_t t){return ok([&]{return stepElimination(c,*s,t,d);});};
    CHECK(step({},0).empty());auto e=step({1},3);CHECK(e.size()==1&&e[0]==(ObjectiveEvent{K::PlayerDied,1,1,1})&&s->players.at(1).respawnAt==10);
    CHECK(step({},9).empty());e=step({},10);CHECK(e.size()==1&&e[0].kind==K::PlayerRespawned&&s->players.at(1).alive);
    e=step({1},12);CHECK(has(e,K::PlayerOutOfLives,1,1)&&s->players.at(1).out);
    step({3},13);step({2},14);CHECK(s->players.at(3).respawnAt==20&&s->players.at(2).respawnAt==20&&s->eliminated.empty());
    auto before=save(*s);CHECK(!stepElimination(c,*s,15,std::vector<int64_t>{1})&&!stepElimination(c,*s,15,std::vector<int64_t>{4})&&!stepElimination(c,*s,14,std::vector<int64_t>{})&&save(*s)==before);
    e=step({},20);CHECK(e.size()==2&&e[0].subject==2&&e[1].subject==3);roundtrip(*s);
    e=step({3,2},21);CHECK(has(e,K::TeamEliminated,1)&&has(e,K::TeamEliminated,2)&&has(e,K::LastTeamStanding,0)&&s->lastStanding==-1);
    EliminationConfig once{1,0,0,false};auto classic=makeElimination(once,roster);e=*stepElimination(once,*classic,0,std::vector<int64_t>{3});
    CHECK(has(e,K::TeamEliminated,2)&&has(e,K::LastTeamStanding,1)&&classic->lastStanding==1);
    CHECK(stepElimination(once,*classic,1,std::vector<int64_t>{1})->size()==2);  // died + out; team 1 still has player 2
    EliminationConfig wipe{0,300,0,true};const std::array<std::pair<int64_t,int64_t>,3> r2{{{1,1},{2,2},{3,2}}};auto w=makeElimination(wipe,r2);
    e=*stepElimination(wipe,*w,0,std::vector<int64_t>{3,2,});CHECK(has(e,K::TeamEliminated,2)&&w->lastStanding==1&&w->players.at(2).out);
    CHECK(!stepElimination(wipe,*w,1,std::vector<int64_t>{1,1}));
    const std::array<std::pair<int64_t,int64_t>,2> oneTeam{{{1,1},{2,1}}};CHECK(!makeElimination(c,oneTeam));
}
void extraction(){
    ExtractionConfig c{3,1,2,true,true};ExtractionState s;
    auto step=[&](std::vector<Extractor> in,uint32_t t){return ok([&]{return stepExtraction(c,s,t,in);});};
    const Extractor a{10,1,1},b{11,1,0},enemy{20,2,0};
    step({a},0);step({a},1);CHECK(s.progress.empty());  // zone opens at tick 2
    step({a,b},2);CHECK(s.progress.size()==1&&s.progress.at(10)==1);
    step({a,enemy},3);CHECK(s.progress.at(10)==1);step({a},4);CHECK(s.progress.at(10)==2);step({},5);CHECK(s.progress.empty());
    step({a},6);auto mid=save(s);step({a},7);auto e=step({a},8);CHECK(e.size()==1&&e[0]==(ObjectiveEvent{K::Extracted,1,10,1})&&s.extracted==std::vector<int64_t>{10});
    CHECK(step({a},9).empty());roundtrip(s);
    ExtractionState r;CHECK(load(mid,r));c.resetOnLeave=false;CHECK(stepExtraction(c,r,7,std::vector<Extractor>{})&&r.progress.at(10)==1);
    CHECK(!stepExtraction(c,r,8,std::vector<Extractor>{a,a})&&!stepExtraction(c,r,8,std::vector<Extractor>{{0,1,1}})&&!stepExtraction(c,r,7,std::vector<Extractor>{}));
}
void bomb(){
    BombConfig c{3,4,2,10,true,true};auto s=makeBomb(1,2,5);CHECK(s&&s->phase==BombPhase::Carried);
    auto step=[&](BombState& st,uint32_t t,BombInput in){return ok([&]{return stepBomb(c,st,t,in);});};
    step(*s,0,{7,{},{},false});step(*s,1,{7,{},{},false});CHECK(s->plantProgress==2);
    auto e=step(*s,2,{});CHECK(has(e,K::BombPlantInterrupted,1,5)&&!s->plantProgress&&!s->site);
    e=step(*s,3,{0,{},{},true});CHECK(has(e,K::BombDropped,1,5)&&s->phase==BombPhase::Dropped&&!s->carrier);
    const std::array<BombActor,3> touching{{{9,2,false},{8,1,false},{6,1,false}}};
    e=step(*s,4,{0,{},touching,false});CHECK(has(e,K::BombPickedUp,1,6)&&s->carrier==6);
    for(uint32_t t=5;t<=7;++t)e=step(*s,t,{8,{},{},false});CHECK(e.size()==1&&e[0]==(ObjectiveEvent{K::BombPlanted,1,8,17})&&bombPending(*s));
    auto planted=*s;roundtrip(planted);
    const std::array<BombActor,2> both{{{30,2,false},{31,2,true}}};const std::array<BombActor,1> kit{{{31,2,true}}};
    step(*s,8,{0,both,{},false});CHECK(s->defuser==30&&s->defuseProgress==1);
    e=step(*s,9,{0,kit,{},false});CHECK(has(e,K::BombDefuseInterrupted,2,30)&&s->defuser==31&&s->defuseProgress==1);
    e=step(*s,10,{0,both,{},false});CHECK(has(e,K::BombDefused,2,31)&&s->phase==BombPhase::Defused);roundtrip(*s);
    // The fuse wins a tie with a finishing defuse.
    const std::array<BombActor,1> slow{{{30,2,false}}};for(uint32_t t=14;t<=16;++t)step(planted,t,{0,slow,{},false});CHECK(planted.defuseProgress==3);
    e=step(planted,17,{0,slow,{},false});CHECK(e.size()==1&&e[0].kind==K::BombExploded&&planted.phase==BombPhase::Exploded);
    auto again=makeBomb(1,2,5);const std::array<BombActor,2> dup{{{30,2,false},{30,2,false}}};
    CHECK(!stepBomb(c,*again,0,{0,dup,{},false})&&!stepBomb(c,*again,0,{-1,{},{},false})&&stepBomb(c,*again,0,{})&&!stepBomb(c,*again,0,{}));
    CHECK(!makeBomb(1,1,5)&&!valid(BombConfig{3,4,5,10,true,true}));
}
void route(){
    RouteGate g{1,{{},{},10_fx},{{},{},1_fx},2_fx};CHECK(crossesGate(g,{{},{},9_fx},{{},{},11_fx})&&!crossesGate(g,{{},{},11_fx},{{},{},9_fx})&&!crossesGate(g,{5_fx,{},9_fx},{5_fx,{},11_fx}));
    RouteConfig c{3,2};const std::array<std::pair<int64_t,int64_t>,2> racers{{{1,1},{2,2}}};auto s=makeRoute(racers);CHECK(s);
    auto step=[&](std::vector<GateCrossing> x,uint32_t t){return ok([&]{return stepRoute(c,*s,t,x);});};
    auto e=step({{1,0},{2,1}},1);CHECK(e.size()==1&&e[0]==(ObjectiveEvent{K::GatePassed,1,1,0}));
    e=step({{2,0},{1,1},{1,2}},2);CHECK(has(e,K::LapCompleted,1,1)&&s->racers.at(1).lap==1&&s->racers.at(2).nextGate==1);
    CHECK((routeStandings(*s)==std::vector<int64_t>{1,2}));auto mid=save(*s);
    e=step({{1,0},{1,1},{1,2}},3);CHECK(has(e,K::RouteFinished,1,1)&&s->racers.at(1).place==1&&s->finishers==1);
    CHECK(step({{1,0}},4).empty());roundtrip(*s);
    RouteState r;CHECK(load(mid,r)&&!stepRoute(c,r,3,std::vector<GateCrossing>{{3,0}})&&!stepRoute(c,r,3,std::vector<GateCrossing>{{1,3}})&&!stepRoute(c,r,2,std::vector<GateCrossing>{}));
}
void clock(){
    ObjectiveClockConfig c{10,2,5,100,true};auto s=startClock(c,0);CHECK(s&&s->deadline==10);
    auto step=[&](ObjectiveClockState& st,uint32_t t,bool contested){return ok([&]{return stepClock(c,st,t,contested);});};
    for(uint32_t t=0;t<10;++t)CHECK(step(*s,t,false).empty());CHECK(extendClock(c,*s,5)&&s->deadline==15&&clockRemaining(*s,10)==5);
    auto base=*s;auto e=step(*s,15,true);CHECK(e.size()==1&&e[0].kind==K::OvertimeStarted&&s->overtime);
    step(*s,16,false);CHECK(s->overtime);e=step(*s,17,false);CHECK(e.size()==2&&e[0]==(ObjectiveEvent{K::OvertimeEnded,0,0,2})&&e[1].kind==K::TimeExpired&&s->expired);
    CHECK(step(*s,18,true).empty()&&!extendClock(c,*s,5));roundtrip(*s);
    auto capped=base;step(capped,15,true);for(uint32_t t=16;t<20;++t)CHECK(step(capped,t,true).empty());CHECK(has(step(capped,20,true),K::TimeExpired)&&capped.expired);
    auto rescued=base;step(rescued,15,true);CHECK(extendClock(c,rescued,10)&&!rescued.overtime);CHECK(step(rescued,16,true).empty());
    auto quiet=base;CHECK(has(step(quiet,15,false),K::TimeExpired));
    CHECK(!extendClock(c,quiet,1)&&!stepClock(c,base,9,false)&&!startClock({0,2,5,100,true},0));
    auto limit=base;CHECK(!extendClock(c,limit,90)&&limit.deadline==15);
}
void spectator(){
    SpectatorConfig c{3,true,false};auto s=makeSpectator(1,1);CHECK(s);
    std::vector<SpectatorCandidate> people{{2,1,true},{3,1,true},{4,2,true},{5,2,true},{1,1,false}};
    auto step=[&](uint32_t t,int cycle=0){return ok([&]{return stepSpectator(c,*s,t,people,cycle);});};
    CHECK(step(9).empty());CHECK(spectatorDied(c,*s,4,10)&&s->mode==SpectatorMode::KillCam);
    auto e=step(10);CHECK(e.size()==1&&e[0]==(ObjectiveEvent{K::SpectatorChanged,1,1,4}));CHECK(step(11).empty());
    e=step(13);CHECK(s->mode==SpectatorMode::Follow&&s->target==2&&e.size()==1);
    people[0].alive=false;step(14);CHECK(s->target==3);CHECK(step(15,1).empty()&&s->target==3);
    people[0].alive=true;step(16,1);CHECK(s->target==2);step(17,-1);CHECK(s->target==3);roundtrip(*s);
    people[0].alive=people[1].alive=false;step(18);CHECK(s->mode==SpectatorMode::Free&&!s->target);
    c.allowEnemies=true;step(19);CHECK(s->mode==SpectatorMode::Follow&&s->target==4);
    CHECK(spectatorRespawned(*s)&&s->mode==SpectatorMode::Alive&&!s->target);
    people[2].alive=false;CHECK(spectatorDied(c,*s,4,20));step(20);CHECK(s->mode==SpectatorMode::Follow&&s->target==5);  // dead killer: no kill cam
    auto dup=people;dup.push_back({2,2,true});CHECK(!stepSpectator(c,*s,21,dup)&&!stepSpectator(c,*s,21,people,2)&&!stepSpectator(c,*s,20,people));
}
void scoring(){
    const std::array<int64_t,2> teams{1,2};auto m=makeMatch(teams);MatchConfig mc;mc.warmupTicks=0;mc.scoreToWin=5;CHECK(stepMatch(mc,*m,0));
    ObjectiveScoring points{{K::ZoneCaptured,2},{K::ZoneHoldTick,1}};uint64_t next=0;
    const ObjectiveEvents events{{K::ZoneContested,0,7,2},{K::ZoneCaptured,1,7,0},{K::ZoneHoldTick,1,7,3},{K::GatePassed,2,1,0}};
    CHECK(awardObjectiveEvents(*m,points,events,next)==2u&&next==2&&m->teams.at(1).score==3);
    const ObjectiveEvents bad{{K::ZoneHoldTick,1,7,3},{K::ZoneHoldTick,9,7,3}};CHECK(!awardObjectiveEvents(*m,points,bad,next)&&next==2&&m->teams.at(1).score==3);
    CHECK(awardObjectiveWin(mc,*m,2,next)&&next==3&&m->teams.at(2).score==5);CHECK(stepMatch(mc,*m,1)&&m->phase==MatchPhase::Finished&&m->winner==2);
}
// Several modes stepped together; a mid-match restore continues bit-identically.
void restore(){
    auto t=track();auto pc=payloadRules();ZoneConfig zc;zc.captureTicks=5;zc.holdScoreInterval=4;ObjectiveClockConfig cc{40,3,10,200,true};
    struct World{ZoneState zone;PayloadState cart;ObjectiveClockState clock;MatchState match;uint64_t next=0;};
    const std::array<int64_t,2> teams{1,2};MatchConfig mc;mc.warmupTicks=0;mc.roundTicks=1000;mc.scoreToWin=1000;
    World a{{},makePayload(1),*startClock(cc,0),*makeMatch(teams),0};CHECK(stepMatch(mc,a.match,0));World b=a;
    ObjectiveScoring points{{K::ZoneCaptured,5},{K::ZoneHoldTick,1},{K::PayloadCheckpoint,3},{K::PayloadDelivered,10}};
    auto tick=[&](World& w,uint32_t n,Hasher& h){
        Occupants o;if(n%7<4)o[1]=1+n%3;if(n%11<3)o[2]=1;
        auto z=stepZone(zc,w.zone,1,o,n);auto p=stepPayload(pc,t,w.cart,n%5<3?2:0,n%13==0?1:0,n);CHECK(z&&p);
        for(const auto& e:*p)if(e.kind==K::PayloadCheckpoint&&!w.clock.expired)CHECK(extendClock(cc,w.clock,uint32_t(e.value)%20));
        auto k=stepClock(cc,w.clock,n,w.zone.contested||w.cart.contested);CHECK(k);
        ObjectiveEvents all=*z;all.insert(all.end(),p->begin(),p->end());all.insert(all.end(),k->begin(),k->end());
        CHECK(awardObjectiveEvents(w.match,points,all,w.next));CHECK(stepMatch(mc,w.match,n+1));
        hashObjective(h,all);hashObjective(h,w.zone);hashObjective(h,w.cart);hashObjective(h,w.clock);
    };
    Hasher ha,hb;std::string zone,cart,clockBytes,match;
    for(uint32_t n=1;n<=80;++n){
        tick(a,n,ha);
        if(n==40){zone=save(a.zone);cart=save(a.cart);clockBytes=save(a.clock);match=save(a.match);CHECK(load(zone,b.zone)&&load(cart,b.cart)&&load(clockBytes,b.clock)&&load(match,b.match));b.next=a.next;hb=ha;}
        if(n>40){tick(b,n,hb);CHECK(save(a.zone)==save(b.zone)&&save(a.cart)==save(b.cart)&&save(a.clock)==save(b.clock)&&save(a.match)==save(b.match));}
    }
    CHECK(ha.value()==hb.value()&&objectiveHash(a.zone)==objectiveHash(b.zone)&&a.cart.delivered&&a.match.teams.at(1).score>0);
    // Records are typed, canonical and validated.
    PayloadState wrong;CHECK(!load(zone,wrong));ZoneState z;auto bytes=zone;bytes.back()=char(2);CHECK(!load(bytes,z));
    ZoneState broken;broken.capturer=1;CHECK(save(broken).empty());BombState bomb=*makeBomb(1,2,0);bomb.carrier=3;CHECK(save(bomb).empty());
    SpectatorState spec=*makeSpectator(1,1);spec.target=1;CHECK(save(spec).empty());
}
void data(const std::string& root){
    auto layout=readObjectiveLayout(Json::parseFile(root+"/examples/libraries/genre/objectives.json"));
    CHECK(layout.zones.size()==4&&layout.zones[2].kind==ObjectiveZoneKind::Extraction&&layout.payload&&layout.payload->checkpoints.size()==2&&layout.gates.size()==3);
    CHECK(insideZone(layout.zones[0],layout.zones[0].center)&&!insideZone(layout.zones[0],layout.zones[0].center+Vec3{100_fx,{},{}}));
    auto expectError=[](const char* text,const std::string& message){
        try{readObjectiveLayout(Json::parse(text));}catch(const JsonError& e){if(std::string(e.what()).find(message)!=std::string::npos)return;throw std::runtime_error(std::string("unexpected error: ")+e.what());}
        throw std::runtime_error("expected error: "+message);
    };
    expectError(R"({"arena":1})","objectives: unknown key 'arena'");
    expectError(R"({"zones":[{"id":1,"kind":"capture","center":[0,0,0],"radiusMilli":5},{"id":1,"kind":"hill","center":[0,0,0],"radiusMilli":5}]})","objectives.zones[1].id: duplicate zone id 1");
    expectError(R"({"zones":[{"id":1,"kind":"flag","center":[0,0,0],"radiusMilli":5}]})","objectives.zones[0].kind: expected");
    expectError(R"({"zones":[{"id":1,"kind":"hill","center":[0,0],"radiusMilli":5}]})","objectives.zones[0].center: expected [x,y,z]");
    expectError(R"({"zones":[{"id":1,"kind":"hill","center":[0,0,0],"radiusMilli":0}]})","objectives.zones[0].radiusMilli: must be in 1..");
    expectError(R"({"payload":{"path":[[0,0,0]]}})","objectives.payload.path: needs at least 2 points");
    expectError(R"({"payload":{"path":[[0,0,0],[0,0,0]]}})","objectives.payload.path[1]: repeats the previous point");
    expectError(R"({"payload":{"path":[[0,0,0],[5000,0,0]],"checkpointsMilli":[6000]}})","checkpoints must ascend strictly inside the path length");
    expectError(R"({"route":{"gates":[{"id":2,"center":[0,0,0],"normal":[0,0,1000],"radiusMilli":5},{"id":1,"center":[0,0,0],"normal":[0,0,1000],"radiusMilli":5}]}})","objectives.route.gates[1].id: gate ids must ascend");
    expectError(R"({"route":{"gates":[{"id":1,"center":[0,0,0],"normal":[0,0,0],"radiusMilli":5}]}})","objectives.route.gates[0].normal: needs a length of at least 100 milli-units");
    auto parsed=aiverse::lib::gameplay::readGameplaySettings(Json::parseFile(root+"/examples/libraries/genre/objective-settings.json"));CHECK(parsed.value&&objectiveSettingsErrors(*parsed.value).empty());
    const auto& v=*parsed.value;auto zc=zoneConfig(v);auto pc=payloadConfig(v);auto bc=bombConfig(v);auto cc=objectiveClockConfig(v);
    CHECK(valid(zc)&&zc.contest==ContestRule::Majority&&zc.holdScoreInterval==60&&valid(pc)&&pc.pushSpeed==Fixed::fromRatio(80,1000)&&valid(bc)&&bc.kitDefuseTicks==150);
    CHECK(valid(cc)&&cc.limitTicks==v.matchRules.roundTicks&&valid(eliminationConfig(v))&&eliminationConfig(v).respawnWaveTicks==600&&valid(extractionConfig(v))&&valid(spectatorConfig(v))&&valid(routeConfig(v,3))&&hillRotation(v).count==3);
    auto settings=[](const char* text){return aiverse::lib::gameplay::readGameplaySettings(Json::parse(text));};
    auto r=settings(R"({"schemaVersion":1,"objectives":{"bombKitDefuseTicks":700}})");CHECK(!r.value&&r.errors.at(0)=="objectives.bombKitDefuseTicks: must not exceed bombDefuseTicks");
    r=settings(R"({"schemaVersion":1,"objectives":{"flagTicks":1}})");CHECK(!r.value&&r.errors.at(0)=="objectives.flagTicks: unknown field");
    r=settings(R"({"schemaVersion":1,"objectives":{"zoneContestRule":2}})");CHECK(!r.value&&r.errors.at(0)=="objectives.zoneContestRule: integer out of range");
    r=settings(R"({"schemaVersion":1,"objectives":{"hillCount":3,"eliminationLives":1,"respawnWaveTicks":10}})");CHECK(r.value);
    auto errors=objectiveSettingsErrors(*r.value);CHECK(errors.size()==2&&errors[0]=="objectives.hillRotateTicks: rotating between hillCount > 1 hills needs at least 1 tick"&&errors[1]=="objectives.respawnWaveTicks: eliminationLives 1 never respawns; set 0");
}
}
void matchObjectives(const std::string& root){objectives::zones();objectives::payload();objectives::elimination();objectives::extraction();objectives::bomb();objectives::route();objectives::clock();objectives::spectator();objectives::scoring();objectives::restore();objectives::data(root);}
// ---- Stealth and senses (stealth-senses package).
#include <aiverselib/genre/SensesConfigured.h>
#include <aiverselib/genre/SensesData.h>
#include <aiverselib/genre/SensesPersistence.h>
// Brace initializers contain commas, so the checks forward variadic arguments.
#define SCHECK(...) CHECK((__VA_ARGS__))
namespace senses {
SenseAgent agent(int64_t id,int64_t team,Vec3 p,Fixed yaw={}){SenseAgent a;a.id=id;a.team=team;a.position=p;a.yaw=yaw;a.eyeHeight={};return a;}
SenseTarget target(int64_t id,int64_t team,Vec3 p){SenseTarget t;t.id=id;t.team=team;t.position=p;return t;}
NoiseEvent noise(int64_t id,int64_t source,Vec3 p,Fixed radius,int64_t loud){NoiseEvent n;n.id=id;n.source=source;n.position=p;n.radius=radius;n.loudnessMilli=loud;return n;}
SensesReport run(const SensesConfig& c,const SenseDefinitions& d,SensesState& s,uint32_t tick,const SensesInput& in){auto r=stepSensesOpen(c,d,s,tick,in);SCHECK(r);return *r;}
const PerceivedTarget* seen(const SensesReport& r,int64_t o,int64_t t){for(const auto& p:r.perceived)if(p.observer==o&&p.target==t)return &p;return nullptr;}
std::size_t count(const SensesReport& r,SenseEventKind k){std::size_t n=0;for(const auto& e:r.events)n+=e.kind==k;return n;}
const SenseEvent* first(const SensesReport& r,SenseEventKind k){for(const auto& e:r.events)if(e.kind==k)return &e;return nullptr;}
void coneAndRange(){
    SightConfig c;auto o=agent(1,1,{});
    auto focus=sampleSight(c,o,target(2,2,{{},{},10_fx}));SCHECK(focus.zone==SightZone::Focus&&focus.strength>=665&&focus.strength<=668);
    SCHECK(sampleSight(c,o,target(2,2,{{},{},25_fx})).strength==0);
    auto side=sampleSight(c,o,target(2,2,{5_fx,{},1_fx}));SCHECK(side.zone==SightZone::Peripheral&&side.strength==400);
    auto behind=sampleSight(c,o,target(2,2,{{},{},Fixed::fromRatio(-3,2)}));SCHECK(behind.zone==SightZone::Near&&behind.strength==1000);
    SCHECK(sampleSight(c,o,target(2,2,{{},{},-3_fx})).zone==SightZone::None);
    SCHECK(sampleSight(c,o,target(2,2,{6_fx,{},-1_fx})).strength==0); // beyond the peripheral cone
    auto dim=target(2,2,{{},{},10_fx});dim.lightMilli=500;dim.coverMilli=500;auto s=sampleSight(c,o,dim);SCHECK(s.strength==focus.strength/2/2||s.strength==focus.strength/4+1||s.strength==focus.strength/4);
    auto bright=target(2,2,{{},{},10_fx});bright.lightMilli=4000;SCHECK(sampleSight(c,o,bright).strength==1000);
    auto blind=o;blind.sightMilli=0;SCHECK(sampleSight(c,blind,target(2,2,{})).strength==0);
    auto turned=agent(1,1,{},Fixed::fromRatio(1,2));SCHECK(sampleSight(c,turned,target(2,2,{{},{},-10_fx})).zone==SightZone::Focus); // yaw 0.5 faces -Z
    auto eye=agent(1,1,{});eye.eyeHeight=2_fx;SCHECK(sampleSight(c,eye,target(2,2,{{},2_fx,10_fx})).strength==focus.strength);
}
void losOrder(){
    SensesConfig c;SenseDefinitions d;SensesState s;SensesInput in;
    in.observers={agent(3,1,{}),agent(1,1,{})};in.targets={target(20,2,{{},{},1_fx}),target(10,2,{{},{},1_fx}),target(30,1,{{},{},1_fx}),target(40,2,{{},{},50_fx})};
    std::vector<std::pair<int64_t,int64_t>> calls;
    auto r=stepSenses(c,d,s,0,in,[&](const SenseAgent& o,const SenseTarget& t){calls.emplace_back(o.id,t.id);return t.id!=20;},[](const SenseAgent&,const NoiseEvent&){return 1000;});
    SCHECK(r&&calls==(std::vector<std::pair<int64_t,int64_t>>{{1,10},{1,20},{3,10},{3,20}})&&r->losQueries==4&&r->losSkipped==0);
    SCHECK(seen(*r,1,10)&&seen(*r,1,10)->visibleNow&&!seen(*r,1,20)&&!seen(*r,1,30)&&!seen(*r,1,40)); // LOS blocked, same team, out of range
    SCHECK(r->perceived.size()==2&&r->perceived[0].observer==1&&r->perceived[1].observer==3);
    c.sight.maxQueries=3;calls.clear();SensesState b;
    auto limited=stepSenses(c,d,b,0,in,[&](const SenseAgent& o,const SenseTarget& t){calls.emplace_back(o.id,t.id);return true;},[](const SenseAgent&,const NoiseEvent&){return 1000;});
    SCHECK(limited&&calls.size()==3&&calls.back()==std::make_pair<int64_t,int64_t>(3,10)&&limited->losSkipped==1&&!seen(*limited,3,20));
}
void hearing(){
    SensesConfig c;SenseDefinitions d;SensesInput in;in.observers={agent(1,1,{}),agent(2,1,{{},{},-1_fx})};
    in.observers[0].sightMilli=in.observers[1].sightMilli=0;
    in.noises={noise(7,10,{5_fx,{},{}},10_fx,500),noise(8,11,{5_fx,{},{}},10_fx,900)};
    std::vector<std::pair<int64_t,int64_t>> calls;SensesState s;
    auto occl=[&](const SenseAgent& o,const NoiseEvent& n){calls.emplace_back(o.id,n.id);return o.id==1?500:0;};
    c.hearing.maxNoises=1;auto r=stepSenses(c,d,s,0,in,[](const SenseAgent&,const SenseTarget&){return true;},occl);
    SCHECK(r&&r->noisesDropped==1&&first(*r,SenseEventKind::NoiseDropped)->value==7&&calls==(std::vector<std::pair<int64_t,int64_t>>{{1,8},{2,8}}));
    auto p=seen(*r,1,11);SCHECK(p&&p->strengthNow==225&&p->lastSense==SenseKind::Hearing&&p->lastKnown==Vec3{5_fx,{},{}}&&p->suspicion==67);
    SCHECK(!seen(*r,2,11)&&!seen(*r,1,10)); // fully occluded; dropped noise
    c.hearing.maxQueries=1;calls.clear();SensesState b;auto budget=stepSenses(c,d,b,0,in,[](const SenseAgent&,const SenseTarget&){return true;},occl);
    SCHECK(budget&&budget->occlusionQueries==1&&budget->occlusionSkipped==1&&calls.size()==1);
    // Attenuation, sensitivity and the threshold: 1000 * (10-5)/10 = 500 before occlusion.
    SCHECK(noiseStrength(noise(1,0,{},10_fx,1000),{5_fx,{},{}})==500&&noiseStrength(noise(1,0,{},10_fx,1000),{10_fx,{},{}})==0);
    SensesConfig open;SensesState q;SensesInput quiet;quiet.observers={agent(1,1,{})};quiet.noises={noise(1,9,{9_fx,{},{}},10_fx,100)};
    SCHECK(!seen(run(open,d,q,0,quiet),1,9)); // 10 milli < threshold 20
    quiet.noises={noise(1,9,{1_fx,{},{}},10_fx,1000)};quiet.observers[0].team=0;auto ally=quiet;ally.noises[0].team=1;ally.observers[0].team=1;
    SCHECK(seen(run(open,d,q,1,quiet),1,9));SensesState q2;SCHECK(!seen(run(open,d,q2,0,ally),1,9)); // own team's noise ignored
}
void suspicionThresholds(){
    SensesConfig c;SenseDefinitions d;SensesState s;SensesInput in;in.observers={agent(1,1,{})};in.targets={target(10,2,{{},{},1_fx})};
    std::vector<std::pair<uint32_t,Awareness>> changes;
    for(uint32_t t=0;t<=16;++t){
        auto r=run(c,d,s,t,in);auto p=seen(r,1,10);SCHECK(p&&p->visibleNow&&p->suspicion==std::min<int64_t>(60*(t+1),1000));
        for(const auto& e:r.events)if(e.kind==SenseEventKind::AwarenessChanged)changes.emplace_back(t,Awareness(e.value));
        SCHECK(count(r,SenseEventKind::AlertSent)==0);
    }
    SCHECK(changes==(std::vector<std::pair<uint32_t,Awareness>>{{3,Awareness::Suspicious},{8,Awareness::Searching},{16,Awareness::Alert}}));
    in.targets.clear();auto lost=run(c,d,s,17,in);SCHECK(count(lost,SenseEventKind::SearchStarted)==1&&seen(lost,1,10)->searching&&seen(lost,1,10)->level==Awareness::Alert);
    // Decay starts decayDelay ticks after the last stimulus, exactly even across skipped ticks.
    c.suspicion.decayDelay=5;c.suspicion.decayPerTick=10;SensesState q;SensesInput see;see.observers={agent(1,1,{})};see.targets={target(10,2,{{},{},1_fx})};
    for(uint32_t t=0;t<4;++t)run(c,d,q,t,see);auto alone=see;alone.targets.clear();
    SCHECK(seen(run(c,d,q,8,alone),1,10)->suspicion==240);SCHECK(seen(run(c,d,q,9,alone),1,10)->suspicion==230);
    auto r=run(c,d,q,20,alone);SCHECK(seen(r,1,10)->suspicion==120&&seen(r,1,10)->level==Awareness::Unaware&&count(r,SenseEventKind::AwarenessChanged)==1);
    SCHECK(run(c,d,q,40,alone).perceived.empty()&&q.observers.empty());
    // Anonymous disturbances stop at Searching.
    SensesState anon;SensesInput loud;loud.observers={agent(1,1,{})};loud.noises={noise(1,0,{},10_fx,1000)};
    for(uint32_t t=0;t<5;++t)loud.noises[0].id=t+1,run(c,d,anon,t,loud);
    SCHECK(awarenessRecord(anon,1,0)->meter==c.suspicion.searchingAt&&awarenessRecord(anon,1,0)->level==Awareness::Searching);
}
void propagation(){
    SensesConfig c;c.suspicion.sightGain=1000;SenseDefinitions d;SensesState s;SensesInput in;
    auto b=agent(2,1,{5_fx,{},{}});b.sightMilli=0;b.hearingMilli=0;
    in.observers={agent(1,1,{}),b,agent(3,1,{100_fx,{},{}},Fixed::fromRatio(1,2)),agent(4,2,{3_fx,{},{}})};in.targets={target(10,2,{{},{},1_fx})};
    auto r=run(c,d,s,0,in);SCHECK(seen(r,1,10)->level==Awareness::Alert&&count(r,SenseEventKind::AlertSent)==1&&first(r,SenseEventKind::AlertSent)->value==2);
    SCHECK(s.alerts.size()==1&&s.alerts[0].deliverAt==30);
    for(uint32_t t=1;t<30;++t){auto x=run(c,d,s,t,in);SCHECK(!seen(x,2,10)&&count(x,SenseEventKind::AlertSent)==0);}
    auto got=run(c,d,s,30,in);auto p=seen(got,2,10);
    SCHECK(count(got,SenseEventKind::AlertReceived)==1&&p&&p->level==Awareness::Searching&&p->suspicion==500&&p->lastSense==SenseKind::Alert&&p->lastKnown==Vec3{{},{},1_fx}&&p->lastKnownTick==0);
    SCHECK(!p->sensedNow&&p->searching&&first(got,SenseEventKind::SearchStarted)->observer==2&&!seen(got,3,10)&&!seen(got,4,10));
    // Receivers that are absent on the delivery tick lose the alert; relay=false receivers never re-broadcast.
    SensesState z;auto gone=in;run(c,d,z,0,in);gone.observers.erase(gone.observers.begin()+1);auto miss=run(c,d,z,30,gone);
    SCHECK(count(miss,SenseEventKind::AlertDropped)==1&&!awarenessRecord(z,2,10));
    c.alert.receivedMilli=1000;c.alert.relay=true;SensesState chain;auto far=in;far.observers[2]=agent(3,1,{18_fx,{},{}});far.observers[2].sightMilli=0;
    run(c,d,chain,0,far);auto hop=run(c,d,chain,30,far);SCHECK(seen(hop,2,10)->level==Awareness::Alert&&count(hop,SenseEventKind::AlertSent)==1&&first(hop,SenseEventKind::AlertSent)->value==3);
    SCHECK(seen(run(c,d,chain,60,far),3,10)->lastKnown==Vec3{{},{},1_fx});
}
void searchGiveUp(){
    SensesConfig c;c.search.maxPoints=3;c.search.giveUpTicks=20;SenseDefinitions d;SensesInput in;in.observers={agent(1,1,{})};
    in.candidates={{5,{3_fx,{},{}},0},{4,{-3_fx,{},{}},0},{2,{1_fx,{},{}},0},{9,{50_fx,{},{}},0}};
    auto plan=planSearch(c.search,{},std::vector<SearchCandidate>{{2,{1_fx,{},{}},0},{4,{-3_fx,{},{}},0},{5,{3_fx,{},{}},0}});
    SCHECK(plan.size()==3&&plan[0].candidate==0&&plan[1].candidate==2&&plan[2].candidate==4);
    auto start=[&](SensesState& s){
        for(uint32_t t=0;t<2;++t){auto x=in;x.noises={noise(t+1,0,{},10_fx,1000)};run(c,d,s,t,x);}
        auto r=run(c,d,s,2,in);SCHECK(first(r,SenseEventKind::SearchStarted)&&first(r,SenseEventKind::SearchStarted)->value==3);
        SCHECK(searchGoal(s,1)->candidate==0);
    };
    SensesState s;start(s);
    auto r=run(c,d,s,3,in);SCHECK(first(r,SenseEventKind::SearchPointReached)->value==0&&searchGoal(s,1)->candidate==2);
    auto step=in;step.observers[0].position={1_fx,{},{}};r=run(c,d,s,4,step);SCHECK(first(r,SenseEventKind::SearchPointReached)->value==2&&searchGoal(s,1)->candidate==4);
    for(uint32_t t=5;t<22;++t)SCHECK(count(run(c,d,s,t,step),SenseEventKind::SearchGaveUp)==0);
    r=run(c,d,s,22,step);auto e=first(r,SenseEventKind::SearchGaveUp);SCHECK(e&&e->extra==2&&!s.observers.at(1).search.active&&!searchGoal(s,1));
    SCHECK(seen(r,1,0)->level==Awareness::Suspicious&&seen(r,1,0)->suspicion==c.suspicion.suspiciousAt&&count(r,SenseEventKind::AwarenessChanged)==1);
    SCHECK(count(run(c,d,s,23,step),SenseEventKind::SearchStarted)==0);
    // Visiting every point ends the search early.
    SensesState all;start(all);run(c,d,all,3,in);run(c,d,all,4,step);auto last=in;last.observers[0].position={-3_fx,{},{}};
    r=run(c,d,all,5,last);SCHECK(first(r,SenseEventKind::SearchGaveUp)&&first(r,SenseEventKind::SearchGaveUp)->extra==3);
    // A stimulus during the search re-plans around the new last known position and extends the deadline.
    SensesState again;start(again);auto heard=in;heard.noises={noise(9,0,{2_fx,{},{}},10_fx,500)};r=run(c,d,again,10,heard);
    SCHECK(count(r,SenseEventKind::SearchUpdated)==1&&again.observers.at(1).search.lastKnown==Vec3{2_fx,{},{}}&&again.observers.at(1).search.deadline==30);
}
void disguises(const std::string& root){
    SensesConfig c;auto d=readSenseDefinitions(Json::parseFile(root+"/examples/libraries/genre/senses.json"));SCHECK(d.disguises.size()==1&&d.spots.size()==2);
    SensesState s;SensesInput in;in.observers={agent(1,1,{}),agent(2,3,{})};auto t=target(10,2,{{},{},1_fx});t.zone=10;t.equipDisguise=1;in.targets={t};
    auto meter=[&](uint32_t tick,int64_t zone,std::vector<int64_t> actions,int64_t equip=-1){auto x=in;x.targets[0].zone=zone;x.targets[0].actions=std::move(actions);x.targets[0].equipDisguise=equip;auto r=run(c,d,s,tick,x);return std::make_pair(seen(r,1,10)->suspicion,r);};
    auto [m0,r0]=meter(0,10,{},1);SCHECK(m0==12&&seen(r0,2,10)->suspicion==60&&count(r0,SenseEventKind::DisguiseEquipped)==1);
    SCHECK(meter(1,12,{}).first==72);            // outside the allowed zones: trespass scale
    SCHECK(meter(2,11,{3}).first==84);           // harmless action
    auto [m3,r3]=meter(3,10,{7});SCHECK(m3==144&&first(r3,SenseEventKind::DisguiseBlown)->value==7&&s.disguises.at(10).blown);
    SCHECK(meter(4,10,{8}).first==204&&count(meter(5,10,{},1).second,SenseEventKind::DisguiseEquipped)==1&&awarenessRecord(s,1,10)->meter==216);
    auto [m6,r6]=meter(6,10,{},0);SCHECK(m6==276&&first(r6,SenseEventKind::DisguiseEquipped)->value==0&&!s.disguises.contains(10));
}
void hiding(const std::string& root){
    SensesConfig c;auto d=readSenseDefinitions(Json::parseFile(root+"/examples/libraries/genre/senses.json"));SensesState s;
    std::vector<int64_t> checked;auto los=[&](const SenseAgent&,const SenseTarget& t){checked.push_back(t.id);return true;};auto open=[](const SenseAgent&,const NoiseEvent&){return 1000;};
    SensesInput in;in.observers={agent(1,1,{})};auto a=target(10,2,{{},{},4_fx});auto b=target(11,2,{10_fx,{},10_fx});a.enterHiding=5;b.enterHiding=6;b.coverMilli=0;in.targets={a,b};
    auto r=stepSenses(c,d,s,0,in,los,open);SCHECK(r&&count(*r,SenseEventKind::HidingEntered)==2&&s.hiding.at(10).revealedTo==std::vector<int64_t>{1}&&s.hiding.at(11).revealedTo.empty());
    SCHECK(checked==std::vector<int64_t>{10}&&hidden(s,11));
    auto stay=in;stay.targets[0].enterHiding=0;stay.targets[1].enterHiding=0;stay.targets[1].coverMilli=1000;auto c3=target(12,2,{{},{},4_fx});c3.enterHiding=5;stay.targets.push_back(c3);
    checked.clear();r=stepSenses(c,d,s,1,stay,los,open);
    SCHECK(r&&seen(*r,1,10)->visibleNow&&!seen(*r,1,11)&&checked==(std::vector<int64_t>{10,12})&&first(*r,SenseEventKind::HidingRefused)->target==12); // capacity 1
    auto farAway=stay;farAway.targets[2]=target(12,2,{30_fx,{},30_fx});farAway.targets[2].enterHiding=6;r=stepSenses(c,d,s,2,farAway,los,open);
    SCHECK(r&&first(*r,SenseEventKind::HidingRefused)->value==6&&!hidden(s,12));
    auto leave=stay;leave.targets.pop_back();leave.targets[0].exitHiding=true;leave.targets[1].position={20_fx,{},20_fx};r=stepSenses(c,d,s,3,leave,los,open);
    SCHECK(r&&count(*r,SenseEventKind::HidingExited)==2&&r->events[0].extra==0&&r->events[1].extra==1&&s.hiding.empty());
    // Searching a hiding spot reveals an occupant that entered unseen.
    SensesState q;SensesConfig sc=c;sc.search.maxPoints=2;auto g=agent(2,1,{10_fx,{},{}},Fixed::fromRatio(1,2));SensesInput base;base.observers={g};
    auto hider=target(11,2,{10_fx,{},10_fx});hider.enterHiding=6;base.targets={hider};base.candidates={{30,{10_fx,{},10_fx},6}};
    run(sc,d,q,0,base);base.targets[0].enterHiding=0;SCHECK(hidden(q,11)&&q.hiding.at(11).revealedTo.empty());
    for(uint32_t t=1;t<4;++t){auto x=base;x.noises={noise(t,0,{10_fx,{},8_fx},20_fx,1000)};run(sc,d,q,t,x);}
    SCHECK(first(run(sc,d,q,4,base),SenseEventKind::SearchStarted));
    auto walk=base;walk.observers[0].position={10_fx,{},8_fx};walk.observers[0].yaw={};r=run(sc,d,q,5,walk);
    SCHECK(first(*r,SenseEventKind::SearchPointReached)&&!seen(*r,2,11)); // next to the spot but concealed
    walk.observers[0].position={10_fx,{},10_fx};r=run(sc,d,q,6,walk);auto found=first(*r,SenseEventKind::HidingRevealed);
    SCHECK(found&&found->observer==2&&found->target==11&&found->value==6&&q.hiding.at(11).revealedTo==std::vector<int64_t>{2});
    walk.observers[0].position={10_fx,{},9_fx};SCHECK(seen(run(sc,d,q,7,walk),2,11)->visibleNow);
}
void trails(){
    SensesConfig c;SenseDefinitions d;SensesState s;SensesInput in;auto o=agent(1,1,{Fixed::fromRatio(3,2),{},{}});o.sightMilli=0;in.observers={o};
    auto t=target(10,2,{});t.leaveTrail=true;in.targets={t};auto r=run(c,d,s,0,in);SCHECK(s.markers.size()==1&&!seen(r,1,10));
    SCHECK(trailStrength(c.trail,s.markers[0],300)==500&&trailStrength(c.trail,s.markers[0],600)==0);
    in.targets.clear();r=run(c,d,s,300,in);auto p=seen(r,1,10);SCHECK(p&&p->lastSense==SenseKind::Trail&&p->strengthNow==250&&p->lastKnown==Vec3{}&&!p->visibleNow);
    run(c,d,s,600,in);SCHECK(s.markers.empty());
    SensesState cap;c.trail.maxMarkers=2;in.targets={t};for(uint32_t k=0;k<3;++k)run(c,d,cap,k,in);SCHECK(cap.markers.size()==2&&cap.markers[0].id==2&&cap.nextMarker==4);
    removeSenseAgent(cap,10);SCHECK(cap.markers.empty()&&!awarenessRecord(cap,1,10));
}
void restore(){
    SensesConfig c;c.alert.delay=5;SenseDefinitions d;d.spots[5]={5,{{},{},4_fx},1};
    auto input=[&](uint32_t t){
        SensesInput in;in.observers={agent(1,1,{}),agent(2,1,{4_fx,{},{}},Fixed::fromRatio(1,2)),agent(3,2,{-6_fx,{},2_fx})};
        auto x=target(10,2,{{},{},Fixed::fromInt(int64_t(t%7))});x.leaveTrail=t%3==0;x.enterHiding=t==12?5:0;x.position.x=t==12?Fixed{}:x.position.x;
        auto y=target(11,1,{-4_fx,{},Fixed::fromInt(int64_t(t%5))});y.zone=t%2;in.targets={x,y};
        if(t%4==0)in.noises={noise(t+1,t%8?10:0,{1_fx,{},Fixed::fromInt(int64_t(t%9))},12_fx,800)};
        in.candidates={{7,{2_fx,{},2_fx},0},{8,{{},{},4_fx},5}};return in;
    };
    SensesState a;std::string mid;
    for(uint32_t t=0;t<40;++t){run(c,d,a,t,input(t));if(t==19)mid=save(a);}
    SensesState b;SCHECK(!mid.empty()&&load(mid,b));
    SensesState again;for(uint32_t t=0;t<20;++t)run(c,d,again,t,input(t));
    for(uint32_t t=20;t<40;++t){auto x=run(c,d,again,t,input(t)),y=run(c,d,b,t,input(t));SCHECK(sensesHash(x)==sensesHash(y));}
    SCHECK(save(a)==save(b)&&sensesHash(a)==sensesHash(b)&&save(again)==save(a));
    roundtrip(a);for(const auto& [id,o]:a.observers){(void)id;roundtrip(o);if(o.search.active)roundtrip(o.search);}
    roundtrip(DisguiseState{1,3,true});roundtrip(HidingState{5,2,{1,4}});
    ObserverState wrong;SCHECK(!load(mid,wrong));
    auto broken=a;broken.nextMarker=0;SCHECK(save(broken).empty());
    broken=a;broken.alerts={{9,1,2,1,10,{}},{8,1,2,1,10,{}}};SCHECK(save(broken).empty());
    HidingState unsorted{5,0,{3,1}};SCHECK(save(unsorted).empty());
    SCHECK(sensesConfigHash(c)!=sensesConfigHash(SensesConfig{}));
}
void rejection(const std::string& root){
    SensesConfig c;SenseDefinitions d;SensesState s;SensesInput in;in.observers={agent(1,1,{})};in.targets={target(10,2,{{},{},1_fx})};
    run(c,d,s,5,in);auto before=save(s);
    SCHECK(!stepSensesOpen(c,d,s,5,in)&&!stepSensesOpen(c,d,s,4,in)&&save(s)==before);
    auto dup=in;dup.observers.push_back(agent(1,1,{}));SCHECK(!stepSensesOpen(c,d,s,6,dup));
    auto bad=in;bad.targets[0].equipDisguise=3;SCHECK(!stepSensesOpen(c,d,s,6,bad));
    bad=in;bad.targets[0].enterHiding=9;SCHECK(!stepSensesOpen(c,d,s,6,bad));
    bad=in;bad.targets[0].lightMilli=5000;SCHECK(!stepSensesOpen(c,d,s,6,bad));
    bad=in;bad.noises={noise(1,0,{},{},500)};SCHECK(!stepSensesOpen(c,d,s,6,bad));
    bad=in;bad.observers[0].id=0;SCHECK(!stepSensesOpen(c,d,s,6,bad));SCHECK(save(s)==before);
    auto wrong=c;wrong.suspicion.searchingAt=wrong.suspicion.suspiciousAt;SCHECK(sensesConfigError(wrong).find("suspiciousAt")!=std::string::npos&&!stepSensesOpen(wrong,d,s,6,in));
    wrong=c;wrong.sight.halfAngle=Fixed::fromRatio(1,2);SCHECK(sensesConfigError(wrong).find("halfAngle")!=std::string::npos);
    wrong=c;wrong.alert.delay=0;SCHECK(sensesConfigError(wrong).find("alert.delay")!=std::string::npos);SCHECK(save(s)==before);
    auto parse=[](const char* text){return aiverse::lib::gameplay::readGameplaySettings(Json::parse(text));};
    auto defaults=parse("{\"schemaVersion\":1}");SCHECK(defaults.value&&sensesSettingsErrors(*defaults.value).empty()&&sensesConfigHash(sensesConfig(*defaults.value))==sensesConfigHash(c));
    auto relation=parse("{\"schemaVersion\":1,\"senses\":{\"sightNearMilli\":30000}}");SCHECK(!relation.value&&relation.errors.at(0)=="senses.sightNearMilli: must not exceed sightRangeMilli");
    auto unknown=parse("{\"schemaVersion\":1,\"senses\":{\"sightRange\":3}}");SCHECK(!unknown.value&&unknown.errors.at(0)=="senses.sightRange: unknown field");
    auto range=parse("{\"schemaVersion\":1,\"senses\":{\"searchMaxPoints\":65}}");SCHECK(!range.value&&range.errors.at(0)=="senses.searchMaxPoints: integer out of range");
    auto type=parse("{\"schemaVersion\":1,\"senses\":{\"alertRelay\":1}}");SCHECK(!type.value&&type.errors.at(0)=="senses.alertRelay: expected boolean");
    auto strict=parse("{\"schemaVersion\":1,\"senses\":{\"suspiciousAtMilli\":500}}");SCHECK(strict.value&&sensesSettingsErrors(*strict.value).at(0)=="senses.suspiciousAtMilli: must be below searchingAtMilli");
    auto example=aiverse::lib::gameplay::readGameplaySettings(Json::parseFile(root+"/examples/libraries/genre/senses-settings.json"));SCHECK(example.value&&sensesSettingsErrors(*example.value).empty());
    auto configured=sensesConfig(*example.value);SCHECK(valid(configured)&&configured.sight.range==25_fx&&configured.sight.halfAngle==Fixed::fromRatio(1,10)&&configured.alert.relay&&configured.alert.delay==20&&configured.trail.lifetime==900);
    auto rejects=[](const std::string& text,const std::string& expected){try{readSenseDefinitions(Json::parse(text));}catch(const JsonError& e){return std::string(e.what()).find(expected)!=std::string::npos;}return false;};
    SCHECK(rejects(R"({"disguise":[]})","senses: unknown key 'disguise'"));
    SCHECK(rejects(R"({"disguises":[{"id":1,"factions":[1]},{"id":1,"factions":[2]}]})","senses.disguise 1: duplicate id"));
    SCHECK(rejects(R"({"disguises":[{"id":2,"factions":[]}]})","senses.disguise 2.factions: list at least one fooled team"));
    SCHECK(rejects(R"({"disguises":[{"id":2,"factions":[1,1]}]})","senses.disguise 2.factions: duplicate id"));
    SCHECK(rejects(R"({"disguises":[{"id":2,"factions":[1],"scaleMilli":5000}]})","senses.disguise 2.scaleMilli: must be in 0..4000"));
    SCHECK(rejects(R"({"hidingSpots":[{"id":3,"position":[0,0]}]})","senses.hidingSpot 3.position: expected [x,y,z]"));
    SCHECK(rejects(R"({"hidingSpots":[{"id":3,"position":[0,0,0],"capacity":0}]})","senses.hidingSpot 3.capacity: must be in 1..64"));
    SCHECK(rejects(R"({"hidingSpots":[{"id":3,"position":[0,0,0],"size":1}]})","unknown key 'size'"));
}
}
#undef SCHECK
void stealthSenses(const std::string& root){senses::coneAndRange();senses::losOrder();senses::hearing();senses::suspicionThresholds();senses::propagation();senses::searchGiveUp();senses::disguises(root);senses::hiding(root);senses::trails();senses::restore();senses::rejection(root);}
// ---- genre-bots: melee/shooter bot policies, squad roles, objective choice (record kinds 56..58) ----
#include <aiverselib/bots/SensesPerception.h>
#include <aiverselib/bots/GenreBotConfig.h>
namespace gbots {
using namespace aiverse::lib::bots;using aiverse::lib::bots::PerceivedTarget; // genre::PerceivedTarget (stealth-senses) is a different record
uint64_t mix(uint64_t running,uint64_t value){Hasher h;h.u64(running);h.u64(value);return h.value();}
const AttackSet& duelAttacks(){static const AttackSet defs{{1,{1,8,4,10,14,6,{2}}},{2,{2,6,4,12,22,6,{}}}};return defs;}
const HitboxSet& duelBoxes(){
    static const HitboxSet boxes=[]{CombatShape blade;blade.kind=ShapeKind::Sphere;blade.a={{},1_fx,Fixed::fromRatio(6,5)};blade.radius=Fixed::fromRatio(3,5);
        auto thrust=blade;thrust.a.z=Fixed::fromRatio(7,5);return HitboxSet{{1,{{1,8,12,blade}}},{2,{{1,6,10,thrust}}}};}();
    return boxes;
}
const DodgeConfig kDodge{12,2,8,20};const GuardConfig kGuard{Fixed::fromRatio(1,8),6,20};const PoiseConfig kPoise{};const LockOnConfig kLock{};
Fixed reachOf(int64_t attack){return attack==2?Fixed::fromRatio(12,5):Fixed::fromRatio(11,5);}
CombatShape torso(){CombatShape s;s.kind=ShapeKind::Capsule;s.a={{},Fixed::fromRatio(1,5),{}};s.b={{},Fixed::fromRatio(9,5),{}};s.radius=Fixed::fromRatio(2,5);return s;}
Fixed clampArena(Fixed v,Fixed m){return fxClamp(v,-m,m);}

// ---- Owner simulation for a melee duel: bots only produce MeleeInput; the owner authorizes and applies it.
struct Fighter {int64_t id=0,team=0;CombatPose pose{};MeleeState melee{};CombatActorState combat{};MeleeBotState bot{};int64_t health=1000;Vec2 dodgeDir{};};
struct MeleeDuel {std::array<Fighter,2> f;std::array<MeleeBotConfig,2> cfg;uint32_t tick=0;Random rng{1};uint64_t hash=0;uint32_t hits=0,blocks=0,parries=0,evades=0;std::array<int64_t,2> dealt{};};
MeleeDuel makeMeleeDuel(uint64_t seed,BotDifficulty a,BotDifficulty b){
    MeleeDuel d;d.rng=Random(seed);d.f[0].id=1;d.f[0].team=1;d.f[0].pose={{{},{},-3_fx},{}};d.f[1].id=2;d.f[1].team=2;d.f[1].pose={{{},{},3_fx},Fixed::fromRatio(1,2)};
    d.cfg[0].difficulty=a;d.cfg[1].difficulty=b;for(auto& c:d.cfg){c.minRange=Fixed::fromRatio(7,5);c.maxRange=Fixed::fromRatio(11,5);c.attackRange=2_fx;}d.cfg[1].strafeTicks=23;d.f[1].pose.position.x=Fixed::fromRatio(1,2);return d;
}
bool stepMeleeDuel(MeleeDuel& d){
    const auto& defs=duelAttacks();const auto& boxes=duelBoxes();uint32_t tick=d.tick;std::array<MeleeInput,2> in{};
    for(std::size_t i=0;i<2;++i){
        const auto& me=d.f[i];const auto& other=d.f[1-i];
        PerceivedTarget seen;seen.id=other.id;seen.team=other.team;seen.position=other.pose.position;seen.yaw=other.pose.yaw;seen.guarding=other.combat.guard.raised;
        seen.vulnerable=attackPhase(defs,other.melee,tick)==AttackPhase::Recovery||staggered(other.combat.poise,tick);seen.healthMilli=std::clamp<int64_t>(other.health,0,1000);
        seen.threat=meleeThreat(defs,other.melee,tick,reachOf(other.melee.attack));
        auto r=stepMeleeBot(d.cfg[i],defs,kDodge,kGuard,d.f[i].bot,tick,{me.id,me.team,me.pose,me.melee,me.combat},std::span<const PerceivedTarget>(&seen,1),d.rng);
        if(!r)return false;in[i]=*r;
    }
    for(std::size_t i=0;i<2;++i){
        auto& me=d.f[i];const auto& other=d.f[1-i];const auto& x=in[i];
        if(!stepPoise(kPoise,me.combat.poise,tick))return false;
        bool stun=staggered(me.combat.poise,tick);auto phase=attackPhase(defs,me.melee,tick);
        if(!stepGuard(kGuard,me.combat.guard,tick,x.guard&&!stun&&phase==AttackPhase::Idle))return false;
        auto dodged=stepDodge(kDodge,me.combat.dodge,tick,x.dodge,[&]{return !stun&&phase==AttackPhase::Idle;});if(!dodged)return false;if(*dodged)me.dodgeDir=x.dodgeDirection;
        bool canAttack=!stun&&!dodging(kDodge,me.combat.dodge,tick)&&!me.combat.guard.raised;
        if(!stepMelee(defs,me.melee,tick,x.attack,[&](int64_t){return canAttack;}))return false;
        const std::array<LockCandidate,1> seen{{{other.id,other.pose.position,true}}};
        if(!stepLockOn(kLock,me.combat.lock,tick,me.pose,seen,x.lock))return false;
        auto now=attackPhase(defs,me.melee,tick);
        if(stun){}
        else if(dodging(kDodge,me.combat.dodge,tick)){me.pose.position.x+=me.dodgeDir.x*Fixed::fromRatio(9,60);me.pose.position.z+=me.dodgeDir.y*Fixed::fromRatio(9,60);}
        else{
            if(now==AttackPhase::Idle){auto speed=me.combat.guard.raised?Fixed::fromRatio(2,60):Fixed::fromRatio(4,60);me.pose.position.x+=x.move.x*speed;me.pose.position.z+=x.move.y*speed;}
            if(now==AttackPhase::Idle||now==AttackPhase::Windup)me.pose.yaw=x.face;
        }
        me.pose.position.x=clampArena(me.pose.position.x,8_fx);me.pose.position.z=clampArena(me.pose.position.z,8_fx);
    }
    std::array<std::vector<HitContact>,2> contacts;std::array<int64_t,2> swing{};
    for(std::size_t i=0;i<2;++i){
        auto& att=d.f[i];const auto& def=d.f[1-i];swing[i]=att.melee.attack;
        CombatBody a{att.id,att.team,att.pose,{{1,torso()}}},b{def.id,def.team,def.pose,{{1,torso()}}};
        auto hits=collectMeleeHits(defs,boxes,att.melee,tick,a,std::span<const CombatBody>(&b,1));if(!hits)return false;contacts[i]=*hits;
    }
    for(std::size_t i=0;i<2;++i)for(const auto& hit:contacts[i]){
        (void)hit;auto& att=d.f[i];auto& def=d.f[1-i];int64_t damage=swing[i]==2?140:90;
        auto contact=resolveCombatContact(kDodge,def.combat.dodge,kGuard,def.combat.guard,def.pose,att.pose.position,tick);
        if(contact==MeleeContact::Hit){def.health-=damage;d.dealt[i]+=damage;++d.hits;}
        else if(contact==MeleeContact::Blocked)++d.blocks;else if(contact==MeleeContact::Evaded)++d.evades;
        else{++d.parries;if(!applyParryStagger(kPoise,att.combat.poise,tick))return false;interruptMelee(att.melee);}
        auto r=applyPoiseHit(kPoise,def.combat.poise,tick,damage*250,contact);if(!r)return false;
        if(contact==MeleeContact::Hit&&r->stunUntil>tick)interruptMelee(def.melee);
    }
    for(const auto& f:d.f){
        Hasher h;h.v3(f.pose.position);h.fx(f.pose.yaw);h.i64(f.health);h.u64(combatHash(f.combat));h.str(save(f.melee));h.u64(genreBotHash(f.bot));d.hash=mix(d.hash,h.value());
    }
    d.hash=mix(d.hash,d.rng.state());++d.tick;return true;
}
bool meleeOver(const MeleeDuel& d){return d.f[0].health<=0||d.f[1].health<=0;}
void runMelee(MeleeDuel& d,uint32_t until){while(d.tick<until&&!meleeOver(d))CHECK(stepMeleeDuel(d));}
std::string snapshotMelee(const MeleeDuel& d){
    std::string out;Archive a(&out);auto c=d;uint64_t state=c.rng.state(),inc=c.rng.increment();
    a(c.tick,state,inc,c.hash,c.hits,c.blocks,c.parries,c.evades,c.dealt[0],c.dealt[1]);
    for(auto& f:c.f){auto m=save(f.melee),k=save(f.combat),b=save(f.bot);CHECK(!m.empty()&&!k.empty()&&!b.empty());a(f.pose.position,f.pose.yaw,f.health,f.dodgeDir.x,f.dodgeDir.y,m,k,b);}
    return out;
}
MeleeDuel restoreMelee(const std::string& bytes,const std::array<MeleeBotConfig,2>& cfg){
    MeleeDuel d=makeMeleeDuel(0,{},{});d.cfg=cfg;Archive a(bytes);uint64_t state=0,inc=0;
    a(d.tick,state,inc,d.hash,d.hits,d.blocks,d.parries,d.evades,d.dealt[0],d.dealt[1]);d.rng.restore(state,inc);
    for(auto& f:d.f){std::string m,k,b;a(f.pose.position,f.pose.yaw,f.health,f.dodgeDir.x,f.dodgeDir.y,m,k,b);CHECK(load(m,f.melee)&&load(k,f.combat)&&load(b,f.bot));}
    CHECK(a.ok()&&!a.remaining());return d;
}

// ---- Owner simulation for a hitscan duel: recoil, spread, ammo feed with heat, cover damage reduction.
const FeedConfig& duelFeed(){static const FeedConfig c=[]{FeedConfig f;f.capacity=10;f.intervalTicks=5;f.reloadTicks=90;f.heatPerShot=80;f.heatCapacity=1000;f.coolPerTick=4;f.overheatTicks=30;f.recoverHeat=300;return f;}();return c;}
const RecoilConfig kRecoil{Fixed::fromRatio(1,600),Fixed::fromRatio(1,150),Fixed::fromRatio(1,1500),Fixed::fromRatio(1,16)};
const std::array<CoverCandidate,2>& duelCover(){static const std::array<CoverCandidate,2> c{{{1,{-4_fx,{},{}},700,true},{2,{4_fx,{},{}},700,true}}};return c;}
struct Gunner {int64_t id=0,team=0;Vec3 pos{};Fixed yaw{},pitch{};RecoilState recoil{};FeedState feed{};uint32_t reserve=200;int64_t health=1000;ShooterBotState bot{};Vec2 velocity{};};
struct GunDuel {std::array<Gunner,2> g;std::array<ShooterBotConfig,2> cfg;uint32_t tick=0;Random rng{1};uint64_t hash=0;uint32_t shots=0,hits=0,covered=0;std::array<int64_t,2> dealt{};};
GunDuel makeGunDuel(uint64_t seed,BotDifficulty a,BotDifficulty b){
    GunDuel d;d.rng=Random(seed);d.g[0].id=1;d.g[0].team=1;d.g[0].pos={{},{},-12_fx};d.g[1].id=2;d.g[1].team=2;d.g[1].pos={{},{},12_fx};d.g[1].yaw=Fixed::fromRatio(1,2);
    for(auto& g:d.g)g.feed=makeFeed(duelFeed());d.cfg[0].difficulty=a;d.cfg[1].difficulty=b;return d;
}
Vec3 eyeOf(Vec3 p){return {p.x,p.y+Fixed::fromRatio(8,5),p.z};}
Vec3 chestOf(Vec3 p){return {p.x,p.y+Fixed::fromRatio(13,10),p.z};}
bool stepGunDuel(GunDuel& d){
    uint32_t tick=d.tick;std::array<ShooterInput,2> in{};const auto& feed=duelFeed();
    for(std::size_t i=0;i<2;++i){
        const auto& me=d.g[i];const auto& other=d.g[1-i];
        PerceivedTarget seen;seen.id=other.id;seen.team=other.team;seen.position=chestOf(other.pos);seen.velocity={other.velocity.x,{},other.velocity.y};seen.healthMilli=std::clamp<int64_t>(other.health,0,1000);
        ShooterBotSelf self{me.id,me.team,me.pos,eyeOf(me.pos),me.yaw,me.pitch,me.recoil,me.feed,me.reserve,std::clamp<int64_t>(me.health,0,1000)};
        auto r=stepShooterBot(d.cfg[i],feed,d.g[i].bot,tick,self,std::span<const PerceivedTarget>(&seen,1),duelCover(),d.rng);if(!r)return false;in[i]=*r;
    }
    for(std::size_t i=0;i<2;++i){
        auto& me=d.g[i];const auto& x=in[i];auto turn=Fixed::fromRatio(1,16);
        me.yaw=wrapBotTurns(me.yaw+fxClamp(wrapBotTurns(x.yaw-me.yaw),-turn,turn));me.pitch=fxClamp(x.pitch,-Fixed::fromRatio(1,4),Fixed::fromRatio(1,4));
        me.velocity=x.move*4_fx;me.pos.x=clampArena(me.pos.x+x.move.x*Fixed::fromRatio(4,60),15_fx);me.pos.z=clampArena(me.pos.z+x.move.y*Fixed::fromRatio(4,60),15_fx);
    }
    for(std::size_t i=0;i<2;++i){
        auto& me=d.g[i];auto& other=d.g[1-i];
        auto step=stepFeed(feed,me.feed,tick,in[i].feed,[&](uint32_t n){auto k=std::min(n,me.reserve);me.reserve-=k;return k;});if(!step)return false;
        if(step->fired){
            ++d.shots;auto spread=sampleSpread(d.rng,Fixed::fromRatio(1,800));if(!spread)return false;
            auto shotYaw=me.yaw+me.recoil.yaw+spread->x,shotPitch=me.pitch+me.recoil.pitch+spread->y;
            auto eye=eyeOf(me.pos),chest=chestOf(other.pos);auto dist=(chest-eye).length();auto arc=dist*Fixed::fromRatio(6283,1000);
            auto lateral=fxAbs(wrapBotTurns(shotYaw-yawToward(eye,chest,me.yaw)))*arc,vertical=fxAbs(shotPitch-pitchToward(eye,chest))*arc;
            if(lateral<=Fixed::fromRatio(2,5)&&vertical<=Fixed::fromRatio(1,2)){
                bool inCover=false;for(const auto& c:duelCover())if(Vec2{other.pos.x-c.position.x,other.pos.z-c.position.z}.length()<=Fixed::fromRatio(6,5))inCover=true;
                int64_t damage=inCover?18:60;other.health-=damage;d.dealt[i]+=damage;++d.hits;if(inCover)++d.covered;
            }
        }
        if(!stepRecoil(kRecoil,me.recoil,step->fired))return false;
    }
    for(const auto& g:d.g){
        Hasher h;h.v3(g.pos);h.fx(g.yaw);h.fx(g.pitch);h.i64(g.health);h.u32(g.reserve);h.str(save(g.recoil));h.str(save(g.feed));h.u64(genreBotHash(g.bot));d.hash=mix(d.hash,h.value());
    }
    d.hash=mix(d.hash,d.rng.state());++d.tick;return true;
}
bool gunOver(const GunDuel& d){return d.g[0].health<=0||d.g[1].health<=0;}
void runGun(GunDuel& d,uint32_t until){while(d.tick<until&&!gunOver(d))CHECK(stepGunDuel(d));}
std::string snapshotGun(const GunDuel& d){
    std::string out;Archive a(&out);auto c=d;uint64_t state=c.rng.state(),inc=c.rng.increment();
    a(c.tick,state,inc,c.hash,c.shots,c.hits,c.covered,c.dealt[0],c.dealt[1]);
    for(auto& g:c.g){auto r=save(g.recoil),f=save(g.feed),b=save(g.bot);CHECK(!r.empty()&&!f.empty()&&!b.empty());a(g.pos,g.yaw,g.pitch,g.reserve,g.health,g.velocity.x,g.velocity.y,r,f,b);}
    return out;
}
GunDuel restoreGun(const std::string& bytes,const std::array<ShooterBotConfig,2>& cfg){
    GunDuel d=makeGunDuel(0,{},{});d.cfg=cfg;Archive a(bytes);uint64_t state=0,inc=0;
    a(d.tick,state,inc,d.hash,d.shots,d.hits,d.covered,d.dealt[0],d.dealt[1]);d.rng.restore(state,inc);
    for(auto& g:d.g){std::string r,f,b;a(g.pos,g.yaw,g.pitch,g.reserve,g.health,g.velocity.x,g.velocity.y,r,f,b);CHECK(load(r,g.recoil)&&load(f,g.feed)&&load(b,g.bot));}
    CHECK(a.ok()&&!a.remaining());return d;
}

void meleeDuels(){
    auto normal=*difficultyPreset(1);
    auto a=makeMeleeDuel(7,normal,normal),b=a;runMelee(a,2400);runMelee(b,2400);
    CHECK(a.hash==b.hash&&a.tick==b.tick&&a.f[0].health==b.f[0].health&&a.f[1].health==b.f[1].health);
    CHECK(a.hits>0&&a.blocks+a.parries+a.evades>0);
    // Save mid-fight, continue the original, restore into a fresh duel and continue: identical results.
    auto c=makeMeleeDuel(7,normal,normal);runMelee(c,90);CHECK(!meleeOver(c)&&c.tick==90);auto bytes=snapshotMelee(c);
    runMelee(c,2400);auto r=restoreMelee(bytes,c.cfg);CHECK(r.tick==90);runMelee(r,2400);
    CHECK(r.hash==a.hash&&c.hash==a.hash&&r.f[0].health==a.f[0].health&&r.f[1].health==a.f[1].health&&r.hits==a.hits&&r.parries==a.parries);
    CHECK(!snapshotMelee(r).empty());
    // Difficulty changes outcomes (never stats): hard beats easy over several seeds with swapped sides.
    int hardWins=0,easyWins=0;int64_t hardDealt=0,easyDealt=0;auto hard=*difficultyPreset(2),easy=*difficultyPreset(0);
    for(uint64_t seed=1;seed<=6;++seed){
        bool hardFirst=seed%2;auto d=hardFirst?makeMeleeDuel(seed,hard,easy):makeMeleeDuel(seed,easy,hard);runMelee(d,3600);
        std::size_t h=hardFirst?0:1;hardDealt+=d.dealt[h];easyDealt+=d.dealt[1-h];
        if(d.f[1-h].health<=0)++hardWins;else if(d.f[h].health<=0)++easyWins;
    }
    CHECK(hardWins>easyWins&&hardDealt>easyDealt*2);
}
void gunDuels(){
    auto normal=*difficultyPreset(1);
    auto a=makeGunDuel(11,normal,normal),b=a;runGun(a,3600);runGun(b,3600);
    CHECK(a.hash==b.hash&&a.tick==b.tick&&a.g[0].health==b.g[0].health&&a.g[1].health==b.g[1].health&&a.shots>0&&a.hits>0);
    auto c=makeGunDuel(11,normal,normal);runGun(c,150);CHECK(!gunOver(c));auto bytes=snapshotGun(c);
    runGun(c,3600);auto r=restoreGun(bytes,c.cfg);runGun(r,3600);
    CHECK(r.hash==a.hash&&c.hash==a.hash&&r.shots==a.shots&&r.hits==a.hits&&r.g[0].health==a.g[0].health);
    int hardWins=0,easyWins=0;int64_t hardDealt=0,easyDealt=0;auto hard=*difficultyPreset(2),easy=*difficultyPreset(0);
    for(uint64_t seed=1;seed<=4;++seed){
        bool hardFirst=seed%2;auto d=hardFirst?makeGunDuel(seed,hard,easy):makeGunDuel(seed,easy,hard);runGun(d,3600);
        std::size_t h=hardFirst?0:1;hardDealt+=d.dealt[h];easyDealt+=d.dealt[1-h];
        if(d.g[1-h].health<=0)++hardWins;else if(d.g[h].health<=0)++easyWins;
    }
    CHECK(hardWins>easyWins&&hardDealt>easyDealt*2);
}
void meleePolicy(){
    const auto& defs=duelAttacks();MeleeBotConfig c;c.difficulty={5,1,{},0};c.openMilli=1000;Random rng(3);
    MeleeBotSelf self{1,1,{{},{}},{},{}};PerceivedTarget foe;foe.id=2;foe.team=2;foe.position={{},{},6_fx};
    auto one=[&](const PerceivedTarget& t){return std::span<const PerceivedTarget>(&t,1);};
    MeleeBotState s;auto out=stepMeleeBot(c,defs,kDodge,kGuard,s,0,self,one(foe),rng);
    CHECK(out&&out->move==Vec2{}&&s.target==2&&s.noticedAt==0);                       // reaction delay: hold
    for(uint32_t t=1;t<5;++t)CHECK(stepMeleeBot(c,defs,kDodge,kGuard,s,t,self,one(foe),rng));
    out=stepMeleeBot(c,defs,kDodge,kGuard,s,5,self,one(foe),rng);
    CHECK(out&&out->move.y>Fixed::fromRatio(9,10)&&out->lock.toggle&&out->attack==0);   // approach + lock-on
    auto before=s;CHECK(!stepMeleeBot(c,defs,kDodge,kGuard,s,5,self,one(foe),rng)&&!stepMeleeBot(c,defs,kDodge,kGuard,s,4,self,one(foe),rng));
    CHECK(save(s)==save(before));
    foe.position={{},{},Fixed::fromRatio(9,5)};out=stepMeleeBot(c,defs,kDodge,kGuard,s,6,self,one(foe),rng);CHECK(out&&out->attack==1&&out->move==Vec2{});
    foe.guarding=true;out=stepMeleeBot(c,defs,kDodge,kGuard,s,7,self,one(foe),rng);CHECK(out&&out->attack==0);   // respect a raised guard
    foe.vulnerable=true;out=stepMeleeBot(c,defs,kDodge,kGuard,s,8,self,one(foe),rng);CHECK(out&&out->attack==1); // ...unless punishable
    foe.guarding=foe.vulnerable=false;
    // Combo only on hit confirm, buffered so stepMelee starts it at the cancel frame (cancelFrom 14, buffer 6).
    MeleeBotSelf swinging=self;swinging.melee.attack=1;swinging.melee.started=10;swinging.melee.lastTick=17;swinging.melee.initialized=true;
    out=stepMeleeBot(c,defs,kDodge,kGuard,s,18,swinging,one(foe),rng);CHECK(out&&out->attack==0);
    swinging.melee.hitTargets={2};out=stepMeleeBot(c,defs,kDodge,kGuard,s,19,swinging,one(foe),rng);CHECK(out&&out->attack==2);
    // Telegraph answers: dodge timed so invulnerability covers the first active frame, guard so the parry window straddles it.
    auto threatAt=[&](uint32_t started){PerceivedTarget t=foe;t.threat={1,started,8,4,reachOf(1),false};return t;};
    {MeleeBotConfig d=c;d.dodgeMilli=1000;MeleeBotState m;auto t=threatAt(100);uint32_t fired=0;
     for(uint32_t tick=100;tick<112;++tick){auto o=stepMeleeBot(d,defs,kDodge,kGuard,m,tick,self,one(t),rng);CHECK(o&&!o->attack&&!o->guard);if(o->dodge&&!fired)fired=tick;}
     CHECK(fired==106&&m.defense==MeleeDefense::Dodge&&m.threatRead);}                 // active at 108, invulnFrom 2
    {MeleeBotConfig d=c;d.dodgeMilli=0;MeleeBotState m;auto t=threatAt(100);uint32_t raised=0;
     for(uint32_t tick=100;tick<112;++tick){auto o=stepMeleeBot(d,defs,kDodge,kGuard,m,tick,self,one(t),rng);CHECK(o&&!o->dodge);if(o->guard&&!raised)raised=tick;}
     CHECK(raised==105&&m.defense==MeleeDefense::Guard);}                              // parryTicks 6 -> raise 3 early
    {MeleeBotConfig d=c;d.difficulty.misreadMilli=1000;MeleeBotState m;auto t=threatAt(100);
     for(uint32_t tick=100;tick<112;++tick){auto o=stepMeleeBot(d,defs,kDodge,kGuard,m,tick,self,one(t),rng);CHECK(o&&!o->dodge&&!o->guard);}
     CHECK(m.defense==MeleeDefense::Ignore);}
    {MeleeBotState m;auto t=threatAt(100);t.threat.unblockable=true;MeleeBotSelf tired=self;tired.combat.dodge={90,200,99,true,true};
     for(uint32_t tick=100;tick<108;++tick){auto o=stepMeleeBot(c,defs,kDodge,kGuard,m,tick,tired,one(t),rng);CHECK(o&&!o->guard&&!o->dodge);if(tick>=105)CHECK(o->move.y< -Fixed::fromRatio(9,10));}
     CHECK(m.defense==MeleeDefense::Retreat);}
    {MeleeBotState m;auto t=threatAt(100);t.position={{},{},5_fx};                    // out of reach: never read, approach instead
     for(uint32_t tick=100;tick<112;++tick){auto o=stepMeleeBot(c,defs,kDodge,kGuard,m,tick,self,one(t),rng);CHECK(o&&!o->dodge&&!o->guard);}CHECK(!m.threatRead);}
    // Invalid input is rejected without touching state.
    MeleeBotState fresh;auto bad=c;bad.minRange=3_fx;CHECK(!stepMeleeBot(bad,defs,kDodge,kGuard,fresh,0,self,one(foe),rng));
    bad=c;bad.opener=9;CHECK(!stepMeleeBot(bad,defs,kDodge,kGuard,fresh,0,self,one(foe),rng));
    bad=c;bad.difficulty.decisionTicks=0;CHECK(!stepMeleeBot(bad,defs,kDodge,kGuard,fresh,0,self,one(foe),rng));
    const std::array<PerceivedTarget,2> twins{foe,foe};CHECK(!stepMeleeBot(c,defs,kDodge,kGuard,fresh,0,self,twins,rng));
    auto future=threatAt(50);CHECK(!stepMeleeBot(c,defs,kDodge,kGuard,fresh,10,self,one(future),rng));
    auto selfBad=self;selfBad.melee.attack=7;CHECK(!stepMeleeBot(c,defs,kDodge,kGuard,fresh,0,selfBad,one(foe),rng));
    CHECK(!fresh.initialized);
    // Teammates and the bot itself are never targets.
    PerceivedTarget mate=foe;mate.team=1;CHECK(stepMeleeBot(c,defs,kDodge,kGuard,fresh,0,self,one(mate),rng)&&fresh.target==0);
}
void shooterPolicy(){
    FeedConfig feed=duelFeed();ShooterBotConfig c;c.difficulty={0,2,{},0};c.strafeTicks=0;Random rng(5);auto rngBefore=rng.state();
    ShooterBotSelf self{1,1,{},{{},Fixed::fromRatio(8,5),{}},Fixed::fromRatio(1,4),{},{},makeFeed(feed),50,1000};
    PerceivedTarget foe;foe.id=2;foe.team=2;foe.position={{},Fixed::fromRatio(8,5),20_fx};
    auto one=[&](const PerceivedTarget& t){return std::span<const PerceivedTarget>(&t,1);};std::span<const CoverCandidate> none;
    ShooterBotState s;uint32_t firstFire=0;
    for(uint32_t t=0;t<40;++t){
        auto o=stepShooterBot(c,feed,s,t,self,one(foe),none,rng);CHECK(o);
        CHECK(fxAbs(wrapBotTurns(o->yaw-self.yaw))<=c.turnPerTick);                     // bounded turn rate
        if(o->feed.fire&&!firstFire)firstFire=t;self.yaw=o->yaw;self.pitch=o->pitch;
    }
    CHECK(firstFire>=15&&fxAbs(self.yaw)<=c.fireTolerance&&rng.state()==rngBefore);   // a quarter turn at 1/64 per tick; no error -> no RNG
    CHECK(s.decision==ShooterDecision::Engage);
    // Lead: a projectile weapon aims ahead of a target crossing to +X; hitscan aims at the body.
    {auto lead=c;lead.projectileSpeed=50_fx;lead.turnPerTick=Fixed::fromRatio(1,2);ShooterBotState m;auto moving=foe;moving.velocity={5_fx,{},{}};
     auto o=stepShooterBot(lead,feed,m,0,self,one(moving),none,rng);CHECK(o);
     auto solution=solveIntercept(lead.aim,self.eye,moving.position,moving.velocity,50_fx,{});CHECK(solution&&solution->found);
     CHECK(o->yaw>Fixed::fromRatio(1,100)&&fxAbs(o->yaw-fxAtan2Turns(solution->launch.velocity.x,solution->launch.velocity.z))<=Fixed::fromRatio(1,1000));
     lead.projectileSpeed={};ShooterBotState h;auto direct=stepShooterBot(lead,feed,h,0,self,one(moving),none,rng);CHECK(direct&&fxAbs(direct->yaw)<=Fixed::fromRatio(1,1000));}
    // Reload decisions from the feed state.
    {ShooterBotState m;auto dry=self;dry.feed.loaded=0;auto o=stepShooterBot(c,feed,m,0,dry,one(foe),none,rng);CHECK(o&&o->feed.reload&&!o->feed.fire&&o->decision==ShooterDecision::Reload);
     dry.reserve=0;ShooterBotState n;o=stepShooterBot(c,feed,n,0,dry,one(foe),none,rng);CHECK(o&&!o->feed.reload&&!o->feed.fire);
     auto low=self;low.feed.loaded=3;ShooterBotState idle;o=stepShooterBot(c,feed,idle,0,low,{},none,rng);CHECK(o&&o->feed.reload);   // top up with no target
     ShooterBotState busy;o=stepShooterBot(c,feed,busy,0,low,one(foe),none,rng);CHECK(o&&!o->feed.reload&&o->feed.fire);}         // keep shooting under fire
    // Heat hysteresis: stop at 900/1000, resume only at or below 400.
    {ShooterBotState m;auto hot=self;hot.feed.heat=950;uint32_t t=0;
     for(auto [heat,expect]:std::array<std::pair<uint32_t,bool>,4>{{{950,false},{600,false},{400,true},{700,true}}}){hot.feed.heat=heat;auto o=stepShooterBot(c,feed,m,t++,hot,one(foe),none,rng);CHECK(o&&o->feed.fire==expect);}}
    // Burst control: hold fire until recoil settles under the tolerance.
    {ShooterBotState m;auto kicked=self;kicked.recoil.pitch=Fixed::fromRatio(1,20);auto o=stepShooterBot(c,feed,m,0,kicked,one(foe),none,rng);CHECK(o&&!o->feed.fire);
     kicked.recoil.pitch=Fixed::fromRatio(1,100);o=stepShooterBot(c,feed,m,1,kicked,one(foe),none,rng);CHECK(o&&o->feed.fire);}
    // Cover: hurt bots choose the best reachable candidate (protection minus travel, ties by id) and move there.
    {const std::array<CoverCandidate,4> spots{{{4,{3_fx,{},{}},900,false},{3,{-4_fx,{},{}},600,true},{1,{4_fx,{},{}},700,true},{2,{-4_fx,{},{}},700,true}}};
     ShooterBotState m;auto hurt=self;hurt.healthMilli=100;auto o=stepShooterBot(c,feed,m,0,hurt,one(foe),spots,rng);
     CHECK(o&&o->cover==1&&o->decision==ShooterDecision::Cover&&o->move.x>Fixed::fromRatio(9,10)&&o->feed.fire);
     CHECK((chooseCover(c,{},spots)==1&&chooseCover(c,{-1_fx,{},{}},spots)==2));
     ShooterBotState healthy;o=stepShooterBot(c,feed,healthy,0,self,one(foe),spots,rng);CHECK(o&&o->cover==0);}
    // Aim error is seeded: same seed -> same samples; larger error -> larger misses.
    {auto noisy=c;noisy.difficulty.aimError=Fixed::fromRatio(1,40);Random x(9),y(9);ShooterBotState a,b;
     auto oa=stepShooterBot(noisy,feed,a,0,self,one(foe),none,x),ob=stepShooterBot(noisy,feed,b,0,self,one(foe),none,y);
     CHECK(oa&&ob&&oa->yaw==ob->yaw&&a.errorYaw==b.errorYaw&&x.state()==y.state()&&save(a)==save(b));}
    // Invalid input.
    ShooterBotState fresh;auto bad=c;bad.minRange=20_fx;CHECK(!stepShooterBot(bad,feed,fresh,0,self,one(foe),none,rng));
    bad=c;bad.heatResumeMilli=950;CHECK(!stepShooterBot(bad,feed,fresh,0,self,one(foe),none,rng));
    bad=c;bad.projectileSpeed=Fixed::fromRatio(1,100);CHECK(!stepShooterBot(bad,feed,fresh,0,self,one(foe),none,rng));
    auto over=self;over.feed.loaded=11;CHECK(!stepShooterBot(c,feed,fresh,0,over,one(foe),none,rng));
    const std::array<CoverCandidate,2> dup{{{1,{},500,true},{1,{},500,true}}};CHECK(!stepShooterBot(c,feed,fresh,0,self,one(foe),dup,rng));
    CHECK(!fresh.initialized);
}
void squadRoles(){
    SquadConfig c;std::vector<SquadMember> team;for(int64_t id=1;id<=5;++id)team.push_back({id*10,{Fixed::fromInt(id),{},{}},kAllRoles,true});
    auto counts=roleCounts(c.offense,5);CHECK(counts&&(*counts==std::array<uint32_t,4>{2,1,1,1}));
    CHECK((*roleCounts(c.defense,5)==std::array<uint32_t,4>{1,3,1,0}));CHECK(!roleCounts({0,0,0,0},3)&&!roleCounts({-1,1,1,1},3));
    Vec3 objective{{},{},30_fx};SquadState s;auto plan=stepSquad(c,s,0,objective,SquadStance::Offense,team);
    CHECK(plan&&plan->reassigned&&plan->orders.size()==5&&(plan->counts==std::array<uint32_t,4>{2,1,1,1}));
    for(std::size_t i=1;i<plan->orders.size();++i)CHECK(plan->orders[i-1].id<plan->orders[i].id);
    // Input order does not matter.
    auto shuffled=team;std::reverse(shuffled.begin(),shuffled.end());SquadState t;auto other=stepSquad(c,t,0,objective,SquadStance::Offense,shuffled);
    CHECK(other&&save(t)==save(s));for(std::size_t i=0;i<5;++i)CHECK(other->orders[i].role==plan->orders[i].role&&other->orders[i].goal==plan->orders[i].goal);
    // Hysteresis: jittered positions inside the interval keep roles and goals.
    auto moved=team;for(auto& m:moved)m.position.z+=3_fx;auto kept=stepSquad(c,s,1,objective,SquadStance::Offense,moved);
    CHECK(kept&&!kept->reassigned);for(std::size_t i=0;i<5;++i)CHECK(kept->orders[i].role==plan->orders[i].role&&kept->orders[i].goal==plan->orders[i].goal);
    CHECK(!stepSquad(c,s,1,objective,SquadStance::Offense,moved));                     // repeated tick
    // A death reassigns at once; survivors keep their previous roles where the counts allow.
    auto roleOf=[](const SquadPlan& p,int64_t id){for(const auto& o:p.orders)if(o.id==id)return o.role;return SquadRole::Attacker;};
    auto lost=moved;lost[4].alive=false;auto after=stepSquad(c,s,2,objective,SquadStance::Offense,lost);
    CHECK(after&&after->reassigned&&after->orders.size()==4&&(after->counts==std::array<uint32_t,4>{2,0,1,1}));
    for(const auto& o:after->orders)if(roleOf(*plan,o.id)!=SquadRole::Defender)CHECK(o.role==roleOf(*plan,o.id));
    // Capability masks: a support-only member takes the support slot; defense stance shifts the mix.
    auto medic=team;medic[0].roles=roleBit(SquadRole::Support);SquadState m;auto mp=stepSquad(c,m,0,objective,SquadStance::Defense,medic);
    CHECK(mp&&roleOf(*mp,10)==SquadRole::Support&&(mp->counts==std::array<uint32_t,4>{1,3,1,0}));
    for(const auto& o:mp->orders)if(o.role==SquadRole::Defender)CHECK(fxAbs((o.goal-objective).length()-c.defendRadius)<=Fixed::fromRatio(1,100));
    // Formation offsets.
    auto line=formationOffsets(FormationShape::Line,3,2_fx);CHECK((line[0]==Vec2{-2_fx,{}}&&line[1]==Vec2{}&&line[2]==Vec2{2_fx,{}}));
    auto wedge=formationOffsets(FormationShape::Wedge,3,2_fx);CHECK((wedge[0]==Vec2{}&&wedge[1]==Vec2{-2_fx,-2_fx}&&wedge[2]==Vec2{2_fx,-2_fx}));
    auto column=formationOffsets(FormationShape::Column,2,2_fx);CHECK((column[1]==Vec2{{},-2_fx}));
    // Invalid input.
    SquadState fresh;auto dup=team;dup[1].id=10;CHECK(!stepSquad(c,fresh,0,objective,SquadStance::Offense,dup));
    auto noRole=team;noRole[0].roles=0;CHECK(!stepSquad(c,fresh,0,objective,SquadStance::Offense,noRole));
    auto bad=c;bad.offense={0,0,0,0};CHECK(!stepSquad(bad,fresh,0,objective,SquadStance::Offense,team));CHECK(!fresh.initialized);
    roundtrip(s);
}
void objectives(){
    ObjectiveWeights w;std::vector<ObjectiveInfo> list{
        {1,ObjectiveKind::Capture,{{},{},10_fx},0,1000,0,0,0,true},     // neutral, near
        {2,ObjectiveKind::Capture,{{},{},40_fx},2,1000,0,0,0,true},     // enemy-owned, far
        {3,ObjectiveKind::Hold,{{},{},5_fx},1,1000,0,0,0,true},         // ours, uncontested
        {4,ObjectiveKind::Capture,{{},{},12_fx},0,1000,0,0,0,false}};   // locked
    auto pick=chooseObjective(w,1,{},list);CHECK(pick&&pick->id==1&&pick->stance==SquadStance::Offense);
    list[2].enemies=3;pick=chooseObjective(w,1,{},list);CHECK(pick&&pick->id==3&&pick->stance==SquadStance::Defense); // our point under threat
    list[2].enemies=0;list[1].progressMilli=1000;list[1].position.z=10_fx;pick=chooseObjective(w,1,{},list);CHECK(pick&&pick->id==2); // nearly taken
    // Stickiness keeps the current choice against a marginally better one; equal scores tie by smaller id.
    std::vector<ObjectiveInfo> twins{{7,ObjectiveKind::Capture,{{},{},10_fx},0,1000,0,0,0,true},{5,ObjectiveKind::Capture,{{},{},10_fx},0,1000,0,0,0,true}};
    CHECK(chooseObjective(w,1,{},twins)->id==5&&chooseObjective(w,1,{},twins,7)->id==7);
    std::vector<ObjectiveInfo> escort{{9,ObjectiveKind::Escort,{{},{},20_fx},1,1000,500,2,1,true}};pick=chooseObjective(w,1,{},escort);
    CHECK(pick&&pick->id==9&&pick->stance==SquadStance::Defense&&pick->score==objectiveScore(w,1,{},escort[0],0));
    CHECK(chooseObjective(w,1,{},std::span<const ObjectiveInfo>{})->id==0);
    auto dup=list;dup[1].id=1;CHECK(!chooseObjective(w,1,{},dup));auto badProgress=list;badProgress[0].progressMilli=1001;CHECK(!chooseObjective(w,1,{},badProgress));
    CHECK(!chooseObjective(w,0,{},list));auto badW=w;badW.distancePerMetre=-1;CHECK(!chooseObjective(badW,1,{},list));
}
void persistence(){
    MeleeBotState m;m.initialized=true;m.target=2;m.threatAttack=1;m.threatStarted=4;m.lastTick=9;m.threatRead=true;m.defense=MeleeDefense::Guard;m.move=MeleeMove::Strafe;roundtrip(m);
    ShooterBotState sh;sh.initialized=true;sh.target=3;sh.cover=1;sh.lastTick=5;sh.errorYaw=Fixed::fromRatio(1,300);sh.decision=ShooterDecision::Cover;sh.cooling=true;roundtrip(sh);
    auto bytes=save(m);ShooterBotState wrong;CHECK(!load(bytes,wrong));                // kinds differ (56 vs 57)
    auto corrupt=bytes;corrupt[4]=char(57);MeleeBotState into;CHECK(!load(corrupt,into));
    MeleeBotState invalid=m;invalid.noticedAt=99;CHECK(save(invalid).empty());
    ShooterBotState badErr=sh;badErr.errorYaw=1_fx;CHECK(save(badErr).empty());
    SquadState q;q.initialized=true;q.lastTick=3;q.roles={{2,SquadRole::Support},{1,SquadRole::Attacker}};CHECK(save(q).empty()); // unsorted roles
    std::swap(q.roles[0],q.roles[1]);roundtrip(q);
    CHECK(genreBotHash(m)==genreBotHash(m)&&genreBotHash(m)!=genreBotHash(MeleeBotState{}));
}
void settings(const std::string& root){
    auto parsed=aiverse::lib::gameplay::readGameplaySettings(Json::parseFile(root+"/examples/libraries/genre/bot-settings.json"));
    CHECK(parsed.value&&genreBotSettingsErrors(*parsed.value).empty());
    auto melee=meleeBotConfig(*parsed.value);auto shooter=shooterBotConfig(*parsed.value);auto squad=squadConfig(*parsed.value);auto weights=objectiveWeights(*parsed.value);
    CHECK(valid(melee)&&valid(shooter)&&valid(squad)&&valid(weights));
    CHECK(melee.difficulty.reactionTicks==6&&melee.attackRange==Fixed::fromRatio(19,10)&&melee.dodgeMilli==700&&shooter.projectileSpeed==80_fx&&shooter.aim.maxIterations==6);
    CHECK(squad.attackShape==FormationShape::Line&&squad.offense[3]==300&&weights.stickiness==250&&shooter.difficulty.aimError==Fixed::fromRatio(2000,1000000));
    auto defaults=aiverse::lib::gameplay::readGameplaySettings(Json::parse(R"({"schemaVersion":1})"));CHECK(defaults.value&&genreBotSettingsErrors(*defaults.value).empty());
    auto m=meleeBotConfig(*defaults.value);MeleeBotConfig d;CHECK(m.minRange==d.minRange&&m.maxRange==d.maxRange&&m.attackRange==d.attackRange&&m.strafeTicks==d.strafeTicks);
    auto sd=shooterBotConfig(*defaults.value);ShooterBotConfig sdef;CHECK(sd.turnPerTick==sdef.turnPerTick&&sd.fireTolerance==sdef.fireTolerance&&sd.recoilTolerance==sdef.recoilTolerance&&sd.maxRange==sdef.maxRange);
    auto read=[](const char* text){return aiverse::lib::gameplay::readGameplaySettings(Json::parse(text));};
    auto r=read(R"({"schemaVersion":1,"genreBots":{"meleeMinRangeMilli":3000}})");CHECK(!r.value&&r.errors.at(0)=="genreBots.meleeMinRangeMilli: must not exceed meleeMaxRangeMilli");
    r=read(R"({"schemaVersion":1,"genreBots":{"heatResumeMilli":950}})");CHECK(!r.value&&r.errors.at(0)=="genreBots.heatResumeMilli: must not exceed heatStopMilli");
    r=read(R"({"schemaVersion":1,"genreBots":{"aimbot":1}})");CHECK(!r.value&&r.errors.at(0)=="genreBots.aimbot: unknown field");
    r=read(R"({"schemaVersion":1,"genreBots":{"decisionTicks":0}})");CHECK(!r.value&&r.errors.at(0)=="genreBots.decisionTicks: integer out of range");
    r=read(R"({"schemaVersion":1,"genreBots":{"useDodge":1}})");CHECK(!r.value&&r.errors.at(0)=="genreBots.useDodge: expected boolean");
    r=read(R"({"schemaVersion":1,"genreBots":{"projectileSpeedMilli":10,"meleeAttackRangeMilli":500,"offenseAttackerMilli":0,"offenseDefenderMilli":0,"offenseSupportMilli":0,"offenseFlankerMilli":0}})");
    CHECK(r.value);auto errors=genreBotSettingsErrors(*r.value);
    CHECK(errors.size()==3&&errors[0]=="genreBots.meleeAttackRangeMilli: must be at least meleeMinRangeMilli"&&errors[1]=="genreBots.projectileSpeedMilli: must be 0 (hitscan) or 63..1000000"
        &&errors[2]=="genreBots.offense*Milli: at least one role weight must be positive");
    r=read(R"({"schemaVersion":1,"genreBots":{"projectileGravityMilli":9800}})");CHECK(r.value&&genreBotSettingsErrors(*r.value).at(0)=="genreBots.projectileGravityMilli: requires projectileSpeedMilli");
}
}
void genreBots(const std::string& root){gbots::meleePolicy();gbots::shooterPolicy();gbots::squadRoles();gbots::objectives();gbots::persistence();gbots::settings(root);gbots::meleeDuels();gbots::gunDuels();}
// ---- senses -> bots bridge: awareness gating, remembered vs visible, owner facts, ordering.
void sensesBotBridge(){
    using A=aiverse::lib::genre::Awareness;namespace sb=aiverse::lib::bots;namespace sg=aiverse::lib::genre;
    std::vector<sg::PerceivedTarget> seen(5);
    seen[0].observer=1;seen[0].target=9;seen[0].level=A::Alert;seen[0].visibleNow=true;seen[0].lastKnown={1_fx,{},{}};
    seen[1].observer=1;seen[1].target=4;seen[1].level=A::Searching;seen[1].lastKnown={{},{},3_fx};
    seen[2].observer=1;seen[2].target=7;seen[2].level=A::Suspicious;seen[2].visibleNow=true;
    seen[3].observer=2;seen[3].target=5;seen[3].level=A::Alert;seen[3].visibleNow=true;
    seen[4].observer=1;seen[4].target=0;seen[4].level=A::Searching;
    auto facts=[](int64_t id)->std::optional<sb::TargetFacts>{if(id==6)return std::nullopt;sb::TargetFacts f;f.team=2;f.position={Fixed::fromInt(id),{},{}};f.velocity={1_fx,{},{}};f.guarding=true;return f;};
    auto out=sb::perceivedFromSenses(std::span<const sg::PerceivedTarget>(seen),1,facts);
    CHECK(out.size()==2&&out[0].id==4&&out[1].id==9);
    CHECK(!out[0].visible&&out[0].position==(Vec3{{},{},3_fx})&&out[0].velocity==Vec3{}&&!out[0].guarding&&out[0].team==2);
    CHECK(out[1].visible&&out[1].position==(Vec3{9_fx,{},{}})&&out[1].guarding&&out[1].velocity==(Vec3{1_fx,{},{}}));
    sb::PerceptionFilter loose{A::Suspicious,A::Suspicious};auto all=sb::perceivedFromSenses(std::span<const sg::PerceivedTarget>(seen),1,facts,loose);
    CHECK(all.size()==3&&all[1].id==7&&all[1].visible);
    seen[0].target=6;CHECK(sb::perceivedFromSenses(std::span<const sg::PerceivedTarget>(seen),1,facts).size()==1);
}
// ---- cinematic-gameplay: time-scale domains, scene timelines, sync waits, QTE, staging, checkpoints (kinds 72..75) ----
#include <aiverselib/genre/Cinematic.h>
#include <aiverselib/genre/CinematicData.h>
#include <aiverselib/genre/CinematicConfigured.h>
#include <aiverselib/genre/CinematicPersistence.h>
#define CCHECK(...) CHECK((__VA_ARGS__))
namespace cine {
using K=SceneEventKind;using C=SceneCommandKind;using Ids=std::vector<int64_t>;
TimeReport tstep(const TimeConfig& c,TimeState& s,uint32_t t,const TimeInput& in={}){auto r=stepTime(c,s,t,in);CCHECK(r);return *r;}
SceneReport run(const SceneLibrary& l,SceneState& s,uint32_t t,const SceneInput& in={}){auto r=stepScene(l,s,t,in);CCHECK(r);return *r;}
std::size_t count(const SceneReport& r,K k){std::size_t n=0;for(const auto& e:r.events)n+=e.kind==k;return n;}
std::size_t count(const TimeReport& r,TimeEventKind k){std::size_t n=0;for(const auto& e:r.events)n+=e.kind==k;return n;}
const SceneEvent* first(const SceneReport& r,K k){for(const auto& e:r.events)if(e.kind==k)return &e;return nullptr;}
Ids cues(const SceneReport& r){Ids out;for(const auto& e:r.events)if(e.kind==K::Cue)out.push_back(e.id);return out;}
void append(Ids& a,const Ids& b){a.insert(a.end(),b.begin(),b.end());}
SceneCommand cmd(uint64_t seq,C k,int64_t v=0,SceneBindings b={}){return {seq,k,v,std::move(b)};}
SceneInput commands(std::vector<SceneCommand> c){SceneInput in;in.commands=std::move(c);return in;}
CueDef cue(int64_t id,uint32_t at,CueKind k,int64_t ref=1,int64_t actor=0,int64_t track=1){CueDef c;c.id=id;c.at=at;c.kind=k;c.ref=ref;c.actor=actor;c.track=track;c.essential=k==CueKind::Event;return c;}
WaitDef line(int64_t c){WaitDef w;w.kind=WaitKind::LineFinished;w.cue=c;return w;}
WaitDef marker(int64_t c,int64_t m){WaitDef w;w.kind=WaitKind::Marker;w.cue=c;w.marker=m;return w;}
WaitDef inPosition(Ids p={}){WaitDef w;w.kind=WaitKind::InPosition;w.participants=std::move(p);return w;}
SyncDef sync(int64_t id,uint32_t at,uint32_t timeout,std::vector<WaitDef> w={},bool checkpoint=false){SyncDef s;s.id=id;s.at=at;s.timeout=timeout;s.waits=std::move(w);s.checkpoint=checkpoint;return s;}
QteDef press(int64_t id,uint32_t at,uint32_t open,uint32_t close,int64_t onSuccess=0,int64_t onFail=0){QteDef q;q.id=id;q.at=at;q.button=1;q.open=open;q.close=close;q.onSuccess=onSuccess;q.onFail=onFail;return q;}
SceneDef scene(int64_t id,uint32_t length,int64_t domain=0){SceneDef d;d.id=id;d.length=length;d.domain=domain;return d;}
SceneLibrary library(std::vector<SceneDef> scenes){
    SceneLibrary l;l.presets[1]={1,hitStop(kDomainWorld,6)};l.presets[2]={2,slowMotion(kDomainCinematic,{1,2},10)};
    for(auto& d:scenes){std::sort(d.cues.begin(),d.cues.end(),[](const auto& a,const auto& b){return std::tuple(a.at,a.track,a.id)<std::tuple(b.at,b.track,b.id);});l.scenes[d.id]=d;}
    indexTriggers(l);if(auto e=sceneLibraryError(l);!e.empty())throw std::runtime_error(e);return l;
}
void timeExact(){
    auto c=defaultTimeConfig();TimeState s;TimeInput in;
    in.add={slowMotion(kDomainWorld,{1,2},0),slowMotion(kDomainCinematic,{1,3},0),slowMotion(kDomainUi,{7,16},0)};
    auto r=tstep(c,s,0,in);uint64_t world=domainSteps(r,kDomainWorld),player=domainSteps(r,kDomainPlayer),cinematic=domainSteps(r,kDomainCinematic),ui=domainSteps(r,kDomainUi);
    const uint32_t N=300000;
    for(uint32_t t=1;t<N;++t){auto x=tstep(c,s,t);world+=domainSteps(x,kDomainWorld);player+=domainSteps(x,kDomainPlayer);cinematic+=domainSteps(x,kDomainCinematic);ui+=domainSteps(x,kDomainUi);}
    CCHECK(world==N/2&&player==N/2&&cinematic==N/3&&ui==uint64_t(N)*7/16);
    CCHECK(s.clocks[kDomainWorld].local==N/2&&s.clocks[kDomainCinematic].local==N/3&&s.clocks[kDomainWorld].units==kTimeUnit/2);
    // A tick gap integrates exactly like stepping every tick.
    auto a=s,b=s;uint64_t perTick=0;for(uint32_t t=N;t<N+1000;++t)perTick+=domainSteps(tstep(c,a,t),kDomainCinematic);
    auto gap=tstep(c,b,N+999);CCHECK(perTick==domainSteps(gap,kDomainCinematic)&&save(a)==save(b)&&cinematicHash(a)==cinematicHash(b));
    // Nested: player (child of world) multiplies its own 1/2 by the world's 1/2.
    TimeState n;TimeInput m;m.add={slowMotion(kDomainWorld,{1,2},0),{0,kDomainPlayer,{1,2},0,0,TimeBlend::Multiply}};uint64_t quarter=0;
    for(uint32_t t=0;t<1000;++t)quarter+=domainSteps(tstep(c,n,t,t?TimeInput{}:m),kDomainPlayer);CCHECK(quarter==250);
    // Fast forward 3x and a non-unit denominator stay exact.
    TimeState f;TimeInput fast;fast.add={slowMotion(kDomainUi,{3,1},0),slowMotion(kDomainWorld,{5,7},0)};uint64_t ff=0,sevenths=0;
    for(uint32_t t=0;t<7000;++t){auto x=tstep(c,f,t,t?TimeInput{}:fast);ff+=domainSteps(x,kDomainUi);sevenths+=domainSteps(x,kDomainWorld);}
    CCHECK(ff==21000&&sevenths==5000);
    roundtrip(s);roundtrip(f);
}
void timeStack(){
    auto c=defaultTimeConfig();TimeState s;TimeInput a;a.add={{7,kDomainWorld,{1,2},10,0,TimeBlend::Override}};
    auto r=tstep(c,s,10,a);CCHECK(count(r,TimeEventKind::Started)==1&&r.events[0].request==1&&domainSteps(r,kDomainWorld)==0);
    TimeInput h;h.add={hitStop(kDomainWorld,6)};r=tstep(c,s,11,h);CCHECK(domainSteps(r,kDomainWorld)==0&&domainSteps(r,kDomainUi)==1);
    uint64_t frozen=0;for(uint32_t t=12;t<=16;++t)frozen+=domainSteps(tstep(c,s,t),kDomainWorld);CCHECK(frozen==0);
    r=tstep(c,s,17);CCHECK(count(r,TimeEventKind::Expired)==1&&r.events[0].request==2&&domainSteps(r,kDomainWorld)==1); // 1/2 resumes, remainder kept
    TimeInput m;m.add={{7,kDomainWorld,{1,2},20,0,TimeBlend::Multiply},{0,kDomainWorld,{2,1},5,0,TimeBlend::Override}};
    uint64_t quarter=0;for(uint32_t t=18;t<=25;++t)quarter+=domainSteps(tstep(c,s,t,t==18?m:TimeInput{}),kDomainWorld);
    CCHECK(quarter==2&&domainUnits(s,kDomainWorld)==kTimeUnit/4); // priority 5 override loses to priority 10, priority 20 multiplies
    TimeInput x;x.cancelTags={7};r=tstep(c,s,26,x);CCHECK(count(r,TimeEventKind::Cancelled)==2&&domainSteps(r,kDomainWorld)==2);
    x={};x.cancel={4};r=tstep(c,s,27,x);CCHECK(count(r,TimeEventKind::Cancelled)==1&&domainSteps(r,kDomainWorld)==1&&s.requests.empty());
    // Expiry inside a gap: hit-stop active for ticks 5..14, the jump to 20 counts only 15..20.
    TimeState g;tstep(c,g,0);TimeInput hs;hs.add={hitStop(kDomainWorld,10)};tstep(c,g,5,hs);
    r=tstep(c,g,20);CCHECK(domainSteps(r,kDomainWorld)==6&&count(r,TimeEventKind::Expired)==1&&domainSteps(r,kDomainUi)==15);
    auto before=save(g);TimeInput bad;bad.add={{0,kDomainWorld,{1,17},0,0,TimeBlend::Override}};
    CCHECK(!stepTime(c,g,21,bad)&&!stepTime(c,g,20,{})&&!stepTime(c,g,19,{}));bad.add={{0,9,{1,2},0,0,TimeBlend::Override}};CCHECK(!stepTime(c,g,21,bad));
    bad.add={{-1,kDomainWorld,{1,2},0,0,TimeBlend::Override}};CCHECK(!stepTime(c,g,21,bad)&&save(g)==before);
    TimeInput flood;for(int i=0;i<300;++i)flood.add.push_back(slowMotion(kDomainUi,{1,1},0));CCHECK(!stepTime(c,g,21,flood));
    flood.add.resize(256);r=tstep(c,g,21,flood);flood.add.resize(1);r=tstep(c,g,22,flood);CCHECK(count(r,TimeEventKind::Rejected)==1&&g.requests.size()==256);
    auto wrong=c;wrong.domains[kDomainPlayer].parent=kDomainCinematic;CCHECK(timeConfigError(wrong).find("domain 2.parent")!=std::string::npos&&!stepTime(wrong,s,30,{}));
    wrong=c;wrong.domains[kDomainUi].base={1,17};CCHECK(timeConfigError(wrong).find("domain 3.base")!=std::string::npos);
    auto other=c;other.domains[9]={9,0,{1,1},"extra"};CCHECK(!stepTime(other,s,30,{})); // saved clocks must match the domains
    roundtrip(g);SceneState wrongKind;CCHECK(!load(save(g),wrongKind));
}
void cueOrder(){
    auto d=scene(10,50);
    d.cues={cue(1,0,CueKind::Camera,300,0,2),cue(2,0,CueKind::Dialogue,9001),cue(3,5,CueKind::Sound,1),cue(4,10,CueKind::Event,5),
        cue(5,20,CueKind::Dialogue,9002),cue(6,30,CueKind::Event,6),cue(7,40,CueKind::Sound,2)};
    d.syncs={sync(1,10,100,{line(2)}),sync(2,30,100,{line(5)})};
    auto l=library({d});SceneState s;Ids order;
    auto r=run(l,s,1,commands({cmd(1,C::TriggerScene,10)}));CCHECK(s.phase==ScenePhase::Pending&&count(r,K::Triggered)==1&&cues(r).empty());
    r=run(l,s,2);CCHECK(s.phase==ScenePhase::Playing&&count(r,K::Started)==1&&cues(r)==Ids{2,1});append(order,cues(r));
    for(uint32_t t=3;t<=12;++t){r=run(l,s,t);append(order,cues(r));}
    CCHECK(s.phase==ScenePhase::Waiting&&s.position==10&&first(r,K::SyncReached)->id==1&&order==Ids({2,1,3}));
    SceneInput done;done.finished={{2,13}};r=run(l,s,13,done);append(order,cues(r));
    CCHECK(first(r,K::SyncReleased)->value==0&&cues(r)==Ids{4}&&s.position==11);
    r=run(l,s,14,commands({cmd(2,C::Skip)}));append(order,cues(r)); // skip to sync 2: dialogue 5 is skipped, its wait is satisfied
    CCHECK(first(r,K::Skipped)->id==2&&r.cuesSkipped==1&&first(r,K::SyncReleased)->id==2&&cues(r)==Ids{6}&&s.position==31);
    CCHECK(r.timeCancelTags==Ids{10});
    r=run(l,s,15,commands({cmd(2,C::Pause),cmd(3,C::Seek,999)}));CCHECK(count(r,K::CommandIgnored)==2&&!s.paused);
    for(uint32_t t=16;t<=40&&s.phase!=ScenePhase::Idle;++t){r=run(l,s,t);append(order,cues(r));if(t==33)CCHECK(count(r,K::Finished)==1);}
    CCHECK(order==Ids({2,1,3,4,6,7})&&s.phase==ScenePhase::Idle&&s.lastOutcome==SceneOutcome::Completed&&s.lastScene==10);
    // Backward seek replays presentation cues once more; essential cues never fire twice.
    SceneState b;run(l,b,1,commands({cmd(1,C::TriggerScene,10)}));
    for(uint32_t t=2;t<=36;++t){SceneInput in;if(t==5)in.finished={{2,5}};if(t==30)in.finished={{5,30}};run(l,b,t,in);}
    CCHECK(b.position==34&&b.essentialDone==Ids({4,6}));
    r=run(l,b,37,commands({cmd(2,C::Seek,0)}));Ids replay=cues(r);CCHECK(first(r,K::Seeked)->value==0&&replay==Ids({2,1})&&b.position==1);
    for(uint32_t t=38;b.phase!=ScenePhase::Idle&&t<200;++t){SceneInput in;if(t==38)in.finished={{2,38}};if(t==57)in.finished={{5,57}};r=run(l,b,t,in);append(replay,cues(r));}
    CCHECK(replay==Ids({2,1,3,5,7})&&b.lastOutcome==SceneOutcome::Completed);
    // Unskippable scenes ignore skip and forward seeks but still allow backward seeks.
    d.skippable=false;auto locked=library({d});SceneState u;run(locked,u,1,commands({cmd(1,C::TriggerScene,10)}));run(locked,u,2);run(locked,u,5);
    r=run(locked,u,6,commands({cmd(2,C::Skip),cmd(3,C::SkipScene),cmd(4,C::Seek,40),cmd(5,C::Seek,0)}));CCHECK(count(r,K::CommandIgnored)==3&&count(r,K::Seeked)==1&&u.position==1);
}
void syncTimeouts(){
    auto d=scene(20,20);d.cues={cue(1,0,CueKind::Dialogue,9001),cue(2,0,CueKind::Animation,40,0,2)};d.syncs={sync(1,5,10,{line(1),marker(2,7)})};
    auto l=library({d});
    auto begin=[&](SceneState& s){run(l,s,1,commands({cmd(1,C::TriggerScene,20)}));for(uint32_t t=2;t<=7;++t)run(l,s,t);CCHECK(s.phase==ScenePhase::Waiting&&s.deadline==17);};
    SceneState s;begin(s);
    SceneInput bad;bad.finished={{1,9},{1,1}};bad.markers={{1,7,8},{2,3,8}};auto r=run(l,s,8,bad);
    CCHECK(count(r,K::ReportIgnored)==3&&s.cues[2].markers==Ids{3}&&s.cues[1].finishedAt<0); // future tick, before emission, marker on a dialogue cue
    SceneInput fin;fin.finished={{1,9}};r=run(l,s,9,fin);CCHECK(s.phase==ScenePhase::Waiting&&s.cues[1].finishedAt==9);
    for(uint32_t t=10;t<17;++t){r=run(l,s,t);CCHECK(count(r,K::SyncReleased)==0);}
    r=run(l,s,17);CCHECK(first(r,K::SyncReleased)->value==1&&s.position==6); // host never reported marker 7: timeout releases
    SceneState m;begin(m);SceneInput both;both.finished={{1,8}};both.markers={{2,7,12}};run(l,m,8,both);for(uint32_t t=9;t<12;++t)run(l,m,t);r=run(l,m,12,both);
    CCHECK(first(r,K::SyncReleased)->value==0&&m.position==6);
    // Pausing shifts the deadline by the paused ticks.
    SceneState p;begin(p);run(l,p,8);run(l,p,9);run(l,p,10,commands({cmd(2,C::Pause)}));for(uint32_t t=11;t<20;++t)run(l,p,t);
    r=run(l,p,20,commands({cmd(3,C::Unpause)}));CCHECK(count(r,K::Unpaused)==1&&p.deadline==27);
    for(uint32_t t=21;t<27;++t)CCHECK(count(run(l,p,t),K::SyncReleased)==0);CCHECK(first(run(l,p,27),K::SyncReleased)->value==1);
    // A frozen domain (hit-stop) cannot stall a wait: timeouts are authoritative ticks.
    auto f=scene(21,10,kDomainCinematic);f.cues={cue(1,0,CueKind::Dialogue,7)};f.syncs={sync(1,1,5,{line(1)})};auto fl=library({f});SceneState z;
    SceneInput frozen;frozen.domainSteps[kDomainCinematic]=0;frozen.commands={cmd(1,C::TriggerScene,21)};run(fl,z,1,frozen);frozen.commands.clear();
    r=run(fl,z,2,frozen);CCHECK(z.phase==ScenePhase::Playing&&cues(r)==Ids{1}&&z.position==0);
    frozen.domainSteps[kDomainCinematic]=1;run(fl,z,3,frozen);CCHECK(z.phase==ScenePhase::Waiting&&z.deadline==8);
    frozen.domainSteps[kDomainCinematic]=0;for(uint32_t t=4;t<8;++t)CCHECK(count(run(fl,z,t,frozen),K::SyncReleased)==0);
    r=run(fl,z,8,frozen);CCHECK(first(r,K::SyncReleased)->value==1&&z.position==1);
    frozen.domainSteps[kDomainCinematic]=3;run(fl,z,9,frozen);CCHECK(z.position==4);
}
void qteJudging(){
    QteDef p=press(1,0,5,10);p.perfectOpen=7;p.perfectClose=8;QteOptions o;
    auto judge=[&](const QteDef& q,const QteOptions& opt,std::vector<QteInput> in,uint32_t tick){auto g=startQte(q,opt,100);return judgeQte(q,g,in,tick);};
    CCHECK(judge(p,o,{{104,1,true}},104)==QteOutcome::Fail&&judge(p,o,{{105,1,true}},105)==QteOutcome::Success&&judge(p,o,{{107,1,true}},107)==QteOutcome::Perfect);
    CCHECK(judge(p,o,{},109)==QteOutcome::None&&judge(p,o,{},110)==QteOutcome::Fail&&judge(p,o,{{110,1,true}},110)==QteOutcome::Success);
    CCHECK(judge(p,o,{{100,1,true}},101)==QteOutcome::None&&judge(p,o,{{105,2,true}},105)==QteOutcome::None&&judge(p,o,{{106,1,false}},106)==QteOutcome::None);
    auto strict=p;strict.strict=true;CCHECK(judge(strict,o,{{105,2,true}},105)==QteOutcome::Fail);
    QteOptions easy;easy.windowScaleMilli=2000;auto g=startQte(p,easy,0);CCHECK(g.open==2&&g.close==13&&g.perfectOpen==6&&g.perfectClose==9);
    QteOptions hard;hard.windowScaleMilli=500;g=startQte(p,hard,0);CCHECK(g.open==6&&g.close==8);
    QteOptions access;access.windowScaleMilli=500;access.accessibleWindowMilli=2000;g=startQte(p,access,0);CCHECK(g.open==5&&g.close==10); // extension undoes hard
    QteDef mash;mash.id=2;mash.kind=QteKind::Mash;mash.button=2;mash.count=5;mash.close=20;
    CCHECK(judge(mash,o,{{101,2,true},{102,2,true},{103,2,true},{104,2,true}},104)==QteOutcome::None);
    CCHECK(judge(mash,o,{{101,2,true},{102,2,true},{103,2,true},{104,2,true},{105,2,true}},105)==QteOutcome::Success);
    CCHECK(judge(mash,o,{{101,2,true},{102,2,true}},120)==QteOutcome::Fail);
    QteOptions half;half.mashScaleMilli=500;half.holdScaleMilli=500;CCHECK(startQte(mash,half,0).count==3&&judge(mash,half,{{101,2,true},{102,2,true},{103,2,true}},103)==QteOutcome::Success);
    QteDef hold;hold.id=3;hold.kind=QteKind::Hold;hold.button=3;hold.hold=10;hold.close=5;
    CCHECK(judge(hold,o,{{103,3,true}},112)==QteOutcome::None&&judge(hold,o,{{103,3,true}},113)==QteOutcome::Success);
    CCHECK(judge(hold,o,{{103,3,true},{108,3,false}},108)==QteOutcome::Fail&&judge(hold,o,{{106,3,true}},106)==QteOutcome::Fail&&judge(hold,o,{},105)==QteOutcome::Fail);
    CCHECK(judge(hold,o,{{103,3,true},{113,3,false}},113)==QteOutcome::Success&&judge(hold,half,{{103,3,true}},108)==QteOutcome::Success);
    QteDef seq;seq.id=4;seq.kind=QteKind::Sequence;seq.steps={{1,10},{2,10},{3,10}};
    CCHECK(judge(seq,o,{{105,1,true},{112,2,true},{125,3,true}},125)==QteOutcome::Fail&&judge(seq,o,{{105,1,true},{112,2,true},{120,3,true}},120)==QteOutcome::Success);
    CCHECK(judge(seq,o,{{105,1,true},{106,3,true}},106)==QteOutcome::None&&judge(seq,o,{{105,1,true}},115)==QteOutcome::Fail);
    // In a scene: fail branches back to sync 1 (retry), success on the second QTE branches forward over cues.
    auto d=scene(30,40);d.syncs={sync(1,5,1),sync(2,30,1)};d.qtes={press(1,10,1,5,0,1),press(2,20,1,5,2,0)};
    d.cues={cue(1,25,CueKind::Sound,5),cue(2,25,CueKind::Event,9,0,2),cue(3,35,CueKind::Sound,6)};auto l=library({d});
    SceneState s;run(l,s,1,commands({cmd(1,C::TriggerScene,30)}));SceneReport r;
    for(uint32_t t=2;t<=12;++t)r=run(l,s,t);CCHECK(s.phase==ScenePhase::Qte&&first(r,K::QtePrompt)->id==1&&s.qte.start==12);
    for(uint32_t t=13;t<17;++t)run(l,s,t);
    r=run(l,s,17);CCHECK(first(r,K::QteResolved)->value==int64_t(QteOutcome::Fail)&&first(r,K::Branch)->id==1&&count(r,K::SyncReached)==1&&s.position==6);
    for(uint32_t t=18;t<=21;++t)r=run(l,s,t);CCHECK(s.phase==ScenePhase::Qte&&s.qte.start==21);
    SceneInput hit;hit.qte={{23,1,true}};run(l,s,22);r=run(l,s,23,hit);CCHECK(qteResult(s,1)==QteOutcome::Success&&count(r,K::Branch)==0&&s.position==11);
    for(uint32_t t=24;t<=32;++t)r=run(l,s,t);CCHECK(s.phase==ScenePhase::Qte&&s.qte.qte==2);
    hit.qte={{34,1,true}};run(l,s,33);r=run(l,s,34,hit);
    CCHECK(first(r,K::Branch)->id==2&&r.cuesSkipped==1&&cues(r)==Ids{2}&&first(r,K::Cue)->flags==1&&s.position==31&&qteResult(s,2)==QteOutcome::Success);
    // Accessibility: auto-succeed resolves every prompt immediately (the forward branch still applies).
    SceneState a;SceneInput opt=commands({cmd(1,C::TriggerScene,30)});QteOptions autoWin;autoWin.autoSucceed=true;opt.options=autoWin;run(l,a,1,opt);
    std::size_t prompts=0,auto_=0;for(uint32_t t=2;a.phase!=ScenePhase::Idle&&t<100;++t){r=run(l,a,t);prompts+=count(r,K::QtePrompt);for(const auto& e:r.events)auto_+=e.kind==K::QteResolved&&e.value==int64_t(QteOutcome::AutoSuccess);}
    CCHECK(prompts==2&&auto_==2&&a.lastOutcome==SceneOutcome::Completed&&a.results.at(2)==QteOutcome::AutoSuccess);
    // Extended windows: the scene stores the scaled window at the prompt.
    SceneState e;opt.options=access;opt.options->windowScaleMilli=1000;run(l,e,1,opt);for(uint32_t t=2;t<=12;++t)run(l,e,t);CCHECK(e.phase==ScenePhase::Qte&&e.qte.close==10&&e.qte.open==1);
    for(uint32_t t=13;t<22;++t)run(l,e,t);CCHECK(e.phase==ScenePhase::Qte);r=run(l,e,22);CCHECK(first(r,K::QteResolved)->value==int64_t(QteOutcome::Fail));
}
SceneDef stagedScene(){
    auto d=scene(40,10);d.marks[1]={1,{{},{},2_fx},Fixed::fromRatio(1,2)};d.marks[2]={2,{3_fx,{},{}},Fixed::fromRatio(1,2)};
    d.participants[1]={1,1,true,5,StagingFallback::Teleport};d.participants[2]={2,2,false,5,StagingFallback::Proceed};d.participants[3]={3,0,true,6,StagingFallback::Teleport};
    d.cues={cue(1,4,CueKind::Move,1,3),cue(2,6,CueKind::InputRelease,0,1)};d.syncs={sync(1,8,20,{inPosition({3})})};return d;
}
void staging(){
    auto l=library({stagedScene()});SceneState s;
    run(l,s,1,commands({cmd(1,C::TriggerScene,40,{{1,100},{2,200}})}));
    auto r=run(l,s,2);CCHECK(s.phase==ScenePhase::Staging&&count(r,K::InputLocked)==2&&count(r,K::MoveToMark)==2&&first(r,K::InputLocked)->entity==100);
    CCHECK(inputLocked(s,1)&&inputLocked(s,3)&&!inputLocked(s,2)&&lockedEntities(s)==Ids({3,100}));
    SceneInput near;near.poses={{1,{{},{},Fixed::fromRatio(7,4)}},{2,{{},{},{}}}};r=run(l,s,3,near);
    CCHECK(first(r,K::InPosition)->id==1&&s.participants[1].status==StageStatus::InPosition&&s.participants[2].status==StageStatus::Moving);
    for(uint32_t t=4;t<7;++t)run(l,s,t);
    r=run(l,s,7);CCHECK(first(r,K::StagingLate)->id==2&&s.phase==ScenePhase::Playing&&s.position==0); // proceed fallback, staging done
    for(uint32_t t=8;t<=11;++t)r=run(l,s,t);CCHECK(first(r,K::MoveToMark)->id==3&&first(r,K::MoveToMark)->ref==1&&s.participants[3].deadline==17);
    run(l,s,12);r=run(l,s,13);CCHECK(first(r,K::InputReleased)->id==1&&!inputLocked(s,1));
    run(l,s,14);r=run(l,s,15);CCHECK(s.phase==ScenePhase::Waiting);run(l,s,16);
    r=run(l,s,17);CCHECK(first(r,K::TeleportRequested)->id==3&&first(r,K::SyncReleased)->value==0&&s.participants[3].status==StageStatus::Teleported);
    r=run(l,s,18);CCHECK(count(r,K::Finished)==1&&count(r,K::InputReleased)==1&&lockedEntities(s).empty());
    // Teleport fallback at the staging timeout and the arrived shortcut.
    SceneState t;run(l,t,1,commands({cmd(1,C::TriggerScene,40)}));run(l,t,2);SceneInput arrived;arrived.arrived={2};run(l,t,3,arrived);
    for(uint32_t k=4;k<7;++k)run(l,t,k);r=run(l,t,7);CCHECK(first(r,K::TeleportRequested)->id==1&&t.phase==ScenePhase::Playing&&t.participants[2].status==StageStatus::InPosition);
    // Abort fallback releases every lock.
    auto d=stagedScene();d.participants[1].fallback=StagingFallback::Abort;auto la=library({d});SceneState a;run(la,a,1,commands({cmd(1,C::TriggerScene,40)}));run(la,a,2);
    for(uint32_t k=3;k<7;++k)run(la,a,k);r=run(la,a,7);CCHECK(count(r,K::Aborted)==1&&count(r,K::InputReleased)==2&&a.phase==ScenePhase::Idle&&a.lastOutcome==SceneOutcome::Aborted);
    // Skipping staging teleports everyone still moving.
    SceneState k;run(l,k,1,commands({cmd(1,C::TriggerScene,40)}));run(l,k,2);r=run(l,k,3,commands({cmd(2,C::Skip)}));
    CCHECK(count(r,K::TeleportRequested)==2&&k.phase==ScenePhase::Playing&&count(r,K::Skipped)==1);
    SceneState q;run(l,q,1,commands({cmd(1,C::TriggerScene,40)}));run(l,q,2);r=run(l,q,3,commands({cmd(2,C::SkipScene)}));
    CCHECK(count(r,K::Finished)==1&&first(r,K::Finished)->value==int64_t(SceneOutcome::Skipped)&&count(r,K::Cue)==2&&lockedEntities(q).empty());
}
void checkpoints(){
    auto d=scene(50,30);d.checkpointBefore=d.checkpointAfter=true;
    d.cues={cue(1,0,CueKind::Dialogue,9001),cue(2,12,CueKind::Event,3),cue(3,14,CueKind::Sound,8),cue(4,20,CueKind::Camera,4)};
    d.syncs={sync(1,10,50,{line(1)},true)};auto l=library({d});SceneState s;
    auto r=run(l,s,1,commands({cmd(1,C::TriggerScene,50)}));CCHECK(first(r,K::SnapshotRequested)->value==0&&s.phase==ScenePhase::Pending);
    auto pending=save(s);
    for(uint32_t t=2;t<=12;++t){SceneInput in;if(t==5)in.finished={{1,5}};r=run(l,s,t,in);}
    CCHECK(first(r,K::SnapshotRequested)->value==1&&first(r,K::SnapshotRequested)->id==1&&s.lastSync==1);
    for(uint32_t t=13;t<=18;++t)run(l,s,t);CCHECK(s.position==16&&s.essentialDone==Ids{2});
    auto mid=save(s);
    // Load after a crash: resume at the last sync point; presentation cues replay, the essential event does not.
    SceneState back;CCHECK(load(mid,back));r=run(l,back,19,commands({cmd(2,C::ResumeCheckpoint)}));
    CCHECK(first(r,K::Resumed)->id==1&&count(r,K::SyncReached)==1&&count(r,K::SyncReleased)==1&&back.position==11);
    Ids replay;std::vector<int64_t> snapshots;
    for(uint32_t t=20;back.phase!=ScenePhase::Idle&&t<100;++t){r=run(l,back,t);append(replay,cues(r));for(const auto& e:r.events)if(e.kind==K::SnapshotRequested)snapshots.push_back(e.value);}
    CCHECK(replay==Ids({3,4})&&snapshots==std::vector<int64_t>{2}&&back.lastOutcome==SceneOutcome::Completed);
    // A save taken at the before-snapshot starts the scene normally after load.
    SceneState p;CCHECK(load(pending,p));r=run(l,p,2);CCHECK(count(r,K::Started)==1&&cues(r)==Ids{1});
    roundtrip(s);roundtrip(s.participants.empty()?ParticipantState{7,1,StageStatus::Moving,4,true}:s.participants.begin()->second);
    SceneState waiting;CCHECK(load(pending,waiting));run(l,waiting,2);run(l,waiting,3);
    for(uint32_t t=4;t<=12;++t)run(l,waiting,t);CCHECK(waiting.phase==ScenePhase::Waiting);roundtrip(waiting);
}
void triggersAndQueue(){
    auto a=scene(60,5);a.trigger=7001;a.cues={cue(1,0,CueKind::Event,1)};auto b=scene(61,3);b.trigger=7002;b.cues={cue(1,0,CueKind::Event,2)};
    auto l=library({a,b});CCHECK(sceneForCue(l,7001)==60&&sceneForCue(l,7002)==61&&sceneForCue(l,5)==0);
    SceneState s;auto r=run(l,s,1,commands({cueTrigger(1,7001),cueTrigger(2,7002),cueTrigger(3,99),cmd(4,C::TriggerScene,60,{{9,1}})}));
    CCHECK(count(r,K::Triggered)==1&&count(r,K::Queued)==1&&count(r,K::TriggerIgnored)==2&&s.queue.size()==1);
    std::vector<int64_t> finished,started;
    for(uint32_t t=2;t<30&&(s.phase!=ScenePhase::Idle||!s.queue.empty());++t){r=run(l,s,t);for(const auto& e:r.events){if(e.kind==K::Finished)finished.push_back(e.scene);if(e.kind==K::Started)started.push_back(e.scene);}}
    CCHECK(finished==std::vector<int64_t>({60,61})&&started==finished&&s.lastScene==61);
    SceneState full;run(l,full,1,commands({cueTrigger(1,7001)}));std::vector<SceneCommand> many;for(uint64_t i=2;i<12;++i)many.push_back(cueTrigger(i,7002));
    r=run(l,full,2,commands(many));CCHECK(full.queue.size()==kSceneMaxQueue&&count(r,K::TriggerIgnored)==2);
}
// Full host loop on the example library: time domains feed the scene, scene time requests feed the next tick.
struct Host {TimeState time;SceneState scene;TimeInput pending;};
uint64_t hostTick(const SceneLibrary& l,Host& h,uint32_t t){
    auto tr=stepTime(l.time,h.time,t,h.pending);CCHECK(tr);
    SceneInput in;in.domainSteps=sceneDomainSteps(*tr);
    if(t==1)in.commands={cueTrigger(1,5001,{{1,100},{2,200}})};
    in.poses={{1,{{},{},Fixed::fromRatio(std::min<int64_t>(2000,int64_t(t)*100),1000)}}};
    for(const auto& [id,rec]:h.scene.cues)if(!rec.skipped&&rec.finishedAt<0){
        if(t>=rec.emittedAt+40)in.finished.push_back({id,t});
        else if(t>=rec.emittedAt+30)in.markers.push_back({id,7,t});
    }
    // The player reacts to the authoritative prompt; button 3 is noise that non-strict QTEs ignore.
    if(t%11==0)in.qte.push_back({t,3,true});
    if(h.scene.phase==ScenePhase::Qte&&h.scene.qte.qte==1&&t==h.scene.qte.start+6)in.qte.push_back({t,1,true});
    if(h.scene.phase==ScenePhase::Qte&&h.scene.qte.qte==2&&t>h.scene.qte.start)in.qte.push_back({t,2,true});
    auto r=stepScene(l,h.scene,t,in);CCHECK(r);
    h.pending={};h.pending.add=r->timeAdd;h.pending.cancelTags=r->timeCancelTags;
    return cinematicHash(*r)^(cinematicHash(*tr)*31);
}
void saveRestore(const std::string& root){
    auto l=readSceneLibrary(Json::parseFile(root+"/examples/libraries/genre/scene.json"));
    Host a;std::vector<uint64_t> hashes;std::string timeMid,sceneMid;TimeInput pendingMid;std::size_t qtes=0;uint64_t slow=0;
    for(uint32_t t=1;t<=600;++t){
        hashes.push_back(hostTick(l,a,t));
        for(const auto& r:a.time.requests)slow+=r.request.domain==kDomainCinematic;
        if(t==150){timeMid=save(a.time);sceneMid=save(a.scene);pendingMid=a.pending;CCHECK(a.scene.phase!=ScenePhase::Idle);}
    }
    qtes=a.scene.results.size();
    CCHECK(a.scene.phase==ScenePhase::Idle&&a.scene.lastScene==10&&a.scene.lastOutcome==SceneOutcome::Completed&&qtes==2&&slow>0);
    Host b;CCHECK(load(timeMid,b.time)&&load(sceneMid,b.scene));b.pending=pendingMid;
    for(uint32_t t=151;t<=600;++t)CCHECK(hostTick(l,b,t)==hashes[t-1]);
    CCHECK(save(a.scene)==save(b.scene)&&save(a.time)==save(b.time)&&cinematicHash(a.scene)==cinematicHash(b.scene));
    Host c;for(uint32_t t=1;t<=600;++t)CCHECK(hostTick(l,c,t)==hashes[t-1]);
    CCHECK(sceneLibraryHash(l)==sceneLibraryHash(readSceneLibrary(Json::parseFile(root+"/examples/libraries/genre/scene.json"))));
    auto changed=l;changed.scenes[10].version=2;CCHECK(sceneLibraryHash(changed)!=sceneLibraryHash(l));
    SceneState mid;CCHECK(load(sceneMid,mid));CCHECK(!stepScene(changed,mid,151,{})); // saved against version 1
}
void invalidData(const std::string& root){
    auto l=readSceneLibrary(Json::parseFile(root+"/examples/libraries/genre/scene.json"));
    CCHECK(l.scenes.size()==2&&sceneForCue(l,5001)==10&&sceneForCue(l,5002)==11&&l.presets.at(3).request.blend==TimeBlend::Multiply&&l.time.domains.size()==4);
    const auto& s10=l.scenes.at(10);CCHECK(s10.cues.front().id==1&&s10.cues[1].id==2&&s10.cues[4].essential&&!s10.cues[0].essential&&s10.qtes[0].perfectClose==18);
    CCHECK(s10.marks.at(2).tolerance==Fixed::fromRatio(3,4)&&s10.participants.at(2).fallback==StagingFallback::Proceed&&s10.syncs[0].waits.size()==3);
    auto rejects=[](const std::string& text,const std::string& expected){
        try{readSceneLibrary(Json::parse(text));}catch(const JsonError& e){if(std::string(e.what()).find(expected)!=std::string::npos)return true;std::cerr<<"got: "<<e.what()<<'\n';return false;}
        std::cerr<<"accepted: "<<text<<'\n';return false;
    };
    auto sc=[](const std::string& body){return R"({"scenes":[{"id":1,"length":10,)"+body+"}]}";};
    CCHECK(rejects(R"({"scene":[]})","cinematic: unknown key 'scene'"));
    CCHECK(rejects(sc(R"("cues":[{"id":1,"at":0,"kind":"explode"}])"),"cinematic.scenes[0].cues[0].kind: unknown value 'explode'"));
    CCHECK(rejects(sc(R"("cues":[{"id":1,"at":11,"kind":"event","ref":1}])"),"cinematic.scene 1.cue 1.at: must be 0..length 10"));
    CCHECK(rejects(sc(R"("cues":[{"id":1,"at":1,"kind":"event","ref":1},{"id":1,"at":2,"kind":"event","ref":1}])"),"cinematic.scenes[0].cues[1].id: duplicate id 1"));
    CCHECK(rejects(sc(R"("cues":[{"id":1,"at":1,"kind":"move","ref":1}])"),"cinematic.scene 1.cue 1.actor: unknown participant 0"));
    CCHECK(rejects(sc(R"("cues":[{"id":1,"at":1,"kind":"timeScale","ref":4}])"),"cinematic.scene 1.cue 1.ref: unknown time preset 4"));
    CCHECK(rejects(sc(R"("cues":[{"id":1,"at":1,"kind":"event","ref":1,"essential":1}])"),"cinematic.scenes[0].cues[0].essential: expected boolean"));
    CCHECK(rejects(sc(R"("syncs":[{"id":1,"at":5,"timeout":0}])"),"cinematic.scenes[0].syncs[0].timeout: must be in 1..1000000"));
    CCHECK(rejects(sc(R"("syncs":[{"id":1,"at":5,"wait":[{"kind":"lineFinished","cue":9}]}])"),"cinematic.scene 1.sync 1.wait[0].cue: unknown dialogue/animation cue 9"));
    CCHECK(rejects(sc(R"("cues":[{"id":2,"at":6,"kind":"dialogue","ref":3}],"syncs":[{"id":1,"at":5,"wait":[{"kind":"lineFinished","cue":2}]}])"),"must start before the sync point"));
    CCHECK(rejects(sc(R"("cues":[{"id":2,"at":1,"kind":"dialogue","ref":3}],"syncs":[{"id":1,"at":5,"wait":[{"kind":"marker","cue":2,"marker":1}]}])"),"wait[0].cue: unknown animation cue 2"));
    CCHECK(rejects(sc(R"("syncs":[{"id":1,"at":5,"wait":[{"kind":"inPosition"}]}])"),"cinematic.scene 1.sync 1.wait[0]: scene has no participants"));
    CCHECK(rejects(sc(R"("syncs":[{"id":1,"at":5,"wait":[{"kind":"lineFinished","cue":1,"participants":[1]}]}])"),"wait[0].participants: only for inPosition"));
    CCHECK(rejects(sc(R"("syncs":[{"id":1,"at":2}],"qtes":[{"id":1,"at":5,"kind":"press","button":1,"onSuccess":1}])"),"cinematic.scene 1.qte 1.onSuccess: must branch forward"));
    CCHECK(rejects(sc(R"("qtes":[{"id":1,"at":5,"kind":"press","button":1,"onFail":4}])"),"cinematic.scene 1.qte 1.onFail: unknown sync point 4"));
    CCHECK(rejects(sc(R"("qtes":[{"id":1,"at":5,"kind":"press","button":1,"open":3,"close":9,"perfect":[1,2]}])"),"cinematic.scenes[0].qtes[0].perfect: must lie inside [open, close]"));
    CCHECK(rejects(sc(R"("qtes":[{"id":1,"at":5,"kind":"mash","button":1,"count":3,"hold":2}])"),"cinematic.scenes[0].qtes[0].hold: not used by kind 'mash'"));
    CCHECK(rejects(sc(R"("qtes":[{"id":1,"at":5,"kind":"sequence","steps":[]}])"),"cinematic.scenes[0].qtes[0].steps: expected at least one step"));
    CCHECK(rejects(sc(R"("qtes":[{"id":1,"at":5,"kind":"hold","button":1,"hold":0}])"),"cinematic.scenes[0].qtes[0].hold: must be in 1..100000"));
    CCHECK(rejects(sc(R"("participants":[{"id":1,"mark":3}])"),"cinematic.scenes[0].participants[0].mark: unknown mark 3"));
    CCHECK(rejects(sc(R"("participants":[{"id":1,"fallback":"wait"}])"),"cinematic.scenes[0].participants[0].fallback: unknown value 'wait' (expected teleport, proceed, abort)"));
    CCHECK(rejects(sc(R"("marks":[{"id":1,"position":[0,0]}])"),"cinematic.scenes[0].marks[0].position: expected [x,y,z]"));
    CCHECK(rejects(sc(R"("domain":9)"),"cinematic.scene 1.domain: unknown time domain 9"));
    CCHECK(rejects(R"({"scenes":[{"id":1,"length":1,"trigger":5},{"id":2,"length":1,"trigger":5}]})","cinematic.scene 2.trigger: cue 5 already starts another scene"));
    CCHECK(rejects(R"({"scenes":[{"id":1,"length":1},{"id":1,"length":2}]})","cinematic.scenes[1].id: duplicate scene"));
    CCHECK(rejects(R"({"timePresets":[{"id":1,"domain":1,"scale":[1,17]}]})","cinematic.timePresets[0].scale: ratio must be 0..16 with a denominator dividing 720720"));
    CCHECK(rejects(R"({"timePresets":[{"id":1,"domain":8,"scale":[1,2]}]})","cinematic.timePresets[0].domain: unknown time domain 8"));
    CCHECK(rejects(R"({"domains":[{"id":1,"parent":2},{"id":2}]})","cinematic.time.domain 1.parent: must be 0 or a declared domain with a smaller id"));
    CCHECK(rejects(R"({"scenes":[{"id":1}]})","cinematic.scenes[0].length: expected integer"));
    // Settings section.
    auto parse=[](const char* text){return aiverse::lib::gameplay::readGameplaySettings(Json::parse(text));};
    auto defaults=parse("{\"schemaVersion\":1}");CCHECK(defaults.value&&cinematicSettingsErrors(*defaults.value).empty());
    auto o=qteOptions(*defaults.value);CCHECK(o.windowScaleMilli==1000&&!o.autoSucceed&&hitStopRequest(*defaults.value).duration==6&&slowMotionRequest(*defaults.value).scale.den==4);
    auto relation=parse("{\"schemaVersion\":1,\"cinematic\":{\"slowMotionNumerator\":5}}");CCHECK(!relation.value&&relation.errors.at(0)=="cinematic.slowMotionNumerator: must not exceed slowMotionDenominator");
    auto unknown=parse("{\"schemaVersion\":1,\"cinematic\":{\"hitStop\":3}}");CCHECK(!unknown.value&&unknown.errors.at(0)=="cinematic.hitStop: unknown field");
    auto range=parse("{\"schemaVersion\":1,\"cinematic\":{\"hitStopTicks\":0}}");CCHECK(!range.value&&range.errors.at(0)=="cinematic.hitStopTicks: integer out of range");
    auto type=parse("{\"schemaVersion\":1,\"cinematic\":{\"qteAutoSucceed\":1}}");CCHECK(!type.value&&type.errors.at(0)=="cinematic.qteAutoSucceed: expected boolean");
    auto example=aiverse::lib::gameplay::readGameplaySettings(Json::parseFile(root+"/examples/libraries/genre/cinematic-settings.json"));
    CCHECK(example.value&&cinematicSettingsErrors(*example.value).empty());
    auto slow=slowMotionRequest(*example.value,kDomainCinematic);o=qteOptions(*example.value);
    CCHECK(hitStopRequest(*example.value).duration==4&&slow.scale.num==1&&slow.scale.den==3&&slow.duration==120&&slow.domain==kDomainCinematic&&o.accessibleWindowMilli==2000&&o.mashScaleMilli==500);
    // Runtime rejection leaves state untouched.
    auto lib=library({stagedScene()});SceneState s;run(lib,s,1,commands({cmd(1,C::TriggerScene,40)}));run(lib,s,2);auto before=save(s);
    SceneInput bad;bad.qte={{5,1,true},{4,1,true}};CCHECK(!stepScene(lib,s,5,bad));
    bad={};bad.qte={{2,1,true}};CCHECK(!stepScene(lib,s,5,bad)); // input tick not after the last step
    bad={};bad.commands={cmd(5,C::Pause),cmd(5,C::Unpause)};CCHECK(!stepScene(lib,s,5,bad));
    bad={};bad.poses={{1,{}},{1,{}}};CCHECK(!stepScene(lib,s,5,bad));
    bad={};bad.poses={{1,{Fixed::fromInt(200000),{},{}}}};CCHECK(!stepScene(lib,s,5,bad));
    bad={};QteOptions wrong;wrong.windowScaleMilli=50;bad.options=wrong;CCHECK(!stepScene(lib,s,5,bad));
    bad={};bad.commands={cmd(5,C::TriggerScene,40,{{1,0}})};CCHECK(!stepScene(lib,s,5,bad));
    CCHECK(!stepScene(lib,s,2,{})&&!stepScene(lib,s,1,{})&&save(s)==before);
    auto broken=s;broken.phase=ScenePhase(9);CCHECK(save(broken).empty());
    broken=s;broken.essentialDone={3,1};CCHECK(save(broken).empty());
    broken=s;broken.qte.qte=1;CCHECK(save(broken).empty()); // a QTE id outside the Qte phase
    auto other=lib;other.scenes.erase(40);indexTriggers(other);CCHECK(!stepScene(other,s,5,{}));
    auto stale=lib;stale.triggers[77]=40;CCHECK(sceneLibraryError(stale)=="cinematic.triggers: out of date (call indexTriggers)");
    TimeState ts;CCHECK(!load(before,ts));QteProgress qp;CCHECK(!load(before,qp));
    auto unsorted=stagedScene();std::swap(unsorted.cues[0],unsorted.cues[1]);SceneLibrary raw;raw.scenes[40]=unsorted;CCHECK(sceneLibraryError(raw).find("must be sorted")!=std::string::npos);
}
}
#undef CCHECK
void cinematicGameplay(const std::string& root){cine::timeExact();cine::timeStack();cine::cueOrder();cine::syncTimeouts();cine::qteJudging();cine::staging();cine::checkpoints();cine::triggersAndQueue();cine::saveRestore(root);cine::invalidData(root);}
int main(int argc,char** argv){try{CHECK(argc==2);movement();combat();weapons();matches();narrative(argv[1]);locomotionModes(argv[1]);combatActions(argv[1]);weaponBallistics(argv[1]);matchObjectives(argv[1]);stealthSenses(argv[1]);genreBots(argv[1]);sensesBotBridge();cinematicGameplay(argv[1]);std::cout<<"Genre policies: locomotion, locomotion modes, melee, combat actions, weapon ballistics, stealth senses, genre bots, projectiles, rounds/capture, match objectives, story and save/restore: OK\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
