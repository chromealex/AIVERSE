#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include "texture_graph_gpu_acceptance.h"

// Same standalone RHI shader archive stub as spectrafx_metal_rhi.mm.
namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
int main(int argc,char** argv){
    const bool offscreen=argc==2&&std::string(argv[1])=="--offscreen";
    if(argc!=1&&!offscreen)return 2;
    @autoreleasepool {
        id<MTLDevice> metal=MTLCreateSystemDefaultDevice();
        if(!metal){std::fprintf(stderr,"FAIL Metal device unavailable\n");return 1;}
        CAMetalLayer* layer=[CAMetalLayer layer];layer.device=metal;
        layer.frame=CGRectMake(0,0,64,64);
        auto dev=aiverse::gfx::createMetalDevice((__bridge void*)layer);
        if(!dev){std::fprintf(stderr,"FAIL Metal RHI unavailable\n");return 1;}
        const auto initial=layer.drawableSize;
        const int result=aiverse::tests::textureGraphGpuAcceptance(*dev,aiverse::gfx::Backend::Metal,offscreen);
        if(offscreen&&!CGSizeEqualToSize(initial,layer.drawableSize)){
            std::fprintf(stderr,"FAIL offscreen frame resized drawable\n");return 1;
        }
        return result;
    }
}
