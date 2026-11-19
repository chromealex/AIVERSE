// LENS / MotionWeave: per-camera LOD hysteresis must never share state.
#include "engine/view/View.h"
#include <cassert>
using namespace aiverse;
namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
int main() {
    View view(0);
    render::Model base,far;
    view::Object object;
    object.id=42;
    object.generation=3;
    object.draw.model=&base;
    object.animationModelGeneration=9;
    object.lodHysteresis=.2f;
    view::ModelLod level;
    level.model=&far;
    level.minDistance=10.f;
    object.lods.push_back(level);
    view.beginPreparedFrame();
    // Camera A enters far LOD at 12; camera B remains near.
    assert(view.selectCameraLodLevel(object,13.f,101)==1);
    assert(view.selectCameraLodLevel(object,5.f,202)==0);
    // In the hysteresis band 8..12, each retains its own last choice.
    assert(view.selectCameraLodLevel(object,10.f,101)==1);
    assert(view.selectCameraLodLevel(object,10.f,202)==0);
    // Legacy scope zero remains independent.
    assert(view.selectCameraLodLevel(object,5.f)==0);
    assert(view.selectCameraLodLevel(object,10.f)==0);
    // Reset camera A only. Object generation is still unchanged.
    view.forgetCameraScope(101);
    assert(view.selectCameraLodLevel(object,10.f,101)==0);
    assert(view.selectCameraLodLevel(object,10.f,202)==0);
    // The same packed scope but a new publication resets stale hysteresis.
    assert(view.selectCameraLodLevel(object,13.f,202)==1);
    object.animationModelGeneration=10;
    assert(view.selectCameraLodLevel(object,10.f,202)==0);
    view.clear();
}
