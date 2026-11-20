#include "engine/render/GiUpdateScheduler.h"
#include "tests/AlwaysCheck.h"
#include <cmath>
#include <cstdint>
#include <stdexcept>

using namespace aiverse::render;

int main() {
    GiUpdateScheduler s;
    s.configure({0, 0, 0}, 1.0f, 4, 2, 2);
    AIVERSE_TEST_CHECK(s.configured());
    AIVERSE_TEST_CHECK(s.size() == 16 && s.pending() == 16);
    AIVERSE_TEST_CHECK(s.inFlight() == 0);
    const auto first = s.claim(1, {0, 0, 0});
    AIVERSE_TEST_CHECK(first.size() == 1 && first[0].index == 0);
    AIVERSE_TEST_CHECK(first[0].reasons == giDirtyBit(GiDirtyReason::Geometry));
    AIVERSE_TEST_CHECK(first[0].position.x == .5f);
    AIVERSE_TEST_CHECK(s.inFlight() == 1);

    // Re-invalidating an in-flight update must not acknowledge stale lighting.
    AIVERSE_TEST_CHECK(s.invalidate({{.5f,.5f,.5f}, {.5f,.5f,.5f}},
                                      GiDirtyReason::Lighting) == 1);
    AIVERSE_TEST_CHECK(!s.complete(first[0]));
    AIVERSE_TEST_CHECK(s.pending() == 16 && s.inFlight() == 0);
    const auto second = s.claim(1, {0, 0, 0});
    AIVERSE_TEST_CHECK(second.size() == 1 && second[0].index == 0);
    AIVERSE_TEST_CHECK(second[0].reasons ==
                       uint8_t(giDirtyBit(GiDirtyReason::Geometry) | giDirtyBit(GiDirtyReason::Lighting)));
    AIVERSE_TEST_CHECK(s.complete(second[0]));
    AIVERSE_TEST_CHECK(!s.complete(second[0]));
    AIVERSE_TEST_CHECK(s.pending() == 15);

    s.resetClean();
    AIVERSE_TEST_CHECK(s.pending() == 0);
    AIVERSE_TEST_CHECK(s.claim(10, {0,0,0}).empty());
    // Exact lattice corners (at 0.5 and 1.5) are selected, not arbitrary voxels.
    AIVERSE_TEST_CHECK(s.invalidate({{.49f, .49f, .49f}, {1.51f, .51f, .51f}},
                                      GiDirtyReason::Material) == 2);
    AIVERSE_TEST_CHECK(s.pending() == 2);
    AIVERSE_TEST_CHECK(s.invalidate({{100,100,100}, {101,101,101}},
                                      GiDirtyReason::Geometry) == 0);
    const auto two = s.claim(2, {0,0,0});
    AIVERSE_TEST_CHECK(two.size() == 2 && two[0].index == 0 && two[1].index == 1);
    AIVERSE_TEST_CHECK(s.abort(two[0]));
    AIVERSE_TEST_CHECK(s.complete(two[1]));
    AIVERSE_TEST_CHECK(s.pending() == 1);
    const auto retry = s.claim(2, {0,0,0});
    AIVERSE_TEST_CHECK(retry.size() == 1 && retry[0].index == 0);
    AIVERSE_TEST_CHECK(s.complete(retry[0]) && s.pending() == 0);

    // The influence radius expands world edits beyond their exact bounds.
    AIVERSE_TEST_CHECK(s.invalidate({{.5f,.5f,.5f},{.5f,.5f,.5f}},
                                      GiDirtyReason::Environment, 1.0f) == 8);
    s.resetClean();

    // Aging: a constantly invalidated near probe must not starve older far probes.
    s.configure({0,0,0}, 1, 4, 1, 1);
    s.resetClean();
    AIVERSE_TEST_CHECK(s.invalidate({{3.5f,.5f,.5f},{3.5f,.5f,.5f}},
                                     GiDirtyReason::Geometry) == 1);
    for (int i = 0; i < 4; ++i) {
        AIVERSE_TEST_CHECK(s.invalidate({{.5f,.5f,.5f},{.5f,.5f,.5f}},
                                         GiDirtyReason::Lighting) == 1);
        const auto selected = s.claim(1, {0,0,0});
        AIVERSE_TEST_CHECK(selected.size() == 1);
        AIVERSE_TEST_CHECK(selected[0].index == (i == 3 ? 3u : 0u));
        AIVERSE_TEST_CHECK(s.complete(selected[0]));
    }
    AIVERSE_TEST_CHECK(s.pending() == 1);

    bool rejected = false;
    try { s.configure({0,0,0}, 0, 4, 1, 1); }
    catch (const std::invalid_argument&) { rejected = true; }
    AIVERSE_TEST_CHECK(rejected);
    rejected = false;
    try { s.invalidate({{2,0,0},{1,1,1}}, GiDirtyReason::Geometry); }
    catch (const std::invalid_argument&) { rejected = true; }
    AIVERSE_TEST_CHECK(rejected);
    rejected = false;
    try { (void)s.claim(1, {0,0,INFINITY}); }
    catch (const std::invalid_argument&) { rejected = true; }
    AIVERSE_TEST_CHECK(rejected);
    return 0;
}
