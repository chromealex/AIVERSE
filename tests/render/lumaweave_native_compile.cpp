// Compile-only gate: force template instantiation of the opt-in native
// Metal/mobile + indirect GI view drivers without linking real RHI backends.
// CMake builds this OBJECT target as part of default ALL with tests enabled.
// The runtime test in lumaweave_gi_frame_contract.cpp covers source contracts.
#include "engine/render/LumaWeaveNativeDriver.h"
#include "engine/render/LumaWeaveWorlds.h"

namespace aiverse::render::compile_contract {

LumaNativeViewResult singleView(LumaWeaveNativeDriver& driver,
                                const LumaNativeViewInput& input,
                                const gfx::PassDesc& output) {
    return driver.frame(input,output,[](Renderer3D&) {});
}

LumaNativeViewResult worldView(LumaWeaveWorlds& worlds,
                               uint64_t worldId,
                               const LumaNativeViewInput& input,
                               const gfx::PassDesc& output) {
    return worlds.frame(worldId,input,output,[](Renderer3D&) {});
}

std::vector<LumaWeaveWorlds::CompletedWorld> pollAllWorlds(
        LumaWeaveWorlds& worlds) {
    return worlds.pollAll();
}

} // namespace aiverse::render::compile_contract
