// Exercise the production GL backend on macOS. Render offscreen: no drawable timing dependency.
#define GL_SILENCE_DEPRECATION
#import <Cocoa/Cocoa.h>
#include "HdrScene.h"
#include "engine/gfx/gl/GLApi.h"
int aiverseMain(int, char**) {
    @autoreleasepool {
        NSOpenGLPixelFormatAttribute attrs[]={NSOpenGLPFAOpenGLProfile,NSOpenGLProfileVersion4_1Core,NSOpenGLPFAColorSize,24,NSOpenGLPFADepthSize,24,0};
        NSOpenGLPixelFormat* pf=[[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
        NSOpenGLContext* context=[[NSOpenGLContext alloc] initWithFormat:pf shareContext:nil];
        [context makeCurrentContext];
        if(!context) return 1;
        while (glGetError()!=GL_NO_ERROR) {} // Cocoa context setup may leave legacy GL errors.
        auto dev=aiverse::gfx::createGLDevice(aiverse::gfx::Backend::GL33);
        if (GLenum e=glGetError()) {std::fprintf(stderr,"GL device error %x\n",e);return 1;}
        aiverse::test::HdrScene scene; scene.init(*dev);
        if (GLenum e=glGetError()) {std::fprintf(stderr,"GL init error %x\n",e);return 1;}
        aiverse::gfx::TextureDesc td;td.width=960;td.height=640;td.renderTarget=true;
        scene.output.colorTarget=dev->createTexture(td);td.format=aiverse::gfx::PixelFormat::Depth;
        scene.output.depthTarget=dev->createTexture(td);
        if (GLenum e=glGetError()) {std::fprintf(stderr,"GL target error %x\n",e);return 1;}
        scene.readOutput=[](aiverse::gfx::Device::CaptureFn fn){
            std::vector<uint8_t> raw(960*640*4), px(raw.size());
            glReadBuffer(GL_COLOR_ATTACHMENT0);glPixelStorei(GL_PACK_ALIGNMENT,1);
            glReadPixels(0,0,960,640,GL_RGBA,GL_UNSIGNED_BYTE,raw.data());
            for(int y=0;y<640;++y) std::copy_n(raw.data()+y*960*4,960*4,px.data()+(639-y)*960*4);
            fn(960,640,std::move(px));
        };
        for(int i=0;i<280&&!scene.captured&&!scene.failed;++i) {
            scene.draw(*dev,960,640);
            GLenum err=glGetError();if(err) {std::fprintf(stderr,"GL frame error %x: %s\n",err,dev->lastError().c_str());scene.failed=true;}
        }
        scene.shutdown(*dev);dev->destroyTexture(scene.output.colorTarget);dev->destroyTexture(scene.output.depthTarget);
        return scene.failed||!scene.captured?1:0;
    }
}
