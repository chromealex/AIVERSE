// Release-safe device-free motion input admission for animated geometry.
// PRISMA motion: RG = current-minus-previous normalized UV, B>0.5 valid.
// Texture metadata must describe the actual frame and view; this test does
// not validate GPU image contents or replace Vulkan motion device tests.
#include "engine/render/LumaWeaveVulkanMotionPolicy.h"
#include "tests/AlwaysCheck.h"

using namespace aiverse;
using namespace aiverse::render;
int main() {
    const auto motion=MotionTextureInput::declared(
        gfx::TextureHandle{11},1920,1080,gfx::PixelFormat::RGBA16F);
    const auto match=[&](MotionTextureInput m,int width,int height,
                         uint64_t sourceFrame,uint64_t currentFrame,
                         uint64_t inputView,uint64_t currentView,
                         uint64_t epoch,uint64_t currentEpoch) {
        return LumaVulkanMotionPolicy::current(
            m,width,height,sourceFrame,currentFrame,
            inputView,currentView,epoch,currentEpoch);
    };
    AIVERSE_TEST_CHECK(match(motion,1920,1080,71,71,5,5,3,3));
    AIVERSE_TEST_CHECK(!match(motion,1920,1080,70,71,5,5,3,3));
    AIVERSE_TEST_CHECK(!match(motion,1920,1080,0,71,5,5,3,3));
    AIVERSE_TEST_CHECK(!match(motion,1920,1080,71,0,5,5,3,3));
    AIVERSE_TEST_CHECK(!match(motion,1920,1080,71,71,4,5,3,3));
    AIVERSE_TEST_CHECK(!match(motion,1920,1080,71,71,0,5,3,3));
    AIVERSE_TEST_CHECK(!match(motion,1920,1080,71,71,5,0,3,3));
    AIVERSE_TEST_CHECK(!match(motion,1920,1080,71,71,5,5,2,3));
    AIVERSE_TEST_CHECK(!match(motion,1280,1080,71,71,5,5,3,3));
    AIVERSE_TEST_CHECK(!match(motion,1920,720,71,71,5,5,3,3));
    AIVERSE_TEST_CHECK(!match(motion,0,1080,71,71,5,5,3,3));
    AIVERSE_TEST_CHECK(!match(MotionTextureInput::legacy({11}),
                              1920,1080,71,71,5,5,3,3));
    auto wrong=MotionTextureInput::declared(
        {11},1920,1080,gfx::PixelFormat::RGBA8);
    AIVERSE_TEST_CHECK(!match(wrong,1920,1080,71,71,5,5,3,3));
    wrong=MotionTextureInput::declared(
        {},1920,1080,gfx::PixelFormat::RGBA16F);
    AIVERSE_TEST_CHECK(!match(wrong,1920,1080,71,71,5,5,3,3));

    const auto reactive=MotionTextureInput::declared(
        gfx::TextureHandle{22},1920,1080,gfx::PixelFormat::RGBA8);
    const auto valid=[&](MotionTextureInput image,
                        uint64_t inputFrame,uint64_t deviceFrame,
                        uint64_t view,uint64_t currentView,
                        uint64_t epoch,uint64_t currentEpoch) {
        return LumaVulkanMotionPolicy::reactiveCurrent(
            image,1920,1080,inputFrame,deviceFrame,view,currentView,
            epoch,currentEpoch);
    };
    AIVERSE_TEST_CHECK(valid(reactive,71,71,5,5,3,3));
    AIVERSE_TEST_CHECK(!valid(reactive,70,71,5,5,3,3));
    AIVERSE_TEST_CHECK(!valid(reactive,0,71,5,5,3,3));
    AIVERSE_TEST_CHECK(!valid(reactive,71,71,4,5,3,3));
    AIVERSE_TEST_CHECK(!valid(reactive,71,71,5,5,2,3));
    AIVERSE_TEST_CHECK(!valid(
        MotionTextureInput::declared({22},1920,1080,
                                     gfx::PixelFormat::RGBA16F),
        71,71,5,5,3,3));
    AIVERSE_TEST_CHECK(!valid(MotionTextureInput::legacy({22}),
                              71,71,5,5,3,3));
    AIVERSE_TEST_CHECK(!valid(MotionTextureInput::declared(
        {},1920,1080,gfx::PixelFormat::RGBA8),71,71,5,5,3,3));
    AIVERSE_TEST_CHECK(!LumaVulkanMotionPolicy::reactiveCurrent(
        reactive,1280,1080,71,71,5,5,3,3));
    return 0;
}
