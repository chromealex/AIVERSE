#include "engine/platform/Platform.h"
#include "engine/render/Prisma.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
using namespace aiverse;
namespace {
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
}

int aiverseMain(int, char**) {
    if (!std::getenv("AIVERSE_PRISMA_COMPOSITOR_GPU")) return 77;
    constexpr int width = 96, height = 64;
    render::Prisma prisma;
    gfx::Device* device = nullptr;
    gfx::PassDesc output;
    std::array<std::vector<uint8_t>, 4> captures;
    gfx::TextureHandle camera;
    int frame = 0;
    bool done = false, failed = false;
    platform::AppConfig cfg;
    cfg.title = "PRISMA authored compositor"; cfg.width = width; cfg.height = height; cfg.highDpi = false;
    platform::AppCallbacks app;
    app.init = [&](gfx::Device& dev) {
        device = &dev;
        require(dev.info().backend == gfx::Backend::Metal && prisma.init(dev), "Metal initialization");
        gfx::TextureDesc tex; tex.width = width; tex.height = height; tex.renderTarget = true;
        output.colorTarget = dev.createTexture(tex);
        camera = dev.createTexture(tex);
        tex.format = gfx::PixelFormat::Depth;
        output.depthTarget = dev.createTexture(tex);
        require(output.colorTarget.valid() && output.depthTarget.valid(), "output targets");
        prisma.setViewId(1); prisma.setQuality(render::PrismaQuality::Low);
    };
    app.frame = [&](gfx::Device& dev, double) {
        try {
            require(frame < 120, "readback timeout");
            if (frame < 4) {
                const int index = frame;
                const Json base = Json::parse(R"({"hdr":"off","effects":[{"type":"colorGrade"}]})");
                Json map = index ? Json::parse(R"({"compositor":{
                    "targets":[{"id":"bright","divisor":2},{"id":"output"}],"passes":[
                    {"id":"extract","op":"bright","inputs":["scene"],"target":"bright","amount":4},
                    {"id":"up","op":"blurX","inputs":["bright"],"target":"output"}],"output":"output"}})") : Json{};
                if (index >= 2) map = Json::parse(R"({"compositor":{
                    "sources":[{"id":"mask","cameraInput":0}],"targets":[{"id":"output"}],
                    "passes":[{"id":"shade","op":"shaderGraph","inputs":["mask"],"target":"output","graph":{
                    "version":1,"domain":"postfx","nodes":[{"id":"src","op":"input"}],"outputs":{"color":"src"}}}],
                    "output":"output"}})");
                if (index < 3) prisma.configure(base, map);
                if (index == 2) {
                    gfx::PassDesc external = output;
                    external.colorTarget = camera;
                    external.clearColor[0] = 0; external.clearColor[1] = 1; external.clearColor[2] = 0;
                    dev.beginPass(external); dev.endPass();
                    render::Prisma::CameraInputs inputs{};
                    inputs[0] = camera;
                    prisma.setCompositorCameraInputs(inputs);
                }
                require(prisma.active(), "active compositor");
                auto scene = prisma.scenePass(output,width,height);
                scene.clearColor[0] = .55f; scene.clearColor[1] = .35f; scene.clearColor[2] = .1f;
                dev.beginPass(scene); dev.endPass();
                render::PostFrame data{}; data.width = width; data.height = height;
                prisma.resolve(dev,output,data); dev.endPass();
                const auto* report = prisma.effectReport(1);
                require(report && report->compositorPasses == std::array<int,4>{0,2,1,0}[index], prisma.lastError() + " pass count");
                require(dev.captureTexture(output.colorTarget,[&,index](int, int, std::vector<uint8_t> image) {
                    captures[index] = std::move(image);
                }), "capture submission");
            }
            ++frame;
            if (!captures[3].empty()) {
                for (const auto& image : captures) require(image.size() == width*height*4, "dimensions");
                for (size_t i = 0; i < captures[0].size(); i += 4) {
                    require(captures[0][i] > 30 && captures[1][i] < 4, "two-pass mismatch");
                    require(captures[2][i] < 4 && captures[2][i+1] > 245, "camera graph mismatch");
                }
                int maxError = 0;
                for (size_t i = 0; i < captures[3].size(); ++i)
                    maxError = std::max(maxError, std::abs(int(captures[3][i]) - int(captures[0][i])));
                std::printf("Fallback versus baseline maximum byte error %d\n", maxError);
                // The standard final composite applies frame-dependent output dithering.
                require(maxError <= 2, "stale camera input or lost fallback scene");
                done = true; platform::requestQuit();
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr,"FAIL %s\n",e.what()); failed = true; platform::requestQuit();
        }
    };
    app.shutdown = [&] {
        if (device) {
            prisma.shutdown();
            device->destroyTexture(camera);
            device->destroyTexture(output.colorTarget);
            device->destroyTexture(output.depthTarget);
        }
        if (failed || !done) std::exit(1);
        std::puts("PASS Metal compositor GPU readback: 6144 pixels each for baseline, two passes, camera shader and missing-camera fallback");
    };
    platform::run(cfg,app);
    return done && !failed ? 0 : 1;
}
