#include "engine/view/Surface.h"
#include "../AlwaysCheck.h"
#include <cmath>
#include <optional>
int main(){
    using namespace aiverse::view;
    auto base=[](float x,float z){return 2.f*x+3.f*z;};
    auto overlay=[](float,float,float h){return h+.25f;};
    const float h=composeSurfaceHeight(3.f,4.f,base,overlay);
    AIVERSE_TEST_CHECK(std::abs(h-18.25f)<.00001f);
    auto flat=sampleSurface(0.f,0.f,.5f,[](float,float){return 1.f;});
    AIVERSE_TEST_CHECK(flat && flat->height==1.f);
    AIVERSE_TEST_CHECK(std::abs(flat->normal.x)<.00001f);
    AIVERSE_TEST_CHECK(std::abs(flat->normal.y-1.f)<.00001f);
    auto sloped=sampleSurface(2.f,3.f,.1f,base);
    AIVERSE_TEST_CHECK(sloped);
    AIVERSE_TEST_CHECK(sloped->normal.x<0 && sloped->normal.y>0 &&
           sloped->normal.z<0);
    AIVERSE_TEST_CHECK(!sampleSurface(1.f,2.f,0.f,base));
}
