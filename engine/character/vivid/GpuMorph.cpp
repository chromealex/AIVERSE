#include "GpuMorph.h"
#include "VividComputeShaders.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace aiverse::vivid {
namespace {
constexpr std::size_t kGpuLimitBytes = 512ull * 1024ull * 1024ull;

bool validComponent(float x) noexcept {
    return std::isfinite(x) && std::fabs(x) <= 100'000'000.0f;
}
bool validVertex(const MorphVertex& vertex) noexcept {
    for (float component : vertex.position) if (!validComponent(component)) return false;
    for (float component : vertex.normal) if (!validComponent(component)) return false;
    for (float component : vertex.tangent) if (!validComponent(component)) return false;
    return true;
}
bool addBudget(std::size_t count, std::size_t itemBytes, std::size_t& total) noexcept {
    if (count > (kGpuLimitBytes - total) / itemBytes) return false;
    total += count * itemBytes;
    return true;
}
} // namespace

GpuMorphVertex packMorphVertex(const MorphVertex& src) noexcept {
    GpuMorphVertex result{};
    for (std::size_t i = 0; i < 3; ++i) {
        result.position[i] = src.position[i];
        result.normal[i] = src.normal[i];
        result.tangent[i] = src.tangent[i];
    }
    return result;
}
MorphVertex unpackMorphVertex(const GpuMorphVertex& src) noexcept {
    MorphVertex result{};
    for (std::size_t i = 0; i < 3; ++i) {
        result.position[i] = src.position[i];
        result.normal[i] = src.normal[i];
        result.tangent[i] = src.tangent[i];
    }
    return result;
}

bool GpuMorphDeformer::initialize(gfx::Device& device, const SparseMorphLibrary& library,
                                  std::span<const MorphVertex> baseVertices) {
    release();
    error_.clear();
    if (!device.supportsCompute()) {
        error_ = "VIVID: compute unavailable on this graphics backend";
        return false;
    }
    if (library.vertices() == 0 || baseVertices.size() != library.vertices() ||
        library.vertices() > std::numeric_limits<std::uint32_t>::max() ||
        library.targets() > std::numeric_limits<std::uint32_t>::max() ||
        library.deltas() > std::numeric_limits<std::uint32_t>::max() ||
        library.gpuVertexLinks().size() > std::numeric_limits<std::uint32_t>::max()) {
        error_ = "VIVID: invalid morph model/vertex counts";
        return false;
    }
    for (const auto& v : baseVertices) {
        if (!validVertex(v)) {
            error_ = "VIVID: nonfinite or out-of-range base vertex";
            return false;
        }
    }
    std::size_t bytes = 0;
    if (!addBudget(baseVertices.size(), sizeof(GpuMorphVertex), bytes) ||
        !addBudget(baseVertices.size(), sizeof(GpuMorphVertex), bytes) ||
        !addBudget(library.gpuDeltas().size(), sizeof(GpuMorphDelta), bytes) ||
        !addBudget(library.gpuVertexOffsets().size(), sizeof(std::uint32_t), bytes) ||
        !addBudget(library.gpuVertexLinks().size(), sizeof(GpuMorphLink), bytes) ||
        !addBudget(std::max<std::size_t>(library.targets(), 1), sizeof(float), bytes)) {
        error_ = "VIVID: morph GPU buffers exceed 512 MiB budget";
        return false;
    }

    device_ = &device;
    compute_ = device.createCompute({kVividMorphMSL, kVividMorphGLSL, "VIVID sparse morph gather"});
    if (!compute_.valid()) {
        error_ = "VIVID: compute pipeline creation failed: " + device.lastError();
        release();
        return false;
    }
    std::vector<GpuMorphVertex> packed(baseVertices.size());
    for (std::size_t i = 0; i < baseVertices.size(); ++i)
        packed[i] = packMorphVertex(baseVertices[i]);
    const std::vector<float> zeroWeights(std::max<std::size_t>(library.targets(), 1), 0.0f);

    auto make = [&](int slot, const void* data, std::size_t count,
                    std::size_t elementBytes, gfx::Usage usage, const char* label) {
        gfx::BufferDesc desc{};
        desc.type = gfx::BufferType::Storage;
        desc.usage = usage;
        desc.size = count ? count * elementBytes : 16;
        desc.data = count ? data : nullptr;
        desc.label = label;
        buffers_[slot] = device.createBuffer(desc);
        return buffers_[slot].valid();
    };
    const bool ok =
        make(0, packed.data(), packed.size(), sizeof(GpuMorphVertex), gfx::Usage::Immutable, "VIVID base") &&
        make(1, library.gpuDeltas().data(), library.gpuDeltas().size(), sizeof(GpuMorphDelta), gfx::Usage::Immutable, "VIVID deltas") &&
        make(2, library.gpuVertexOffsets().data(), library.gpuVertexOffsets().size(), sizeof(std::uint32_t), gfx::Usage::Immutable, "VIVID offsets") &&
        make(3, library.gpuVertexLinks().data(), library.gpuVertexLinks().size(), sizeof(GpuMorphLink), gfx::Usage::Immutable, "VIVID links") &&
        make(4, zeroWeights.data(), zeroWeights.size(), sizeof(float), gfx::Usage::Dynamic, "VIVID weights") &&
        make(5, nullptr, packed.size(), sizeof(GpuMorphVertex), gfx::Usage::Immutable, "VIVID result");
    if (!ok) {
        error_ = "VIVID: GPU resource allocation failed: " + device.lastError();
        release();
        return false;
    }
    vertexCount_ = library.vertices();
    targetCount_ = library.targets();
    gpuBytes_ = bytes;
    uniforms_.vertices = static_cast<std::uint32_t>(vertexCount_);
    uniforms_.targets = static_cast<std::uint32_t>(targetCount_);
    uniforms_.deltas = static_cast<std::uint32_t>(library.deltas());
    uniforms_.links = static_cast<std::uint32_t>(library.gpuVertexLinks().size());
    return true;
}

bool GpuMorphDeformer::dispatch(std::span<const float> weights, bool normalizeDirections) {
    if (!ready() || weights.size() != targetCount_) {
        error_ = "VIVID: compute deformer not ready or weight count mismatch";
        return false;
    }
    for (float w : weights) {
        if (!std::isfinite(w) || w < 0.0f || w > 1.0f) {
            error_ = "VIVID: invalid shape weight";
            return false;
        }
    }
    if (!device_->canUpdateSharedBuffers() || !device_->gpuHealthy()) {
        error_ = "VIVID: device cannot safely upload shape weights";
        return false;
    }
    // Metal/GL/Vulkan implementations upload buffer changes in command order.
    // A zero-target asset has one dummy allocated float and no shape links.
    if (!weights.empty()) device_->updateBuffer(buffers_[4], weights.data(), weights.size_bytes());
    uniforms_.normalizeDirections = normalizeDirections ? 1u : 0u;
    if (!device_->beginCompute(compute_)) {
        error_ = "VIVID: beginCompute must be inside a GPU frame and outside render passes";
        return false;
    }
    for (int slot = 0; slot < 6; ++slot)
        device_->setComputeBuffer(slot, buffers_[slot]);
    device_->setComputeUniforms(&uniforms_, sizeof(uniforms_));
    device_->dispatch(static_cast<std::uint32_t>(vertexCount_));
    device_->endCompute();
    error_.clear();
    return true;
}

bool GpuMorphDeformer::download(std::span<MorphVertex> output) const {
    if (!ready() || output.size() != vertexCount_) return false;
    std::vector<GpuMorphVertex> packed(vertexCount_);
    if (!device_->readBuffer(buffers_[5], packed.data(), packed.size() * sizeof(GpuMorphVertex)))
        return false;
    for (std::size_t i = 0; i < vertexCount_; ++i)
        output[i] = unpackMorphVertex(packed[i]);
    return true;
}

void GpuMorphDeformer::release() noexcept {
    if (device_) {
        for (auto& buffer : buffers_) {
            if (buffer.valid()) device_->destroyBuffer(buffer);
            buffer = {};
        }
        if (compute_.valid()) device_->destroyCompute(compute_);
    }
    device_ = nullptr;
    compute_ = {};
    vertexCount_ = targetCount_ = gpuBytes_ = 0;
    uniforms_ = {};
}

} // namespace aiverse::vivid
