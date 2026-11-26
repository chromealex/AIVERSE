#pragma once
#include "engine/data/Json.h"
#include "engine/render/Renderer3D.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace aiverse::editor {
template<class Ground,class Edge,class Label>
void erosionZoneGizmo(const Json& zone,bool picked,Ground ground,Edge edge,Label label) {
    const float k=float(zone["coefficient"].toDouble(1));
    if(!std::isfinite(k))return;
    // 0 = protected (blue), 1 = default (yellow), >= 3 = fast (red).
    const float t = std::clamp(k / 3.f, 0.f, 1.f);
    const int r = k <= 0.f ? 90 : int(230 + 25 * t), g = k <= 0.f ? 160 : int(210 - 170 * t),
              b = k <= 0.f ? 255 : int(60 - 40 * t);
    const auto color = render::rgba(r, g, b, picked ? 255 : 210);
    std::vector<render::float3> ring;
    if (zone.getStr("shape", zone.has("points") ? "polygon" : "circle") == "polygon") {
        for (const auto& p : zone["points"].items())
            if (p.isArray() && p.size() == 2) {
                const float x = float(p[0].toDouble(0)), z = float(p[1].toDouble(0));
                if(!std::isfinite(x)||!std::isfinite(z))return;
            ring.push_back({x, ground(x, z) + 0.3f, z});
            }
    } else {
        const float zx = float(zone["x"].toDouble(0)), zz = float(zone["z"].toDouble(0));
        const float rad = std::max(0.5f, float(zone["radius"].toDouble(1)));
        if(!std::isfinite(zx)||!std::isfinite(zz)||!std::isfinite(rad))return;
        for (int a = 0; a < 48; ++a) {
            const float ang = float(a) * 6.2831853f / 48.f;
            const float x = zx + std::cos(ang) * rad, z = zz + std::sin(ang) * rad;
            ring.push_back({x, ground(x, z) + 0.3f, z});
        }
    }
    if (ring.size() < 3 || std::any_of(ring.begin(),ring.end(),[](const auto& p){
        return !std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z);
    })) return;
    // Densify edges so outlines follow the terrain.
    render::float3 centre{0, 0, 0};
    for (size_t a = 0; a < ring.size(); ++a) {
        const auto& p0 = ring[a];
        const auto& p1 = ring[(a + 1) % ring.size()];
        const int steps = int(std::clamp(std::hypot(p1.x - p0.x, p1.z - p0.z) / 2.f, 1.f, 64.f));
        render::float3 prev = p0;
        for (int s2 = 1; s2 <= steps; ++s2) {
            const float f = float(s2) / float(steps);
            const float x = p0.x + (p1.x - p0.x) * f, z = p0.z + (p1.z - p0.z) * f;
            const render::float3 cur{x, ground(x, z) + 0.3f, z};
            edge(prev,cur,color);
            prev = cur;
        }
        centre.x += p0.x; centre.y += p0.y; centre.z += p0.z;
    }
    centre.x /= float(ring.size()); centre.y /= float(ring.size()); centre.z /= float(ring.size());
    char rate[32];
    std::snprintf(rate,sizeof rate," x%.2g",double(k));
    auto name=zone.getStr("id","zone");
    if(name.size()>64){
        size_t end=61;
        while(end>0 && (static_cast<unsigned char>(name[end])&0xc0)==0x80)--end;
        name=name.substr(0,end)+"...";
    }
    auto text=name+rate;
    const auto soil=zone.getStr("soil");
    if(!soil.empty())text+=" "+soil.substr(0,16);
    centre.y+=1;
    label(centre,text,color);
}
}
