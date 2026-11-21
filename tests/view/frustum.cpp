#include "engine/view/Frustum.h"
#include "../AlwaysCheck.h"
using namespace aiverse;
int main() {
    const render::mat4 identity{};
    const auto a=view::SideFrustum::fromViewProjection(identity);
    AIVERSE_TEST_CHECK(a.visibleAABB({-.5f,-.5f,-1},{.5f,.5f,1}));
    AIVERSE_TEST_CHECK(!a.visibleAABB({2,0,0},{3,1,1}));
    AIVERSE_TEST_CHECK(!a.visibleAABB({-3,0,0},{-2,1,1}));
    const auto s=view::SideFrustum::fromViewProjection(identity,true);
    AIVERSE_TEST_CHECK(s.normalLengths[0]>0.999f && s.normalLengths[0]<1.001f);
    AIVERSE_TEST_CHECK(s.visibleSphere({0,0,0},.2f));
    AIVERSE_TEST_CHECK(!s.visibleSphere({3,0,0},.5f));
    AIVERSE_TEST_CHECK(s.visibleSphere({1.15f,0,0},.2f));
    AIVERSE_TEST_CHECK(!s.visibleSphere({0,3,0},.5f));
}
