#include <aiverselib/animation/EcsRootMotionSystems.h>
#include <cstdio>
#include <cstdlib>
using namespace aiverse;
using namespace aiverse::lib::animation;
static void check(bool ok,const char* message) {
    if(!ok) {std::fprintf(stderr,"root motor: %s\n",message);std::exit(1);}
}
int main() {
    RootMotionTrack clip;
    clip.id=7;clip.rate=60;clip.frames={{{1_fx,Fixed{},Fixed{}},Fixed::fromRatio(1,4)},
                                       {{1_fx,Fixed{},Fixed{}},Fixed{}}};
    RootMotionIntentComponent intent{7,1,true};
    RootMotionStateComponent state;
    Transform x;
    auto motor=[](Vec3 d){return RootMotorResult{d,true};};
    auto a=consumeRootMotion(clip,intent,state,x,60,0,motor);
    check(a.valid && !a.blocked && state.frame==1 && x.pos.x==1_fx, "root motion regression");
    auto b=consumeRootMotion(clip,intent,state,x,60,1,motor);
    check(b.valid && state.finished, "root motion regression");
    check(fxAbs(x.pos.z+1_fx)<Fixed::fromRatio(1,100), "root motion regression");
    auto idle=consumeRootMotion(clip,intent,state,x,60,2,motor);
    check(!idle.valid, "root motion regression");
    ++intent.sequence;
    auto reject=[](Vec3){return RootMotorResult{};};
    auto c=consumeRootMotion(clip,intent,state,x,60,3,reject);
    check(c.valid && c.blocked && x.pos.x==1_fx, "root motion regression");
    check(!validRootTrack(clip,30), "root motion regression");
    RootMotionTracksResource lib;lib.clips.push_back(clip);
    check(findRootTrack(lib,7)!=nullptr, "root motion regression");
    return 0;
}
