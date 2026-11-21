// LENS / MotionWeave: per-camera LOD hysteresis must never share state.
#include "engine/view/View.h"
#include "tests/AlwaysCheck.h"
#include <cstdio>
#include <exception>
using namespace aiverse;
namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
static void run() {
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
    AIVERSE_TEST_CHECK(view.selectCameraLodLevel(object,13.f,101)==1);
    AIVERSE_TEST_CHECK(view.selectCameraLodLevel(object,5.f,202)==0);
    // In the hysteresis band 8..12, each retains its own last choice.
    AIVERSE_TEST_CHECK(view.selectCameraLodLevel(object,10.f,101)==1);
    AIVERSE_TEST_CHECK(view.selectCameraLodLevel(object,10.f,202)==0);
    // Legacy scope zero remains independent.
    AIVERSE_TEST_CHECK(view.selectCameraLodLevel(object,5.f)==0);
    AIVERSE_TEST_CHECK(view.selectCameraLodLevel(object,10.f)==0);
    // Reset camera A only. Object generation is still unchanged.
    view.forgetCameraScope(101);
    AIVERSE_TEST_CHECK(view.selectCameraLodLevel(object,10.f,101)==0);
    AIVERSE_TEST_CHECK(view.selectCameraLodLevel(object,10.f,202)==0);
    // The same packed scope but a new publication resets stale hysteresis.
    AIVERSE_TEST_CHECK(view.selectCameraLodLevel(object,13.f,202)==1);
    object.animationModelGeneration=10;
    AIVERSE_TEST_CHECK(view.selectCameraLodLevel(object,10.f,202)==0);
    view.clear();
}
int main() {
    try {
        run();
    } catch(const std::exception& e) {
        std::fprintf(stderr,"%s\n",e.what());
        return 1;
    }
    return 0;
}
