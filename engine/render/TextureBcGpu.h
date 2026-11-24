// GPU BC block encoder, consumes tightly packed RGBA8 from the graph pack pass.
// One invocation per 4x4 block; writes 2 or 4 uints in DDS block order.
#pragma once
#include "TextureBcEncode.h"
namespace aiverse::render::bc_encode {
inline size_t gpuBlockCount(int n){return size_t((n+3)/4)*((n+3)/4);}
inline std::string gpuBcSource(bool metal,int n,Format f){
    if(n<8||n>1024)throw std::invalid_argument("Invalid GPU BC extent");
    std::string s=metal?
        "#include <metal_stdlib>\nusing namespace metal;\n#define V3 int3\n#define ARR thread const uint* p\n#define U2 uint2\n":
        "#define V3 ivec3\n#define ARR uint p[16]\n#define U2 uvec2\nlayout(std430,set=0,binding=12) writeonly buffer Result { uint result[]; };\nlayout(std430,set=0,binding=13) readonly buffer Input { uint input0[]; };\n";
    s+="#define SIDE "+std::to_string(n)+"\n#define BW "+std::to_string((n+3)/4)+"\n";
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
    if(f==Format::BC1)s+="U2 a=colorBlock(p);result[tid*2u]=a.x;result[tid*2u+1u]=a.y;";
    else if(f==Format::BC4)s+="U2 a=channelBlock(p,0);result[tid*2u]=a.x;result[tid*2u+1u]=a.y;";
    else if(f==Format::BC3)s+="U2 a=channelBlock(p,3),b=colorBlock(p);result[tid*4u]=a.x;result[tid*4u+1u]=a.y;result[tid*4u+2u]=b.x;result[tid*4u+3u]=b.y;";
    else s+="U2 a=channelBlock(p,0),b=channelBlock(p,1);result[tid*4u]=a.x;result[tid*4u+1u]=a.y;result[tid*4u+2u]=b.x;result[tid*4u+3u]=b.y;";
    return s+"\n}\n";
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
} // namespace aiverse::render::bc_encode
