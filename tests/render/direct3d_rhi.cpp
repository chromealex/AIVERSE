#include "engine/gfx/Gfx.h"
#include "engine/gfx/d3d/Direct3D.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
using namespace aiverse::gfx;
int main(int argc,char** argv) {
    if((argc!=2 && argc!=3) || (std::strcmp(argv[1],"11") && std::strcmp(argv[1],"9")))return 2;
    const bool modern=std::strcmp(argv[1],"11")==0;
    const bool windowed=argc==3 && std::strcmp(argv[2],"--window")==0;
    if(argc==3 && !windowed)return 2;
    struct FocusWindow {
        HWND handle{};
        ~FocusWindow(){if(handle)DestroyWindow(handle);}
    } window;
    if(windowed) {
        window.handle=CreateWindowExW(0,L"STATIC",L"AIVERSE presenter test",WS_POPUP,0,0,25,21,
                                      nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!window.handle)return 1;
    }
    std::string error;
    const auto factory=windowed?createDirect3DWindowDevice:createDirect3DDevice;
    auto device=factory(modern?Direct3DVersion::D3D11:Direct3DVersion::D3D9,17,15,window.handle,modern,error);
    if(!device){std::fprintf(stderr,"%s\n",error.c_str());return 1;}
    const char* sm4=R"(
        struct Input {float4 pos:TEXCOORD0;float4 rgba:TEXCOORD1;};
        struct Out {float4 pos:SV_Position;float4 rgba:COLOR0;};
        Out vs_main(Input i){Out o;o.pos=i.pos;o.rgba=i.rgba;return o;}
        Texture2D image:register(t0);SamplerState samp:register(s0);
        float4 ps_main(Out i):SV_Target{return i.rgba*image.Sample(samp,float2(.8,.5));}
    )";
    const char* sm3=R"(
        struct Input {float4 pos:TEXCOORD0;float4 rgba:TEXCOORD1;};
        struct Out {float4 pos:POSITION;float4 rgba:COLOR0;};
        Out vs_main(Input i){Out o;o.pos=i.pos;o.rgba=i.rgba;return o;}
        sampler2D image:register(s0);
        float4 ps_main(float4 rgba:COLOR0):COLOR0{return rgba*tex2D(image,float2(.8,.5));}
    )";
    ShaderDesc shader;shader.textureSlots=1;shader.hlsl11=sm4;shader.hlsl9=sm3;
    PipelineDesc p;p.shader=device->createShader(shader);
    if(!p.shader.valid()){std::fprintf(stderr,"%s\n",device->lastError().c_str());return 1;}
    p.stride=32;p.attrCount=2;p.cull=CullMode::None;
    p.attrs[0]={0,VertexFormat::Float4,0};p.attrs[1]={1,VertexFormat::Float4,16};
    auto pipeline=device->createPipeline(p);
    if(!pipeline.valid()){std::fprintf(stderr,"%s\n",device->lastError().c_str());return 1;}
    const float vertices[]={-.8f,-.8f,.4f,1,1,1,1,1, 0,.8f,.4f,1,1,1,1,1, .8f,-.8f,.4f,1,1,1,1,1};
    uint16_t indices[]={0,1,2};
    auto vb=device->createBuffer({BufferType::Vertex,Usage::Immutable,sizeof(vertices),vertices});
    auto ib=device->createBuffer({BufferType::Index,Usage::Immutable,sizeof(indices),indices});
    const uint8_t texels[]={0,0,255,255,255,0,0,255};
    TextureDesc td;td.width=2;td.height=1;td.filter=Filter::Nearest;td.data=texels;
    auto texture=device->createTexture(td);
    if(!texture.valid())return 1;
    int captured=0;
    for(int frame=0;frame<2;++frame) {
        if(frame){
            const uint8_t green[]={0,255,0,255};
            device->updateTexture(texture,1,0,1,1,green,4);
        }
        const int width=frame?23:17,height=frame?19:15;
        device->beginFrame(width,height);
        PassDesc pass;pass.clearColor[0]=pass.clearColor[1]=0;pass.clearColor[2]=1;
        device->beginPass(pass);device->setPipeline(pipeline);device->setVertexBuffer(vb);
        device->setIndexBuffer(ib);device->setTexture(0,texture);device->drawIndexed(3);device->endPass();
        device->captureNextFrame([&](int w,int h,std::vector<uint8_t> rgba) {
            size_t center=4*(size_t(height/2)*width+width/2);
            if(w==width && h==height && rgba.size()==size_t(w)*h*4 && rgba[0]==0 && rgba[2]==255 &&
               rgba[center+(frame?1:0)]==255 && rgba[center+2]==0)++captured;
        });
        device->endFrame();
    }
    if(captured!=2 || !device->lastError().empty()){std::fprintf(stderr,"RHI output mismatch: %s\n",device->lastError().c_str());return 1;}
}
