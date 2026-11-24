#include "engine/animation/AnimationMontage.h"
#include "engine/animation/EcsAnimationSystems.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

// Host tests do not link the platform runtime, matching animation_graphs.
namespace aiverse::platform { bool readContentFile(const std::string&,std::string&){return false;} }
using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::lib::animation;
static void check(bool b,const char* msg){if(!b){std::fprintf(stderr,"montage: %s\n",msg);std::exit(1);}}
int main(){
    Model model;model.nodes.resize(1);model.nodes[0].name="root";model.order={0};
    AnimClip clip;clip.name="Attack";clip.duration=1;
    AnimChannel channel;channel.node=0;channel.times={0,1};
    channel.values={0,0,0,2,0,0};clip.channels.push_back(channel);
    model.clips.push_back(clip);
    MontageDefinition spec;spec.model=&model;spec.slot="upper";
    spec.blendIn=0;spec.blendOut=0;
    spec.sections={{"windup",0,0,.5f,1,{{.25f,"fire"}}},
                   {"recovery",0,.5f,1.f,-1,{{.75f,"recover"}}}};
    auto asset=MontageDefinition::compile(spec);
    MontagePlayer player;
    check(player.play(asset),"start");
    player.update(.8f);
    check(player.active() && player.section()==1,"section transition");
    check(player.events().size()==2,"events across sections");
    check(player.events()[0].name=="fire" && player.events()[1].name=="recover","ordered events");
    Pose pose;bindPose(model,pose);
    check(player.apply(model,pose),"overlay");
    check(std::abs(pose[0].t.x-1.6f)<.001f,"overlay sample");
    player.update(.3f);
    check(!player.active() && player.section()==-1,"one-shot finished");
    MontageDefinition cyc=spec;
    cyc.sections={spec.sections[0]};
    cyc.sections[0].next=0;
    auto repeating=MontageDefinition::compile(std::move(cyc));
    check(player.play(repeating),"repeat start");
    player.update(1.3f);
    check(player.active() && player.events().size()==3,"repeated section");
    player.update(100.f);
    check(!player.active() && !player.error().empty(),"transition budget");
    // Exercise the same generic montage through the presentation ECS.
    auto graph=AnimationGraph::compile(model,Json::parse(R"({
        "nodes":[{"id":"bind","kind":"bind"}],"output":"bind"})"));
    World world;Entity e=world.create();
    auto& src=world.add<AnimationAssetComponent>(e);
    src.model=&model;src.graph=graph;
    world.add<AnimationPlayerComponent>(e);
    world.add<AnimationPoseComponent>(e);
    auto& slots=world.add<AnimationMontageSlotsComponent>(e);
    slots.players.emplace_back();
    check(slots.players.back().play(asset),"ECS slot play");
    stepAnimationPresentation(world,.25f);
    check(std::abs(world.get<AnimationPoseComponent>(e).local[0].t.x-.5f)<.001f,
          "ECS montage on graph pose");
    check(world.get<AnimationMontageSlotsComponent>(e).events.size()==1,
          "ECS notifies");
    std::puts("montage OK");
}
