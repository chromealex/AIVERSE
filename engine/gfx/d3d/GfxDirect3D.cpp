// Windows RHI for single-target native HLSL triangle and line programs.
#include "engine/gfx/Gfx.h"
#include "Direct3D.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
#include <cstdio>
#include <unordered_map>

namespace aiverse::gfx {
namespace {
using Microsoft::WRL::ComPtr;
struct Buffer {
    BufferType type{};Usage usage{};
    std::vector<uint8_t> bytes;
    ComPtr<ID3D11Buffer> native11;
    ComPtr<IDirect3DVertexBuffer9> vertex9;
    ComPtr<IDirect3DIndexBuffer9> index9;
    IndexType indexType9=IndexType::None;
};
struct Texture {
    int width=0,height=0;
    Filter filter=Filter::Linear;
    bool repeat=false;
    ComPtr<ID3D11Texture2D> image11;
    ComPtr<ID3D11ShaderResourceView> view11;
    ComPtr<ID3D11SamplerState> sampler11;
    ComPtr<IDirect3DTexture9> image9;
};
struct Shader {
    ComPtr<ID3DBlob> vertexCode;
    ComPtr<ID3D11VertexShader> vertex11;
    ComPtr<ID3D11PixelShader> pixel11;
    ComPtr<IDirect3DVertexShader9> vertex9;
    ComPtr<IDirect3DPixelShader9> pixel9;
};
struct Pipeline {
    PipelineDesc desc{};
    Shader program;
    ComPtr<ID3D11InputLayout> layout11;
    ComPtr<ID3D11RasterizerState> raster11;
    ComPtr<ID3D11DepthStencilState> depth11;
    ComPtr<ID3D11BlendState> blend11;
    ComPtr<IDirect3DVertexDeclaration9> layout9;
};
struct Format { DXGI_FORMAT dxgi; BYTE declaration; uint32_t bytes; };
Format vertexFormat(VertexFormat f) {
    switch(f){
    case VertexFormat::Float2:return {DXGI_FORMAT_R32G32_FLOAT,D3DDECLTYPE_FLOAT2,8};
    case VertexFormat::Float3:return {DXGI_FORMAT_R32G32B32_FLOAT,D3DDECLTYPE_FLOAT3,12};
    case VertexFormat::Float4:return {DXGI_FORMAT_R32G32B32A32_FLOAT,D3DDECLTYPE_FLOAT4,16};
    case VertexFormat::UByte4N:return {DXGI_FORMAT_R8G8B8A8_UNORM,D3DDECLTYPE_UBYTE4N,4};
    case VertexFormat::UByte4:return {DXGI_FORMAT_R8G8B8A8_UINT,D3DDECLTYPE_UBYTE4,4};
    }
    return {DXGI_FORMAT_UNKNOWN,D3DDECLTYPE_UNUSED,0};
}

class WindowsDevice final : public Device {
    std::unique_ptr<Direct3DTarget> target;
    DeviceInfo identity;
    int width,height;
    bool windowed=false;
    std::string error;
    std::unordered_map<uint32_t,Buffer> buffers;
    std::unordered_map<uint32_t,Shader> shaders;
    std::unordered_map<uint32_t,Texture> textures;
    std::array<TextureHandle,16> boundTextures{};
    std::unordered_map<uint32_t,Pipeline> pipelines;
    std::array<ComPtr<ID3D11Buffer>,16> uniforms;
    ComPtr<ID3D11DepthStencilView> screenDepth11;
    ComPtr<IDirect3DSurface9> screenDepth9;
    uint32_t nextId=1;
    CaptureFn capture;
    bool frame=false,pass=false;
    BufferHandle vb{},ib{};size_t vertexOffset=0,indexOffset=0;
    PipelineHandle bound{};
    ID3D11Device* dx11() const {return target->device11();}
    ID3D11DeviceContext* context() const {return target->context11();}
    IDirect3DDevice9* dx9() const {return target->device9();}
    bool check(HRESULT result,const char* operation) {
        if(SUCCEEDED(result))return true;
        char code[16];std::snprintf(code,sizeof(code),"0x%08lx",static_cast<unsigned long>(result));
        error=std::string(operation)+" failed ("+code+")";return false;
    }
public:
    WindowsDevice(std::unique_ptr<Direct3DTarget> t,Direct3DVersion version,int w,int h):
        target(std::move(t)),identity{version==Direct3DVersion::D3D11?Backend::Direct3D11:Backend::Direct3D9,
        true,true,version==Direct3DVersion::D3D11?"Direct3D 11":"Direct3D 9"},width(w),height(h){}
    bool init() {return resizeDepth(width,height,screenDepth9,screenDepth11);}
    bool attach(void* window) {windowed=target->attachWindow(window,error);return windowed;}
private:
    bool resizeDepth(int w,int h,ComPtr<IDirect3DSurface9>& z9,ComPtr<ID3D11DepthStencilView>& z11) {
        if(dx9())return check(dx9()->CreateDepthStencilSurface(w,h,D3DFMT_D16,
            D3DMULTISAMPLE_NONE,0,FALSE,z9.GetAddressOf(),nullptr),"CreateDepthStencilSurface");
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=w;desc.Height=h;desc.ArraySize=desc.MipLevels=desc.SampleDesc.Count=1;
        desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;desc.Format=DXGI_FORMAT_D32_FLOAT;
        ComPtr<ID3D11Texture2D> texture;
        return check(dx11()->CreateTexture2D(&desc,nullptr,texture.GetAddressOf()),"CreateTexture2D(depth)") &&
               check(dx11()->CreateDepthStencilView(texture.Get(),nullptr,z11.GetAddressOf()),"CreateDepthStencilView");
    }
public:
    const DeviceInfo& info() const override {return identity;}
    const std::string& lastError() const override {return error;}
    bool supports(PixelFormat f) const override {return f==PixelFormat::RGBA8;}
    TextureHandle createTexture(const TextureDesc& d) override {
        if(validateTextureDesc(d)!=TextureDescError::None || d.format!=PixelFormat::RGBA8 ||
           d.dimension!=TextureDimension::Texture2D || d.renderTarget || d.mipmaps || d.mipLevels>1 ||
           d.levelCount || d.sampleCount!=1 || d.storage || d.width>8192 || d.height>8192) {
            error="Direct3D requires a single-level sampled RGBA8 image";return {};
        }
        Texture t;t.width=d.width;t.height=d.height;t.filter=d.filter;t.repeat=d.repeat;
        if(dx11()) {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width=d.width;desc.Height=d.height;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
            desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA init{d.data,UINT(d.width*4),0};
            if(!check(dx11()->CreateTexture2D(&desc,d.data?&init:nullptr,t.image11.GetAddressOf()),"CreateTexture2D(sampled)") ||
               !check(dx11()->CreateShaderResourceView(t.image11.Get(),nullptr,t.view11.GetAddressOf()),"CreateShaderResourceView"))return {};
            D3D11_SAMPLER_DESC sampler{};
            sampler.Filter=d.filter==Filter::Linear?D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT:D3D11_FILTER_MIN_MAG_MIP_POINT;
            sampler.AddressU=sampler.AddressV=sampler.AddressW=d.repeat?D3D11_TEXTURE_ADDRESS_WRAP:D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;sampler.MaxAnisotropy=1;
            if(!check(dx11()->CreateSamplerState(&sampler,t.sampler11.GetAddressOf()),"CreateSamplerState"))return {};
        } else {
            if(!check(dx9()->CreateTexture(d.width,d.height,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,t.image9.GetAddressOf(),nullptr),"CreateTexture"))return {};
            if(d.data && !write9(t,0,0,d.width,d.height,d.data,d.width*4))return {};
        }
        auto id=nextId++;textures.emplace(id,std::move(t));return {id};
    }
    bool textureInfo(TextureHandle h,TextureInfo& info) const override {
        auto it=textures.find(h.id);if(it==textures.end())return false;
        info={it->second.width,it->second.height,PixelFormat::RGBA8};return true;
    }
    void updateTexture(TextureHandle h,int x,int y,int w,int ht,const void* data,int pitch) override {
        auto it=textures.find(h.id);
        if(it==textures.end() || !data || x<0 || y<0 || w<=0 || ht<=0 ||
           x>it->second.width-w || y>it->second.height-ht || (pitch && pitch<w*4)) {
            error="invalid Direct3D texture update";return;
        }
        if(!pitch)pitch=w*4;
        if(dx11()){
            D3D11_BOX region{UINT(x),UINT(y),0,UINT(x+w),UINT(y+ht),1};
            context()->UpdateSubresource(it->second.image11.Get(),0,&region,data,UINT(pitch),0);
        }else write9(it->second,x,y,w,ht,data,pitch);
    }
    void destroyTexture(TextureHandle h) override {
        if(!h.valid())return;
        for(size_t i=0;i<boundTextures.size();++i)if(boundTextures[i].id==h.id)setTexture(int(i),{});
        textures.erase(h.id);
    }
    void setTexture(int slot,TextureHandle h) override {
        if(slot<0 || size_t(slot)>=boundTextures.size()){error="invalid Direct3D texture slot";return;}
        auto it=textures.find(h.id);
        if(h.valid() && it==textures.end()){error="invalid Direct3D texture handle";return;}
        Texture* t=h.valid()?&it->second:nullptr;
        if(dx11()){
            auto* view=t?t->view11.Get():nullptr;
            auto* sampler=t?t->sampler11.Get():nullptr;
            context()->PSSetShaderResources(slot,1,&view);context()->PSSetSamplers(slot,1,&sampler);
            context()->VSSetShaderResources(slot,1,&view);context()->VSSetSamplers(slot,1,&sampler);
        }else {
            if(!check(dx9()->SetTexture(slot,t?t->image9.Get():nullptr),"SetTexture"))return;
            if(t){
                DWORD address=t->repeat?D3DTADDRESS_WRAP:D3DTADDRESS_CLAMP;
                DWORD filter=t->filter==Filter::Linear?D3DTEXF_LINEAR:D3DTEXF_POINT;
                if(!check(dx9()->SetSamplerState(slot,D3DSAMP_ADDRESSU,address),"SetSamplerState(U)") ||
                   !check(dx9()->SetSamplerState(slot,D3DSAMP_ADDRESSV,address),"SetSamplerState(V)") ||
                   !check(dx9()->SetSamplerState(slot,D3DSAMP_MAGFILTER,filter),"SetSamplerState(mag)") ||
                   !check(dx9()->SetSamplerState(slot,D3DSAMP_MINFILTER,filter),"SetSamplerState(min)"))return;
            }
        }
        boundTextures[size_t(slot)]=h;
    }
    BufferHandle createBuffer(const BufferDesc& d) override {
        if(!d.size || d.size>UINT32_MAX || (d.type!=BufferType::Vertex && d.type!=BufferType::Index)){
            error="invalid Direct3D vertex/index buffer";return {};
        }
        Buffer b;b.type=d.type;b.usage=d.usage;b.bytes.resize(d.size);
        if(d.data)std::memcpy(b.bytes.data(),d.data,d.size);
        auto id=nextId++;buffers.emplace(id,std::move(b));return {id};
    }
    void updateBuffer(BufferHandle h,const void* data,size_t size,size_t offset) override {
        auto it=buffers.find(h.id);
        if(it==buffers.end() || it->second.usage!=Usage::Dynamic || !data ||
           offset>UINT32_MAX || size>UINT32_MAX-offset){error="invalid Direct3D buffer update";return;}
        auto& b=it->second;
        if(b.bytes.size()<offset+size)b.bytes.resize(offset+size);
        std::memcpy(b.bytes.data()+offset,data,size);
        b.native11.Reset();b.vertex9.Reset();b.index9.Reset();
    }
    void destroyBuffer(BufferHandle h) override {buffers.erase(h.id);}
    ShaderHandle createShader(const ShaderDesc& d) override {
        const bool modern=dx11()!=nullptr;
        const char* src=modern?d.hlsl11:d.hlsl9;
        if(!src || d.textureSlots<0 || d.textureSlots>16){error="missing native Direct3D shader or invalid texture count";return {};}
        auto compile=[&](const char* entry,const char* profile,ID3DBlob** result){
            ComPtr<ID3DBlob> messages;
            HRESULT hr=D3DCompile(src,std::strlen(src),d.label,nullptr,nullptr,entry,profile,
                                    modern ? D3DCOMPILE_ENABLE_STRICTNESS : 0,0,result,messages.GetAddressOf());
            if(check(hr,entry))return true;
            if(messages)error.append(": ").append(static_cast<const char*>(messages->GetBufferPointer()),messages->GetBufferSize());
            return false;
        };
        Shader s;ComPtr<ID3DBlob> frag;
        if(!compile("vs_main",modern?"vs_4_0":"vs_3_0",s.vertexCode.GetAddressOf()) ||
           !compile("ps_main",modern?"ps_4_0":"ps_3_0",frag.GetAddressOf()))return {};
        auto* vs=s.vertexCode->GetBufferPointer();auto* ps=frag->GetBufferPointer();
        if(modern) {
            if(!check(dx11()->CreateVertexShader(vs,s.vertexCode->GetBufferSize(),nullptr,s.vertex11.GetAddressOf()),"CreateVertexShader") ||
               !check(dx11()->CreatePixelShader(ps,frag->GetBufferSize(),nullptr,s.pixel11.GetAddressOf()),"CreatePixelShader"))return {};
        } else {
            if(!check(dx9()->CreateVertexShader(static_cast<const DWORD*>(vs),s.vertex9.GetAddressOf()),"CreateVertexShader") ||
               !check(dx9()->CreatePixelShader(static_cast<const DWORD*>(ps),s.pixel9.GetAddressOf()),"CreatePixelShader"))return {};
        }
        auto id=nextId++;shaders.emplace(id,std::move(s));return {id};
    }
    void destroyShader(ShaderHandle h) override {shaders.erase(h.id);}
    PipelineHandle createPipeline(const PipelineDesc& d) override {
        auto shader=shaders.find(d.shader.id);
        if(shader==shaders.end() || d.attrCount<1 || d.attrCount>8 || !d.stride || d.stride>2048 ||
           d.colorFormat!=PixelFormat::RGBA8 || d.colorAttachmentCount!=1 || d.sampleCount!=1 ||
           d.depthOnly || d.vertexPerInstance || d.depthBiasConstant || d.depthBiasSlope ||
           (d.blend!=BlendMode::Opaque && d.blend!=BlendMode::Alpha)) {
            error="unsupported Direct3D pipeline layout";return {};
        }
        Pipeline p;p.program=shader->second;p.desc=d;p.desc.label=nullptr;
        std::array<D3D11_INPUT_ELEMENT_DESC,8> elements{};
        std::array<D3DVERTEXELEMENT9,9> declaration{};
        for(int i=0;i<d.attrCount;++i) {
            const auto& a=d.attrs[i];const auto f=vertexFormat(a.format);
            if(!f.bytes || f.bytes>d.stride || a.offset>d.stride-f.bytes || a.location>=16){
                error="invalid Direct3D vertex attribute";return {};
            }
            elements[i]={"TEXCOORD",a.location,f.dxgi,0,a.offset,D3D11_INPUT_PER_VERTEX_DATA,0};
            declaration[i]={0,WORD(a.offset),f.declaration,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,BYTE(a.location)};
        }
        if(dx11()) {
            if(!check(dx11()->CreateInputLayout(elements.data(),d.attrCount,p.program.vertexCode->GetBufferPointer(),
                   p.program.vertexCode->GetBufferSize(),p.layout11.GetAddressOf()),"CreateInputLayout"))return {};
            D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.DepthClipEnable=TRUE;r.ScissorEnable=TRUE;
            r.FrontCounterClockwise=TRUE;
            r.CullMode=d.cull==CullMode::None?D3D11_CULL_NONE:d.cull==CullMode::Back?D3D11_CULL_BACK:D3D11_CULL_FRONT;
            D3D11_DEPTH_STENCIL_DESC z{};z.DepthEnable=d.depthTest||d.depthWrite;z.DepthFunc=d.depthTest?D3D11_COMPARISON_LESS_EQUAL:D3D11_COMPARISON_ALWAYS;
            z.DepthWriteMask=d.depthWrite?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;
            D3D11_BLEND_DESC blend{};auto& rt=blend.RenderTarget[0];
            rt.RenderTargetWriteMask=d.colorWrite?D3D11_COLOR_WRITE_ENABLE_ALL:0;
            rt.BlendEnable=d.blend==BlendMode::Alpha;rt.SrcBlend=D3D11_BLEND_SRC_ALPHA;rt.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;
            rt.SrcBlendAlpha=D3D11_BLEND_ONE;rt.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;rt.BlendOp=rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;
            if(!check(dx11()->CreateRasterizerState(&r,p.raster11.GetAddressOf()),"CreateRasterizerState") ||
               !check(dx11()->CreateDepthStencilState(&z,p.depth11.GetAddressOf()),"CreateDepthStencilState") ||
               !check(dx11()->CreateBlendState(&blend,p.blend11.GetAddressOf()),"CreateBlendState"))return {};
        } else {
            declaration[d.attrCount]=D3DDECL_END();
            if(!check(dx9()->CreateVertexDeclaration(declaration.data(),p.layout9.GetAddressOf()),"CreateVertexDeclaration"))return {};
        }
        auto id=nextId++;pipelines.emplace(id,std::move(p));return {id};
    }
    void destroyPipeline(PipelineHandle h) override {pipelines.erase(h.id);}
    void beginFrame(int w,int h) override {
        if(frame || w<=0 || h<=0 || w>8192 || h>8192 || uint64_t(w)*h>16777216){error="invalid Direct3D beginFrame";return;}
        if(w!=width || h!=height){
            ComPtr<IDirect3DSurface9> newDepth9;ComPtr<ID3D11DepthStencilView> newDepth11;
            if(!resizeDepth(w,h,newDepth9,newDepth11) || !target->resize(w,h,error))return;
            screenDepth9=std::move(newDepth9);screenDepth11=std::move(newDepth11);width=w;height=h;
        }
        fbW_=w;fbH_=h;frame=true;
    }
    void endFrame() override {
        if(!frame || pass){error="invalid Direct3D endFrame";return;}
        frame=false;
        auto fn=std::move(capture);capture={};
        std::vector<uint8_t> rgba;
        if(fn) {
            std::string readError;
            if(!target->readRgba(rgba,readError))error=std::move(readError);
        }
        if(windowed) {
            std::string presentError;
            if(!target->present(presentError))error=std::move(presentError);
        }
        if(fn) {
            if(rgba.empty())fn(0,0,{});
            else fn(width,height,std::move(rgba));
        }
    }
    void captureNextFrame(CaptureFn cb) override {capture=std::move(cb);}
    void beginPass(const PassDesc& d) override {
        if(!frame || pass || d.colorTarget.valid() || d.depthTarget.valid()){error="invalid Direct3D screen pass";return;}
        const std::array<float,4> clear={d.clearColor[0],d.clearColor[1],d.clearColor[2],d.clearColor[3]};
        if(!(d.clearColorEnabled?target->begin(clear,error):target->beginLoad(error)))return;
        pass=true;bound={};
        if(dx11()) {
            ComPtr<ID3D11RenderTargetView> rt;context()->OMGetRenderTargets(1,rt.GetAddressOf(),nullptr);
            auto* r=rt.Get();context()->OMSetRenderTargets(1,&r,screenDepth11.Get());
            if(d.clearDepthEnabled)context()->ClearDepthStencilView(screenDepth11.Get(),D3D11_CLEAR_DEPTH,d.clearDepth,0);
        } else {
            if(!check(dx9()->SetDepthStencilSurface(screenDepth9.Get()),"SetDepthStencilSurface") ||
               (d.clearDepthEnabled && !check(dx9()->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,d.clearDepth,0),"Clear(depth)"))) {
                std::string ignored;target->end(ignored);pass=false;return;
            }
        }
        setViewport({0,0,width,height});setScissor({});
    }
    void endPass() override {
        if(!pass){error="no active Direct3D pass";return;}
        pass=false;std::string finishError;
        if(!target->end(finishError))error=std::move(finishError);
    }
    void setViewport(const Rect& r) override {
        if(r.x<0 || r.y<0 || r.w<=0 || r.h<=0 || r.x>width-r.w || r.y>height-r.h){error="viewport outside Direct3D target";return;}
        if(dx11()) {D3D11_VIEWPORT v{float(r.x),float(r.y),float(r.w),float(r.h),0,1};context()->RSSetViewports(1,&v);}
        else {D3DVIEWPORT9 v{DWORD(r.x),DWORD(r.y),DWORD(r.w),DWORD(r.h),0,1};check(dx9()->SetViewport(&v),"SetViewport");}
    }
    void setScissor(const Rect& r) override {
        auto within=[](int64_t n,int limit){return LONG(std::clamp<int64_t>(n,0,limit));};
        RECT rect={0,0,width,height};
        if(r.w)rect={within(r.x,width),within(r.y,height),within(int64_t(r.x)+std::max(0,r.w),width),within(int64_t(r.y)+std::max(0,r.h),height)};
        if(dx11())context()->RSSetScissorRects(1,&rect);
        else {check(dx9()->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE),"SetRenderState(scissor)");check(dx9()->SetScissorRect(&rect),"SetScissorRect");}
    }
    void setPipeline(PipelineHandle h) override {
        auto it=pipelines.find(h.id);bound={};
        if(it==pipelines.end()){error="invalid Direct3D pipeline";return;}
        auto& p=it->second;const auto& d=p.desc;
        if(dx11()) {
            auto* c=context();
            c->IASetInputLayout(p.layout11.Get());
            c->IASetPrimitiveTopology(d.primitive==Primitive::Triangles?D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST:D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
            c->VSSetShader(p.program.vertex11.Get(),nullptr,0);c->PSSetShader(p.program.pixel11.Get(),nullptr,0);
            c->RSSetState(p.raster11.Get());c->OMSetDepthStencilState(p.depth11.Get(),0);c->OMSetBlendState(p.blend11.Get(),nullptr,UINT32_MAX);
        } else {
            auto* c=dx9();
            if(!check(c->SetVertexDeclaration(p.layout9.Get()),"SetVertexDeclaration") ||
               !check(c->SetVertexShader(p.program.vertex9.Get()),"SetVertexShader") ||
               !check(c->SetPixelShader(p.program.pixel9.Get()),"SetPixelShader"))return;
            const std::pair<D3DRENDERSTATETYPE,DWORD> states[]={
                {D3DRS_CULLMODE,d.cull==CullMode::None?D3DCULL_NONE:d.cull==CullMode::Back?D3DCULL_CW:D3DCULL_CCW},
                {D3DRS_ZENABLE,d.depthTest||d.depthWrite},{D3DRS_ZWRITEENABLE,d.depthWrite},
                {D3DRS_ZFUNC,d.depthTest?D3DCMP_LESSEQUAL:D3DCMP_ALWAYS},
                {D3DRS_COLORWRITEENABLE,d.colorWrite?15u:0u},{D3DRS_ALPHABLENDENABLE,d.blend==BlendMode::Alpha},
                {D3DRS_SRCBLEND,D3DBLEND_SRCALPHA},{D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA},
                {D3DRS_SEPARATEALPHABLENDENABLE,TRUE},{D3DRS_SRCBLENDALPHA,D3DBLEND_ONE},{D3DRS_DESTBLENDALPHA,D3DBLEND_INVSRCALPHA}};
            for(auto [key,value]:states)if(!check(c->SetRenderState(key,value),"SetRenderState(pipeline)"))return;
        }
        bound=h;
    }
    void setVertexBuffer(BufferHandle h,size_t offset) override {vb=h;vertexOffset=offset;}
    void setIndexBuffer(BufferHandle h,size_t offset) override {ib=h;indexOffset=offset;}
    void setUniforms(int slot,const void* data,size_t bytes) override {
        if(!data || !bytes || bytes>4096 || slot<0 || slot>=14){error="invalid Direct3D uniform block";return;}
        // D3D9 SM3 has no CB slots. The portable first stage exposes U0 at c0.
        if(dx9() && (slot || bytes>224*16)){error="D3D9 supports U0 up to 224 float4 registers";return;}
        std::array<float,1024> padded{};std::memcpy(padded.data(),data,bytes);
        UINT registers=UINT((bytes+15)/16);
        if(dx11()){
            D3D11_BUFFER_DESC bd{};bd.ByteWidth=registers*16;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            bd.Usage=D3D11_USAGE_IMMUTABLE;D3D11_SUBRESOURCE_DATA initial{padded.data(),0,0};
            ComPtr<ID3D11Buffer> buffer;
            if(!check(dx11()->CreateBuffer(&bd,&initial,buffer.GetAddressOf()),"CreateBuffer(uniform)"))return;
            uniforms[slot]=buffer;auto* raw=buffer.Get();
            context()->VSSetConstantBuffers(slot,1,&raw);context()->PSSetConstantBuffers(slot,1,&raw);
        } else {
            check(dx9()->SetVertexShaderConstantF(0,padded.data(),registers),"SetVertexShaderConstantF");
            check(dx9()->SetPixelShaderConstantF(0,padded.data(),registers),"SetPixelShaderConstantF");
        }
    }
    void draw(uint32_t n,uint32_t first) override {submit(n,first,0,false);}
    void drawIndexed(uint32_t n,uint32_t first,int32_t base,uint32_t instances) override {
        if(!instances)return;
        if(instances!=1){error="Direct3D instancing unavailable";return;}
        submit(n,first,base,true);
    }
private:
    bool write9(Texture& t,int x,int y,int w,int h,const void* data,int pitch) {
        const RECT rect{x,y,x+w,y+h};
        D3DLOCKED_RECT mapped{};
        if(!check(t.image9->LockRect(0,&mapped,&rect,0),"LockRect(texture update)"))return false;
        for(int row=0;row<h;++row){
            const auto* src=static_cast<const uint8_t*>(data)+size_t(row)*pitch;
            auto* dst=static_cast<uint8_t*>(mapped.pBits)+ptrdiff_t(row)*mapped.Pitch;
            for(int col=0;col<w;++col,src+=4,dst+=4){
                dst[0]=src[2];dst[1]=src[1];dst[2]=src[0];dst[3]=src[3];
            }
        }
        return check(t.image9->UnlockRect(0),"UnlockRect(texture update)");
    }
    bool upload(Buffer& b,IndexType type) {
        const UINT size=UINT(b.bytes.size());
        if(dx11()){
            if(b.native11)return true;
            D3D11_BUFFER_DESC desc{};desc.ByteWidth=size;desc.Usage=D3D11_USAGE_IMMUTABLE;
            desc.BindFlags=b.type==BufferType::Vertex?D3D11_BIND_VERTEX_BUFFER:D3D11_BIND_INDEX_BUFFER;
            D3D11_SUBRESOURCE_DATA data{b.bytes.data(),0,0};
            return check(dx11()->CreateBuffer(&desc,&data,b.native11.GetAddressOf()),"CreateBuffer(geometry)");
        }
        void* data=nullptr;
        if(b.type==BufferType::Vertex) {
            if(b.vertex9)return true;
            ComPtr<IDirect3DVertexBuffer9> native;
            if(!check(dx9()->CreateVertexBuffer(size,0,0,D3DPOOL_MANAGED,native.GetAddressOf(),nullptr),"CreateVertexBuffer") ||
               !check(native->Lock(0,0,&data,0),"Lock(vertices)"))return false;
            std::memcpy(data,b.bytes.data(),size);
            if(!check(native->Unlock(),"Unlock(vertices)"))return false;
            b.vertex9=std::move(native);return true;
        }
        if(b.index9 && b.indexType9==type)return true;
        ComPtr<IDirect3DIndexBuffer9> native;
        auto format=type==IndexType::UInt16?D3DFMT_INDEX16:D3DFMT_INDEX32;
        if(!check(dx9()->CreateIndexBuffer(size,0,format,D3DPOOL_MANAGED,native.GetAddressOf(),nullptr),"CreateIndexBuffer") ||
           !check(native->Lock(0,0,&data,0),"Lock(indices)"))return false;
        std::memcpy(data,b.bytes.data(),size);
        if(!check(native->Unlock(),"Unlock(indices)"))return false;
        b.index9=std::move(native);b.indexType9=type;return true;
    }
    void submit(uint32_t count,uint32_t first,int32_t base,bool indexed) {
        auto it=pipelines.find(bound.id);auto vertex=buffers.find(vb.id);
        if(!pass || it==pipelines.end() || vertex==buffers.end() || vertex->second.type!=BufferType::Vertex ||
           vertexOffset>vertex->second.bytes.size()){error="invalid Direct3D draw bindings";return;}
        const auto& p=it->second.desc;auto& v=vertex->second;
        const size_t vertices=(v.bytes.size()-vertexOffset)/p.stride;
        if(!indexed && uint64_t(first)+count>vertices){error="Direct3D vertex range exceeds buffer";return;}
        if(!upload(v,IndexType::None))return;
        if(dx11()){
            auto* raw=v.native11.Get();UINT offset=UINT(vertexOffset);
            context()->IASetVertexBuffers(0,1,&raw,&p.stride,&offset);
        }else if(!check(dx9()->SetStreamSource(0,v.vertex9.Get(),UINT(vertexOffset),p.stride),"SetStreamSource"))return;
        D3DPRIMITIVETYPE primitive=p.primitive==Primitive::Triangles?D3DPT_TRIANGLELIST:D3DPT_LINELIST;
        const UINT primitives=count/(p.primitive==Primitive::Triangles?3:2);
        if(!indexed){
            if(dx11())context()->Draw(count,first);
            else check(dx9()->DrawPrimitive(primitive,first,primitives),"DrawPrimitive");
            return;
        }
        auto index=buffers.find(ib.id);
        const size_t unit=p.indexType==IndexType::UInt16?2:4;
        if(index==buffers.end() || index->second.type!=BufferType::Index || p.indexType==IndexType::None ||
           indexOffset%unit || indexOffset>index->second.bytes.size() ||
           uint64_t(first)+count>(index->second.bytes.size()-indexOffset)/unit) {
            error="invalid Direct3D index range";return;
        }
        auto& i=index->second;
        if(!count || !upload(i,p.indexType))return;
        if(dx11()) {
            context()->IASetIndexBuffer(i.native11.Get(),unit==2?DXGI_FORMAT_R16_UINT:DXGI_FORMAT_R32_UINT,UINT(indexOffset));
            context()->DrawIndexed(count,first,base);
        }else {
            auto start=UINT(indexOffset/unit)+first;
            const UINT minimum=base<0?UINT(-int64_t(base)):0;
            const int64_t available=int64_t(vertices)-int64_t(base)-minimum;
            if(available<=0 || uint64_t(available)+minimum>uint64_t(UINT32_MAX)+1){error="invalid Direct3D base vertex";return;}
            if(check(dx9()->SetIndices(i.index9.Get()),"SetIndices"))
                check(dx9()->DrawIndexedPrimitive(primitive,base,minimum,UINT(available),start,primitives),"DrawIndexedPrimitive");
        }
    }
};
}
namespace {
std::unique_ptr<Device> makeDevice(Direct3DVersion version,int width,int height,void* window,bool warp,
                                   bool present,std::string& error) {
    auto target=createDirect3DTarget(version,width,height,window,warp,error);
    if(!target)return {};
    auto device=std::make_unique<WindowsDevice>(std::move(target),version,width,height);
    if(!device->init() || (present && !device->attach(window))){error=device->lastError();return {};}
    return device;
}
}
std::unique_ptr<Device> createDirect3DDevice(Direct3DVersion version,int width,int height,void* window,bool warp,std::string& error) {
    return makeDevice(version,width,height,window,warp,false,error);
}
std::unique_ptr<Device> createDirect3DWindowDevice(Direct3DVersion version,int width,int height,void* window,bool warp,std::string& error) {
    return makeDevice(version,width,height,window,warp,true,error);
}
}
