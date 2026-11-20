// Vulkan spatial + reprojected temporal resolve for already BRDF-reflected
// LumaWeave DIRECT light. No new GI/baked contribution, no albedo remultiply.
// A single compute pass writes RGBA16F: filtered RGB + current scene depth A.
// Source/geometry/view epochs are checked by the CPU owner BEFORE enabling
// reprojection; the GPU additionally checks historical projected depth,
// current material/surface depth, current normal and local color envelope.
// Previous history must be a texture from an actual completed VkFence.
#pragma once
namespace aiverse::render::luma_vk {
inline constexpr const char* kDenoiseCompute=R"GLSL(
#version 450
layout(local_size_x=64,local_size_y=1,local_size_z=1) in;
// Fixed GI-compatible compute RHI ABI: four SSBOs, one storage image,
// seven sampled textures and exactly 80 bytes of push constants.
layout(set=0,binding=0,std430) readonly buffer LightDummy {float lightData[];};
layout(set=0,binding=1,std430) readonly buffer FilterData {
    mat4 inverseCurrentVP;    // original GL convention (-1..1 Z)
    mat4 previousVP;          // original GL convention (-1..1 Z)
    vec4 eye;
    vec4 dimensions;          // low width/height, scene width/height
    vec4 settings;            // spatial depth, normal dot, history weight, clamp
    vec4 flags;               // historyValid, reprojDepthTolerance, motionBound, requireValidMotion
} view;
layout(set=0,binding=2,std430) readonly buffer TriangleDummy {float triangleData[];};
layout(set=0,binding=3,std430) readonly buffer NodeDummy {float nodeData[];};
layout(rgba16f,set=0,binding=4) writeonly uniform image2D filteredOutput;
layout(set=0,binding=5) uniform sampler2D rawLight;
layout(set=0,binding=6) uniform sampler2D previousHistory;
layout(set=0,binding=7) uniform sampler2D sceneDepth;
layout(set=0,binding=8) uniform sampler2D surfaceNormal;
layout(set=0,binding=9) uniform sampler2D surfaceDepth;
layout(set=0,binding=10) uniform sampler2D objectMotion;
layout(set=0,binding=11) uniform sampler2D previousNormals;
layout(push_constant) uniform Params {
    uvec4 counts;uvec4 control;vec4 spare0;vec4 spare1;vec4 spare2;
} params;

bool finite3(vec3 value) {
    return !any(isnan(value)) && !any(isinf(value));
}
vec3 normalAt(vec2 uv) {
    vec3 raw=textureLod(surfaceNormal,uv,0.0).xyz;
    float n2=dot(raw,raw);
    return finite3(raw) && n2>.25 ? raw*inversesqrt(n2) : vec3(0.0);
}

void main() {
    uint index=gl_GlobalInvocationID.x;
    if(index>=params.counts.y || params.control.y==0u)return;
    ivec2 extent=textureSize(rawLight,0);
    ivec2 pixel=ivec2(int(index%params.control.y),
                      int(index/params.control.y));
    if(any(greaterThanEqual(pixel,extent)))return;
    vec4 current=texelFetch(rawLight,pixel,0);
    vec2 uv=(vec2(pixel)+vec2(.5))/vec2(extent);
    float sceneZ=textureLod(sceneDepth,uv,0.0).r;
    float receiverZ=textureLod(surfaceDepth,uv,0.0).r;
    float depthTolerance=view.settings.x;
    if(current.a<=0.0 || current.a>=.99999 ||
       sceneZ>=.99999 || receiverZ>=.99999 ||
       abs(current.a-sceneZ)>depthTolerance ||
       abs(receiverZ-sceneZ)>depthTolerance) {
        imageStore(filteredOutput,pixel,vec4(0.0,0.0,0.0,
                                            clamp(sceneZ,0.0,1.0)));
        return;
    }
    vec3 normal=normalAt(uv);
    if(dot(normal,normal)<.5) {
        imageStore(filteredOutput,pixel,vec4(0.0,0.0,0.0,sceneZ));
        return;
    }
    vec3 total=vec3(0.0), minimum=vec3(65504.0),maximum=vec3(0.0);
    float totalWeight=0.0;
    // Conservative 3x3 bilateral filter: depth and normal silhouettes are
    // not blended, so missing coverage cannot import light through walls.
    for(int oy=-1;oy<=1;++oy)for(int ox=-1;ox<=1;++ox) {
        ivec2 q=clamp(pixel+ivec2(ox,oy),ivec2(0),extent-ivec2(1));
        vec4 item=texelFetch(rawLight,q,0);
        if(item.a<=0.0 || item.a>=.99999 ||
           abs(item.a-sceneZ)>depthTolerance)continue;
        vec2 quv=(vec2(q)+vec2(.5))/vec2(extent);
        vec3 qnormal=normalAt(quv);
        if(dot(qnormal,normal)<view.settings.y)continue;
        vec3 sampleLight=clamp(item.rgb,vec3(0),vec3(65504.0));
        if(!finite3(sampleLight))continue;
        float w=1.0/(1.0+float(ox*ox+oy*oy));
        w*=max(0.0,1.0-abs(item.a-sceneZ)/
               max(depthTolerance,0.00001));
        total+=sampleLight*w;
        totalWeight+=w;
        minimum=min(minimum,sampleLight);
        maximum=max(maximum,sampleLight);
    }
    vec3 spatial=totalWeight>0.0?total/totalWeight:
                 clamp(current.rgb,vec3(0),vec3(65504.0));
    if(totalWeight<=0.0){minimum=maximum=spatial;}
    vec3 finalColor=spatial;

    // Current-minus-previous UV velocities (PRISMA Renderer3D contract)
    // identify genuinely moving objects, including skinning/deformation.
    // If per-pixel motion is unknown in a dynamic scene, keep the CURRENT
    // spatial result rather than borrowing history via camera-only motion.
    if(view.flags.x>.5) {
        vec2 oldUv=vec2(-1);
        float expectedDepth=-1.0;
        bool motionValid=false;
        if(view.flags.z>.5) {
            vec4 motion=textureLod(objectMotion,uv,0.0);
            motionValid=motion.b>.5 &&
                        !any(isnan(motion.xy)) &&
                        !any(isinf(motion.xy)) &&
                        all(lessThan(abs(motion.xy),vec2(2.0)));
            if(motionValid)oldUv=uv-motion.xy;
        }
        if(!motionValid && view.flags.w<.5) {
            vec4 world4=view.inverseCurrentVP*
                        vec4(uv*2.0-1.0,sceneZ*2.0-1.0,1.0);
            if(abs(world4.w)>1e-8) {
                vec3 world=world4.xyz/world4.w;
                vec4 projected=view.previousVP*vec4(world,1.0);
                if(projected.w>1e-8 && finite3(projected.xyz)) {
                    vec3 ndc=projected.xyz/projected.w;
                    oldUv=ndc.xy*.5+.5;
                    expectedDepth=ndc.z*.5+.5;
                }
            }
        }
        if(all(greaterThanEqual(oldUv,vec2(0))) &&
           all(lessThanEqual(oldUv,vec2(1)))) {
            vec4 old=textureLod(previousHistory,oldUv,0.0);
            vec4 oldNormal=textureLod(previousNormals,oldUv,0.0);
            float oldN2=dot(oldNormal.rgb,oldNormal.rgb);
            // Old normal/depth must be FROM THE SAME completed history
            // slot as old direct radiance. This catches previous surface
            // identity changes, rotated/deformed skinned mesh normals and
            // disocclusions even when object UV velocity is available.
            const bool coherent=old.a>0.0 && old.a<.99999 &&
                oldNormal.a>0.0 && oldNormal.a<.99999 &&
                oldN2>.25 && finite3(oldNormal.rgb) &&
                finite3(old.rgb) &&
                abs(old.a-oldNormal.a)<=view.flags.y &&
                dot(normal,oldNormal.rgb*inversesqrt(oldN2))>=view.settings.y &&
                (motionValid ||
                 (expectedDepth>0.0 && expectedDepth<.99999 &&
                  abs(old.a-expectedDepth)<=view.flags.y));
            if(coherent) {
                vec3 range=max(maximum-minimum,vec3(.025));
                vec3 low=max(vec3(0),minimum-range*view.settings.w);
                vec3 high=maximum+range*view.settings.w;
                vec3 stable=clamp(old.rgb,low,high);
                finalColor=mix(spatial,stable,view.settings.z);
            }
        }
    }
    imageStore(filteredOutput,pixel,
               vec4(clamp(finalColor,vec3(0),vec3(65504.0)),sceneZ));
}
)GLSL";
} // namespace aiverse::render::luma_vk
