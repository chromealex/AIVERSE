#include "apps/studio-native/StudioViewportLayout.h"
#include "tests/TestCheck.h"
#include <cmath>
#include <iostream>
using namespace aiverse::studio;

int main() {
    const auto a = viewportPlacement({250,120,800,500,2,true},1440,900,2);
    AIVERSE_TEST_CHECK(a.visible && a.x==250 && a.y==280 && a.width==800 && a.height==500);
    AIVERSE_TEST_CHECK(a.pixelsW==1600 && a.pixelsH==1000);
    const auto clipped = viewportPlacement({-20,50,100,300,1,true},300,200,1);
    AIVERSE_TEST_CHECK(clipped.visible && clipped.x==0 && clipped.y==0);
    AIVERSE_TEST_CHECK(clipped.width==80 && clipped.height==150);
    AIVERSE_TEST_CHECK(!viewportPlacement({0,0,20,20,1,false},500,500,2).visible);
    AIVERSE_TEST_CHECK(!viewportPlacement({1000,0,20,20,1,true},500,500,2).visible);
    AIVERSE_TEST_CHECK(!viewportPlacement({0,0,NAN,20,1,true},500,500,2).visible);
    AIVERSE_TEST_CHECK(!viewportPlacement({0,0,50,50,999,true},500,500,2).visible);
    AIVERSE_TEST_CHECK(!viewportPlacement({0,0,50,50,2,true},500,500,0).visible);
    // Scene, Game and SpectraFX have independent native surfaces. Hiding or
    // resizing FX must not change the layout/visibility of the map viewport.
    const auto scene=viewportPlacement({0,40,700,610,2,true},1440,900,2);
    const auto fx=viewportPlacement({720,160,620,400,2,true},1440,900,2);
    AIVERSE_TEST_CHECK(scene.visible && fx.visible);
    AIVERSE_TEST_CHECK(scene.x==0 && scene.y==250 && fx.x==720 && fx.y==340);
    AIVERSE_TEST_CHECK(scene.pixelsW==1400 && scene.pixelsH==1220);
    AIVERSE_TEST_CHECK(fx.pixelsW==1240 && fx.pixelsH==800);
    const auto fxHidden=viewportPlacement({720,160,620,400,2,false},1440,900,2);
    AIVERSE_TEST_CHECK(!fxHidden.visible && scene.visible);
    const auto fxResized=viewportPlacement({720,160,400,290,2,true},1440,900,2);
    AIVERSE_TEST_CHECK(fxResized.visible && fxResized.pixelsW==800 && fxResized.pixelsH==580);
    AIVERSE_TEST_CHECK(scene.pixelsW==1400 && scene.pixelsH==1220);
    std::cout << "Studio viewport layout contract PASS\n";
}
