// Native GPU IBL prefilter, retaining caller-owned resources through GPU fences.
#pragma once
#include "EnvironmentIbl.h"
#include "engine/gfx/BufferTextureCopy.h"
#include <string>
namespace aiverse::render::environment_ibl {
static_assert(bandWidth==130&&bandHeight==66&&radianceHeight==330&&combinedHeight==460);
inline bool gpuPrefilterSupported(gfx::Device& d){
    const auto b=d.info().backend;
    return (b==gfx::Backend::Metal||b==gfx::Backend::Vulkan)&&d.gpuHealthy()&&d.supportsCompute()&&
        d.supportsStorageBufferTextureCopy()&&d.supports(gfx::PixelFormat::RGBA16F)&&d.maxFragmentTextureSlots()>=14;
}
inline std::string gpuPrefilterSource(bool metal,int side){
    if(side<8||side>1024||side%2)throw std::invalid_argument("Invalid GPU environment extent");
    const auto layout=gfx::rgbaCopyLayout(bandWidth,combinedHeight,8);
    std::string src=metal?
        "#include <metal_stdlib>\nusing namespace metal;\n#define V3 float3\n#define V2 float2\n#define TEX_ARG texture2d<float,access::read> source,\n#define TEX_PASS source,\n#define FETCH(x,y) source.read(uint2(x,y)).rgb\n#define AZ(y,x) atan2(y,x)\n":
        "#define V3 vec3\n#define V2 vec2\n#define TEX_ARG\n#define TEX_PASS\nlayout(set=0,binding=4) uniform sampler2D source;\nlayout(std430,set=0,binding=12) writeonly buffer Output { uint outWords[]; };\n#define FETCH(x,y) texelFetch(source,ivec2(x,y),0).rgb\n#define AZ(y,x) atan(y,x)\n";
    src+="#define SIDE "+std::to_string(side)+"\n#define PITCH "+std::to_string(layout.rowPitchBytes/4)+"\n#define PI 3.14159265358979323846f\n";
    src+=R"SHADER(
float radical(uint b){
    b=(b<<16)|(b>>16);
    b=((b&0x55555555u)<<1)|((b&0xaaaaaaaau)>>1);
    b=((b&0x33333333u)<<2)|((b&0xccccccccu)>>2);
    b=((b&0x0f0f0f0fu)<<4)|((b&0xf0f0f0f0u)>>4);
    b=((b&0x00ff00ffu)<<8)|((b&0xff00ff00u)>>8);
    return float(b)*2.3283064365386963e-10f;
}
V3 directionAt(float u,float v){
    float phi=(u-0.5f)*2.0f*PI,theta=v*PI,s=sin(theta);
    return V3(cos(phi)*s,cos(theta),sin(phi)*s);
}
V3 texelLatLong(TEX_ARG int x,int y){
    x=(x%SIDE+SIDE)%SIDE;y=clamp(y,0,SIDE/2-1);
    return 0.5f*(clamp(FETCH(x,2*y),V3(0.0f),V3(65504.0f))+
                clamp(FETCH(x,2*y+1),V3(0.0f),V3(65504.0f)));
}
V3 sampleLatLong(TEX_ARG V3 d){
    float u=AZ(d.z,d.x)/(2.0f*PI)+0.5f,v=acos(clamp(d.y,-1.0f,1.0f))/PI;
    float fx=u*float(SIDE)-0.5f,fy=v*float(SIDE/2)-0.5f;
    int x=int(floor(fx)),y=int(floor(fy));float a=fx-float(x),b=fy-float(y);
    return mix(mix(texelLatLong(TEX_PASS x,y),texelLatLong(TEX_PASS x+1,y),a),
               mix(texelLatLong(TEX_PASS x,y+1),texelLatLong(TEX_PASS x+1,y+1),a),b);
}
V3 halfVector(float rough,float u,float v){
    float a=rough*rough,a2=a*a;
    float z=sqrt((1.0f-u)/(1.0f+(a2-1.0f)*u)),s=sqrt(max(0.0f,1.0f-z*z)),phi=2.0f*PI*v;
    return V3(s*cos(phi),s*sin(phi),z);
}
float smith(float nd,float a2){return nd<=0.0f?0.0f:2.0f*nd/(nd+sqrt(a2+(1.0f-a2)*nd*nd));}
float fifth(float x){float x2=x*x;return x2*x2*x;}
V2 brdf(float ndv,float rough){
    V3 v=V3(sqrt(max(0.0f,1.0f-ndv*ndv)),0.0f,ndv);
    float a=rough*rough,a2=a*a,gv=smith(ndv,a2);
    V2 sum=V2(0.0f);
    for(uint i=0u;i<512u;++i){
        V3 h=halfVector(rough,(float(i)+0.5f)/512.0f,radical(i));
        float vdh=dot(v,h),ndl=2.0f*vdh*h.z-ndv;
        if(vdh<=0.0f||ndl<=0.0f||h.z<=0.0f)continue;
        float vis=gv*smith(ndl,a2)*vdh/(h.z*ndv),fc=fifth(1.0f-clamp(vdh,0.0f,1.0f));
        sum+=V2((1.0f-fc)*vis,fc*vis);
    }
    return clamp(sum/512.0f,V2(0.0f),V2(1.0f));
}
V3 filtered(TEX_ARG int band,int x,int y){
    V3 n=directionAt((float(x)+0.5f)/128.0f,(float(y)+0.5f)/64.0f);
    if(band==0)return sampleLatLong(TEX_PASS n);
    V3 t=normalize(cross(abs(n.y)<0.99f?V3(0.0f,1.0f,0.0f):V3(1.0f,0.0f,0.0f),n)),b=cross(n,t);
    V3 sum=V3(0.0f);float weights=0.0f;
    uint count=band==4?128u:256u;
    for(uint i=0u;i<count;++i){
        float u=(float(i)+0.5f)/float(count),v=radical(i);V3 l;float w;
        if(band==4){float phi=2.0f*PI*v,s=sqrt(u);l=V3(s*cos(phi),s*sin(phi),sqrt(1.0f-u));w=1.0f;}
        else {V3 h=halfVector(float(band)/3.0f,u,v);l=2.0f*h.z*h-V3(0.0f,0.0f,1.0f);w=max(0.0f,l.z);}
        if(w<=0.0f)continue;
        sum+=sampleLatLong(TEX_PASS normalize(t*l.x+b*l.y+n*l.z))*w;weights+=w;
    }
    return clamp(sum/max(weights,0.000001f),V3(0.0f),V3(65504.0f));
}
V3 atlasTexel(TEX_ARG int x,int y){
    if(y>=330){
        int bx=clamp(x-1,0,127),by=clamp(y-331,0,127);
        V2 ab=brdf((float(bx)+0.5f)/128.0f,(float(by)+0.5f)/128.0f);
        return V3(ab,0.0f);
    }
    int band=y/66,localY=y%66;
    int sx=(x+127)%128,sy=clamp(localY-1,0,63);
    return filtered(TEX_PASS band,sx,sy);
}
)SHADER";
    src+=metal?R"SHADER(
kernel void cs_main(device uint* outWords [[buffer(0)]],
    texture2d<float,access::read> source [[texture(0)]],uint tid [[thread_position_in_grid]]){
    if(tid>=130u*460u)return;
    int x=int(tid%130u),y=int(tid/130u);V3 rgb=atlasTexel(source,x,y);
    uint dst=uint(y*PITCH+x*2);
    outWords[dst]=as_type<uint>(half2(rgb.xy));outWords[dst+1]=as_type<uint>(half2(rgb.z,1.0f));
}
)SHADER":R"SHADER(
void main(){
    uint tid=gl_GlobalInvocationID.x;if(tid>=130u*460u)return;
    int x=int(tid%130u),y=int(tid/130u);V3 rgb=atlasTexel(x,y);
    uint dst=uint(y*PITCH+x*2);
    outWords[dst]=packHalf2x16(rgb.xy);outWords[dst+1]=packHalf2x16(vec2(rgb.z,1.0f));
}
)SHADER";
    return src;
}
struct GpuPrefilter {
    gfx::TextureHandle source{},atlas{};
    gfx::BufferHandle packed{};
    gfx::ComputeHandle pipeline{};
    uint64_t fence=0;
    int side=0;
    bool encoded=false;
    void releaseAfterIdle(gfx::Device& dev){
        if(source.valid())dev.destroyTexture(source);
        if(atlas.valid())dev.destroyTexture(atlas);
        if(packed.valid())dev.destroyBuffer(packed);
        if(pipeline.valid())dev.destroyCompute(pipeline);
        *this={};
    }
    bool encode(gfx::Device& dev){
        if(!gpuPrefilterSupported(dev)||!source.valid()||fence)return false;
        gfx::TextureInfo info{};
        if(!dev.textureInfo(source,info)||info.width!=side||info.height!=side||info.format!=gfx::PixelFormat::RGBA16F)return false;
        const auto token=dev.gpuSubmissionFence();if(!token||token<=dev.gpuCompletedFence())return false;
        fence=token;
        const bool metal=dev.info().backend==gfx::Backend::Metal;
        auto code=gpuPrefilterSource(metal,side);
        pipeline=dev.createCompute({metal?code.c_str():nullptr,metal?nullptr:code.c_str(),"Environment GGX irradiance BRDF"});
        const auto layout=gfx::rgbaCopyLayout(bandWidth,combinedHeight,8);
        gfx::BufferDesc bd;bd.type=gfx::BufferType::Storage;bd.usage=gfx::Usage::Immutable;bd.size=layout.bytes;bd.label="Environment atlas packed RGBA16F";
        packed=dev.createBuffer(bd);
        gfx::TextureDesc td;td.width=bandWidth;td.height=combinedHeight;td.format=gfx::PixelFormat::RGBA16F;td.filter=gfx::Filter::Linear;td.label="GPU IBL atlas";
        atlas=dev.createTexture(td);
        if(!pipeline.valid()||!packed.valid()||!atlas.valid()||!dev.beginCompute(pipeline))return false;
        bool ok=false;
        try{dev.setComputeTexture(0,source);dev.setComputeBuffer(0,packed);ok=dev.dispatchChecked(bandWidth*combinedHeight);}
        catch(...){dev.endCompute();throw;}
        dev.endCompute();
        if(!ok||!dev.gpuHealthy()||(metal&&!dev.computeDispatchSucceeded()))return false;
        encoded=dev.copyStorageBufferToTexture2D(packed,atlas,{bandWidth,combinedHeight,0,layout.rowPitchBytes,false,8});
        return encoded;
    }
};
} // namespace aiverse::render::environment_ibl
