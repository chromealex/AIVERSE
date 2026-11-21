#include "engine/character/vivid/SparseMorph.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse::vivid;
namespace {
void check(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}
bool close(float a, float b, float epsilon = 1e-4f) {
    return std::fabs(a-b) <= epsilon;
}
template<typename F> void reject(F&& fn) {
    try { fn(); }
    catch (const std::invalid_argument&) { return; }
    catch (const std::length_error&) { return; }
    throw std::runtime_error("invalid sparse morph library accepted");
}
MorphVertex neutral() {
    MorphVertex v{};
    v.position = {1, 2, 3};
    v.normal = {0, 1, 0};
    v.tangent = {1, 0, 0};
    return v;
}
MorphDelta delta(std::uint32_t vertex, float x) {
    MorphDelta d{};
    d.vertex = vertex;
    d.position = {x, 0, 0};
    return d;
}
void validatePacking() {
    MorphTargetInput b{"blink", {delta(2, 2), delta(0, 0), delta(1, 1)}};
    MorphTargetInput j{"jaw", {delta(0, 0.5f)}};
    const auto asset = SparseMorphLibrary::compile(4, {b, j});
    check(asset.vertices()==4 && asset.targets()==2 && asset.deltas()==3, "failed sparse zero compaction");
    check(asset.targetName(0)=="blink" && asset.targetName(1)=="jaw", "target order changed");
    std::size_t found = 99;
    check(asset.findTarget("jaw", found) && found==1, "name lookup failed");
    check(!asset.findTarget("JAW", found) && found==1, "unknown name modified index");
    check(asset.targetName(99).empty(), "invalid target name returned data");
    check(asset.gpuRanges()[0].first==0 && asset.gpuRanges()[0].count==2, "first range not compacted");
    check(asset.gpuDeltas()[0].vertex==1 && asset.gpuDeltas()[1].vertex==2,
          "GPU deltas not sorted by vertex");
    check(asset.gpuRanges()[1].first==2 && asset.gpuRanges()[1].count==1, "second range wrong");
    check(asset.bufferBytes()==asset.deltas()*64 + asset.targets()*16 +
          (asset.vertices()+1)*4 + asset.deltas()*8, "binary size wrong");
    check(asset.gpuVertexOffsets().size()==5 && asset.gpuVertexLinks().size()==3,
          "CSR buffer counts wrong");
    check(asset.gpuVertexOffsets()[0]==0 && asset.gpuVertexOffsets()[1]==1 &&
          asset.gpuVertexOffsets()[2]==2 && asset.gpuVertexOffsets()[3]==3 &&
          asset.gpuVertexOffsets()[4]==3, "CSR offsets wrong");
    check(asset.gpuVertexLinks()[0].target==1 &&
          asset.gpuVertexLinks()[0].delta==2, "jaw CSR reference wrong");
    check(asset.gpuVertexLinks()[1].target==0 &&
          asset.gpuVertexLinks()[1].delta==0, "blink CSR reference wrong");
    check(asset.gpuDeltas()[0].position[3]==0 && asset.gpuDeltas()[0].normal[3]==0,
          "reserved components not initialized");
}
void validateMalformed() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    reject([] { SparseMorphLibrary::compile(1, {{"", {delta(0, 1)}}}); });
    reject([] { SparseMorphLibrary::compile(1, {{"a", {delta(1, 1)}}}); });
    reject([] { SparseMorphLibrary::compile(1, {{"a", {delta(0, 1), delta(0, 2)}}}); });
    reject([] { SparseMorphLibrary::compile(1, {{"a", {}}, {"a", {}}}); });
    reject([&] { SparseMorphLibrary::compile(1, {{"a", {delta(0, nan)}}}); });
    reject([&] { SparseMorphLibrary::compile(1, {{"a", {delta(0, inf)}}}); });
    reject([] { SparseMorphLibrary::compile(1, {{"a", {delta(0, 1e20f)}}}); });
    reject([] { SparseMorphLibrary::compile(0, {{"a", {delta(0, 1)}}}); });
    reject([] { SparseMorphLibrary::compile(1, std::vector<MorphTargetInput>(4097)); });
}
void verifyManyTargets() {
    constexpr std::size_t kCount = 160;
    std::vector<MorphTargetInput> inputs;
    inputs.reserve(kCount);
    float expected = 1.0f;
    for (std::size_t i=0; i<kCount; ++i) {
        const float d = float(i+1)*0.0005f;
        inputs.push_back({"shape_"+std::to_string(i), {delta(0, d)}});
        expected += d * 0.25f;
    }
    const auto library = SparseMorphLibrary::compile(2, std::move(inputs));
    check(library.targets()==kCount, "more than 8 morph targets were truncated");
    std::array<MorphVertex,2> base{neutral(), neutral()}, output{};
    std::vector<float> weights(kCount, 0.25f);
    check(library.evaluate(base, weights, output), "160 target CPU deform failed");
    check(close(output[0].position[0], expected), "160 target position sum wrong");
    check(close(output[1].position[0], 1), "untouched vertex moved");
    check(close(output[0].normal[1], 1) && close(output[0].tangent[0], 1),
          "normal/tangent was corrupted");
    std::array<MorphVertex,2> inPlace = base;
    check(library.evaluate(inPlace, weights, inPlace), "in-place CPU deform failed");
    check(close(inPlace[0].position[0], output[0].position[0]), "in-place parity mismatch");
    std::array<MorphVertex,2> gathered{};
    check(library.evaluateGather(base, weights, gathered), "GPU-style gather reference failed");
    check(close(gathered[0].position[0], output[0].position[0]), "scatter/gather numerical parity");
    check(close(gathered[1].position[0], output[1].position[0]), "unaffected gather vertex moved");
    check(library.gpuVertexOffsets().size()==3, "gather offsets count");
    check(library.gpuVertexOffsets()[1]==kCount, "160 contributions not gathered to vertex");
    check(library.gpuVertexLinks().size()==kCount, "gather links lost");
    for (std::size_t i = 0; i < kCount; ++i)
        check(library.gpuVertexLinks()[i].target==i, "gather ordering not deterministic");


    // Failed input is transactional, including a bad later weight.
    weights.back() = std::numeric_limits<float>::quiet_NaN();
    output[0].position[0] = 456;
    check(!library.evaluate(base, weights, output), "NaN weight accepted");
    check(output[0].position[0] == 456, "invalid weight partially wrote output");
    check(!library.evaluateGather(base, weights, output), "gather NaN weight accepted");
    check(output[0].position[0] == 456, "gather partially wrote invalid weights");
    weights.back() = 1.1f;
    check(!library.evaluate(base, weights, output), "out-of-range weight accepted");
    weights.back() = 0.25f;
    base[1].position[0] = std::numeric_limits<float>::infinity();
    check(!library.evaluate(base, weights, output), "invalid base accepted");
    check(output[0].position[0] == 456, "invalid base partially wrote output");
    base[1].position[0] = 1;
    std::array<MorphVertex,1> tooSmall{};
    check(!library.evaluate(base, weights, tooSmall), "mismatched output accepted");
    check(!library.evaluate(base, std::span<const float>{}, output), "wrong target count accepted");
}
void verifyNormalDeltas() {
    MorphDelta d{};
    d.vertex = 0;
    d.normal = {1, 0, 0};
    d.tangent = {0, 1, 0};
    const auto asset = SparseMorphLibrary::compile(1, {{"deform", {d}}});
    std::array<MorphVertex,1> base{neutral()}, out{};
    std::array<float,1> weights{1};
    check(asset.evaluate(base, weights, out), "normal delta eval failed");
    const float unit = std::sqrt(0.5f);
    check(close(out[0].normal[0], unit) && close(out[0].normal[1], unit),
          "normal wasn't normalized");
    check(close(out[0].tangent[0], unit) && close(out[0].tangent[1], unit),
          "tangent wasn't normalized");
    check(asset.evaluate(base, weights, out, false), "non-normalized eval failed");
    check(close(out[0].normal[0], 1) && close(out[0].normal[1], 1),
          "optional normalization ignored");
    std::array<MorphVertex,1> gather{};
    check(asset.evaluateGather(base, weights, gather, false), "unnormalized gather failed");
    check(close(gather[0].normal[0], out[0].normal[0]) &&
          close(gather[0].tangent[1], out[0].tangent[1]), "gather directions not equivalent");
}
} // namespace

int main() {
    try {
        validatePacking();
        validateMalformed();
        verifyManyTargets();
        verifyNormalDeltas();
        std::cout << "VIVID sparse morph PASS (160 targets, stable GPU ABI/CSR gather, CPU parity)\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "VIVID sparse morph FAIL: " << ex.what() << '\n';
        return 1;
    }
}
