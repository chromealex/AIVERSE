// Windows-only native offscreen surface. Release on its creating render thread.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDirect3DDevice9;

namespace aiverse::gfx {
class Device;
enum class Direct3DVersion { D3D9, D3D11 };
class Direct3DTarget {
public:
    virtual ~Direct3DTarget() = default;
    // Bind the render target and clear it. Native callers may issue draw commands before readback.
    virtual bool begin(const std::array<float,4>& clear,std::string& error)=0;
    // Bind the target and preserve existing pixels.
    virtual bool beginLoad(std::string& error)=0;
    virtual bool end(std::string& error)=0;
    virtual bool resize(int width,int height,std::string& error)=0;
    virtual bool attachWindow(void* nativeWindow,std::string& error)=0;
    virtual bool present(std::string& error)=0;
    // Finish an active pass if needed and copy top-down RGBA8 pixels. On failure preserve previous output.
    virtual bool readRgba(std::vector<uint8_t>& pixels,std::string& error)=0;
    virtual ID3D11Device* device11() const { return nullptr; }
    virtual ID3D11DeviceContext* context11() const { return nullptr; }
    virtual IDirect3DDevice9* device9() const { return nullptr; }
};
// D3D11 uses hardware unless warp is requested. D3D9 requires a real window handle;
// pass nullptr to let the target own a hidden window. D3D9 does not implement WARP.
std::unique_ptr<Device> createDirect3DDevice(Direct3DVersion version,
    int width,int height,void* nativeWindow,bool warp,std::string& error);
// Windowed variant presents to a caller-owned HWND at endFrame and resizes on beginFrame.
std::unique_ptr<Device> createDirect3DWindowDevice(Direct3DVersion version,
    int width,int height,void* nativeWindow,bool warp,std::string& error);
std::unique_ptr<Direct3DTarget> createDirect3DTarget(Direct3DVersion version,
    int width,int height,void* nativeWindow,bool warp,std::string& error);
}
