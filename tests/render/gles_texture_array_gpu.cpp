// Opt-in real EGL/GLES3 texture-array acceptance. No fake GPU or silent SKIP.
#include "engine/gfx/Gfx.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

using namespace aiverse::gfx;
namespace {
int failures=0;
void check(bool ok,const char* message) {
    if(!ok) { ++failures; std::fprintf(stderr,"FAIL: %s\n",message); }
}
struct Context {
    EGLDisplay display=EGL_NO_DISPLAY;
    EGLSurface surface=EGL_NO_SURFACE;
    EGLContext context=EGL_NO_CONTEXT;
    bool init() {
        display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
        EGLint major=0,minor=0;
        if(display==EGL_NO_DISPLAY || !eglInitialize(display,&major,&minor) ||
           !eglBindAPI(EGL_OPENGL_ES_API))return false;
        const EGLint configAttribs[]={
            EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,
            EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,
            EGL_ALPHA_SIZE,8,EGL_NONE};
        EGLConfig config=nullptr;
        EGLint count=0;
        if(!eglChooseConfig(display,configAttribs,&config,1,&count) || count!=1)
            return false;
        const EGLint surfaceAttribs[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};
        surface=eglCreatePbufferSurface(display,config,surfaceAttribs);
        const EGLint contextAttribs[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
        context=eglCreateContext(display,config,EGL_NO_CONTEXT,contextAttribs);
        return surface!=EGL_NO_SURFACE && context!=EGL_NO_CONTEXT &&
               eglMakeCurrent(display,surface,surface,context)==EGL_TRUE;
    }
    ~Context() {
        if(display==EGL_NO_DISPLAY)return;
        eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        if(context!=EGL_NO_CONTEXT)eglDestroyContext(display,context);
        if(surface!=EGL_NO_SURFACE)eglDestroySurface(display,surface);
        eglTerminate(display);
    }
};
struct Read {
    bool done=false;
    TextureReadbackResult result;
    std::vector<uint8_t> expected;
};
std::vector<uint8_t> image(int size,int pitch,std::array<uint8_t,4> color) {
    std::vector<uint8_t> result(size_t(size)*size_t(pitch),0xA5);
    for(int y=0;y<size;++y)
        for(int x=0;x<size;++x)
            std::memcpy(result.data()+size_t(y)*pitch+size_t(x)*4,color.data(),4);
    return result;
}
std::vector<uint8_t> packed(int size,std::array<uint8_t,4> color) {
    std::vector<uint8_t> result(size_t(size)*size_t(size)*4);
    for(size_t i=0;i<result.size();i+=4)
        std::copy(color.begin(),color.end(),result.begin()+std::ptrdiff_t(i));
    return result;
}
}
int main() {
    Context egl;
    if(!egl.init()) {
        std::fprintf(stderr,"FAIL: EGL ES3 context unavailable (never SKIP)\n");
        return 2;
    }
    GLint version=0;
    glGetIntegerv(GL_MAJOR_VERSION,&version);
    if(version<3) {
        std::fprintf(stderr,"FAIL: expected real OpenGL ES 3.x\n");
        return 2;
    }
    while(glGetError()!=GL_NO_ERROR) {}
    auto dev=createGLDevice(Backend::GLES3);
    check(bool(dev),"create production GLES3 Device");
    if(!dev)return 1;
    TextureDesc desc;
    desc.dimension=TextureDimension::Texture2DArray;
    desc.width=desc.height=4;
    desc.arrayLayers=2;
    desc.mipLevels=2;
    desc.format=PixelFormat::RGBA8;
    desc.filter=Filter::Nearest;
    // Deliberately dirty all unpack states BEFORE resource allocation.
    // An unsafe glTexImage3D would misinterpret null as offset zero in this
    // undersized bound PBO, and a region upload could apply SKIP_* offsets.
    GLuint sentinel=0,pbo=0;
    glGenTextures(1,&sentinel);
    glBindTexture(GL_TEXTURE_2D_ARRAY,sentinel);
    glGenBuffers(1,&pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER,4,nullptr,GL_STREAM_DRAW);
    glPixelStorei(GL_UNPACK_ALIGNMENT,8);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,7);
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT,7);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS,1);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,1);
    glPixelStorei(GL_UNPACK_SKIP_IMAGES,1);
    GLint originalTexture=0,originalPbo=0,originalAlign=0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY,&originalTexture);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&originalPbo);
    glGetIntegerv(GL_UNPACK_ALIGNMENT,&originalAlign);
    auto statePreserved=[&]() {
        GLint tex=0,buffer=0,align=0,row=0,img=0,px=0,rows=0,imgs=0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY,&tex);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&buffer);
        glGetIntegerv(GL_UNPACK_ALIGNMENT,&align);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row);
        glGetIntegerv(GL_UNPACK_IMAGE_HEIGHT,&img);
        glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&px);
        glGetIntegerv(GL_UNPACK_SKIP_ROWS,&rows);
        glGetIntegerv(GL_UNPACK_SKIP_IMAGES,&imgs);
        return tex==originalTexture && buffer==originalPbo && align==originalAlign
            && row==7 && img==7 && px==1 && rows==1 && imgs==1;
    };
    auto atlas=dev->createTexture(desc);
    check(atlas.valid(),"create 2-layer, 2-mip GLES3 texture with dirty PBO");
    check(statePreserved(),"allocation restores all unpack PBO/skip/row state");
    if(!atlas.valid())return 1;
    struct Sample {int size,mip,layer,pitch;std::array<uint8_t,4> color;};
    const std::array<Sample,4> samples{{
        {4,0,0,20,{17,31,53,255}},
        {4,0,1,24,{151,89,24,255}},
        {2,1,0,12,{62,178,93,255}},
        {2,1,1,16,{221,15,145,255}}
    }};
    std::vector<std::shared_ptr<Read>> reads;
    dev->beginFrame(16,16);
    for(const auto& sample:samples) {
        auto src=image(sample.size,sample.pitch,sample.color);
        TextureUploadDesc upload;
        upload.mipLevel=sample.mip;
        upload.arrayLayer=sample.layer;
        upload.width=upload.height=sample.size;
        upload.rowPitchBytes=sample.pitch;
        upload.slicePitchBytes=int(src.size());
        upload.data=src.data();
        check(dev->updateTextureRegion(atlas,upload),
              "GLES3 padded row array layer/mip upload");
        check(statePreserved(),"upload restores texture/PBO/all unpack pixel-store state");
    }
    // Legacy Texture2D allocation and void update must also be isolated
    // from a bound PBO and deliberately dirty UNPACK_ROW/SKIP state.
    TextureDesc legacyDesc;
    legacyDesc.width=legacyDesc.height=4;
    legacyDesc.format=PixelFormat::RGBA8;
    legacyDesc.filter=Filter::Nearest;
    const auto legacy=dev->createTexture(legacyDesc);
    check(legacy.valid(),"legacy Texture2D allocation with dirty unpack state");
    check(statePreserved(),"legacy Texture2D allocation restores unpack state");
    const auto legacyColor=std::array<uint8_t,4>{78,145,214,255};
    // Row 0 carries a distinct marker: readback must return uploaded rows
    // in top-left order (no flip for non-render-target textures).
    const auto legacyMarker=std::array<uint8_t,4>{240,16,32,255};
    auto legacyBytes=image(4,20,legacyColor);
    for(int x=0;x<4;++x)
        std::memcpy(legacyBytes.data()+size_t(x)*4,legacyMarker.data(),4);
    if(legacy.valid()) {
        dev->updateTexture(legacy,0,0,4,4,legacyBytes.data(),20);
        check(statePreserved(),"legacy updateTexture restores unpack/PBO/binding");
        check(dev->lastError().empty(),"legacy updateTexture succeeds with padded row pitch");
    }
    // Readback uses the production RHI FBO/PBO path, not an independent copy
    // of the texture data or the upload code.
    for(const auto& sample:samples) {
        auto result=std::make_shared<Read>();
        result->expected=packed(sample.size,sample.color);
        check(dev->readTextureAsync(atlas,{sample.mip,sample.layer},
              [result](TextureReadbackResult value) {
                  result->result=std::move(value);
                  result->done=true;
              }),"GLES3 layer/mip GPU readback queued");
        reads.push_back(std::move(result));
    }
    if(legacy.valid()) {
        auto read=std::make_shared<Read>();
        read->expected=packed(4,legacyColor);
        for(int x=0;x<4;++x)
            std::memcpy(read->expected.data()+size_t(x)*4,legacyMarker.data(),4);
        check(dev->readTextureAsync(legacy,{0,0},
            [read](TextureReadbackResult value) {
                read->result=std::move(value);
                read->done=true;
            }),"legacy Texture2D GPU readback queued");
        reads.push_back(std::move(read));
    }
    dev->endFrame();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(std::chrono::steady_clock::now()<deadline) {
        if(std::all_of(reads.begin(),reads.end(),
           [](const auto& r){return r->done;}))break;
        dev->beginFrame(16,16);
        dev->endFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    for(const auto& r:reads)
        check(r->done && r->result.success() &&
              r->result.pixels==r->expected,
              "GLES3 array slice/mip GPU bytes equal independent expected pixels");
    check(dev->gpuHealthy(),"GLES3 GPU healthy after array readbacks");
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    glBindTexture(GL_TEXTURE_2D_ARRAY,0);
    glDeleteBuffers(1,&pbo);
    glDeleteTextures(1,&sentinel);
    if(legacy.valid()) dev->destroyTexture(legacy);
    dev->destroyTexture(atlas);
    if(failures)return 1;
    std::puts("PASS: native GLES3 texture-array mip/layer padded upload and state isolation");
    return 0;
}
