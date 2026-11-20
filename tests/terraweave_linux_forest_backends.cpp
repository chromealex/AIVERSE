// TerraWeave shared Arbor/Flora acceptance on real Linux GLES3 and Vulkan.
// Uses the production gfx::Device, Renderer3D and ArborForestAdapter.
// No stub GL functions, genre-specific renderer or synthetic pixels.
// PlatformLinux supplies main() forwarding to this aiverseMain().
#include "engine/gfx/Gfx.h"
#include "engine/gfx/vulkan/Vulkan.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/TerraWeaveForestAdapter.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace {
namespace gfx=aiverse::gfx;
namespace render=aiverse::render;
namespace terra=aiverse::terraweave;
constexpr int kSide=256;

struct EglOwner {
    EGLDisplay display=EGL_NO_DISPLAY;
    EGLContext context=EGL_NO_CONTEXT;
    EGLSurface surface=EGL_NO_SURFACE;
    bool init() {
        display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if(display==EGL_NO_DISPLAY || !eglInitialize(display,nullptr,nullptr))
            return false;
        if(!eglBindAPI(EGL_OPENGL_ES_API))return false;
        const EGLint attrs[]={
            EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,
            EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,
            EGL_DEPTH_SIZE,24,EGL_NONE
        };
        EGLConfig config{};
        EGLint count=0;
        if(!eglChooseConfig(display,attrs,&config,1,&count)||count!=1)
            return false;
        const EGLint contextAttrs[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
        context=eglCreateContext(display,config,EGL_NO_CONTEXT,contextAttrs);
        const EGLint surfaceAttrs[]={EGL_WIDTH,kSide,EGL_HEIGHT,kSide,EGL_NONE};
        surface=eglCreatePbufferSurface(display,config,surfaceAttrs);
        return context!=EGL_NO_CONTEXT && surface!=EGL_NO_SURFACE &&
               eglMakeCurrent(display,surface,surface,context)==EGL_TRUE;
    }
    ~EglOwner() {
        if(display==EGL_NO_DISPLAY)return;
        eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        if(surface!=EGL_NO_SURFACE)eglDestroySurface(display,surface);
        if(context!=EGL_NO_CONTEXT)eglDestroyContext(display,context);
        eglTerminate(display);
    }
};

struct Frame {
    int width=0,height=0;
    std::vector<uint8_t> pixels;
};
bool valid(const Frame& f) {
    return f.width==kSide && f.height==kSide &&
           f.pixels.size()==size_t(kSide*kSide*4);
}
bool savePpm(const Frame& f,const char* suffix) {
    const char* prefix=std::getenv("AIVERSE_TERRA_LINUX_CAPTURE");
    if(!prefix || !*prefix)return true;
    if(!valid(f))return false;
    const std::string filename=std::string(prefix)+"-"+suffix+".ppm";
    std::FILE* out=std::fopen(filename.c_str(),"wb");
    if(!out)return false;
    std::fprintf(out,"P6\n%d %d\n255\n",f.width,f.height);
    bool ok=true;
    for(size_t i=0;i<f.pixels.size();i+=4)
        if(std::fwrite(f.pixels.data()+i,1,3,out)!=3) {
            ok=false;break;
        }
    if(std::fclose(out)!=0)ok=false;
    if(ok)std::printf("[terra-linux] capture=%s\n",filename.c_str());
    return ok;
}
int changedPixels(const Frame& a,const Frame& b) {
    if(!valid(a)||!valid(b))return -1;
    int changed=0;
    for(size_t i=0;i<a.pixels.size();i+=4) {
        const int delta=std::max({
            std::abs(int(a.pixels[i])-int(b.pixels[i])),
            std::abs(int(a.pixels[i+1])-int(b.pixels[i+1])),
            std::abs(int(a.pixels[i+2])-int(b.pixels[i+2]))});
        changed+=delta>16;
    }
    return changed;
}
} // namespace

int aiverseMain(int argc,char** argv) {
    if(argc!=2 || (std::string(argv[1])!="gles3" &&
                   std::string(argv[1])!="vulkan")) {
        std::fputs("Usage: terraweave_linux_forest_backends {gles3|vulkan}\n",stderr);
        return 2;
    }
    const bool vulkan=std::string(argv[1])=="vulkan";
    // Keep EGL owner alive longer than the GL device. For offscreen Vulkan,
    // no EGL, X11 or window is necessary (lavapipe is suitable for CI).
    EglOwner egl;
    std::unique_ptr<gfx::Device> dev;
    if(vulkan) {
        std::string error;
        dev=gfx::createVulkanDevice(gfx::VulkanWindow{},&error);
        if(!dev) {
            std::fprintf(stderr,"FAIL [terra-linux] Vulkan unavailable: %s\n",
                         error.c_str());
            return 1; // NEVER pass due to Vulkan CTest's historical skip path
        }
    } else {
        if(!egl.init()) {
            std::fprintf(stderr,"FAIL [terra-linux] EGL ES3 pbuffer unavailable, error=0x%x\n",
                         unsigned(eglGetError()));
            return 1;
        }
        if(!glGetString(GL_VERSION)) {
            std::fputs("FAIL [terra-linux] missing GLES3 context\n",stderr);
            return 1;
        }
        dev=gfx::createGLDevice(gfx::Backend::GLES3);
        if(!dev) {
            std::fputs("FAIL [terra-linux] production GLES3 device unavailable\n",stderr);
            return 1;
        }
    }
    if(dev->info().backend!=(vulkan?gfx::Backend::Vulkan:gfx::Backend::GLES3)) {
        std::fputs("FAIL [terra-linux] wrong GPU backend / silent fallback\n",stderr);
        return 1;
    }
    std::printf("[terra-linux] backend=%s device=%s\n",
                vulkan?"vulkan":"gles3",dev->info().name.c_str());

    render::Renderer3D renderer;
    if(!renderer.init(*dev)) {
        std::fprintf(stderr,"FAIL [terra-linux] Renderer3D init: %s\n",
                     dev->lastError().c_str());
        return 1;
    }
    renderer.setShadowQuality(render::ShadowQuality::Off);
    renderer.setTraceEnabled(false);
    renderer.setModelReflectionsEnabled(false);
    renderer.lights.setQuality(render::LightQuality::Off);
    renderer.lighting.fogDensity=0;

    terra::ArborForestAdapter forest;
    terra::TreeProfile profile;
    profile.speciesId=77002;
    profile.primaryBranches=12;
    profile.minHeightCm=950;
    profile.maxHeightCm=1050;
    forest.addPrototype(profile,0x517eeda1u);
    if(!forest.uploadModels(*dev)) {
        std::fprintf(stderr,"FAIL [terra-linux] GPU forest upload: %s\n",
                     dev->lastError().c_str());
        return 1;
    }
    terra::FoliageInstance instance;
    instance.speciesId=profile.speciesId;
    instance.key=0x1234;
    instance.scalePermille=1000;
    const std::vector<terra::FloraBatch> nearBatches{
        {profile.speciesId,terra::TreeLOD::High,{instance}}};
    const std::vector<terra::FloraBatch> farBatches{
        {profile.speciesId,terra::TreeLOD::Impostor,{instance}}};

    render::RTSCamera camera;
    camera.target={0,5,0};
    camera.distance=27;
    camera.yaw=0.12f;
    camera.pitch=0.15f;
    camera.fovY=0.65f;
    camera.zNear=0.1f;camera.zFar=200;

    const auto frame=[&](const std::vector<terra::FloraBatch>* batches) {
        Frame output;
        dev->captureNextFrame([&output](int width,int height,
                                         std::vector<uint8_t> px) {
            output.width=width;output.height=height;
            output.pixels=std::move(px);
        });
        dev->beginFrame(kSide,kSide);
        renderer.beginScene(camera,kSide,kSide);
        if(batches)forest.submit(renderer,*batches);
        gfx::PassDesc desc{};
        desc.clearColor[0]=0.015f;
        desc.clearColor[1]=0.019f;
        desc.clearColor[2]=0.024f;
        desc.clearColor[3]=1.0f;
        renderer.render(desc);
        dev->endPass();
        dev->endFrame();
        return output;
    };
    const Frame baseline=frame(nullptr);
    const Frame near=frame(&nearBatches);
    const Frame far=frame(&farBatches);
    if(!valid(baseline)||!valid(near)||!valid(far)) {
        std::fputs("FAIL [terra-linux] real backend did not deliver 256x256 RGBA frames\n",stderr);
        return 1;
    }
    const int nearPixels=changedPixels(baseline,near);
    const int farPixels=changedPixels(baseline,far);
    std::printf("[terra-linux] near=%d far=%d\n",nearPixels,farPixels);
    if(nearPixels<=60 || farPixels<=60 ||
       !savePpm(baseline,"baseline") ||
       !savePpm(near,"branch-lod") ||
       !savePpm(far,"impostor")) {
        std::fputs("FAIL [terra-linux] forest missing/capture missing on real GPU\n",stderr);
        return 1;
    }

    if(!dev->waitForResourceIdle()) {
        std::fputs("FAIL [terra-linux] GPU idle barrier failed\n",stderr);
        return 1;
    }
    forest.releaseModels(*dev);
    renderer.shutdown();
    dev.reset(); // the EGL context stays current until GL cleanup is done
    std::printf("PASS [terra-linux] %s real Renderer3D BranchLOD/impostor "
                "pixels and GPU retirement\n",vulkan?"Vulkan":"GLES3");
    return 0;
}
