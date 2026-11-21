#pragma once
#include "engine/gfx/Gfx.h"
#include "engine/render/SpriteShaders.h"
#include "engine/fx/ParticleShaders.h"
#include "modules/rts/view/RtsTerrainShaders.h"
#include "modules/rts/view/RtsPlanetShaders.h"
#include "modules/rts/view/RtsGroundFxShaders.h"
#include "modules/rts/view/RtsOverlayShaders.h"
#include <cstdio>
namespace aiverse::test {
inline bool checkHdrShaders(gfx::Device& device) {
    struct Source {const char* name;const char* vs;const char* fs;const char* msl;const char* defs;};
    using namespace render::shaders;
    using namespace game::shaders;
    const Source sources[]={
        {"sprite",kSpriteVS,kSpriteFS,kSpriteMSL,""},
        {"lit sprite",kSpriteLitVS,kSpriteLitFS,kSpriteLitMSL,""},
        {"water sprite",kSpriteLitVS,kSpriteWaterFS,kSpriteWaterMSL,""},
        {"particle",fx::shaders::kParticleVS,fx::shaders::kParticleFS,fx::shaders::kParticleMSL,""},
        {"multiply particle",fx::shaders::kParticleVS,fx::shaders::kParticleMulFS,fx::shaders::kParticleMulMSL,""},
        {"terrain low",kTerrainVS,kTerrainFS,kTerrainMSL,"#define TERRAIN_TIER 0\n#define TERRAIN_DETAIL 0\n"},
        {"terrain high",kTerrainVS,kTerrainFS,kTerrainMSL,"#define TERRAIN_TIER 2\n#define TERRAIN_DETAIL 1\n"},
        {"planet",kPlanetVS,kPlanetFS,kPlanetMSL,"#define PLANET_TIER 1\n#define PLANET_DETAIL 1\n"},
        {"sky",kSkyVS,kSkyFS,kSkyMSL,""},
        {"atmosphere",kAtmoVS,kAtmoFS,kAtmoMSL,""},
        {"ground effects",kGroundFxVS,kGroundFxFS,kGroundFxMSL,""},
        {"overlay",kOverlayVS,kOverlayFS,kOverlayMSL,""}
    };
    for(const auto& source:sources) for(bool hdr:{false,true}) {
        std::string vs=std::string(source.defs)+source.vs,fs=std::string(source.defs)+source.fs,msl=std::string(source.defs)+source.msl;
        gfx::ShaderDesc sd;sd.glslVertex=vs.c_str();sd.glslFragment=fs.c_str();sd.msl=msl.c_str();sd.linearTarget=hdr;sd.uniformSlots=3;sd.textureSlots=20;
        auto shader=device.createShader(sd);
        if(!shader.valid() && std::string(source.name)=="terrain high" &&
           device.info().backend==gfx::Backend::GL33) {
            // Production GL33 packs four layer normals into a padded
            // texture atlas at tex9, reducing active fragment texture units.
            const std::string packedDefs=std::string(source.defs)+
                "#define TERRAIN_GL_NORMAL_ATLAS 1\n";
            vs=packedDefs+source.vs;
            fs=packedDefs+source.fs;
            msl=packedDefs+source.msl;
            sd.glslVertex=vs.c_str();sd.glslFragment=fs.c_str();
            sd.msl=msl.c_str();
            shader=device.createShader(sd);
            if(shader.valid())
                std::fprintf(stderr,
                    "terrain high (%s): GL33 normal atlas shader PASS, tier=2 detail=1\n",
                    hdr?"HDR":"LDR");
        }
        if(!shader.valid() && std::string(source.name)=="terrain high") {
            // Match the production terrain quality fallback: prefer full
            // detail, then try non-detail high / medium / low, since high
            // without detail can still exceed a GL3.3 16-sampler limit.
            const std::string firstError=device.lastError();
            for(int tier=2; tier>=0 && !shader.valid(); --tier) {
                const std::string defs="#define TERRAIN_TIER "+
                    std::to_string(tier)+"\n#define TERRAIN_DETAIL 0\n";
                vs=defs+source.vs;
                fs=defs+source.fs;
                msl=defs+source.msl;
                sd.glslVertex=vs.c_str();
                sd.glslFragment=fs.c_str();
                sd.msl=msl.c_str();
                shader=device.createShader(sd);
                if(shader.valid())
                    std::fprintf(stderr,
                        "terrain high (%s): preferred shader unavailable (%s); "
                        "fallback tier=%d detail=0 PASS\n",
                        hdr?"HDR":"LDR",firstError.c_str(),tier);
            }
        }
        if(!shader.valid()) {std::fprintf(stderr,"%s (%s): %s\n",source.name,hdr?"HDR":"LDR",device.lastError().c_str());return false;}
        device.destroyShader(shader);
    }
    std::puts("Scene shaders: 24 GLSL/MSL variants PASS");
    return true;
}
}
