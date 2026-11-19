#include "engine/world/TerraWeaveArborMesh.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <future>
#include <stdexcept>
#include <vector>

using namespace aiverse::terraweave;

namespace {
void sameTree(const TreeSkeleton& a, const TreeSkeleton& b) {
    assert(a.speciesId == b.speciesId);
    assert(a.seed == b.seed);
    assert(a.heightCm == b.heightCm);
    assert(a.crownRadiusCm == b.crownRadiusCm);
    assert(a.branches == b.branches);
    assert(a.leaves == b.leaves);
}
void validateGraph(const TreeSkeleton& tree) {
    assert(!tree.branches.empty());
    assert(tree.branches.front().parent == -1);
    assert(tree.branches.front().y0 == 0);
    assert(tree.branches.front().y1 == tree.heightCm);
    for (size_t i = 1; i < tree.branches.size(); ++i) {
        const TreeBranch& b = tree.branches[i];
        assert(b.parent >= 0 && static_cast<size_t>(b.parent) < i);
        assert(b.startRadiusCm > 0 && b.endRadiusCm > 0);
        assert(b.y0 >= 0 && b.y0 <= tree.heightCm);
        assert(b.y1 >= b.y0);
        assert(std::abs(static_cast<int64_t>(b.x1)) <= tree.crownRadiusCm);
        assert(std::abs(static_cast<int64_t>(b.z1)) <= tree.crownRadiusCm);
    }
}
void testTreeShapesAndLods() {
    for (CrownShape shape : {CrownShape::Broadleaf, CrownShape::Conifer,
                             CrownShape::Columnar}) {
        TreeProfile profile;
        profile.speciesId = 42;
        profile.shape = shape;
        profile.primaryBranches = 25;
        const uint64_t seed = 0xab83f13922334455ULL;
        auto high = generateTree(profile, seed, TreeLOD::High);
        auto med = generateTree(profile, seed, TreeLOD::Medium);
        auto low = generateTree(profile, seed, TreeLOD::Low);
        auto impostor = generateTree(profile, seed, TreeLOD::Impostor);
        validateGraph(high);
        validateGraph(med);
        validateGraph(low);
        assert(high.heightCm == med.heightCm && med.heightCm == low.heightCm);
        assert(low.heightCm == impostor.heightCm);
        assert(high.crownRadiusCm == impostor.crownRadiusCm);
        assert(high.branches.size() == 26);
        assert(med.branches.size() == 14);
        assert(low.branches.size() == 8);
        assert(high.leaves.size() == 50);
        assert(med.leaves.size() == 13);
        assert(low.leaves.size() == 7);
        assert(impostor.branches.empty() && impostor.leaves.empty());
        sameTree(high, generateTree(profile, seed, TreeLOD::High));
        auto changed = generateTree(profile, seed + 1, TreeLOD::High);
        assert(high.branches != changed.branches || high.heightCm != changed.heightCm);
    }
}
void testInstancesAndIndependentWorkers() {
    TreeProfile profile;
    profile.speciesId = 11;
    FoliageInstance instance;
    instance.speciesId = profile.speciesId;
    instance.key = 0x12345678abcdefULL;
    auto original = generateTree(profile, instance, TreeLOD::High);
    std::vector<std::future<TreeSkeleton>> jobs;
    for (int i = 0; i < 8; ++i)
        jobs.push_back(std::async(std::launch::async, [=] {
            return generateTree(profile, instance, TreeLOD::High);
        }));
    for (auto& job : jobs) sameTree(original, job.get());
    // World position is intentionally NOT baked into local-space tree skeleton.
    instance.xCm = -123456;
    instance.zCm = 99999;
    instance.yawMilliDegrees = 180000;
    sameTree(original, generateTree(profile, instance, TreeLOD::High));
}
void testGeneratedGeometry() {
    TreeProfile profile;
    profile.primaryBranches = 12;
    for (TreeLOD tier : {TreeLOD::High, TreeLOD::Medium, TreeLOD::Low, TreeLOD::Impostor}) {
        const TreeSkeleton tree = generateTree(profile, 777, tier);
        const ArborMesh mesh = buildArborMesh(tree);
        if (tier == TreeLOD::Impostor) {
            assert(mesh.vertices.empty() && mesh.indices.empty());
            continue;
        }
        assert(!mesh.vertices.empty() && !mesh.indices.empty());
        assert(mesh.indices.size() % 3 == 0);
        bool bark = false, leaf = false;
        for (const auto& v : mesh.vertices) {
            bark |= v.material == 0;
            leaf |= v.material == 1;
            assert(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z));
            assert(std::isfinite(v.nx) && std::isfinite(v.ny) && std::isfinite(v.nz));
            assert(v.material <= 1);
        }
        assert(bark && leaf);
        for (uint32_t idx : mesh.indices) assert(idx < mesh.vertices.size());
    }
    bool threw = false;
    try { (void)buildArborMesh(generateTree(profile, 42), {2, true}); }
    catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
}
void testInvalidProfiles() {
    TreeProfile profile;
    bool threw = false;
    profile.minHeightCm = 0;
    try { (void)generateTree(profile, 3); }
    catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
    profile.minHeightCm = 800;
    profile.primaryBranches = 300;
    threw = false;
    try { (void)generateTree(profile, 3); }
    catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
    profile.primaryBranches = 20;
    FoliageInstance wrongSpecies;
    wrongSpecies.speciesId = 999;
    threw = false;
    try { (void)generateTree(profile, wrongSpecies, TreeLOD::Medium); }
    catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
}
}
int main() {
    testTreeShapesAndLods();
    testInstancesAndIndependentWorkers();
    testInvalidProfiles();
    testGeneratedGeometry();
}
