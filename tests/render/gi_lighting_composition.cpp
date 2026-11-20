#include "engine/render/GiLightingComposition.h"
#include "tests/AlwaysCheck.h"
#include <stdexcept>
using namespace aiverse::render;

int main() {
    GiIndirectLayers gi;
    gi.hemisphere = {1, 1, 1};
    gi.bakedLightmap = {2, 0, 0};
    gi.bakedProbes = {0, 3, 0};
    gi.scalableProbes = {0, 0, 4};
    gi.dynamicBounce = {.5f, .25f, .125f};
    gi.screenBounce = {.1f, .1f, .1f};
    gi.indirectOcclusion = .5f;
    auto c = composeIndirectGi(gi);
    AIVERSE_TEST_CHECK(c.owner == GiBaseSource::Hemisphere);
    AIVERSE_TEST_CHECK(c.unoccluded.x > 1.59f && c.unoccluded.x < 1.61f);
    AIVERSE_TEST_CHECK(c.incident.x > .79f && c.incident.x < .81f);
    gi.baseSource = GiBaseSource::BakedLightmap;
    c = composeIndirectGi(gi);
    AIVERSE_TEST_CHECK(c.incident.x > 1.29f && c.incident.x < 1.31f);
    AIVERSE_TEST_CHECK(c.incident.y > .17f && c.incident.y < .18f);
    // Changing unselected sources cannot alter the result (prevents double static GI).
    gi.hemisphere = {500,500,500};
    gi.scalableProbes = {100,100,100};
    gi.bakedProbes = {100,100,100};
    auto same = composeIndirectGi(gi);
    AIVERSE_TEST_CHECK(same.incident.x == c.incident.x);
    AIVERSE_TEST_CHECK(same.incident.y == c.incident.y);
    gi.baseSource = GiBaseSource::BakedProbes;
    AIVERSE_TEST_CHECK(composeIndirectGi(gi).incident.x > 50);
    gi.baseSource = GiBaseSource::ScalableProbes;
    AIVERSE_TEST_CHECK(composeIndirectGi(gi).incident.x > 50);
    gi.indirectOcclusion = 0;
    AIVERSE_TEST_CHECK(composeIndirectGi(gi).incident.x == 0);

    bool rejected = false;
    gi.indirectOcclusion = 2;
    try { (void)composeIndirectGi(gi); }
    catch (const std::invalid_argument&) { rejected = true; }
    AIVERSE_TEST_CHECK(rejected);
    rejected = false;
    gi.indirectOcclusion = 1;
    gi.dynamicBounce = {-1,0,0};
    try { (void)composeIndirectGi(gi); }
    catch (const std::invalid_argument&) { rejected = true; }
    AIVERSE_TEST_CHECK(rejected);
    rejected = false;
    gi.dynamicBounce = {};
    gi.baseSource = static_cast<GiBaseSource>(99);
    try { (void)composeIndirectGi(gi); }
    catch (const std::invalid_argument&) { rejected = true; }
    AIVERSE_TEST_CHECK(rejected);
}
