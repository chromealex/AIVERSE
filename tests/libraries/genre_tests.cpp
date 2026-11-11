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
int main(int argc,char** argv){try{CHECK(argc==2);movement();combat();weapons();matches();narrative(argv[1]);std::cout<<"Genre policies: locomotion, melee, projectiles, rounds/capture, story and save/restore: OK\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
