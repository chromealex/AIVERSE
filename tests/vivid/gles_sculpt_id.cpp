// VIVID native GLES3 offscreen GPU ID/depth and fenced PBO pixel readback.
// Requires EGL pbuffer + GLES3 context. No window or project simulation.
// Return 77 when no native EGL/GLES3 driver is available (CI SKIP).
#include "engine/gfx/Gfx.h"
#include "engine/character/vivid/SculptGpuIdPass.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace aiverse;
using namespace aiverse::vivid;

namespace {
void require(bool valid,const char* reason) {
    if(!valid)throw std::runtime_error(reason);
}
struct EGLFixture {
    EGLDisplay display=EGL_NO_DISPLAY;
    EGLSurface surface=EGL_NO_SURFACE;
    EGLContext context=EGL_NO_CONTEXT;
    bool initialize() {
        display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if(display==EGL_NO_DISPLAY||!eglInitialize(display,nullptr,nullptr))
            return false;
        if(!eglBindAPI(EGL_OPENGL_ES_API))return false;
        constexpr EGLint kEs3RenderableBit=0x0040;
        const EGLint configAttrs[]{
            EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE,kEs3RenderableBit,
            EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,
            EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,24,EGL_NONE
        };
        EGLConfig config=nullptr;
        EGLint count=0;
        if(!eglChooseConfig(display,configAttrs,&config,1,&count)||
           count<1)return false;
        const EGLint surfaceAttrs[]{EGL_WIDTH,64,EGL_HEIGHT,64,EGL_NONE};
        surface=eglCreatePbufferSurface(display,config,surfaceAttrs);
        if(surface==EGL_NO_SURFACE)return false;
        const EGLint contextAttrs[]{EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
        context=eglCreateContext(display,config,EGL_NO_CONTEXT,contextAttrs);
        return context!=EGL_NO_CONTEXT &&
               eglMakeCurrent(display,surface,surface,context)==EGL_TRUE;
    }
    ~EGLFixture() {
        if(display==EGL_NO_DISPLAY)return;
        eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        if(context!=EGL_NO_CONTEXT)eglDestroyContext(display,context);
        if(surface!=EGL_NO_SURFACE)eglDestroySurface(display,surface);
        eglTerminate(display);
    }
};
std::vector<std::uint8_t> sample(gfx::Device& device,
                                 SculptGpuIdPass& pass,
                                 const render::Model& model,
                                 const SkinnedWeightSurface& surface,
                                 std::uint32_t x,std::uint32_t y,
                                 std::span<const SculptGpuIdVertex> blockers={}) {
    device.beginFrame(64,64);
    SculptGpuPickRequest request{};
    request.generation=1;request.modelRevision=1;
    request.frameSerial=device.frameSerial();
    request.meshId=0;request.width=request.height=64;
    request.x=x;request.y=y;
    require(request.valid(),"native GLES3 frame serial unavailable");
    require(pass.record(model,surface,model.vertices.size(),{},
                        render::mat4::identity(),request,blockers),
            "native GLES3 ID pass failed to submit");
    device.endFrame();
    SculptGpuIdPass::Completed completed;
    for(int i=0;i<80;++i) {
        device.beginFrame(64,64);
        const bool ready=pass.takeCompleted(completed);
        device.endFrame();
        if(ready) {
            require(completed.request==request &&
                    completed.rgba.size()==4,
                    "native GLES3 fenced PBO callback missing/stale");
            return completed.rgba;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw std::runtime_error("native GLES3 pixel readback timed out");
}
}

int main() {
    EGLFixture egl;
    if(!egl.initialize()) {
        std::cout<<"VIVID GLES3 ID SKIP: no native EGL pbuffer/GLES3\n";
        return 77;
    }
    try {
        auto device=gfx::createGLDevice(gfx::Backend::GLES3);
        require(bool(device),"GLES3 Device creation failed");
        render::Model model;
        model.vertices.resize(6);
        const std::array<render::float3,3> points{{
            {-0.7f,-0.7f,0.f},{0.7f,-0.7f,0.f},{0,0.7f,0.f}
        }};
        for(std::size_t i=0;i<3;++i) {
            auto far=points[i],near=points[i];
            far.z=0.8f;near.z=0.2f;
            model.vertices[i].pos=far;
            model.vertices[i+3].pos=near;
        }
        model.indices={0,1,2,3,4,5};
        render::MeshDef mesh;
        render::Primitive primitive;
        primitive.firstIndex=0;primitive.indexCount=6;
        mesh.primitives.push_back(primitive);
        model.meshes.push_back(mesh);
        render::Node owner;owner.mesh=0;
        model.nodes.push_back(owner);
        SkinnedWeightSurface surface;
        surface.mesh=0;surface.ready=true;
        surface.included.assign(6,1);
        for(const auto& v:model.vertices)surface.world.push_back(v.pos);
        SculptGpuIdPass pass;
        require(pass.initialize(*device),"native GLES3 GPU ID shader unavailable");
        require(sample(*device,pass,model,surface,32,32)==
                std::vector<std::uint8_t>({2,0,0,0}),
                "GLES3 GPU ID did not select the nearer triangle");
        require(sample(*device,pass,model,surface,2,2)==
                std::vector<std::uint8_t>({0,0,0,0}),
                "GLES3 background ID is not zero");
        const std::array<SculptGpuIdVertex,3> blocker{{
            {{-0.7f,-0.7f,0.1f},{0,0,0,0}},
            {{ 0.7f,-0.7f,0.1f},{0,0,0,0}},
            {{ 0.0f, 0.7f,0.1f},{0,0,0,0}}
        }};
        require(sample(*device,pass,model,surface,32,32,blocker)==
                std::vector<std::uint8_t>({0,0,0,0}),
                "GLES3 nearer scene occluder failed to write zero-ID depth");
        require(pass.shutdownAfterIdle(),
                "native GLES3 GPU resource retirement failed idle");
        std::cout<<"VIVID native GLES3 ID/depth async PBO pass=1\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"VIVID native GLES3 ID FAIL: "<<error.what()<<"\n";
        return 1;
    }
}
