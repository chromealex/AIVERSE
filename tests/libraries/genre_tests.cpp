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
// ---- genre-bots: melee/shooter bot policies, squad roles, objective choice (record kinds 56..58) ----
#include <aiverselib/bots/GenreBotConfig.h>
namespace gbots {
using namespace aiverse::lib::bots;
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
int main(int argc,char** argv){try{CHECK(argc==2);movement();combat();weapons();matches();narrative(argv[1]);locomotionModes(argv[1]);combatActions(argv[1]);weaponBallistics(argv[1]);genreBots(argv[1]);std::cout<<"Genre policies: locomotion, locomotion modes, melee, combat actions, weapon ballistics, genre bots, projectiles, rounds/capture, story and save/restore: OK\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
