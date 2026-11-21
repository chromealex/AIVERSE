#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include "modules/rts/view/RtsTerrainShaders.h"
#include "engine/gfx/ColorSpace.h"
#include <cstdio>
int main(){@autoreleasepool {
 id<MTLDevice> device=MTLCreateSystemDefaultDevice();if(!device){std::puts("No Metal device");return 2;}
 for(int tier=0;tier<3;++tier)for(int detail=0;detail<2;++detail)for(int hdr=0;hdr<2;++hdr){
 std::string source=aiverse::gfx::colorShaderPrefix(true,hdr)+"#define TERRAIN_TIER "+std::to_string(tier)+"\n#define TERRAIN_DETAIL "+std::to_string(detail)+"\n#define TERRAIN_GL_NORMAL_ATLAS 0\n"+aiverse::game::shaders::kTerrainMSL;
 NSError* error=nil;id<MTLLibrary> library=[device newLibraryWithSource:[NSString stringWithUTF8String:source.c_str()] options:nil error:&error];
 if(!library){std::fprintf(stderr,"tier=%d detail=%d hdr=%d: %s\n",tier,detail,hdr,[[error localizedDescription] UTF8String]);return 1;}
 }
 std::puts("PASS: 12 Metal terrain PBR shader libraries (tiers/detail/HDR)");return 0;
}}
