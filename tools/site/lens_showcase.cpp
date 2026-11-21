// LENS showcase host: a GeometryScene filmed by two ECS camera entities.
// Main camera: CameraLensComponent with driveExposure + driveDepthOfField, so the
// thin-lens focus interval and exposure reach PRISMA through CameraPostFxPool.
// Optional second camera renders into its own target; CameraCompositor puts it
// over the main frame as picture-in-picture. Capture with the platform hook:
//   AIVERSE_CONTENT=<dir> AIVERSE_SCREENSHOT=out.ppm AIVERSE_EXIT_AFTER_FRAMES=N
//   AIVERSE_PRISMA_DOF=1   (PRISMA defocus pass opt-in)
//   LENS_SCENE=scene.json  GeometryScene JSON relative to AIVERSE_CONTENT
//   LENS_POST=post.json    PRISMA block; needs depthOfField (+ autoExposure, hdr on)
//   LENS_EYE="x,y,z" LENS_AT="x,y,z"   main camera position and look-at/focus point
//   LENS_LENS="focalMm,fNumber,shutterSeconds,iso[,compensationEV,focusMetres]"
//              focus defaults to |AT-EYE|
//   LENS_PIP="eyeX,eyeY,eyeZ,atX,atY,atZ,fovDeg,x,y,w,h"  second camera; viewport in
//              0..1 of the frame (x,y from the top-left)
//   LENS_SUN, LENS_SUNDIR, LENS_SUNCOLOR, LENS_AMBIENT, LENS_SKY, LENS_LIGHTS as in the
//   SpectraFX host (FX_* there). LENS_W / LENS_H frame size.
// Website frames only: no engine behaviour changes.
#include "engine/platform/Platform.h"
#include "engine/render/GeometryScene.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/CameraPostFxPool.h"
#include "engine/render/CameraCompositor.h"
#include "engine/render/CameraRig.h"
#include "engine/sim/SceneHierarchy.h"
#include "engine/data/Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace aiverse;
using namespace aiverse::render;

namespace {
const char* env(const char* k, const char* d) { const char* v = std::getenv(k); return v && *v ? v : d; }
std::vector<float> floats(const std::string& s) {
    std::vector<float> out; std::stringstream ss(s); std::string p;
    while (std::getline(ss, p, ',')) out.push_back(float(std::atof(p.c_str())));
    return out;
}
Fixed fx(double v) { return Fixed::fromRaw(int64_t(std::llround(v * 65536.0))); }
Vec3 vec(float3 v) { return {fx(v.x), fx(v.y), fx(v.z)}; }

struct Target {
    gfx::TextureHandle color, depth;
    int w = 0, h = 0;
    bool make(gfx::Device& d, int width, int height, const char* label) {
        gfx::TextureDesc c; c.width = width; c.height = height; c.renderTarget = true; c.label = label;
        gfx::TextureDesc z = c; z.format = gfx::PixelFormat::Depth;
        color = d.createTexture(c); depth = d.createTexture(z); w = width; h = height;
        return color.valid() && depth.valid();
    }
};

struct Host {
    Renderer3D r3d;
    GeometryScene scene;
    CameraPostFxPool pool;
    CameraCompositor compositor;
    CameraRigEvaluator rigs;
    World world;
    SceneHierarchy tree;
    Entity mainCam{}, pipCam{};
    Target mainTarget, pipTarget;
    gfx::TextureHandle frameTex;   // 1x1 white: PiP border
    gfx::Rect pipRect{}, pipBorder{};
    std::vector<float> extraLights;
    float3 clear{};
    int frame = 0;

    Entity makeCamera(float3 eye, float3 at, float fovDeg) {
        const Entity target = world.create();
        LocalTransform tl; tl.position = vec(at);
        tree.attach(world, target, tl);
        const Entity cam = world.create();
        LocalTransform cl; cl.position = vec(eye);
        tree.attach(world, cam, cl);
        CameraComponent c; c.verticalFov = fx(fovDeg * 0.0174532925); c.nearPlane = fx(0.05); c.farPlane = fx(400);
        world.add<CameraComponent>(cam, c);
        CameraRigComponent rig; rig.mode = CameraRigMode::LookAt; rig.target = target; rig.smoothing = 0_fx;
        world.add<CameraRigComponent>(cam, rig);
        return cam;
    }

    void init(gfx::Device& d) {
        r3d.init(d);
        r3d.setShadowQuality(ShadowQuality::High);
        r3d.setMaterialQuality(MaterialQuality::High);
        // The Mac window may not report its drawable size during init: size the
        // camera targets from the requested frame instead.
        int w = d.framebufferWidth(), h = d.framebufferHeight();
        if (w <= 0 || h <= 0) { w = std::atoi(env("LENS_W", "1920")); h = std::atoi(env("LENS_H", "1080")); }
        pool.init(d);
        if (const char* file = std::getenv("LENS_POST")) {
            std::string text;
            if (platform::readContentFile(file, text))
                pool.configure(parsePrisma(Json::parse(text), Json()), PrismaQuality::High, false);
            else std::fprintf(stderr, "[lens-showcase] post not found: %s\n", file);
        }
        if (const char* file = std::getenv("LENS_SCENE")) {
            std::string text;
            if (platform::readContentFile(file, text))
                scene.load(d, Json::parse(text), [](const std::string& p, std::string& out) { return platform::readContentFile(p, out); });
            else std::fprintf(stderr, "[lens-showcase] scene not found: %s\n", file);
        }
        auto eye = floats(env("LENS_EYE", "0,1.6,6")); eye.resize(3, 0.f);
        auto at = floats(env("LENS_AT", "0,1.2,0")); at.resize(3, 0.f);
        mainCam = makeCamera({eye[0], eye[1], eye[2]}, {at[0], at[1], at[2]}, 40);
        auto l = floats(env("LENS_LENS", "50,1.8,0.0166667,100"));
        l.resize(6, 0.f);
        CameraLensComponent lens;
        lens.physicalFov = true;
        lens.focalLengthMm = fx(l[0]); lens.fNumber = fx(l[1]);
        lens.shutterSeconds = fx(l[2]); lens.iso = fx(l[3]);
        lens.exposureCompensation = fx(l[4]);
        const float dx = at[0] - eye[0], dy = at[1] - eye[1], dz = at[2] - eye[2];
        lens.focusDistance = fx(l[5] > 0 ? l[5] : std::sqrt(dx * dx + dy * dy + dz * dz));
        lens.driveExposure = true; lens.driveDepthOfField = true;
        world.add<CameraLensComponent>(mainCam, lens);
        mainTarget.make(d, w, h, "lens main camera");
        if (const char* p = std::getenv("LENS_PIP")) {
            auto v = floats(p); v.resize(11, 0.f);
            pipCam = makeCamera({v[0], v[1], v[2]}, {v[3], v[4], v[5]}, v[6] > 0 ? v[6] : 50);
            pipRect = {int(v[7] * w), int(v[8] * h), int(v[9] * w), int(v[10] * h)};
            pipTarget.make(d, pipRect.w, pipRect.h, "lens pip camera");
            const int b = std::max(2, h / 270);
            pipBorder = {pipRect.x - b, pipRect.y - b, pipRect.w + 2 * b, pipRect.h + 2 * b};
            gfx::TextureDesc t; t.width = t.height = 1; const uint8_t white[4] = {236, 230, 220, 255}; t.data = white;
            frameTex = d.createTexture(t);
        }
        tree.update(world);
        compositor.init(d);
        extraLights = floats(env("LENS_LIGHTS", ""));
        auto sd = floats(env("LENS_SUNDIR", "0.6,0.35,-0.5")); sd.resize(3, 0.5f);
        r3d.lighting.sunDir = {sd[0], sd[1], sd[2]};
        if (const char* c = std::getenv("LENS_SUNCOLOR")) { auto sc = floats(c); sc.resize(3, 1.f); r3d.lighting.sunColor = {sc[0], sc[1], sc[2]}; }
        if (const char* a = std::getenv("LENS_AMBIENT")) {
            auto am = floats(a); am.resize(6, 0.1f);
            r3d.lighting.skyColor = {am[0], am[1], am[2]}; r3d.lighting.groundColor = {am[3], am[4], am[5]};
        }
        r3d.lighting.sunIntensity = float(std::atof(env("LENS_SUN", "1.0")));
        r3d.lighting.fogDensity = 0.f;
        auto sky = floats(env("LENS_SKY", "0.5,0.6,0.75")); sky.resize(3, 0.5f);
        clear = {sky[0], sky[1], sky[2]};
    }

    void drawView(gfx::Device& d, Entity cam, const Target& t, bool post) {
        EcsCameraView view;
        if (!resolveEcsCamera(world, cam, view)) return;
        rigs.apply(world, view, 0.0, 0.f);
        Camera camera = post ? pool.prepare(view, t.w, t.h) : view.camera;
        r3d.beginScene(camera, t.w, t.h);
        for (size_t i = 0; i + 8 <= extraLights.size(); i += 8) {
            const float* e = &extraLights[i];
            r3d.lights.add({e[0], e[1], e[2]}, e[3], {e[4], e[5], e[6]}, e[7]);
        }
        scene.draw(r3d);
        gfx::PassDesc pass;
        pass.colorTarget = t.color; pass.depthTarget = t.depth;
        pass.clearColor[0] = clear.x; pass.clearColor[1] = clear.y; pass.clearColor[2] = clear.z;
        PostProcess* pp = post ? pool.get(cam) : nullptr;
        r3d.render(pass, pp);
        if (pp) r3d.resolvePrisma();
        d.endPass();
    }

    void tick(gfx::Device& d) {
        d.beginFrame(platform::framebufferWidth(), platform::framebufferHeight());
        pool.bindWorld(world);
        drawView(d, mainCam, mainTarget, true);
        if (pipTarget.color.valid()) drawView(d, pipCam, pipTarget, false);
        std::vector<CameraCompositeLayer> layers{{mainTarget.color, {0, 0, mainTarget.w, mainTarget.h}, 0, 1.f, 1}};
        if (pipTarget.color.valid()) {
            layers.push_back({frameTex, pipBorder, 1, 1.f, 2});
            layers.push_back({pipTarget.color, pipRect, 2, 1.f, 3});
        }
        compositor.composite(layers, mainTarget.w, mainTarget.h);
        if (++frame == 3) {
            const auto s = pool.lensState(mainCam);
            std::fprintf(stderr, "[lens-showcase] post=%d pip=%d\n", pool.get(mainCam) ? 1 : 0, pipTarget.color.valid() ? 1 : 0);
            std::fprintf(stderr, "[lens-showcase] lens EV %+.2f, dof %d focus %.2f m range %.2f m blur %.1f px\n",
                         s.ev, s.depthOfField ? 1 : 0, s.dof.focusDistance, s.dof.focusRangeMeters, s.dof.maxBlur);
        }
    }
};
std::unique_ptr<Host> g;
} // namespace

int aiverseMain(int, char**) {
    platform::AppConfig cfg;
    cfg.title = "AIVERSE LENS showcase";
    cfg.width = std::atoi(env("LENS_W", "1920"));
    cfg.height = std::atoi(env("LENS_H", "1080"));
    cfg.highDpi = false;
    platform::AppCallbacks cb;
    cb.init = [](gfx::Device& d) { g = std::make_unique<Host>(); g->init(d); };
    cb.frame = [](gfx::Device& d, double) { g->tick(d); };
    cb.event = [](const platform::Event&) {};
    cb.shutdown = [] { g.reset(); };
    return platform::run(cfg, cb);
}
