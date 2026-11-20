// Stage22 MAIN integration compile contract. This project uses the real
// MAIN Renderer3D PostProcess interface, not a copied old renderer. Tests
// preserve native PRISMA motion/reactive hooks AND two independent LumaWeave
// opaque PBR receiver passes. This is NOT a Vulkan pixel/benchmark test.
#include "engine/render/Renderer3D.h"
#include "engine/render/LumaWeaveVulkanPostProcess.h"
#include "engine/render/LumaWeaveNativeDriver.h"
#include "engine/render/LumaWeaveVulkanDirect.h"
#include "tests/AlwaysCheck.h"
#include <type_traits>
#include <concepts>
#include <cstdint>

using namespace aiverse;
using namespace aiverse::render;

static_assert(std::is_base_of_v<PostProcess,LumaWeaveVulkanPostProcess>);
static_assert(std::is_same_v<decltype(LumaNativeViewInput{}.camera),Camera3D>);
static_assert(std::is_same_v<decltype(LumaNativeViewInput{}.vulkanHistoryKey),
                             uint64_t>);
static_assert(std::is_same_v<decltype(LumaNativeViewInput{}.vulkanReactiveGeometryVisible),
                             bool>);
static_assert(std::is_same_v<decltype(LumaNativeViewResult{}.vulkanReactiveMaskBound),
                             bool>);
static_assert(std::is_same_v<decltype(LumaWeaveVulkanDirect::DenoiseView{}.reactiveSettings),
                             float4>);
static_assert(sizeof(LumaWeaveVulkanDirect::DenoiseView)==208);

// This expression compile-checks both existing PRISMA callbacks and NEW
// material/surface callbacks on the same actual MAIN PostProcess type.
template<class P>
concept PrismaLumaFrame = requires(P& post, const P& readonly,
                                   uint64_t key,mat4 previousVP,
                                   MotionTextureInput velocity,
                                   gfx::TextureHandle texture) {
    {readonly.wantsMaterialReceiverPass()} -> std::convertible_to<bool>;
    {readonly.wantsSurfaceReceiverPass()} -> std::convertible_to<bool>;
    {post.materialReceiverPass(64,64)} -> std::same_as<gfx::PassDesc>;
    {post.surfaceReceiverPass(64,64)} -> std::same_as<gfx::PassDesc>;
    post.materialReceiverPublished(texture,64,64);
    post.surfaceReceiverPublished(texture,64,64);
    {readonly.wantsObjectMotionVectors()} -> std::convertible_to<bool>;
    {readonly.objectMotionSceneDepth()} -> std::same_as<gfx::TextureHandle>;
    {readonly.objectMotionEpoch(key)} -> std::convertible_to<uint64_t>;
    {readonly.objectMotionContext(key,previousVP)} -> std::convertible_to<bool>;
    post.acceptObjectMotionVectors(velocity);
    post.acceptMotionReactiveMask(velocity);
};
static_assert(PrismaLumaFrame<PostProcess>);
static_assert(PrismaLumaFrame<LumaWeaveVulkanPostProcess>);

// Runtime absence on unrelated PostProcesses MUST remain no-op.
struct EmptyPost final:PostProcess {
    bool active() const override{return true;}
    gfx::PassDesc scenePass(const gfx::PassDesc& out,int,int) override{return out;}
    void resolve(gfx::Device&,const gfx::PassDesc&,const PostFrame&) override {}
};

int main() {
    EmptyPost post;
    AIVERSE_TEST_CHECK(!post.wantsMaterialReceiverPass());
    AIVERSE_TEST_CHECK(!post.wantsSurfaceReceiverPass());
    AIVERSE_TEST_CHECK(!post.wantsObjectMotionVectors());
    AIVERSE_TEST_CHECK(!post.objectMotionSceneDepth().valid());
    MotionTextureInput typed=MotionTextureInput::declared(
        {11},32,32,gfx::PixelFormat::RGBA8);
    post.acceptMotionReactiveMask(typed);
    post.acceptObjectMotionVectors(typed);
    AIVERSE_TEST_CHECK(!post.materialReceiverPass(32,32).colorTarget.valid());
    AIVERSE_TEST_CHECK(!post.surfaceReceiverPass(32,32).colorTarget.valid());
    return 0;
}
