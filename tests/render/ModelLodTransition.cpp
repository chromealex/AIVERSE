#include "engine/render/ModelLodTransition.h"
#include <vector>
#include <cassert>
#include <limits>
#include <cmath>
struct Level{int mesh;float maxPixels;};
using namespace aiverse::render;
int main(){
 const std::vector<Level> levels{{4,100},{7,40}};
 auto s=selectModelLod(0,levels,130,.15f);assert(s.mesh==0&&s.incoming<0);
 s=selectModelLod(0,levels,100,.15f);assert(s.mesh==0&&s.incoming==4&&std::abs(s.progress-.5f)<1e-5f);
 s=selectModelLod(0,levels,70,.15f);assert(s.mesh==4&&s.incoming<0);
 s=selectModelLod(0,levels,40,.15f);assert(s.mesh==4&&s.incoming==7&&std::abs(s.progress-.5f)<1e-5f);
 s=selectModelLod(0,levels,10,.15f);assert(s.mesh==7&&s.incoming<0);
 assert(selectModelLod(0,levels,99,0).mesh==4);
 assert(selectModelLod(0,levels,100,0).mesh==0);
 assert(selectModelLod(0,levels,std::numeric_limits<float>::infinity(),.15f).mesh==0);
 assert(selectModelLod(0,std::vector<Level>{},10,.15f).incoming<0);
 float prior=0;
 for(float p=114;p>=86;p-=1){auto x=selectModelLod(0,levels,p,.15f);assert(x.progress>=prior);prior=x.progress;}
 const std::vector<Level> close{{1,100},{2,99}};
 for(float p=80;p<120;p+=.1f){auto x=selectModelLod(0,close,p,.49f);assert(x.progress>=0&&x.progress<=1);assert(x.incoming<0||x.incoming==x.mesh+1);}
 // The shader Bayer permutation visits every rank once.
 bool ranks[16]{};
 for(int x=0;x<4;++x)for(int y=0;y<4;++y){int r=((x&1)^(y&1))*8+(y&1)*4+((x>>1)^(y>>1))*2+(y>>1);assert(r>=0&&r<16&&!ranks[r]);ranks[r]=true;}
 // Each pixel belongs to exactly one LOD, including endpoint coverage.
 for(float t : {0.f,.1f,.5f,.9f,1.f})for(int rank=0;rank<16;++rank){
  const float noise=(rank+.5f)/16;assert(int(noise>=t)+int(noise<t)==1);
 }
}
