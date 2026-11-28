#pragma once
#include "engine/world/PhysisNaturePresentation.h"
#include <algorithm>
#include <cmath>

namespace aiverse::editor {
// An undirected burning sample has zero displacement and is drawn as a ring.
template<class Emit>
void natureFieldArrows(const world::NaturePresentationFrame& frame,float cx,float cz,float radius,float step,
                      bool wind,bool fire,Emit emit){
    if(!frame.valid||(!wind&&!fire))return;
    const int intervals=int(std::floor(2*radius/step));
    for(int iz=0;iz<=intervals;++iz)for(int ix=0;ix<=intervals;++ix){
        const float dx=ix*step-radius,dz=iz*step-radius;
        if(dx*dx+dz*dz>radius*radius)continue;
        const float x=cx+dx,z=cz+dz;
        if(x<frame.originX*frame.cellSize||z<frame.originZ*frame.cellSize||
           x>=(frame.originX+frame.width)*frame.cellSize||z>=(frame.originZ+frame.height)*frame.cellSize)continue;
        const auto at=frame.sample(x,z);
        if(!at.valid)continue;
        const float speed=std::hypot(at.windX,at.windZ);
        if(wind&&speed>.05f){
            const float length=step*.75f*std::clamp(speed/std::max(1.f,frame.maxWindSpeed),.25f,1.f);
            emit(x,z,at.windX/speed*length,at.windZ/speed*length,false,std::clamp(speed/20.f,0.f,1.f));
        }
        if(fire&&at.fire>.05f){
            const float heat=std::clamp(at.fire,0.f,1.f);
            const float scale=speed>.3f?step*(.6f+.8f*heat)/speed:0.f;
            emit(x,z,at.windX*scale,at.windZ*scale,true,heat);
        }
    }
}
}
