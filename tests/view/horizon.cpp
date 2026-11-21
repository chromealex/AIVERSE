#include "engine/view/Horizon.h"
#include "../AlwaysCheck.h"
using namespace aiverse;
int main(){
    const render::float3 eye{0,0,10};
    const float r=5;
    AIVERSE_TEST_CHECK(!view::sphericalCapHiddenByHorizon(eye,r,{0,0,1},1.f,r));
    AIVERSE_TEST_CHECK(view::sphericalCapHiddenByHorizon(eye,r,{0,0,-1},1.f,r));
    AIVERSE_TEST_CHECK(!view::sphericalCapHiddenByHorizon(eye,r,{0,0,-1},-1.f,r));
    AIVERSE_TEST_CHECK(!view::sphericalCapHiddenByHorizon({0,0,4},r,{0,0,-1},1.f,r));
    const auto cached=view::SphericalHorizon::fromLocalCamera(eye,r);
    AIVERSE_TEST_CHECK(cached.active);
    AIVERSE_TEST_CHECK(!cached.hidden({0,0,1},1.f,r));
    AIVERSE_TEST_CHECK(cached.hidden({0,0,-1},1.f,r));
    const auto inside=view::SphericalHorizon::fromLocalCamera({0,0,4},r);
    AIVERSE_TEST_CHECK(!inside.active && !inside.hidden({0,0,-1},1.f,r));
}
