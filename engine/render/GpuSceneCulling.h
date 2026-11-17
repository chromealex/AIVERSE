#pragma once
// Experimental GPU frustum visibility stage for persistent scene records.
// This dispatch is diagnostic; results are NOT used to decide draw visibility
// until shaders/indirect commands can consume the GPU visibility buffer.
#include "engine/gfx/Gfx.h"
#include "Math3D.h"
#include <array>
#include <cstdint>
#include <cstring>

namespace aiverse::render {
class GpuSceneCulling {
public:
    GpuSceneCulling() = default;
    ~GpuSceneCulling() = default;
    GpuSceneCulling(const GpuSceneCulling&) = delete;
    GpuSceneCulling& operator=(const GpuSceneCulling&) = delete;

    bool dispatch(gfx::Device& device, gfx::BufferHandle scene,
                  uint32_t objectSlots, uint64_t frameIndex, const mat4& viewProj) {
        if (device.info().backend != gfx::Backend::Metal || !device.supportsCompute() ||
            !scene.valid() || !objectSlots || !frameIndex) return false;
        if (!pipeline_.valid()) {
            pipeline_ = device.createCompute({kMetalShader, nullptr, "persistent scene frustum culling"});
            if (!pipeline_.valid()) return false;
        }
        Slot& slot = slots_[frameIndex % slots_.size()];
        const size_t required = size_t(objectSlots) * sizeof(uint32_t);
        if (!slot.visible.valid() || slot.bytes < required) {
            if (slot.visible.valid()) device.destroyBuffer(slot.visible);
            gfx::BufferDesc desc{};
            desc.type = gfx::BufferType::Storage;
            desc.size = required;
            desc.label = "persistent scene visible flags";
            slot.visible = device.createBuffer(desc);
            slot.bytes = slot.visible.valid() ? required : 0;
            if (!slot.visible.valid()) return false;
        }
        if (!device.beginCompute(pipeline_)) return false;
        device.setComputeBuffer(0, scene);
        device.setComputeBuffer(1, slot.visible);
        struct alignas(16) Uniforms {
            float viewProj[16];
            uint32_t count;
            uint32_t pad[3] = {};
        } uniforms{};
        std::memcpy(uniforms.viewProj, viewProj.m, sizeof(uniforms.viewProj));
        uniforms.count = objectSlots;
        device.setComputeUniforms(&uniforms, sizeof(uniforms));
        device.dispatch(objectSlots);
        device.endCompute();
        return true;
    }
    gfx::BufferHandle visibility(uint64_t frameIndex) const {
        return slots_[frameIndex % slots_.size()].visible;
    }
    void release(gfx::Device& device) {
        for (auto& slot : slots_) {
            if (slot.visible.valid()) device.destroyBuffer(slot.visible);
            slot = {};
        }
        if (pipeline_.valid()) device.destroyCompute(pipeline_);
        pipeline_ = {};
    }
private:
    struct Slot { gfx::BufferHandle visible{}; size_t bytes = 0; };
    std::array<Slot, 3> slots_{};
    gfx::ComputeHandle pipeline_{};
    static constexpr const char* kMetalShader = R"MSL(
#include <metal_stdlib>
using namespace metal;
struct SceneRecord {
    float4 c0, c1, c2, c3;
    float4 lo, hi;
    uint4 meta;
};
struct Uniforms {
    float4x4 viewProj;
    uint count;
    uint pad0, pad1, pad2;
};
kernel void cs_main(device const SceneRecord* objects [[buffer(0)]],
                    device uint* visible [[buffer(1)]],
                    constant Uniforms& uniforms [[buffer(8)]],
                    uint index [[thread_position_in_grid]]) {
    if (index >= uniforms.count) return;
    const SceneRecord object = objects[index];
    if (object.meta.x == 0) {
        visible[index] = 0;
        return;
    }
    const float4x4 world(object.c0, object.c1, object.c2, object.c3);
    const float4x4 clipMatrix = uniforms.viewProj * world;
    uint allOutside = 63;
    for (uint corner = 0; corner < 8; ++corner) {
        const float3 p = float3(
            (corner & 1) ? object.hi.x : object.lo.x,
            (corner & 2) ? object.hi.y : object.lo.y,
            (corner & 4) ? object.hi.z : object.lo.z);
        const float4 clip = clipMatrix * float4(p, 1.0);
        uint outside = 0;
        if (clip.x < -clip.w) outside |= 1;
        if (clip.x > clip.w) outside |= 2;
        if (clip.y < -clip.w) outside |= 4;
        if (clip.y > clip.w) outside |= 8;
        if (clip.z < 0.0) outside |= 16;
        if (clip.z > clip.w) outside |= 32;
        allOutside &= outside;
    }
    visible[index] = allOutside == 0 ? 1 : 0;
}
)MSL";
};
} // namespace aiverse::render
