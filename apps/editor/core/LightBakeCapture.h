#pragma once
#include "engine/bake/CapturedBake.h"
#include "engine/render/Renderer3D.h"
#include "engine/view/game/GameLights.h"
#include <cmath>

namespace aiverse::editor {
template<class Height>
void captureBakeEnvironment(bake::CapturedBake& input, const render::Lighting& lighting,
                            const Json& lights, int width, int depth, Height height, bool captureTerrain = true) {
    auto linear = [](render::float3 c) { return render::float3{c.x*c.x, c.y*c.y, c.z*c.z}; };
    input.sun.dir = lighting.sunDir;
    input.sun.color = linear(lighting.sunColor) * lighting.sunIntensity;
    input.sky.sky = linear(lighting.skyColor);
    input.sky.ground = linear(lighting.groundColor);
    input.lights.clear();
    for (const auto& source : game::GameLights::parseMapLights(lights)) {
        if (!source.bake) continue;
        const auto light = game::GameLights::glowLight(source, {source.x, height(source.x,source.z), source.z}, 0, 1, 0, 0);
        bake::BakeLight output;
        output.pos = light.pos;
        output.color = light.color * light.intensity;
        output.radius = light.radius;
        output.dir = light.dir;
        output.cosOuter = light.cosOuter;
        output.cosInner = light.cosInner;
        output.bakedOnly = source.bake == 2;
        input.lights.push_back(output);
    }
    if (!captureTerrain) {
        input.ex = input.ez = 0;
        input.nx = input.nz = 0;
        input.heights.clear();
        return;
    }
    input.ex = float(width);
    input.ez = float(depth);
    input.nx = int(std::ceil(input.ex / input.step)) + 1;
    input.nz = int(std::ceil(input.ez / input.step)) + 1;
    input.heights.resize(size_t(input.nx) * input.nz);
    for (int z = 0; z < input.nz; ++z)
        for (int x = 0; x < input.nx; ++x)
            input.heights[size_t(z)*input.nx + x] =
                height(std::min(x*input.step,input.ex), std::min(z*input.step,input.ez));
}
} // namespace aiverse::editor
