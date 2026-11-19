#include "engine/world/TerraWeaveHeightmap.h"
#include <cassert>
#include <limits>
#include <stdexcept>

using namespace aiverse::terraweave;

int main() {
    TerrainSettings t;
    t.seed = 38;
    const auto middle = generateLegacyCorners(t, 32, 32);
    const auto east = generateLegacyCorners(t, 32, 32, {3200, 0, 100});
    assert(middle.size() == 33 * 33);
    for (size_t z = 0; z < 33; ++z)
        assert(middle[z * 33 + 32] == east[z * 33]);

    t.baseHeightCm = -100;
    t.reliefCm = 0;
    const auto below = generateLegacyCorners(t, 1, 1);
    assert(below.size() == 4);
    for (int16_t h : below) assert(h == -16);

    bool threw = false;
    t.baseHeightCm = 300000;
    try { (void)generateLegacyCorners(t, 1, 1); }
    catch (const std::out_of_range&) { threw = true; }
    assert(threw);
    threw = false;
    t.baseHeightCm = 0;
    try {
        (void)generateLegacyCorners(t, 32, 32,
                  {std::numeric_limits<int64_t>::max(), 0, 100});
    } catch (const std::out_of_range&) { threw = true; }
    assert(threw);
    threw = false;
    try { (void)generateLegacyCorners(t, 0, 1); }
    catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
}
