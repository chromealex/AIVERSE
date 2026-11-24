#include "engine/animation/AnimationBudgetPolicy.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace aiverse;
using namespace aiverse::lib::animation;
namespace aiverse::platform {bool readContentFile(const std::string&,std::string&){return false;}}
static void check(bool v,const char* message){
    if(!v){std::fprintf(stderr,"animation budget: %s\n",message);std::exit(1);}
}
int main(){
    World world;
    Entity a=world.create(),b=world.create(),c=world.create(),d=world.create(),e=world.create();
    auto add=[&](Entity entity,float pixels,bool visible,bool force){
        auto& s=world.add<AnimationSignificanceComponent>(entity);
        s.pixels=pixels;s.visible=visible;s.alwaysEvaluate=force;
        world.add<AnimationBudgetComponent>(entity);
    };
    add(a,10,true,false);
    add(b,100,true,false);
    add(c,100,true,false);
    add(d,1,false,false);
    add(e,1,true,true);
    AnimationBudgetPolicy config;
    config.fullRateSlots=1;config.mediumRateSlots=1;config.mediumInterval=1.f/20.f;
    const auto result=assignAnimationBudgets(world,config);
    check(result.full==2 && result.forced==1,"mandatory and optional full rate");
    check(result.medium==1 && result.low==1 && result.hidden==1,"slot counts");
    check(world.get<AnimationBudgetComponent>(e).interval==0,"forced priority");
    check(world.get<AnimationBudgetComponent>(b).interval==0,"highest importance");
    check(std::abs(world.get<AnimationBudgetComponent>(c).interval-.05f)<.0001f,"equal score stable entity id");
    check(std::abs(world.get<AnimationBudgetComponent>(a).interval-.1f)<.0001f,"low rate");
    check(!world.get<AnimationBudgetComponent>(d).visible,"offscreen freeze");
    world.get<AnimationSignificanceComponent>(c).pixels=200;
    assignAnimationBudgets(world,config);
    check(world.get<AnimationBudgetComponent>(c).interval==0,"camera score moves slot");
    std::puts("animation budget allocator OK");
}
