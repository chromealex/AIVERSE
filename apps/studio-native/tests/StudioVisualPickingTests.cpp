#include "apps/studio-native/StudioVisualPicking.h"
#include "tests/TestCheck.h"
#include <cmath>
#include <iostream>
#include <limits>

using namespace aiverse;
using namespace aiverse::studio;
using render::float3;

int main() {
    StudioDocumentProxy rotated;
    rotated.id="collider:rotated";
    rotated.center={0,0,0};
    rotated.half={1,.5f,.1f};
    rotated.yaw=3.14159265358979323846f/4;
    rotated.calculateBounds();

    // A downward ray in the AABB corner hits empty space, outside the
    // actually rendered rotated cube. Old AABB picking selected it.
    const float3 corner{.75f,3,.75f};
    AIVERSE_TEST_CHECK(corner.x<rotated.boundsMax.x && corner.z<rotated.boundsMax.z);
    AIVERSE_TEST_CHECK(!std::isfinite(studioRayProxyBox(rotated,corner,{0,-1,0})));

    // The same ray through visible geometry does hit the rotated cube.
    const float hit=studioRayProxyBox(rotated,{0,3,0},{0,-1,0});
    AIVERSE_TEST_CHECK(std::isfinite(hit));
    AIVERSE_TEST_CHECK(std::abs(hit-2.5f)<1e-4f);

    StudioDocumentProxy rear=rotated;
    rear.id="collider:behind";
    rear.center.y=-4;
    rear.calculateBounds();
    auto picked=studioPickVisibleProxy({rear,rotated},{0,3,0},{0,-1,0});
    AIVERSE_TEST_CHECK(picked.id=="collider:rotated");

    // Camera can begin inside an object; choose its first exit face.
    const auto inside=studioRayProxyBox(rotated,{0,0,0},{0,1,0});
    AIVERSE_TEST_CHECK(std::abs(inside-.5f)<1e-4f);

    const auto miss=studioPickVisibleProxy({rotated},corner,{0,-1,0});
    AIVERSE_TEST_CHECK(miss.id.empty());

    // Invalid rays must never pick random scene geometry.
    const float inf=std::numeric_limits<float>::infinity();
    AIVERSE_TEST_CHECK(studioPickVisibleProxy({rotated},{0,3,0},{0,0,0}).id.empty());
    AIVERSE_TEST_CHECK(studioPickVisibleProxy({rotated},{inf,3,0},{0,-1,0}).id.empty());
    std::cout<<"PASS studio visual shape picking\n";
}
