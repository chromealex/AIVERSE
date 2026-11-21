// VIVID render-stream acceptance test. This test does not simulate GPU pixels;
// it validates the dedicated model-relative vertex-buffer safety policy.
#include "engine/render/Renderer3D.h"
#include "engine/render/ModelVertexStreamOverride.h"

#include <stdexcept>
#include <iostream>

using namespace aiverse;
using namespace aiverse::render;
namespace {
void verify(bool condition, const char* message) {
    if(!condition) throw std::runtime_error(message);
}
Model createModel() {
    Model model;
    model.vb = {7};
    model.ib = {8};
    model.meshes.resize(1);
    model.meshes[0].primitives.resize(1);
    auto& prim = model.meshes[0].primitives[0];
    prim.firstVertex = 0;
    prim.vertexCount = 16;
    prim.firstIndex = 0;
    prim.indexCount = 24;
    model.vertices.resize(16);
    return model;
}
void test() {
    Model model = createModel();
    const gfx::BufferHandle stream{71};
    verify(modelVertexOverrideStatus(model,{},0) ==
           ModelVertexOverrideStatus::Disabled, "legacy default changed");
    verify(modelVertexOverrideStatus(model,{},16) ==
           ModelVertexOverrideStatus::MissingBuffer, "missing buffer accepted");
    verify(modelVertexOverrideStatus(model,stream,0) ==
           ModelVertexOverrideStatus::InvalidVertexCount, "zero count accepted");
    verify(modelVertexOverrideStatus(model,stream,15) ==
           ModelVertexOverrideStatus::InvalidVertexCount, "truncated buffer accepted");
    verify(modelVertexOverrideStatus(model,stream,17) ==
           ModelVertexOverrideStatus::InvalidVertexCount, "extra CPU vertices accepted");
    verify(modelVertexOverrideStatus(model,stream,16) ==
           ModelVertexOverrideStatus::Ready, "valid GPU stream rejected");

    // CPU vertices are normally discarded after GPU upload: primitive bounds
    // remain a conservative validation of the available vertex indices.
    model.vertices.clear();
    verify(modelVertexOverrideStatus(model,stream,16) ==
           ModelVertexOverrideStatus::Ready, "post-upload model rejected");
    verify(modelVertexOverrideStatus(model,stream,15) ==
           ModelVertexOverrideStatus::InvalidVertexCount, "truncated post-upload stream");

    model.gpuBaseVertex = 1;
    verify(modelVertexOverrideStatus(model,stream,16) ==
           ModelVertexOverrideStatus::UnsupportedSharedGeometry, "pooled base vertex accepted");
    model.gpuBaseVertex = 0;
    model.gpuFirstIndex = 3;
    verify(modelVertexOverrideStatus(model,stream,16) ==
           ModelVertexOverrideStatus::UnsupportedSharedGeometry, "pooled index offset accepted");
    model.gpuFirstIndex = 0;
    model.meshes[0].primitives[0].vertexCount = 17;
    verify(modelVertexOverrideStatus(model,stream,16) ==
           ModelVertexOverrideStatus::InvalidVertexCount, "mesh range overflow accepted");

    // Regression for procedural modelFromParts: this routine previously left
    // Primitive::firstVertex/vertexCount at zero even with valid vertex data,
    // causing the VIVID stream safety policy to reject the entire model.
    MeshBuilder partA, partB;
    for (float x : {-0.3f, 0.3f, 0.0f})
        partA.addVertex({x, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 0xffffffffu);
    partA.addTriangle(0, 1, 2);
    for (float x : {-0.5f, 0.5f, 0.0f})
        partB.addVertex({x, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, 0xffffffffu);
    partB.addTriangle(0, 1, 2);
    const Material material{};
    auto procedural = modelFromParts("vivid-vertex-ranges",
        {{"part_a", &partA, material, {}}, {"part_b", &partB, material, {}}});
    verify(bool(procedural) && procedural->vertices.size() == 6 &&
           procedural->meshes.size() == 2, "procedural model source incomplete");
    const auto& first = procedural->meshes[0].primitives[0];
    const auto& second = procedural->meshes[1].primitives[0];
    verify(first.firstVertex == 0 && first.vertexCount == 3 &&
           second.firstVertex == 3 && second.vertexCount == 3,
           "procedural mesh part vertex range metadata missing");
    procedural->vb = {21};
    procedural->ib = {22};
    verify(modelVertexOverrideStatus(*procedural, stream, 6) ==
           ModelVertexOverrideStatus::Ready,
           "valid procedural model override rejected");
    procedural->vertices.clear();
    verify(modelVertexOverrideStatus(*procedural, stream, 6) ==
           ModelVertexOverrideStatus::Ready,
           "procedural model override broken after CPU vertex data eviction");

    // Compile-time check: the public per-instance override handle defaults
    // off, and never changes the existing ModelVertex layout.
    ModelDraw draw;
    verify(!draw.vertexOverride.valid() && draw.vertexOverrideCount==0,
           "ModelDraw enabled override without explicit input");
    static_assert(sizeof(ModelVertex)==60);
}
}
int main() {
    try {
        test();
        std::cout << "VIVID render vertex override safety policy PASS\n";
        return 0;
    } catch(const std::exception& ex) {
        std::cerr << "VIVID render vertex override FAIL: " << ex.what() << '\n';
        return 1;
    }
}
