#include <aiverselib/animation/EcsRootMotionSystems.h>
#include <cassert>
using namespace aiverse;
using namespace aiverse::lib::animation;
int main() {
    RootMotionTrack clip;
    clip.id=7;clip.rate=60;clip.frames={{{1_fx,Fixed{},Fixed{}},Fixed::fromRatio(1,4)},
                                       {{1_fx,Fixed{},Fixed{}},Fixed{}}};
    RootMotionIntentComponent intent{7,1,true};
    RootMotionStateComponent state;
    Transform x;
    auto motor=[](Vec3 d){return RootMotorResult{d,true};};
    auto a=consumeRootMotion(clip,intent,state,x,60,0,motor);
    assert(a.valid && !a.blocked && state.frame==1 && x.pos.x==1_fx);
    auto b=consumeRootMotion(clip,intent,state,x,60,1,motor);
    assert(b.valid && state.finished);
    assert(fxAbs(x.pos.z+1_fx)<Fixed::fromRatio(1,100));
    auto idle=consumeRootMotion(clip,intent,state,x,60,2,motor);
    assert(!idle.valid);
    ++intent.sequence;
    auto reject=[](Vec3){return RootMotorResult{};};
    auto c=consumeRootMotion(clip,intent,state,x,60,3,reject);
    assert(c.valid && c.blocked && x.pos.x==1_fx);
    assert(!validRootTrack(clip,30));
    RootMotionTracksResource lib;lib.clips.push_back(clip);
    assert(findRootTrack(lib,7)!=nullptr);
    return 0;
}
