// Exercise the production GL backend on macOS. Render offscreen: no drawable timing dependency.
#define GL_SILENCE_DEPRECATION
#import <Cocoa/Cocoa.h>
#include "Bench.h"
#include "engine/gfx/gl/GLApi.h"
int rtsMain(int argc, char** argv) {
    try {
    @autoreleasepool {
        NSOpenGLPixelFormatAttribute attrs[]={NSOpenGLPFAOpenGLProfile,NSOpenGLProfileVersion4_1Core,NSOpenGLPFAColorSize,24,NSOpenGLPFADepthSize,24,0};
        NSOpenGLPixelFormat* pf=[[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
        NSOpenGLContext* context=[[NSOpenGLContext alloc] initWithFormat:pf shareContext:nil];
        [context makeCurrentContext];
        if(!context) return 1;
        while (glGetError()!=GL_NO_ERROR) {} // Cocoa context setup may leave legacy GL errors.
        auto dev=rts::gfx::createGLDevice(rts::gfx::Backend::GL33);
        if (GLenum e=glGetError()) {std::fprintf(stderr,"GL device error %x\n",e);return 1;}
        rts::bench::Bench scene(rts::bench::Options::parse(argc,argv));
        const int width=scene.width,height=scene.height;
        if (GLenum e=glGetError()) {std::fprintf(stderr,"GL init error %x\n",e);return 1;}
        rts::gfx::TextureDesc td;td.width=width;td.height=height;td.renderTarget=true;
        scene.output.colorTarget=dev->createTexture(td);td.format=rts::gfx::PixelFormat::Depth;
        scene.output.depthTarget=dev->createTexture(td);
        if (GLenum e=glGetError()) {std::fprintf(stderr,"GL target error %x\n",e);return 1;}
        scene.readOutput=[width,height](rts::gfx::Device::CaptureFn fn){
            std::vector<uint8_t> raw(width*height*4), px(raw.size());
            glReadBuffer(GL_COLOR_ATTACHMENT0);glPixelStorei(GL_PACK_ALIGNMENT,1);
            glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,raw.data());
            for(int y=0;y<height;++y) std::copy_n(raw.data()+y*width*4,width*4,px.data()+(height-1-y)*width*4);
            fn(width,height,std::move(px));
        };
        scene.init(*dev);
        for(int i=0;i<1000000&&!scene.done;++i) {
            scene.draw(*dev);
            GLenum err=glGetError();if(err) {std::fprintf(stderr,"GL frame error %x: %s\n",err,dev->lastError().c_str());scene.fail("GL API error");}
        }
        scene.shutdown(*dev);dev->destroyTexture(scene.output.colorTarget);dev->destroyTexture(scene.output.depthTarget);
        return scene.failed||!scene.done?1:0;
    }
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
