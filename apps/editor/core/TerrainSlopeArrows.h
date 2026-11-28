#pragma once
#include "engine/render/Math3D.h"
#include <algorithm>
#include <cmath>

namespace aiverse::editor {
template<class Height,class Arrow>
void terrainSlopeArrows(float x,float z,float radius,float step,Height height,Arrow arrow) {
    if(!std::isfinite(x)||!std::isfinite(z)||!std::isfinite(radius)||!std::isfinite(step))return;
    step=std::clamp(step,2.f,32.f);
    radius=std::clamp(radius,8.f,200.f);
    const float left=std::floor(x/step)*step-radius,top=std::floor(z/step)*step-radius;
    const int intervals=int(2*radius/step);
    for(int row=0;row<=intervals;++row)for(int col=0;col<=intervals;++col){
        const float dx=col*step-radius,dz=row*step-radius;
        if(dx*dx+dz*dz>radius*radius)continue;
        const float px=left+col*step,pz=top+row*step;
        const float gx=(height(px+1,pz)-height(px-1,pz))*.5f;
        const float gz=(height(px,pz+1)-height(px,pz-1))*.5f;
        const float slope=std::hypot(gx,gz);
        if(!std::isfinite(slope)||slope<.02f)continue;
        const float strength=std::min(slope/.6f,1.f);
        const float length=step*(.35f+.5f*strength);
        const render::float3 a{px,height(px,pz)+.4f,pz};
        arrow(a,render::float3{px-gx/slope*length,a.y-slope*length,pz-gz/slope*length},strength);
    }
}
}
