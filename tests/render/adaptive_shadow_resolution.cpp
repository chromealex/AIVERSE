#include "engine/render/AdaptiveShadowResolution.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <limits>
using namespace aiverse::render;
int main(){
 AdaptiveShadowResolution p;
 assert(p.update(900)&&p.resolution==1024);
 for(int i=0;i<300;i++)assert(!p.update(i%2?1050:1000));
 assert(p.update(1400)&&p.resolution==2048);
 assert(p.update(3100)&&p.resolution==4096);
 for(int i=0;i<119;i++)assert(!p.update(500));
 assert(p.update(500)&&p.resolution==1024);
 assert(p.update(8000,2048)&&p.resolution==2048);
 assert(!p.update(std::numeric_limits<float>::quiet_NaN()));
 assert(p.update(8000,1024)&&p.resolution==1024);
 assert(shadowTexelDemand(6,1080,1,.75f)>shadowTexelDemand(6,1080,8,.75f)*7.9f);
 assert(shadowTexelDemand(6,2160,4,.75f)==2*shadowTexelDemand(6,1080,4,.75f));
 assert(shadowTexelDemand(6,1080,0,.75f)==0);
}
