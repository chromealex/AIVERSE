#include "engine/gfx/Gfx.h"
#include "engine/gfx/d3d/Direct3D.h"
#include <cstdio>
#include <cstring>
using namespace aiverse::gfx;
int main(int argc,char** argv) {
    if(argc!=2 || (std::strcmp(argv[1],"11") && std::strcmp(argv[1],"9")))return 2;
    const bool modern=std::strcmp(argv[1],"11")==0;
    std::string error;
    auto device=createDirect3DDevice(modern?Direct3DVersion::D3D11:Direct3DVersion::D3D9,17,15,nullptr,modern,error);
    if(!device){std::fprintf(stderr,"%s\n",error.c_str());return 1;}
    const char* sm4=R"(
        struct Input {float4 pos:TEXCOORD0;float4 rgba:TEXCOORD1;};
        struct Out {float4 pos:SV_Position;float4 rgba:COLOR0;};
        Out vs_main(Input i){Out o;o.pos=i.pos;o.rgba=i.rgba;return o;}
        float4 ps_main(Out i):SV_Target{return i.rgba;}
    )";
    const char* sm3=R"(
        struct Input {float4 pos:TEXCOORD0;float4 rgba:TEXCOORD1;};
        struct Out {float4 pos:POSITION;float4 rgba:COLOR0;};
        Out vs_main(Input i){Out o;o.pos=i.pos;o.rgba=i.rgba;return o;}
        float4 ps_main(float4 rgba:COLOR0):COLOR0{return rgba;}
    )";
    ShaderDesc shader;shader.hlsl11=sm4;shader.hlsl9=sm3;
    PipelineDesc p;p.shader=device->createShader(shader);p.stride=32;p.attrCount=2;p.cull=CullMode::None;
    p.attrs[0]={0,VertexFormat::Float4,0};p.attrs[1]={1,VertexFormat::Float4,16};
    auto pipeline=device->createPipeline(p);
    if(!pipeline.valid()){std::fprintf(stderr,"%s\n",device->lastError().c_str());return 1;}
    const float vertices[]={-.8f,-.8f,.4f,1,1,0,0,1, 0,.8f,.4f,1,1,0,0,1, .8f,-.8f,.4f,1,1,0,0,1};
    uint16_t indices[]={0,1,2};
    auto vb=device->createBuffer({BufferType::Vertex,Usage::Immutable,sizeof(vertices),vertices});
    auto ib=device->createBuffer({BufferType::Index,Usage::Immutable,sizeof(indices),indices});
    int captured=0;
    for(int frame=0;frame<2;++frame) {
        device->beginFrame(17,15);
        PassDesc pass;pass.clearColor[0]=pass.clearColor[1]=0;pass.clearColor[2]=1;
        device->beginPass(pass);device->setPipeline(pipeline);device->setVertexBuffer(vb);
        device->setIndexBuffer(ib);device->drawIndexed(3);device->endPass();
        device->captureNextFrame([&](int w,int h,std::vector<uint8_t> rgba) {
            size_t center=4*(7*17+8);
            if(w==17 && h==15 && rgba.size()==17*15*4 && rgba[0]==0 && rgba[2]==255 &&
               rgba[center]==255 && rgba[center+2]==0)++captured;
        });
        device->endFrame();
    }
    if(captured!=2 || !device->lastError().empty()){std::fprintf(stderr,"RHI output mismatch: %s\n",device->lastError().c_str());return 1;}
}
