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
    std::cout << "Studio viewport layout contract PASS\n";
}
