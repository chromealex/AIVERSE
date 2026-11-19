#include "engine/view/Frustum.h"
#include <cassert>
using namespace aiverse;
int main() {
    const render::mat4 identity{};
    const auto a=view::SideFrustum::fromViewProjection(identity);
    assert(a.visibleAABB({-.5f,-.5f,-1},{.5f,.5f,1}));
    assert(!a.visibleAABB({2,0,0},{3,1,1}));
    assert(!a.visibleAABB({-3,0,0},{-2,1,1}));
    const auto s=view::SideFrustum::fromViewProjection(identity,true);
    assert(s.normalLengths[0]>0.999f && s.normalLengths[0]<1.001f);
    assert(s.visibleSphere({0,0,0},.2f));
    assert(!s.visibleSphere({3,0,0},.5f));
    assert(s.visibleSphere({1.15f,0,0},.2f));
    assert(!s.visibleSphere({0,3,0},.5f));
}
