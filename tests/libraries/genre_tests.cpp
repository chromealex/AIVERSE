#include <aiverselib/genre/Configured.h>
#include <aiverselib/genre/Persistence.h>
#include <aiverselib/genre/Data.h>
#include <aiverselib/genre/LocomotionModes.h>
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
int main(int argc,char** argv){try{CHECK(argc==2);movement();combat();weapons();matches();narrative(argv[1]);locomotionModes(argv[1]);std::cout<<"Genre policies: locomotion, locomotion modes, melee, projectiles, rounds/capture, story and save/restore: OK\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
