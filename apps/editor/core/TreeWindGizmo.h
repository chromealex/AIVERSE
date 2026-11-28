#pragma once
#include "apps/editor/core/MapDoc.h"
#include "engine/render/Renderer3D.h"
#include <algorithm>
#include <cmath>

namespace aiverse::editor {
inline bool isWindGizmoTree(const DocEntity& entity) {
    return entity.type.rfind("TerraTree",0)==0 || entity.extra["components"].has("TerraTree");
}

inline uint32_t windGizmoColor(float t,int alpha=230) {
    // calm cyan -> strong white -> gale orange
    t = std::clamp(t, 0.f, 1.f);
    const float r = t < .5f ? 80 + 350 * t : 255;
    const float g = t < .5f ? 200 + 110 * t : 255 - 160 * (t - .5f);
    const float b = t < .5f ? 240 : 240 - 400 * (t - .5f);
    return render::rgba(int(std::clamp(r, 0.f, 255.f)), int(std::clamp(g, 0.f, 255.f)),
                    int(std::clamp(b, 0.f, 255.f)), alpha);
}

// Both editors use the same reference height and the wind already submitted
// to the renderer. The calm ring keeps a stationary crown visible.
template<class Ground,class Line,class Arrow,class Ring>
void treeWindGizmo(const DocEntity& tree,const render::wind::SceneWind& wind,
                   Ground ground,Line line,Arrow arrow,Ring ring) {
    const auto& y=tree.extra["components"]["Transform"]["y"];
    const render::float3 base{tree.x,y.isNumber()?float(y.toDouble()):ground(tree.x,tree.z),tree.z};
    const render::float3 crown{base.x,base.y+12.f,base.z};
    line(base,crown,render::rgba(200,160,110,200));
    const float speed=std::hypot(wind.x,wind.z);
    if(speed<=.05f){
        ring(crown,render::rgba(160,220,255,200));
        return;
    }
    const float scale=(3.f+std::min(speed,30.f)*.4f)/speed;
    arrow(crown,render::float3{crown.x+wind.x*scale,crown.y,crown.z+wind.z*scale},windGizmoColor(speed/20.f));
}
}
