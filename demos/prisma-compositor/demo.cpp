// Draw a high-contrast input plate, then pass it through the same PRISMA
// configuration and resolve path used by a project camera.
#include "engine/render/Prisma.h"
#include <GLES3/gl3.h>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <cstdio>
#include <memory>

using namespace aiverse;
namespace {
constexpr int width = 640, height = 320;
std::unique_ptr<gfx::Device> device;
render::Prisma prisma;

void patch(int x, int y, int w, int h, float r, float g, float b) {
    glScissor(x, y, w, h);
    glClearColor(r, g, b, 1);
    glClear(GL_COLOR_BUFFER_BIT);
}
}

extern "C" EMSCRIPTEN_KEEPALIVE int render_compositor(const char* settings) {
    if (!device) return -1;
    try {
        prisma.configure(Json::parse(settings), Json{});
        device->beginFrame(width, height);
        gfx::PassDesc output;
        const auto scene = prisma.scenePass(output, width, height);
        device->beginPass(scene);
        glEnable(GL_SCISSOR_TEST);
        patch(0, 0, width, height, .028f, .038f, .064f);
        for (int i = 0; i < 6; ++i) {
            const int x = 78 + i * 83, y = 45 + (i % 3) * 28;
            patch(x, y, 8, 148, .25f + i * .13f, .86f - i * .11f, .96f);
            patch(x - 14, y + 28, 37, 4, .92f, .78f, .32f);
        }
        glDisable(GL_SCISSOR_TEST);
        device->endPass();
        render::PostFrame data{};
        data.width = width;
        data.height = height;
        prisma.resolve(*device, output, data);
        device->endPass();
        device->endFrame();
        const auto* report = prisma.effectReport(1);
        return report ? report->compositorPasses : -1;
    } catch (const std::exception& err) {
        std::fprintf(stderr, "%s\n", err.what());
        return -1;
    }
}

int main() {
    EmscriptenWebGLContextAttributes options;
    emscripten_webgl_init_context_attributes(&options);
    options.majorVersion = 2;
    options.alpha = false;
    options.antialias = false;
    options.preserveDrawingBuffer = true;
    auto context = emscripten_webgl_create_context("#frame", &options);
    if (context <= 0 || emscripten_webgl_make_context_current(context) != EMSCRIPTEN_RESULT_SUCCESS)
        return 1;
    device = gfx::createGLDevice(gfx::Backend::GLES3);
    if (!device || !prisma.init(*device)) return 2;
    prisma.setViewId(1);
    prisma.setQuality(render::PrismaQuality::Low);
    EM_ASM({ window.startCompositor(); });
    return 0;
}
