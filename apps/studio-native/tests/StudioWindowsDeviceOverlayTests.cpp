// Exercise the actual layered HWND used by the Windows Game panel without needing a Vulkan driver.
#include "apps/studio-native/StudioGameDevice.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "apps/studio-native/win/GameDeviceOverlay.h"
#include "tests/TestCheck.h"

int main() {
    using namespace aiverse::studio;
    auto host=[](){return CreateWindowExW(0,L"STATIC",nullptr,WS_POPUP,0,0,900,900,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);};
    HWND dock=host(),floating=host();
    AIVERSE_TEST_CHECK(dock && floating);
    HWND game=CreateWindowExW(0,L"STATIC",nullptr,WS_CHILD|WS_CLIPCHILDREN,4,8,393,852,dock,nullptr,GetModuleHandleW(nullptr),nullptr);
    AIVERSE_TEST_CHECK(game);
    GameDeviceOverlay overlay;
    StudioGameDevice phone{"iphone-15",false,true};
    AIVERSE_TEST_CHECK(overlay.update(game,phone.json()));
    HWND mask=GetWindow(game,GW_CHILD);
    AIVERSE_TEST_CHECK(mask && GetParent(mask)==game);
    const auto style=GetWindowLongPtrW(mask,GWL_EXSTYLE);
    constexpr auto required=WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE;
    AIVERSE_TEST_CHECK((style&required)==required);
    // The opaque camera island and transparent game area both leave the Game surface as the input target.
    for(POINT point : {POINT{196,29},POINT{120,350}}) {
        AIVERSE_TEST_CHECK(ChildWindowFromPointEx(game,point,CWP_SKIPTRANSPARENT)==game);
    }
    SetParent(game,floating);
    MoveWindow(game,9,13,852,393,FALSE);
    phone.landscape=true;
    AIVERSE_TEST_CHECK(overlay.update(game,phone.json()) && GetWindow(game,GW_CHILD)==mask);
    RECT bounds{};
    GetWindowRect(mask,&bounds);
    MapWindowPoints(nullptr,game,reinterpret_cast<POINT*>(&bounds),2);
    AIVERSE_TEST_CHECK(bounds.left==0 && bounds.top==0 && bounds.right==852 && bounds.bottom==393);
    phone.safeArea=false;
    AIVERSE_TEST_CHECK(overlay.update(game,phone.json()));
    AIVERSE_TEST_CHECK(overlay.update(game,StudioGameDevice{}.json()));
    AIVERSE_TEST_CHECK((GetWindowLongPtrW(mask,GWL_STYLE)&WS_VISIBLE)==0);
    AIVERSE_TEST_CHECK(overlay.update(game,phone.json()));
    AIVERSE_TEST_CHECK((GetWindowLongPtrW(mask,GWL_STYLE)&WS_VISIBLE)!=0);
    DestroyWindow(dock);
    DestroyWindow(floating);
    AIVERSE_TEST_CHECK(!IsWindow(mask));
}
