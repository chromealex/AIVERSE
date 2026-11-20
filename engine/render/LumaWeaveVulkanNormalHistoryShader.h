// Real scene-derived, low-resolution world-normal + depth history.
// The previous normal must come from a GPU image captured in THAT frame,
// not from the current scene or a reconstructed camera-only approximation.
// RGB = normalized signed world normal, A = that frame's scene depth.
// Invalid receiver coverage yields RGBA(0) and temporal rejection.
#pragma once
namespace aiverse::render::luma_vk {
inline constexpr const char* kNormalHistoryCompute=R"GLSL(
#version 450
layout(local_size_x=64,local_size_y=1,local_size_z=1) in;
layout(set=0,binding=0,std430) readonly buffer B0 {float unused0[];};
layout(set=0,binding=1,std430) readonly buffer B1 {float unused1[];};
layout(set=0,binding=2,std430) readonly buffer B2 {float unused2[];};
layout(set=0,binding=3,std430) readonly buffer B3 {float unused3[];};
layout(rgba16f,set=0,binding=4) writeonly uniform image2D outputNormals;
layout(set=0,binding=5) uniform sampler2D sceneDepth;
layout(set=0,binding=6) uniform sampler2D surfaceNormals;
layout(set=0,binding=7) uniform sampler2D surfaceDepth;
layout(push_constant) uniform P {
    uvec4 counts;uvec4 control;vec4 x;vec4 y;vec4 z;
} p;

void main() {
    uint index=gl_GlobalInvocationID.x;
    if(index>=p.counts.y || p.control.y==0u)return;
    ivec2 pixel=ivec2(int(index%p.control.y),
                      int(index/p.control.y));
    vec2 uv=(vec2(pixel)+.5)/vec2(imageSize(outputNormals));
    float depth=textureLod(sceneDepth,uv,0).r;
    float materialDepth=textureLod(surfaceDepth,uv,0).r;
    vec3 normal=textureLod(surfaceNormals,uv,0).xyz;
    float n2=dot(normal,normal);
    bool good=depth>0 && depth<.99999 && materialDepth>0 &&
              abs(materialDepth-depth)<.008 &&
              !isnan(n2) && !isinf(n2) && n2>.25;
    if(!good) {
        imageStore(outputNormals,pixel,vec4(0));
        return;
    }
    imageStore(outputNormals,pixel,
               vec4(clamp(normal*inversesqrt(n2),vec3(-1),vec3(1)),depth));
}
)GLSL";
} // namespace aiverse::render::luma_vk
