// Release-safe regression for giValidateGpuBvh containment tolerance.
// Self-contained: builds flat GiGpuBvh packets by hand (no paged arena,
// no GPU device). Covers the float32-ULP containment rule that replaced the
// old 1e-5 * |world coordinate| margin, plus basic structural rejections.
// StaticPointShadows.h (pulled in via GiGpuBvh.h) relies on <cstdint>
// already being included.
#include <cstdint>
#include "engine/render/GiGpuBvhValidator.h"
#include "tests/AlwaysCheck.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

using namespace aiverse::render;

namespace {

GiGpuBvhTri tri(float x0, float x1) {
    return {{x0, 0.f, 0.f, 0.f}, {x1, 1.f, 0.f, 0.f}, {x0, 0.f, 1.f, 0.f},
            {.4f, .3f, .2f, 0.f}};
}

GiGpuBvhNode leaf(float lox, float hix, uint32_t first, uint32_t count) {
    return {{lox, 0.f, 0.f, 0.f}, {hix, 1.f, 1.f, 0.f}, {0, 0, first, count}};
}

GiGpuBvhNode inner(float lox, float hix, uint32_t left, uint32_t right) {
    return {{lox, 0.f, 0.f, 0.f}, {hix, 1.f, 1.f, 0.f}, {left, right, 0, 0}};
}

float up(float v, int steps) {
    for (int i = 0; i < steps; ++i)
        v = std::nextafter(v, std::numeric_limits<float>::infinity());
    return v;
}

} // namespace

int main() {
    // Empty packet is valid; half-empty packets are not.
    GiGpuBvh empty;
    AIVERSE_TEST_CHECK(giValidateGpuBvh(empty).valid);
    GiGpuBvh onlyNode;
    onlyNode.nodes.push_back(leaf(0, 1, 0, 0));
    AIVERSE_TEST_CHECK(!giValidateGpuBvh(onlyNode).valid);

    // Simple valid leaf near the origin.
    GiGpuBvh simple;
    simple.triangles.push_back(tri(0.f, 1.f));
    simple.nodes.push_back(leaf(0.f, 1.f, 0, 1));
    const auto ok = giValidateGpuBvh(simple);
    AIVERSE_TEST_CHECK(ok.valid);
    AIVERSE_TEST_CHECK(ok.reachableNodes == 1);
    AIVERSE_TEST_CHECK(ok.referencedTriangles == 1);

    // Near origin: old tolerance (1e-5 at |x|<=1) accepted an escape of
    // 5e-6; the one-ULP rule must reject it.
    GiGpuBvh smallEscape = simple;
    smallEscape.triangles[0].b.x = 1.f + 5e-6f;
    AIVERSE_TEST_CHECK(!giValidateGpuBvh(smallEscape).valid);

    // Large world: 112 units outside a leaf at x=1e8 was admitted by the old
    // world-scaled margin (1e-5 * 1e8 = 1000). Must be rejected now.
    GiGpuBvh distant;
    distant.triangles.push_back(tri(100000128.f, 100000128.f));
    distant.nodes.push_back(leaf(100000000.f, 100000016.f, 0, 1));
    AIVERSE_TEST_CHECK(!giValidateGpuBvh(distant).valid);
    distant.nodes[0].hi.x = 100000128.f;
    AIVERSE_TEST_CHECK(giValidateGpuBvh(distant).valid);

    // Exactly one representable float step of rounding is tolerated at each
    // endpoint, two steps are not (both small and large magnitudes).
    for (float base : {1.f, 4096.f, 100000000.f}) {
        GiGpuBvh b;
        b.triangles.push_back(tri(base, up(base, 1)));
        b.nodes.push_back(leaf(base, base, 0, 1));
        AIVERSE_TEST_CHECK(giValidateGpuBvh(b).valid);
        b.triangles[0].b.x = up(base, 2);
        AIVERSE_TEST_CHECK(!giValidateGpuBvh(b).valid);
    }

    // Internal node: child AABB escaping its parent at a large coordinate is
    // rejected with the child-escape diagnostic.
    GiGpuBvh tree;
    tree.triangles.push_back(tri(50000000.f, 50000004.f));
    tree.triangles.push_back(tri(50000016.f, 50000032.f));
    tree.nodes.push_back(inner(50000000.f, 50000032.f, 1, 2));
    tree.nodes.push_back(leaf(50000000.f, 50000004.f, 0, 1));
    tree.nodes.push_back(leaf(50000016.f, 50000032.f, 1, 1));
    AIVERSE_TEST_CHECK(giValidateGpuBvh(tree).valid);
    tree.nodes[0].hi.x = 50000016.f; // right child escapes by 16 units (4 ULP)
    const auto esc = giValidateGpuBvh(tree);
    AIVERSE_TEST_CHECK(!esc.valid);
    AIVERSE_TEST_CHECK(esc.error != nullptr);

    // Structural rejections still apply.
    GiGpuBvh cycle;
    cycle.triangles.push_back(tri(0.f, 1.f));
    cycle.nodes.push_back(inner(0.f, 1.f, 0, UINT32_MAX));
    AIVERSE_TEST_CHECK(!giValidateGpuBvh(cycle).valid);
    GiGpuBvh badRange = simple;
    badRange.nodes[0].meta[2] = 5;
    AIVERSE_TEST_CHECK(!giValidateGpuBvh(badRange).valid);
    GiGpuBvh nan = simple;
    nan.triangles[0].a.y = std::numeric_limits<float>::quiet_NaN();
    AIVERSE_TEST_CHECK(!giValidateGpuBvh(nan).valid);

    std::puts("gi_gpu_bvh_validator: OK");
    return 0;
}
