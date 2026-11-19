#include "engine/view/TriangleFacing.h"
#include <cassert>
#include <limits>
using namespace aiverse;
int main(){
    const render::float3 a{0,0,0},b{1,0,0},c{0,0,1};
    const auto up=view::orientTriangleTo(a,b,c,{0,1,0});
    assert(up.valid && up.flip);
    assert(up.normal.y>0.99f);
    const auto down=view::orientTriangleTo(a,b,c,{0,-1,0});
    assert(down.valid && !down.flip);
    assert(down.normal.y<-.99f);
    const auto deg=view::orientTriangleTo(a,a,a,{0,1,0});
    assert(!deg.valid && !deg.flip);
    const float nan=std::numeric_limits<float>::quiet_NaN();
    const float inf=std::numeric_limits<float>::infinity();
    assert(!view::orientTriangleTo(a,b,c,{nan,1,0}).valid);
    assert(!view::orientTriangleTo(a,b,c,{0,inf,0}).valid);
    assert(!view::orientTriangleTo(a,b,c,{0,1,-inf}).valid);
    assert(!view::orientTriangleTo(a,b,{nan,0,1},{0,1,0}).valid);
}
