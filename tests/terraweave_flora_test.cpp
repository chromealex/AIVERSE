#include "engine/world/TerraWeaveFlora.h"
#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <vector>

using namespace aiverse::terraweave;

namespace {
FoliageInstance makeInstance(uint64_t key, uint32_t species, int64_t x, int64_t z) {
    return {key, species, x, z, 0, 0, 1000};
}
std::vector<FoliageInstance> flatten(const std::vector<FloraBatch>& batches) {
    std::vector<FoliageInstance> out;
    for (const auto& batch : batches) {
        for (const auto& instance : batch.instances) {
            assert(batch.speciesId == instance.speciesId);
            out.push_back(instance);
        }
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.key < b.key;
    });
    return out;
}
void testGroupsAndLod() {
    const FloraView view{0, 0, 100, 300, 600, 1000, 1000};
    const std::vector<FoliageInstance> input{
        makeInstance(4, 10, 900, 0),
        makeInstance(5, 20, 0, 0),
        makeInstance(2, 10, 200, 0),
        makeInstance(6, 10, 1400, 0),
        makeInstance(3, 10, 500, 0),
        makeInstance(1, 10, 0, 0)
    };
    const auto batches = buildFloraBatches(input, view);
    assert(batches.size() == 5);
    assert(batches[0].speciesId == 10 && batches[0].lod == TreeLOD::High);
    assert(batches[1].speciesId == 10 && batches[1].lod == TreeLOD::Medium);
    assert(batches[2].speciesId == 10 && batches[2].lod == TreeLOD::Low);
    assert(batches[3].speciesId == 10 && batches[3].lod == TreeLOD::Impostor);
    assert(batches[4].speciesId == 20 && batches[4].lod == TreeLOD::High);
    assert(flatten(batches).size() == 5);
    auto shuffled = input;
    std::reverse(shuffled.begin(), shuffled.end());
    shuffled.push_back(shuffled.front()); // same key/species must be deduplicated
    const auto sorted = buildFloraBatches(shuffled, view);
    assert(flatten(batches) == flatten(sorted));
    for (size_t i = 0; i < batches.size(); ++i) {
        assert(batches[i].speciesId == sorted[i].speciesId);
        assert(batches[i].lod == sorted[i].lod);
        assert(batches[i].instances == sorted[i].instances);
    }
}
void testFarThinning() {
    FloraView view{0, 0, 0, 0, 0, 1000, 0};
    const std::vector<FoliageInstance> input{makeInstance(1, 1, 500, 0)};
    assert(buildFloraBatches(input, view).empty());
    view.farDensityPermille = 1000;
    const auto batches = buildFloraBatches(input, view);
    assert(batches.size() == 1 && batches[0].lod == TreeLOD::Impostor);
    assert(batches[0].instances.size() == 1);
}
void testStaggeredImpostorTransition() {
    FloraView view{0, 0, 100, 300, 600, 1100, 1000};
    view.impostorBlendWidthCm = 300; // stable [450,750] transition
    const std::vector<FoliageInstance> input{
        makeInstance(1, 1, 400, 0),
        makeInstance(2, 1, 800, 0),
        makeInstance(3, 1, 600, 0),
        makeInstance(4, 1, 600, 0)
    };
    const auto batches = buildFloraBatches(input, view);
    assert(flatten(batches).size() == input.size());
    auto shuffled = input;
    std::reverse(shuffled.begin(), shuffled.end());
    const auto again = buildFloraBatches(shuffled, view);
    assert(flatten(batches) == flatten(again));
    bool nearLow = false, farImpostor = false;
    for (const auto& batch : batches) {
        for (const auto& instance : batch.instances) {
            if (instance.key == 1) nearLow = batch.lod == TreeLOD::Low;
            if (instance.key == 2) farImpostor = batch.lod == TreeLOD::Impostor;
        }
    }
    assert(nearLow && farImpostor);
    view.impostorBlendWidthCm = 900; // would overlap the medium LOD tier
    bool invalid = false;
    try { (void)buildFloraBatches(input, view); }
    catch (const std::invalid_argument&) { invalid = true; }
    assert(invalid);
}
void testValidation() {
    FloraView invalid;
    invalid.mediumRangeCm = -1;
    bool threw = false;
    try { (void)buildFloraBatches({}, invalid); }
    catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
    threw = false;
    try { (void)buildFloraBatches({makeInstance(1, 1, INT64_MAX, 0)}); }
    catch (const std::out_of_range&) { threw = true; }
    assert(threw);
}
}
int main() {
    testGroupsAndLod();
    testFarThinning();
    testStaggeredImpostorTransition();
    testValidation();
}
