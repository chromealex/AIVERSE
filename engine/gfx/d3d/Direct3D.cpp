#include "Direct3D.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3d9.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace aiverse::gfx {
namespace {
using Microsoft::WRL::ComPtr;
bool checked(HRESULT result,const char* operation,std::string& error) {
    if(SUCCEEDED(result))return true;
    char hex[16];std::snprintf(hex,sizeof(hex),"0x%08lx",static_cast<unsigned long>(result));
    error=std::string(operation)+" failed ("+hex+")";
    return false;
}
bool validClear(const std::array<float,4>& clear,std::string& error) {
    if(std::all_of(clear.begin(),clear.end(),[](float c){return std::isfinite(c) && c>=0 && c<=1;}))return true;
    error="clear color must contain four finite values in [0,1]";return false;
}
class Target11 final : public Direct3DTarget {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> color, staging;
    ComPtr<ID3D11RenderTargetView> view;
    const int width,height;
    bool active=false;
public:
    Target11(int w,int h):width(w),height(h){}
    bool init(bool warp,std::string& error) {
        // Explicit 11_0 feature level to keep the native contract usable by SM5 shaders.
        constexpr D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0;
        if(!checked(D3D11CreateDevice(nullptr,warp?D3D_DRIVER_TYPE_WARP:D3D_DRIVER_TYPE_HARDWARE,
            nullptr,0,&level,1,D3D11_SDK_VERSION,device.GetAddressOf(),nullptr,context.GetAddressOf()),
            "D3D11CreateDevice",error))return false;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=UINT(width);desc.Height=UINT(height);desc.MipLevels=1;desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
        desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        if(!checked(device->CreateTexture2D(&desc,nullptr,color.GetAddressOf()),"CreateTexture2D(target)",error) ||
           !checked(device->CreateRenderTargetView(color.Get(),nullptr,view.GetAddressOf()),"CreateRenderTargetView",error))return false;
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        return checked(device->CreateTexture2D(&desc,nullptr,staging.GetAddressOf()),"CreateTexture2D(readback)",error);
    }
    ID3D11Device* device11() const override {return device.Get();}
    ID3D11DeviceContext* context11() const override {return context.Get();}
    bool begin(const std::array<float,4>& clear,std::string& error) override {
        if(active){error="Direct3D 11 pass already active";return false;}
        if(!validClear(clear,error))return false;
        if(!beginLoad(error))return false;
        context->ClearRenderTargetView(view.Get(),clear.data());
        return true;
    }
    bool beginLoad(std::string& error) override {
        if(active){error="Direct3D 11 pass already active";return false;}
        auto* target=view.Get();
        context->OMSetRenderTargets(1,&target,nullptr);
        D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};
        context->RSSetViewports(1,&viewport);
        active=true;error.clear();return true;
    }
    bool end(std::string& error) override {
        if(!active){error="no active Direct3D 11 pass";return false;}
        active=false;context->OMSetRenderTargets(0,nullptr,nullptr);
        error.clear();return true;
    }
    bool readRgba(std::vector<uint8_t>& pixels,std::string& error) override {
        if(active && !end(error))return false;
        std::vector<uint8_t> result(size_t(width)*height*4);
        context->CopyResource(staging.Get(),color.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if(!checked(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Map(readback)",error))return false;
        for(int y=0;y<height;++y)
            std::memcpy(result.data()+size_t(y)*width*4,
                        static_cast<const uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch,size_t(width)*4);
        context->Unmap(staging.Get(),0);
        pixels.swap(result);error.clear();return true;
    }
};
struct OwnedWindow {
    HWND window=nullptr;
    ~OwnedWindow(){if(window)DestroyWindow(window);}
};
class Target9 final : public Direct3DTarget {
    // COM objects must be released before destroying the focus window.
    OwnedWindow owned;
    ComPtr<IDirect3D9> api;
    ComPtr<IDirect3DDevice9> device;
    ComPtr<IDirect3DSurface9> target,staging;
    const int width,height;
    bool active=false;
public:
    Target9(int w,int h):width(w),height(h){}
    ~Target9() override {if(active)device->EndScene();}
    bool init(HWND window,std::string& error) {
        if(!window) {
            owned.window=CreateWindowExW(0,L"STATIC",L"AIVERSE D3D9 offscreen",WS_POPUP,0,0,1,1,
                                         nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
            if(!owned.window){error="cannot create the Direct3D 9 focus window";return false;}
            window=owned.window;
        }
        api.Attach(Direct3DCreate9(D3D_SDK_VERSION));
        if(!api){error="Direct3DCreate9 returned null";return false;}
        D3DPRESENT_PARAMETERS pp{};
        pp.BackBufferWidth=1;pp.BackBufferHeight=1;pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
        pp.BackBufferFormat=D3DFMT_UNKNOWN;pp.hDeviceWindow=window;
        if(!checked(api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                        &pp,device.GetAddressOf()),"CreateDevice(D3D9 HAL)",error))return false;
        if(!checked(device->CreateRenderTarget(width,height,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,
                        target.GetAddressOf(),nullptr),"CreateRenderTarget",error))return false;
        return checked(device->CreateOffscreenPlainSurface(width,height,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,
                     staging.GetAddressOf(),nullptr),"CreateOffscreenPlainSurface",error);
    }
    IDirect3DDevice9* device9() const override {return device.Get();}
    bool begin(const std::array<float,4>& clear,std::string& error) override {
        if(active){error="Direct3D 9 pass already active";return false;}
        if(!validClear(clear,error))return false;
        auto byte=[&](int i){return DWORD(std::lround(clear[i]*255));};
        if(!beginLoad(error))return false;
        if(checked(device->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_ARGB(byte(3),byte(0),byte(1),byte(2)),1,0),"Clear",error))return true;
        std::string ignored;end(ignored);return false;
    }
    bool beginLoad(std::string& error) override {
        if(active){error="Direct3D 9 pass already active";return false;}
        if(!checked(device->SetRenderTarget(0,target.Get()),"SetRenderTarget",error) ||
           !checked(device->BeginScene(),"BeginScene",error))return false;
        active=true;error.clear();return true;
    }
    bool end(std::string& error) override {
        if(!active){error="no active Direct3D 9 pass";return false;}
        active=false;return checked(device->EndScene(),"EndScene",error);
    }
    bool readRgba(std::vector<uint8_t>& pixels,std::string& error) override {
        if(active && !end(error))return false;
        std::vector<uint8_t> result(size_t(width)*height*4);
        if(!checked(device->GetRenderTargetData(target.Get(),staging.Get()),"GetRenderTargetData",error))return false;
        D3DLOCKED_RECT mapped{};
        if(!checked(staging->LockRect(&mapped,nullptr,D3DLOCK_READONLY),"LockRect(readback)",error))return false;
        for(int y=0;y<height;++y) {
            const auto* src=static_cast<const uint8_t*>(mapped.pBits)+ptrdiff_t(y)*mapped.Pitch;
            auto* dst=result.data()+size_t(y)*width*4;
            for(int x=0;x<width;++x,src+=4,dst+=4){
                dst[0]=src[2];dst[1]=src[1];dst[2]=src[0];dst[3]=src[3];
            }
        }
        if(!checked(staging->UnlockRect(),"UnlockRect(readback)",error))return false;
        pixels.swap(result);error.clear();return true;
    }
};
}
std::unique_ptr<Direct3DTarget> createDirect3DTarget(Direct3DVersion version,int width,int height,
        void* nativeWindow,bool warp,std::string& error) {
    if(width<=0 || height<=0 || width>8192 || height>8192 || uint64_t(width)*height>16777216) {
        error="invalid Direct3D target size";return {};
    }
    error.clear();
    switch(version) {
    case Direct3DVersion::D3D11:{
        auto target=std::make_unique<Target11>(width,height);
        if(!target->init(warp,error))return {};
        return target;
    }
    case Direct3DVersion::D3D9:{
        if(warp){error="WARP is a Direct3D 11 driver";return {};}
        auto target=std::make_unique<Target9>(width,height);
        if(!target->init(static_cast<HWND>(nativeWindow),error))return {};
        return target;
    }
    }
    error="unknown Direct3D version";return {};
}
}
