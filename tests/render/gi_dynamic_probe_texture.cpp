#include "engine/render/GiDynamicProbeTexture.h"
#include "tests/cloth/FakeDevice.h"
#include "tests/AlwaysCheck.h"
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace aiverse::render;

int main() {
    FakeDevice device;
    GiDynamicProbeReference gi;
    // 130 probes deliberately cross the 128-probe texture-row boundary.
    gi.configure({-1,-1,-1},1,130,1,1,16,5);
    StaticShadowGeometry wall;
    wall.triangles = {{{-4,-4,1},{4,-4,1},{4,4,1},0},
                      {{-4,-4,1},{4,4,1},{-4,4,1},0}};
    wall.albedos = {{1,1,1}};
    wall.build();
    gi.setGeometry(&wall,1);
    gi.setLights({GiMovingLight{1,{0,0,0},{1,0,0},4,10}});

    std::vector<uint32_t> updated;
    auto step = gi.update(130,{0,0,0},&updated);
    AIVERSE_TEST_CHECK(step.published == 130 && updated.size() == 130);

    GiDynamicProbeTexture gpu;
    AIVERSE_TEST_CHECK(gpu.initialize(device,gi.coefficients().size()));
    AIVERSE_TEST_CHECK(gpu.probeCount() == 130 && gpu.height() == 2);
    auto transfer = gpu.upload(gi,updated);
    AIVERSE_TEST_CHECK(transfer.probes == 130 && transfer.regions == 2);
    AIVERSE_TEST_CHECK(transfer.bytes == 130 * 3 * 16);
    const auto handle = gpu.texture();
    const auto& texels = device.textures.at(handle.id).data;
    for (size_t id : {size_t(0),size_t(127),size_t(128),size_t(129)}) {
        const size_t texel = (id / 128) * 384 + (id % 128) * 3;
        for (size_t k=0;k<3;++k) {
            const auto a = texels[texel + k];
            const auto b = gi.coefficient(uint32_t(id)).irradiance[k];
            AIVERSE_TEST_CHECK(a.x == b.x && a.y == b.y &&
                               a.z == b.z && a.w == b.w);
        }
    }

    // Duplicate, unsorted ids must not trigger repeated GPU writes.
    transfer = gpu.upload(gi,{129,128,129,0});
    AIVERSE_TEST_CHECK(transfer.probes == 3 && transfer.regions == 2);

    bool rejected=false;
    try { (void)gpu.upload(gi,{130}); }
    catch (const std::out_of_range&) { rejected=true; }
    AIVERSE_TEST_CHECK(rejected);

    // UploadAll works after a context/recreation event without touching the static grid.
    gpu.release();
    AIVERSE_TEST_CHECK(!gpu.texture().valid() && gpu.probeCount() == 0);
    AIVERSE_TEST_CHECK(gpu.initialize(device,130));
    transfer=gpu.uploadAll(gi);
    AIVERSE_TEST_CHECK(transfer.probes==130 && transfer.regions==2);
    return 0;
}
