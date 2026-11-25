// GPU BC block encoder, consumes tightly packed RGBA8 from the graph pack pass.
// One invocation per 4x4 block; writes 2 or 4 uints in DDS block order.
#pragma once
#include "TextureBcEncode.h"
namespace aiverse::render::bc_encode {
inline size_t gpuBlockCount(int n){return size_t((n+3)/4)*((n+3)/4);}
inline size_t gpuBcBytes(int n,Format f,bool mips){
    size_t bytesTotal=0;for(;;){bytesTotal+=gpuBlockCount(n)*bytes(f);if(!mips||n==1)return bytesTotal;n=std::max(1,n/2);}
}
inline size_t gpuBcScratchBytes(int n,bool mips){size_t total=0;while(mips&&n>1){n=std::max(1,n/2);total+=size_t(n)*n*4u;}return total;}
inline std::string gpuBcSource(bool metal,int n,Format f,size_t wordOffset=0){
    if(n<1||n>1024)throw std::invalid_argument("Invalid GPU BC extent");
    std::string s=metal?
        "#include <metal_stdlib>\nusing namespace metal;\n#define V3 int3\n#define ARR thread const uint* p\n#define U2 uint2\n":
        "#define V3 ivec3\n#define ARR uint p[16]\n#define U2 uvec2\nlayout(std430,set=0,binding=12) writeonly buffer Result { uint result[]; };\nlayout(std430,set=0,binding=13) readonly buffer Input { uint input0[]; };\n";
    s+="#define SIDE "+std::to_string(n)+"\n#define BW "+std::to_string((n+3)/4)+"\n#define OUT_OFFSET "+std::to_string(wordOffset)+"u\n";
    s+=R"BC(
V3 rgb(uint t){return V3(int(t&255u),int((t>>8)&255u),int((t>>16)&255u));}
uint pack565(V3 t){return uint(((t.x*31+127)/255)<<11|((t.y*63+127)/255)<<5|((t.z*31+127)/255));}
V3 expand565(uint t){int r=int(t>>11),g=int((t>>5)&63u),b=int(t&31u);return V3((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2));}
U2 colorBlock(ARR){
    V3 lo=V3(255),hi=V3(0);
    for(int i=0;i<16;++i){V3 t=rgb(p[i]);lo=min(lo,t);hi=max(hi,t);}
    uint a=pack565(hi),b=pack565(lo);
    if(a<b){uint temp=a;a=b;b=temp;}
    if(a==b){if(a<65535u)++a;else --b;}
    V3 pal[4];pal[0]=expand565(a);pal[1]=expand565(b);pal[2]=(2*pal[0]+pal[1])/3;pal[3]=(pal[0]+2*pal[1])/3;
    uint bits=0u;
    for(int i=0;i<16;++i){int best=0,error=1000000;V3 t=rgb(p[i]);
        for(int j=0;j<4;++j){V3 v=t-pal[j];int e=v.x*v.x+v.y*v.y+v.z*v.z;if(e<error){error=e;best=j;}}
        bits|=uint(best)<<(2*i);
    }
    return U2(a|(b<<16),bits);
}
U2 channelBlock(ARR,int c){
    int lo=255,hi=0;
    for(int i=0;i<16;++i){int v=int((p[i]>>(8*c))&255u);lo=min(lo,v);hi=max(hi,v);}
    int pal[8];pal[0]=hi;pal[1]=lo;
    if(hi>lo){for(int i=2;i<8;++i)pal[i]=((8-i)*hi+(i-1)*lo)/7;}
    else {for(int i=2;i<6;++i)pal[i]=lo;pal[6]=0;pal[7]=255;}
    // 16-bit endpoints followed by 48 bits of 3-bit selectors.
    uint a=uint(hi)|(uint(lo)<<8),b=0u;
    for(int i=0;i<16;++i){
        int v=int((p[i]>>(8*c))&255u),best=0,error=256;
        for(int j=0;j<8;++j){int e=abs(v-pal[j]);if(e<error){error=e;best=j;}}
        int bit=16+3*i;
        if(bit<32){a|=uint(best)<<bit;if(bit>29)b|=uint(best)>>(32-bit);}
        else b|=uint(best)<<(bit-32);
    }
    return U2(a,b);
}
)BC";
    s+=metal?"kernel void cs_main(device uint* result [[buffer(0)]],device const uint* input0 [[buffer(1)]],uint tid [[thread_position_in_grid]]){\n":
        "void main(){uint tid=gl_GlobalInvocationID.x;\n";
    s+="if(tid>=uint(BW*BW))return;int bx=int(tid%uint(BW))*4,by=int(tid/uint(BW))*4;uint p[16];\n";
    s+="for(int y=0;y<4;++y)for(int x=0;x<4;++x)p[y*4+x]=input0[min(SIDE-1,by+y)*SIDE+min(SIDE-1,bx+x)];\n";
    if(f==Format::BC1)s+="U2 a=colorBlock(p);result[OUT_OFFSET+tid*2u]=a.x;result[OUT_OFFSET+tid*2u+1u]=a.y;";
    else if(f==Format::BC4)s+="U2 a=channelBlock(p,0);result[OUT_OFFSET+tid*2u]=a.x;result[OUT_OFFSET+tid*2u+1u]=a.y;";
    else if(f==Format::BC3)s+="U2 a=channelBlock(p,3),b=colorBlock(p);result[OUT_OFFSET+tid*4u]=a.x;result[OUT_OFFSET+tid*4u+1u]=a.y;result[OUT_OFFSET+tid*4u+2u]=b.x;result[OUT_OFFSET+tid*4u+3u]=b.y;";
    else s+="U2 a=channelBlock(p,0),b=channelBlock(p,1);result[OUT_OFFSET+tid*4u]=a.x;result[OUT_OFFSET+tid*4u+1u]=a.y;result[OUT_OFFSET+tid*4u+2u]=b.x;result[OUT_OFFSET+tid*4u+3u]=b.y;";
    return s+"\n}\n";
}
// Ordered GPU box reduction of packed RGBA8. Color is filtered in linear
// light with alpha weighting; normal vectors are renormalized.
inline std::string gpuBcDownsampleSource(bool metal,int side,bool color,bool normal){
    if(side<2||side>1024)throw std::invalid_argument("Invalid BC mip source");
    const int next=std::max(1,side/2);
    std::string s=metal?"#include <metal_stdlib>\nusing namespace metal;\n#define V3 float3\n":
        "#define V3 vec3\nlayout(std430,set=0,binding=12) writeonly buffer Result { uint result[]; };\nlayout(std430,set=0,binding=13) readonly buffer Input { uint input0[]; };\n";
    s+="#define SIDE "+std::to_string(side)+"\n#define NEXT "+std::to_string(next)+"\n";
    s+="#define COLOR "+std::string(color?"1":"0")+"\n#define NORMAL "+std::string(normal?"1":"0")+"\n";
    s+=R"MIP(
float toLinear(float v){return v<=0.04045f?v/12.92f:pow((v+0.055f)/1.055f,2.4f);}
float toSrgb(float v){return v<=0.0031308f?v*12.92f:1.055f*pow(v,1.0f/2.4f)-0.055f;}
uint quant(float v){return uint(floor(clamp(v,0.0f,1.0f)*255.0f+0.5f));}
)MIP";
    s+=metal?"kernel void cs_main(device uint* result [[buffer(0)]],device const uint* input0 [[buffer(1)]],uint tid [[thread_position_in_grid]]){\n":
        "void main(){uint tid=gl_GlobalInvocationID.x;\n";
    s+=R"MIP(
if(tid>=uint(NEXT*NEXT))return;
int x=int(tid%uint(NEXT)),y=int(tid/uint(NEXT)),x0=x*SIDE/NEXT,x1=(x+1)*SIDE/NEXT,y0=y*SIDE/NEXT,y1=(y+1)*SIDE/NEXT;
float count=float((x1-x0)*(y1-y0)),alpha=0.0f,weight=0.0f;V3 sum=V3(0.0f);
for(int yy=y0;yy<y1;++yy)for(int xx=x0;xx<x1;++xx){
    uint t=input0[yy*SIDE+xx];float a=float(t>>24)/255.0f;
    V3 v=V3(float(t&255u),float((t>>8)&255u),float((t>>16)&255u))/255.0f;
    alpha+=a;
#if COLOR
    sum+=V3(toLinear(v.x),toLinear(v.y),toLinear(v.z))*a;weight+=a;
#elif NORMAL
    sum+=2.0f*v-V3(1.0f);
#else
    sum+=v;
#endif
}
#if NORMAL
float len=length(sum);sum=len>0.000001f?0.5f+0.5f*sum/len:V3(0.5f,0.5f,1.0f);
#elif COLOR
sum/=max(weight,0.000001f);sum=V3(toSrgb(sum.x),toSrgb(sum.y),toSrgb(sum.z));
#else
sum/=count;
#endif
result[tid]=quant(sum.x)|(quant(sum.y)<<8)|(quant(sum.z)<<16)|(quant(alpha/count)<<24);
}
)MIP";
    return s;
}
// Compare decoded error against the same CPU encoder. One-unit upstream GPU
// quantization differences can legitimately select different BC endpoints.
inline bool validateGpuBc(std::span<const uint8_t> rgba,int side,Format f,std::span<const uint8_t> blocks){
    if(rgba.size()!=size_t(side)*side*4||blocks.size()!=gpuBlockCount(side)*bytes(f))return false;
    const auto baseline=encodeLevel(rgba,side,side,f);
    const auto format=f==Format::BC1?BlockFormat::BC1:f==Format::BC3?BlockFormat::BC3:f==Format::BC4?BlockFormat::BC4:BlockFormat::BC5;
    const int bw=(side+3)/4,channels=f==Format::BC4?1:f==Format::BC5?2:f==Format::BC1?3:4;
    for(int by=0;by<bw;++by)for(int bx=0;bx<bw;++bx){
        const size_t at=(size_t(by)*bw+bx)*bytes(f);uint8_t gpu[64]{},cpu[64]{};
        if(!decodeBlock(format,blocks.data()+at,gpu)||!decodeBlock(format,baseline.data()+at,cpu))return false;
        int ge=0,ce=0;
        for(int y=0;y<4;++y)for(int x=0;x<4;++x)for(int c=0;c<channels;++c){
            const int v=rgba[(size_t(std::min(side-1,by*4+y))*side+std::min(side-1,bx*4+x))*4+c];
            int g=int(gpu[(y*4+x)*4+c])-v,k=int(cpu[(y*4+x)*4+c])-v;ge+=g*g;ce+=k*k;
        }
        if(ge>ce+16*channels*64+ce/5)return false;
    }
    return true;
}
inline bool validateGpuBcChain(std::span<const uint8_t> rgba,int side,Format f,bool color,bool normal,bool mips,std::span<const uint8_t> blocks){
    if(blocks.size()!=gpuBcBytes(side,f,mips))return false;
    std::vector<uint8_t> pixels(rgba.begin(),rgba.end());size_t offset=0;int n=side;
    for(;;){
        const size_t bytesHere=gpuBlockCount(n)*bytes(f);
        if(!validateGpuBc(pixels,n,f,blocks.subspan(offset,bytesHere)))return false;
        offset+=bytesHere;
        if(!mips||n==1)return offset==blocks.size();
        pixels=nextMip(pixels,n,n,color,normal);n=std::max(1,n/2);
    }
}
inline Encoded wrapGpuBcChain(int side,Format f,bool color,bool mips,std::span<const uint8_t> blocks){
    if(blocks.size()!=gpuBcBytes(side,f,mips))throw std::invalid_argument("Invalid GPU BC chain extent");
    std::vector<std::vector<uint8_t>> levels;size_t offset=0;int n=side;
    for(;;){
        const size_t bytesHere=gpuBlockCount(n)*bytes(f);
        levels.emplace_back(blocks.begin()+offset,blocks.begin()+offset+bytesHere);offset+=bytesHere;
        if(!mips||n==1)break;n=std::max(1,n/2);
    }
    return wrapDds(side,side,f,color,levels);
}
} // namespace aiverse::render::bc_encode
