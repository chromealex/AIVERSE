#include <aiverselib/animation/AnimationMotionWarp.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace aiverse::render;
using namespace aiverse::lib::animation;
namespace aiverse::platform { bool readContentFile(const std::string&,std::string&){return false;} }
static void check(bool ok,const char* msg) {if(!ok){std::fprintf(stderr,"motion warp: %s\n",msg);std::exit(1);}}
int main() {
    Model m;m.nodes.resize(1);m.nodes[0].name="root";m.order={0};
    AnimClip clip;clip.duration=1;clip.name="Vault";
    AnimChannel x;x.node=0;x.times={0,1};x.values={0,0,0,2,0,0};
    clip.channels.push_back(x);
    RootMotionTransform target{{4,0,0},quat::axisAngle({0,1,0},1.57079632679f)};
    auto zero=warpedRootAt(m,clip,0,0,target);
    check(zero && std::abs(zero->position.x)<.001f,"starts at origin");
    auto end=warpedRootAt(m,clip,0,1,target);
    check(end && std::abs(end->position.x-4.f)<.001f,"reaches target");
    check(end && std::abs(dot(end->rotation,target.rotation))>.999f,"matches target turn");
    auto mid=warpedRootAt(m,clip,0,.5,target);
    check(mid && std::abs(mid->position.x-2.f)<.001f,"smooth correction");
    auto early=warpedRootAt(m,clip,0,.25,target,{.5f,1.f});
    check(early && std::abs(early->position.x-.5f)<.001f,"warp window");
    auto step=warpedRootDelta(m,clip,0,0,1,target);
    check(step && std::abs(step->position.x-4.f)<.001f,"delta request");
    check(!warpedRootAt(m,clip,0,.5,target,{.8f,.5f}),"invalid window");
    std::puts("motion warp OK");
}
