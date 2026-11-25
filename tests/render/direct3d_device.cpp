#include "engine/gfx/d3d/Direct3D.h"
#include <d3d11.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>

namespace {
using Microsoft::WRL::ComPtr;
using namespace aiverse::gfx;
bool draw11(Direct3DTarget& target) {
    const char* source=R"(
        float4 vs_main(uint id:SV_VertexID):SV_Position {
            float2 points[3]={float2(-.8,-.8),float2(0,.8),float2(.8,-.8)};
            return float4(points[id],.5,1);
        }
        float4 ps_main():SV_Target { return float4(1,0,0,1); }
    )";
    ComPtr<ID3DBlob> vs,ps;
    auto compile=[&](const char* entry,const char* profile,ID3DBlob** result){
        return SUCCEEDED(D3DCompile(source,std::strlen(source),"triangle",nullptr,nullptr,
                                    entry,profile,0,0,result,nullptr));
    };
    if(!compile("vs_main","vs_4_0",vs.GetAddressOf()) || !compile("ps_main","ps_4_0",ps.GetAddressOf()))return false;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    auto* device=target.device11();auto* context=target.context11();
    if(FAILED(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,vertex.GetAddressOf())) ||
       FAILED(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,pixel.GetAddressOf())))return false;
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertex.Get(),nullptr,0);context->PSSetShader(pixel.Get(),nullptr,0);
    context->Draw(3,0);return true;
}
bool draw9(Direct3DTarget& target) {
    struct Vertex {float x,y,z,w;DWORD color;};
    const Vertex vertices[]={{1,13,.5f,1,0xffff0000u},{8,1,.5f,1,0xffff0000u},{15,13,.5f,1,0xffff0000u}};
    auto* d=target.device9();
    return SUCCEEDED(d->SetRenderState(D3DRS_LIGHTING,FALSE)) &&
           SUCCEEDED(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE)) &&
           SUCCEEDED(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE)) &&
           SUCCEEDED(d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1)) &&
           SUCCEEDED(d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE)) &&
           SUCCEEDED(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,vertices,sizeof(Vertex)));
}
}
int main(int argc,char** argv) {
    using namespace aiverse::gfx;
    if(argc!=2 || (std::strcmp(argv[1],"9") && std::strcmp(argv[1],"11")))return 2;
    const bool modern=std::strcmp(argv[1],"11")==0;
    std::string error;
    auto target=createDirect3DTarget(modern?Direct3DVersion::D3D11:Direct3DVersion::D3D9,17,15,nullptr,modern,error);
    if(!target){std::fprintf(stderr,"%s\n",error.c_str());return 1;}
    for(int frame=0;frame<2;++frame) {
        if(!target->begin({0,0,1,1},error) || !(modern?draw11(*target):draw9(*target)))return 1;
        std::vector<uint8_t> rgba;
        if(!target->readRgba(rgba,error)){std::fprintf(stderr,"%s\n",error.c_str());return 1;}
        const size_t center=4*(7*17+8);
        if(rgba.size()!=17*15*4 || rgba[0]!=0 || rgba[2]!=255 || rgba[center]!=255 || rgba[center+2]!=0)return 1;
    }
}
