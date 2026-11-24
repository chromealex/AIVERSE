#include "engine/animation/AnimationPlayback.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

// Host tests do not link the platform runtime, matching animation_graphs.
namespace aiverse::platform { bool readContentFile(const std::string&,std::string&){return false;} }
using namespace aiverse::render;
using namespace aiverse::lib::animation;
static void check(bool ok,const char* msg) {
    if (!ok) { std::fprintf(stderr,"playback: %s\n",msg);std::exit(1); }
}
int main() {
    const std::array<AnimationNotifyKey,2> marks{{{.25f,"stepA"},{.75f,"stepB"}}};
    auto events=clipNotifies(marks,1.f,.6,2.3,true);
    check(events && events->size()==4,"multi-loop event count");
    check((*events)[0].name=="stepB" && (*events)[1].name=="stepA" &&
          (*events)[2].name=="stepB" && (*events)[3].name=="stepA","event ordering");
    check((*events)[0].cycle==0 && (*events)[3].cycle==2,"cycle indexing");
    events=clipNotifies(marks,1.f,.6,2.3,false);
    check(events && events->size()==1,"one-shot events do not repeat");
    events=clipNotifies(marks,1.f,2.0,1.0,true);
    check(events && events->empty(),"backward seek suppresses notifies");
    check(!clipNotifies(marks,1.f,0.,30.,true,2),"overflow atomic rejection");
    const std::array<AnimationNotifyKey,1> invalid{{{1.5f,"outside"}}};
    check(!clipNotifies(invalid,1.f,0.,1.,true),"invalid key");
    Model m; m.nodes.resize(1);m.nodes[0].name="root";m.order={0};
    AnimClip clip;clip.name="Move";clip.duration=1;
    AnimChannel c;c.node=0;c.path=AnimChannel::Translation;
    c.times={0,1};c.values={0,0,0,2,0,0};
    clip.channels.push_back(c);
    auto one=extractClipRootDelta(m,clip,0,.25,.75,true);
    check(one && std::abs(one->position.x-1.f)<.001f,"fractional translation");
    auto multi=extractClipRootDelta(m,clip,0,.75,2.25,true);
    check(multi && std::abs(multi->position.x-3.f)<.001f,"loop root delta");
    auto end=extractClipRootDelta(m,clip,0,0.,10.,false);
    check(end && std::abs(end->position.x-2.f)<.001f,"one-shot clamps");
    check(!extractClipRootDelta(m,clip,23,0.,1.,true),"invalid root rejected");
    // Rotation accumulates across cycles, instead of repeating translation.
    AnimChannel rot;rot.node=0;rot.path=AnimChannel::Rotation;
    rot.times={0,1};
    quat quarter=quat::axisAngle({0,1,0},1.57079632679f);
    rot.values={0,0,0,1,quarter.x,quarter.y,quarter.z,quarter.w};
    clip.channels.push_back(rot);
    auto two=extractClipRootDelta(m,clip,0,0.,2.,true);
    check(two && std::abs(two->position.x-2.f)<.01f &&
          std::abs(two->position.z+2.f)<.01f,"turned looping trajectory");
    std::puts("animation playback events/root motion OK");
}
