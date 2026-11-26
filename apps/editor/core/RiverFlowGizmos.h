#pragma once
#include "apps/editor/core/MapDoc.h"
#include "engine/render/Math3D.h"
#include <algorithm>
#include <cmath>

namespace aiverse::editor {
// Geometry of the authored river current in map space. Each segment emits
// both bank edges and a downstream arrow with normalized local speed.
template<class Bank,class Arrow>
void riverFlowGizmos(const DocEntity& e,Bank bank,Arrow arrow) {
    const auto& water=e.extra["components"]["NereidWater"];
    const Json& pts = water["points"];
    if (!pts.isArray() || pts.size() < 2) return;
    const float baseY = float(e.extra["components"]["Transform"]["y"].toDouble(0)) +
                        float(water["surfaceY"].toDouble(0)) + 0.25f;
    // Downstream = spline order unless the authored flow vector points
    // the other way.
    const Json& flow = water["flow"];
    float fx = 0, fz = 0;
    if (flow.isArray() && flow.size() == 3) { fx = float(flow[0].toDouble(0)); fz = float(flow[2].toDouble(0)); }
    const auto P = [&](size_t k) {
        const Json& p = pts[k];
        return render::float3{e.x + float(p[0].toDouble(0)), baseY, e.z + float(p[1].toDouble(0))};
    };
    const auto half = [&](size_t k) { return pts[k].size() > 2 ? float(pts[k][2].toDouble(2)) : 2.f; };
    const auto first = P(0), last = P(pts.size() - 1);
    const bool reverse = fx * (last.x - first.x) + fz * (last.z - first.z) < 0;
    const float speed = std::sqrt(fx * fx + fz * fz);
    const Json& nodes = water["currentField"]["nodes"];
    for (size_t k = 0; k + 1 < pts.size(); ++k) {
        const auto a = P(k), b = P(k + 1);
        float tx = b.x - a.x, tz = b.z - a.z;
        const float segment = std::sqrt(tx * tx + tz * tz);
        if (segment < 1e-3f) continue;
        tx /= segment; tz /= segment;
        // Bank lines (half-width either side).
        const float ha = half(k), hb = half(k + 1);
        bank({a.x - tz * ha, a.y, a.z + tx * ha}, {b.x - tz * hb, b.y, b.z + tx * hb});
        bank({a.x + tz * ha, a.y, a.z - tx * ha}, {b.x + tz * hb, b.y, b.z - tx * hb});
        // Local downstream speed: currentField node, else |flow|.
        float local = speed;
        if (nodes.isArray() && nodes.size() == pts.size())
            local = float(nodes[k][0].toDouble(speed));
        const float t = std::clamp(local / 4.f, 0.f, 1.f);
        const float len = std::clamp(segment * 0.7f, 2.f, 12.f) * (0.5f + 0.5f * t);
        const render::float3 mid{(a.x + b.x) * .5f, a.y, (a.z + b.z) * .5f};
        const float dir = reverse ? -1.f : 1.f;
        const render::float3 tip{mid.x + tx * len * dir * .5f, mid.y, mid.z + tz * len * dir * .5f};
        const render::float3 tail{mid.x - tx * len * dir * .5f, mid.y, mid.z - tz * len * dir * .5f};
        arrow(tail, tip, t);
    }
}
}
