// Vulkan RHI backend (Windows, Linux, Android). It keeps the GL conventions of the RHI exactly
// (clip depth [-1, 1], render targets not flipped, gl_FragCoord / dFdy from the bottom row), so the
// engine's GLSL runs unchanged: the GLSL is compiled to SPIR-V at run time (glslang) and the screen
// is drawn into an offscreen back buffer that is flipped into the swapchain at present. See docs/vulkan.md.
#pragma once
#include "../Gfx.h"
#include <memory>
#include <string>

namespace aiverse::gfx {

// Native window the swapchain presents to. Kind::None: offscreen device (tests, tools): the screen is
// only the back buffer, readable with captureNextFrame.
struct VulkanWindow {
    enum class Kind { None, Xlib, Win32, Android };
    Kind kind = Kind::None;
    void* display = nullptr;     // Xlib: Display*        Win32: HINSTANCE
    uint64_t window = 0;         // Xlib: Window (XID)     Win32: HWND      Android: ANativeWindow*
    bool vsync = true;
    // Writable folder for compiled (optimized) SPIR-V; empty = no disk cache. The first launch compiles and
    // optimizes every program (seconds on a phone), later launches load the results.
    std::string shaderCacheDir;
};

// nullptr (with the reason in *error) when there is no loader, no suitable GPU or no surface support:
// the caller falls back to GL.
std::unique_ptr<Device> createVulkanDevice(const VulkanWindow& window, std::string* error = nullptr);
// Android: the native window comes and goes with the activity. nullptr detaches (swapchain released,
// frames render offscreen); a window attaches a new surface. False when the surface can't be created.
bool vulkanSetWindow(Device& device, const VulkanWindow* window);

} // namespace aiverse::gfx
