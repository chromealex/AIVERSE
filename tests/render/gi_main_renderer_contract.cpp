// Stage23 MAIN renderer GI atlas policy — device-free regression source.
// Proves CPU RGBA32F probe L1 / Metal RGBA16F cascades use strict borrowed
// resource metadata and model shader opt-in contracts. No GPU pixels read.
#include "engine/render/GiRendererOverlayPolicy.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/ModelShaders.h"
#include "tests/cloth/FakeDevice.h"
#include "tests/AlwaysCheck.h"
#include <concepts>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

using namespace aiverse;
using namespace aiverse::render;

struct GiFake final:FakeDevice {
    bool supports(gfx::PixelFormat f) const override {
        return f==gfx::PixelFormat::RGBA16F || FakeDevice::supports(f);
    }
};

template<class R>
concept GiRendererBridge=requires(R& renderer,
                                  const GiDynamicProbeTexture* atlas,
                                  const GiClipmapValidityTexture* validity,
                                  float3 origin) {
    {renderer.setDynamicGiOverlay(atlas,origin,1.f,2,2,2,
                                  0,0,0,validity)}->std::same_as<bool>;
    {renderer.setDynamicGiCascadeOverlay(atlas,origin,2.f,2,2,2)}
        ->std::same_as<bool>;
    {renderer.setDynamicGiThirdCascadeOverlay(atlas,origin,4.f,2,2,2)}
        ->std::same_as<bool>;
    {renderer.dynamicGiOverlayActive()}->std::same_as<bool>;
    renderer.clearDynamicGiOverlay();
};
static_assert(GiRendererBridge<Renderer3D>);
static_assert(sizeof(float4)==16);

int main() {
    GiFake dev;
    dev.info_.backend=gfx::Backend::GLES3;
    GiDynamicProbeTexture cpu;
    AIVERSE_TEST_CHECK(cpu.initialize(dev,8,false));
    AIVERSE_TEST_CHECK(cpu.texture().valid());
    AIVERSE_TEST_CHECK(cpu.probeCount()==8);
    AIVERSE_TEST_CHECK(!cpu.computeWritable());
    GiClipmapValidityTexture validity;
    AIVERSE_TEST_CHECK(validity.initialize(dev,8));

    AIVERSE_TEST_CHECK(GiRendererOverlayPolicy::acceptBase(
        dev,cpu,{0,0,0},1,2,2,2));
    AIVERSE_TEST_CHECK(GiRendererOverlayPolicy::acceptBase(
        dev,cpu,{0,0,0},1,2,2,2,1,1,1,&validity));
    AIVERSE_TEST_CHECK(!GiRendererOverlayPolicy::acceptBase(
        dev,cpu,{0,0,0},1,2,2,2,2,0,0));
    AIVERSE_TEST_CHECK(!GiRendererOverlayPolicy::acceptBase(
        dev,cpu,{0,0,0},1,2,2,3));
    AIVERSE_TEST_CHECK(!GiRendererOverlayPolicy::acceptBase(
        dev,cpu,{0,0,0},0,2,2,2));
    AIVERSE_TEST_CHECK(!GiRendererOverlayPolicy::acceptBase(
        dev,cpu,{0,0,0},1,2,2,2,0,0,0,&validity,false));
    AIVERSE_TEST_CHECK(!GiRendererOverlayPolicy::acceptBase(
        dev,cpu,{std::numeric_limits<float>::quiet_NaN(),0,0},
        1,2,2,2));

    // Vulkan and GL33 use the same CPU-traced RGBA32F L1 texture contract
    // as GLES3, with no claim of a Vulkan GPU GI clipmap compute shader.
    dev.info_.backend=gfx::Backend::Vulkan;
    AIVERSE_TEST_CHECK(GiRendererOverlayPolicy::acceptBase(
        dev,cpu,{0,0,0},1,2,2,2));
    dev.info_.backend=gfx::Backend::GL33;
    AIVERSE_TEST_CHECK(GiRendererOverlayPolicy::acceptBase(
        dev,cpu,{0,0,0},1,2,2,2));
    dev.info_.backend=gfx::Backend::GLES3;

    GiFake other;
    other.info_.backend=gfx::Backend::GLES3;
    AIVERSE_TEST_CHECK(!GiRendererOverlayPolicy::acceptBase(
        other,cpu,{0,0,0},1,2,2,2));
    GiClipmapValidityTexture wrongDevice;
    AIVERSE_TEST_CHECK(wrongDevice.initialize(other,8));
    AIVERSE_TEST_CHECK(!GiRendererOverlayPolicy::acceptBase(
        dev,cpu,{0,0,0},1,2,2,2,0,0,0,&wrongDevice));
    GiClipmapValidityTexture wrongCount;
    AIVERSE_TEST_CHECK(wrongCount.initialize(dev,9));
    AIVERSE_TEST_CHECK(!GiRendererOverlayPolicy::acceptBase(
        dev,cpu,{0,0,0},1,2,2,2,0,0,0,&wrongCount));

    // Metal uses actual GPU-writable RGBA16F atlases with a separate
    // fine R8 validity mask. No Vulkan/GLES GPU cascade is advertised.
    dev.info_.backend=gfx::Backend::Metal;
    dev.computeEnabled=true;
    GiDynamicProbeTexture gpu;
    AIVERSE_TEST_CHECK(gpu.initialize(dev,8,true));
    AIVERSE_TEST_CHECK(gpu.computeWritable());
    AIVERSE_TEST_CHECK(GiRendererOverlayPolicy::acceptBase(
        dev,gpu,{0,0,0},2,2,2,2,0,0,0,&validity));
    AIVERSE_TEST_CHECK(GiRendererOverlayPolicy::acceptMetalCascade(
        dev,gpu,{0,0,0},2,2,2,2));
    AIVERSE_TEST_CHECK(!GiRendererOverlayPolicy::acceptMetalCascade(
        dev,cpu,{0,0,0},2,2,2,2));
    dev.info_.backend=gfx::Backend::Vulkan;
    AIVERSE_TEST_CHECK(!GiRendererOverlayPolicy::acceptMetalCascade(
        dev,gpu,{0,0,0},2,2,2,2));
    dev.info_.backend=gfx::Backend::GLES3;
    AIVERSE_TEST_CHECK(!GiRendererOverlayPolicy::acceptMetalCascade(
        dev,gpu,{0,0,0},2,2,2,2));

    // The SAME current MAIN model shaders carry optional DYNAMIC_GI
    // permutations; ordinary models have zero extra GI sampler cost.
    const std::string glsl=shaders::kModelFS;
    const std::string metal=shaders::kModelMSL;
    for(const char* token:{"DYNAMIC_GI","dynamicGiAt","dynamicGiCascadedAt",
                            "u_dynamicGiOrigin","tex13","tex14","tex15"}) {
        AIVERSE_TEST_CHECK(glsl.find(token)!=std::string::npos);
    }
    for(const char* token:{"DYNAMIC_GI","dynamicGiAt","dynamicGiTripleAt",
                            "texture(13)","texture(14)","texture(15)",
                            "texture(16)"}) {
        AIVERSE_TEST_CHECK(metal.find(token)!=std::string::npos);
    }
    gpu.release();
    cpu.release();
    validity.release();
    wrongCount.release();
    wrongDevice.release();
    AIVERSE_TEST_CHECK(dev.textures.empty() && other.textures.empty());
    return 0;
}
