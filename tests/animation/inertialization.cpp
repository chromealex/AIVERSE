#include "engine/animation/PoseInertialization.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace aiverse::render;
using namespace aiverse::lib::animation;
namespace aiverse::platform {bool readContentFile(const std::string&,std::string&){return false;}}
static void check(bool b,const char* msg){if(!b){std::fprintf(stderr,"inertial: %s\n",msg);std::exit(1);}}
int main() {
    Pose out(1),in(1);out[0].t={2,0,0};in[0].t={0,0,0};
    out[0].r=quat::axisAngle({0,1,0},1.57079632679f);
    out[0].w[0]=.7f;
    PoseInertializer player;player.begin(out,in,1.f);
    Pose frame=in;player.apply(frame);
    check(std::abs(frame[0].t.x-2.f)<.001f,"initial position continuity");
    check(std::abs(dot(frame[0].r,out[0].r))>.999f,"initial rotation continuity");
    check(std::abs(frame[0].w[0]-.7f)<.001f,"morph continuity");
    player.advance(.5f);frame=in;player.apply(frame);
    check(std::abs(frame[0].t.x-1.f)<.001f,"midpoint cubic decay");
    player.advance(.5f);frame=in;player.apply(frame);
    check(std::abs(frame[0].t.x)<.001f && std::abs(frame[0].w[0])<.001f,"end convergence");
    check(std::abs(dot(frame[0].r,in[0].r))>.999f,"rotation end convergence");
    check(!player.active(),"finished");
    BonePoseVelocity speed;speed.translation={1,0,0};
    player.begin(out,in,1.f,{speed},{});
    player.advance(.001f);frame=in;player.apply(frame);
    check((frame[0].t.x-2.f)/.001f>.8f,"initial velocity continuity");
    player.reset();frame=in;player.apply(frame);
    check(std::abs(frame[0].t.x)<.001f,"reset");
    std::puts("pose inertialization OK");
}
