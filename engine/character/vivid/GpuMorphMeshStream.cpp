#include "GpuMorphMeshStream.h"
#include "GpuMorphMeshShaders.h"

#include <limits>
#include <type_traits>
#include <vector>

namespace aiverse::vivid {
namespace {
constexpr std::size_t kMaxStreamBytes = 512ull * 1024ull * 1024ull;
static_assert(std::is_trivially_copyable_v<render::ModelVertex>,
              "VIVID requires the shared ModelVertex storage ABI");
} // namespace

bool GpuMorphMeshStream::initialize(
    gfx::Device& device, std::span<const render::ModelVertex> vertices) {
    release();
    error_.clear();
    if (!device.supportsCompute()) {
        error_ = "VIVID mesh stream: compute is not available";
        return false;
    }
    if (vertices.empty() ||
        vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
        vertices.size() > kMaxStreamBytes / (2 * sizeof(render::ModelVertex))) {
        error_ = "VIVID mesh stream: vertex count or 512 MiB budget exceeded";
        return false;
    }
    device_ = &device;
    compute_ = device.createCompute({kVividMeshStreamMSL, kVividMeshStreamGLSL,
                                     "VIVID ModelVertex stream packing"});
    if (!compute_.valid()) {
        error_ = "VIVID mesh stream: createCompute failed: " + device.lastError();
        release();
        return false;
    }

    const std::size_t bytes = vertices.size_bytes();
    gfx::BufferDesc input{};
    input.type = gfx::BufferType::Storage;
    input.usage = gfx::Usage::Immutable;
    input.size = bytes;
    input.data = vertices.data();
    input.label = "VIVID original 60B ModelVertex stream";
    original_ = device.createBuffer(input);
    if (!original_.valid()) {
        error_ = "VIVID mesh stream: original buffer allocation failed: " + device.lastError();
        release();
        return false;
    }
    gfx::BufferDesc output{};
    output.type = gfx::BufferType::Storage;
    output.usage = gfx::Usage::Immutable;
    output.size = bytes;
    output.label = "VIVID packed 60B ModelVertex output";
    output_ = device.createBuffer(output);
    if (!output_.valid()) {
        error_ = "VIVID mesh stream: output buffer allocation failed: " + device.lastError();
        release();
        return false;
    }
    vertexCount_ = vertices.size();
    params_.vertexCount = static_cast<std::uint32_t>(vertexCount_);
    return true;
}

bool GpuMorphMeshStream::dispatch(gfx::BufferHandle deformed,
                                  std::size_t deformedVertexCount) {
    if (!ready() || !deformed.valid() || deformedVertexCount != vertexCount_ ||
        deformed == original_ || deformed == output_) {
        error_ = "VIVID mesh stream: invalid deformer buffer or vertex count";
        return false;
    }
    if (!device_->gpuHealthy()) {
        error_ = "VIVID mesh stream: graphics device unhealthy";
        return false;
    }
    if (!device_->beginCompute(compute_)) {
        error_ = "VIVID mesh stream: dispatch must occur in a frame outside rendering";
        return false;
    }
    device_->setComputeBuffer(0, original_);
    device_->setComputeBuffer(1, deformed);
    device_->setComputeBuffer(2, output_);
    device_->setComputeUniforms(&params_, sizeof(params_));
    device_->dispatch(static_cast<std::uint32_t>(vertexCount_));
    device_->endCompute();
    error_.clear();
    return true;
}

bool GpuMorphMeshStream::download(std::span<render::ModelVertex> output) const {
    if (!ready() || output.size() != vertexCount_) return false;
    return device_->readBuffer(output_, output.data(), output.size_bytes());
}

void GpuMorphMeshStream::release() noexcept {
    if (device_) {
        if (original_.valid()) device_->destroyBuffer(original_);
        if (output_.valid()) device_->destroyBuffer(output_);
        if (compute_.valid()) device_->destroyCompute(compute_);
    }
    original_ = {};
    output_ = {};
    compute_ = {};
    device_ = nullptr;
    vertexCount_ = 0;
    params_ = {};
}

} // namespace aiverse::vivid
