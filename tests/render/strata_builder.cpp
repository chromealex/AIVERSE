#include "engine/render/StrataBuilder.h"
#include <algorithm>
// Keep CTest assertions active under Release (-DNDEBUG) builds.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace aiverse::render::strata;
int main() {
    // A contiguous open surface with shared vertices: 32x32 quads.
    constexpr uint32_t side = 33;
    std::vector<float> positions;
    std::vector<uint32_t> source;
    for (uint32_t y = 0; y < side; ++y)
        for (uint32_t x = 0; x < side; ++x) {
            positions.push_back(float(x));
            positions.push_back(float(y));
            positions.push_back(0.15f * std::sin(float(x) * .4f));
        }
    for (uint32_t y = 0; y + 1 < side; ++y)
        for (uint32_t x = 0; x + 1 < side; ++x) {
            uint32_t a = y * side + x, b = a + 1, c = a + side, d = c + 1;
            source.insert(source.end(), {a, b, c, b, d, c});
        }
    BuildInput input;
    input.indices = source.data();
    input.indexCount = source.size();
    input.positions = positions.data();
    input.vertexCount = positions.size() / 3;
    input.positionStride = sizeof(float) * 3;
    input.materialGroup = 7;
    Asset asset;
    std::string error;
    assert(build(input, {}, asset, &error));
    assert(error.empty());
    assert(validate(asset.nodes, asset.roots));
    assert(asset.bounds.size() == asset.nodes.size());
    assert(!asset.meshlets.empty());
    size_t leafIndices = 0;
    std::vector<uint32_t> reconstructed;
    for (const Meshlet& m : asset.meshlets) {
        assert(m.node < asset.nodes.size());
        assert(asset.nodes[m.node].childCount == 0);
        assert(asset.nodes[m.node].materialGroup == 7);
        assert(m.vertexCount <= 64 && m.triangleCount <= 124);
        assert(m.vertexCount >= 3 && m.triangleCount >= 1);
        assert(size_t(m.vertexOffset) + m.vertexCount <= asset.meshletVertices.size());
        assert(size_t(m.triangleOffset) + m.triangleCount * 3 <= asset.meshletTriangles.size());
        leafIndices += asset.nodes[m.node].indexCount;
        for (size_t i = 0; i < m.triangleCount * 3; ++i) {
            const auto local = asset.meshletTriangles[m.triangleOffset + i];
            assert(local < m.vertexCount);
            const uint32_t global = asset.meshletVertices[m.vertexOffset + local];
            reconstructed.push_back(global);
            assert(asset.indices[asset.nodes[m.node].firstIndex + i] == global);
        }
    }
    assert(leafIndices == source.size());
    assert(reconstructed == source); // exact leaf LOD0 triangle order
    for (const auto& node : asset.nodes) {
        assert(size_t(node.firstIndex) + node.indexCount <= asset.indices.size());
        if (node.childCount)
            for (uint32_t k = 0; k < node.childCount; ++k)
                assert(node.geometricError >=
                       asset.nodes[node.firstChild + k].geometricError);
    }

    std::vector<float> pixels(asset.nodes.size(), 2.f);
    SelectionInput selectInput;
    selectInput.errorPixels = pixels.data();
    selectInput.errorCount = uint32_t(pixels.size());
    SelectionOutput result;
    assert(select(asset.nodes, asset.roots, selectInput, result));
    assert(result.nodes.size() == asset.meshlets.size());

    selectInput.thresholdPixels = 100;
    assert(select(asset.nodes, asset.roots, selectInput, result));
    assert(result.nodes.size() == asset.roots.size());
    selectInput.thresholdPixels = 1;
    selectInput.maxVisibleNodes = uint32_t(asset.roots.size());
    assert(select(asset.nodes, asset.roots, selectInput, result));
    assert(result.nodes.size() == asset.roots.size());

    if (asset.nodes[asset.roots.front()].childCount) {
        const uint32_t child = asset.nodes[asset.roots.front()].firstChild;
        asset.nodes[child].resident = false;
        selectInput.maxVisibleNodes = 65536;
        assert(select(asset.nodes, asset.roots, selectInput, result));
        assert(result.missingRefinements > 0);
        asset.nodes[child].resident = true;
    }

    auto broken = asset.nodes;
    broken[asset.roots.front()].firstChild = 0; // invalid back edge
    broken[asset.roots.front()].childCount = 1;
    assert(!validate(broken, asset.roots));
    pixels[asset.roots.front()] = std::numeric_limits<float>::infinity();
    assert(!select(asset.nodes, asset.roots, selectInput, result));

    Asset unchanged = asset;
    const uint32_t invalid[] = {0, 1, UINT32_MAX};
    input.indices = invalid;
    input.indexCount = 3;
    assert(!build(input, {}, asset, &error));
    assert(!error.empty() && asset.indices == unchanged.indices);

    // Attribute-aware API accepts a regular float stream and refuses invalid
    // weights/layouts without mutating the previous successful asset.
    const float weights[3] = {0.5f, 0.5f, 0.5f};
    input.indices = source.data();
    input.indexCount = source.size();
    input.attributes = positions.data();
    input.attributeStride = sizeof(float) * 3;
    input.attributeComponents = 3;
    input.attributeWeights = weights;
    assert(build(input, {}, asset, &error));
    assert(validate(asset.nodes, asset.roots));
    unchanged = asset;
    const float badWeights[3] = {0.5f, -1.f, 0.5f};
    input.attributeWeights = badWeights;
    assert(!build(input, {}, asset, &error));
    assert(asset.indices == unchanged.indices);
    input.attributeWeights = weights;
    input.attributeStride = sizeof(float);
    assert(!build(input, {}, asset, &error));
    assert(asset.indices == unchanged.indices);

    // Import order deliberately alternates two islands separated by 1000m,
    // one complete 64-triangle row at a time. Each row is already a leaf
    // meshlet (66 unique vertices). LOD parents must spatially group rows
    // inside their own island, NOT the adjacent island in import order.
    // The lossless LOD0 leaf stream is kept in its original triangle order.
    std::vector<float> islandsPositions;
    std::vector<uint32_t> islandsIndices;
    constexpr uint32_t islandSide = 33;
    for (uint32_t island = 0; island < 2; ++island)
        for (uint32_t y = 0; y < islandSide; ++y)
            for (uint32_t x = 0; x < islandSide; ++x) {
                islandsPositions.push_back(float(x + island * 1000));
                islandsPositions.push_back(float(y));
                islandsPositions.push_back(0.f);
            }
    for (uint32_t y = 0; y + 1 < islandSide; ++y)
        for (uint32_t island = 0; island < 2; ++island)
            for (uint32_t x = 0; x + 1 < islandSide; ++x) {
                const uint32_t a = island * islandSide * islandSide +
                                   y * islandSide + x;
                const uint32_t b = a + 1, c = a + islandSide, d = c + 1;
                islandsIndices.insert(islandsIndices.end(), {a,b,c,b,d,c});
            }
    BuildInput islandsInput{};
    islandsInput.indices = islandsIndices.data();
    islandsInput.indexCount = islandsIndices.size();
    islandsInput.positions = islandsPositions.data();
    islandsInput.vertexCount = islandsPositions.size() / 3;
    islandsInput.positionStride = sizeof(float) * 3;
    BuildOptions islandsOptions{};
    islandsOptions.maxTriangles = 64;
    islandsOptions.maxVertices = 70;
    islandsOptions.maxLevels = 1;
    islandsOptions.relativeError = .5f;
    Asset spatial;
    assert(build(islandsInput, islandsOptions, spatial, &error));
    assert(validate(spatial.nodes, spatial.roots));
    std::vector<uint32_t> originalLeaves;
    for (const Meshlet& m : spatial.meshlets)
        for (size_t n = 0; n < m.triangleCount * 3; ++n)
            originalLeaves.push_back(
                spatial.meshletVertices[m.vertexOffset +
                    spatial.meshletTriangles[m.triangleOffset + n]]);
    assert(originalLeaves == islandsIndices);
    size_t parents = 0;
    for (size_t node = 0; node < spatial.nodes.size(); ++node) {
        if (!spatial.nodes[node].childCount) continue;
        ++parents;
        // First-level parent: leaf children only. No 1000m-spanning bounds.
        assert(spatial.bounds[node].max[0] -
               spatial.bounds[node].min[0] < 100.f);
    }
    assert(parents > 0);
    return 0;
}
