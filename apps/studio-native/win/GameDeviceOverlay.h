#pragma once
#include "apps/studio-native/StudioDeviceMask.h"
#include <windows.h>

namespace aiverse::studio {
class GameDeviceOverlay {
public:
    // This child follows the Game surface when it moves between docked and floating windows.
    bool update(HWND game, const Json& device) {
        RECT rect{};
        if(!GetClientRect(game,&rect)) return false;
        if(device["width"].toDouble()<=0 || rect.right<=0 || rect.bottom<=0) {
            if(window_) ShowWindow(window_,SW_HIDE);
            width_=0;
            return true;
        }
        const UINT dpi=GetDpiForWindow(game);
        if(width_==rect.right && height_==rect.bottom && dpi_==dpi && device==device_) return true;
        if(!window_) {
            window_=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,L"STATIC",nullptr,
                WS_CHILD,0,0,0,0,game,nullptr,GetModuleHandleW(nullptr),nullptr);
            if(!window_) return false;
        }
        BITMAPINFO info{};
        info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=rect.right;info.bmiHeader.biHeight=-rect.bottom;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        HDC dc=CreateCompatibleDC(nullptr);
        if(!dc) { ShowWindow(window_,SW_HIDE);width_=0;return false; }
        void* bits=nullptr;
        HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        bool ok=false;
        if(bitmap) {
            HGDIOBJ prior=SelectObject(dc,bitmap);
            if(prior && prior!=HGDI_ERROR) {
                drawDeviceMask(static_cast<uint32_t*>(bits),rect.right,rect.bottom,double(dpi)/96,device);
                POINT origin{};
                SIZE size{rect.right,rect.bottom};
                BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
                ok=UpdateLayeredWindow(window_,nullptr,nullptr,&size,dc,&origin,0,&blend,ULW_ALPHA)!=FALSE;
                SelectObject(dc,prior);
            }
            DeleteObject(bitmap);
        }
        DeleteDC(dc);
        if(!ok) { ShowWindow(window_,SW_HIDE);width_=0;return false; }
        ShowWindow(window_,SW_SHOWNOACTIVATE);
        width_=rect.right;height_=rect.bottom;dpi_=dpi;device_=device;
        return true;
    }
private:
    HWND window_=nullptr; // destroyed by the parent Game HWND
    LONG width_=0,height_=0;
    UINT dpi_=0;
    Json device_;
};
} // namespace aiverse::studio
