#include "engine/render/GiClipmapValidityTexture.h"
#include "engine/render/GiDynamicShaders.h"
#include "engine/render/ModelShaders.h"
#include "tests/AlwaysCheck.h"
#include "tests/cloth/FakeDevice.h"
#include <cstdint>
#include <string>
#include <vector>

using namespace aiverse::render;

int main() {
    FakeDevice device;
    GiClipmapVolume volume;
    volume.configure({0,0,0},1,4,2,2,{0,0,0});
    GiClipmapValidityTexture mask;
    AIVERSE_TEST_CHECK(mask.initialize(device,volume.size()));
    AIVERSE_TEST_CHECK(mask.size()==16 && mask.validCount()==0);
    AIVERSE_TEST_CHECK(device.textures.at(mask.texture().id).r8.size()==128);
    auto stats=mask.sync(volume);
    AIVERSE_TEST_CHECK(stats.changed==0 && stats.bytes==0);

    auto first=volume.claim(2,{0,0,0});
    AIVERSE_TEST_CHECK(first.size()==2);
    for(const auto& t:first)AIVERSE_TEST_CHECK(volume.complete(t));
    stats=mask.sync(volume);
    AIVERSE_TEST_CHECK(stats.changed==2 && mask.validCount()==2);
    const auto& stored=device.textures.at(mask.texture().id).r8;
    for(const auto& ticket:first) {
        AIVERSE_TEST_CHECK(mask.valid(ticket.index));
        AIVERSE_TEST_CHECK(stored[ticket.index]==255);
    }
    AIVERSE_TEST_CHECK(mask.sync(volume).changed==0);

    // Invalidate an accepted probe: texel must be zero before any shader
    // can use its older coefficient. Other accepted probes remain valid.
    const auto changed=first.front();
    AIVERSE_TEST_CHECK(volume.invalidate({changed.position,changed.position},
                                          GiDirtyReason::Lighting)==1);
    stats=mask.sync(volume);
    AIVERSE_TEST_CHECK(stats.changed==1);
    AIVERSE_TEST_CHECK(!mask.valid(changed.index) && stored[changed.index]==0);
    AIVERSE_TEST_CHECK(mask.validCount()==1);

    // Complete the full volume and verify a camera scroll invalidates only
    // the entering physical slab, not the entire R8 mask.
    while(volume.pending()) {
        auto tickets=volume.claim(8,{0,0,0});
        AIVERSE_TEST_CHECK(!tickets.empty());
        for(auto t:tickets)AIVERSE_TEST_CHECK(volume.complete(t));
    }
    stats=mask.sync(volume);
    AIVERSE_TEST_CHECK(mask.validCount()==16);
    AIVERSE_TEST_CHECK(stats.changed==15);
    std::vector<uint32_t> recycled;
    const auto scroll=volume.scroll({1.2f,0,0},&recycled);
    AIVERSE_TEST_CHECK(scroll.changed && recycled.size()==4);
    stats=mask.sync(volume);
    AIVERSE_TEST_CHECK(stats.changed==4);
    AIVERSE_TEST_CHECK(mask.validCount()==12);
    for(const auto id:recycled)
        AIVERSE_TEST_CHECK(!mask.valid(id) && stored[id]==0);

    // The normal shader path remains opt-in and only checks the R8 mask when
    // DYNAMIC_GI_VALIDITY is defined. String checks do not compile GPU code.
    const std::string gl=AIVERSE_GLSL_DYNAMIC_GI;
    const std::string metal=AIVERSE_MSL_DYNAMIC_GI;
    const std::string modelMetal=shaders::kModelMSL;
    AIVERSE_TEST_CHECK(gl.find("tex14")!=std::string::npos);
    AIVERSE_TEST_CHECK(gl.find("DYNAMIC_GI_VALIDITY")!=std::string::npos);
    AIVERSE_TEST_CHECK(metal.find("tex14.read")!=std::string::npos);
    AIVERSE_TEST_CHECK(modelMetal.find("dynamicGiAt(tex13,dgi,tex14")!=std::string::npos);
    AIVERSE_TEST_CHECK(modelMetal.find("texture(14)")!=std::string::npos);

    mask.release();
    AIVERSE_TEST_CHECK(device.textures.empty());
}
