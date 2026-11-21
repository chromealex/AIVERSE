// Editor-only direction gizmos for PHYSIS, TerraWeave and NEREID in the Scene
// view, projected through ImDrawList like PhysisSceneOverlay. Presentation
// only: reads the PHYSIS nature frame, the map document and the display
// terrain; never edits the map or the simulation.
//   PHYSIS  : wind arrows on a grid around the camera target and fire-spread
//             arrows on burning cells (while a nature preview / game runs).
//   NEREID  : downstream flow arrows along the selected river + its banks.
//   TERRA   : downhill (slope) arrows around a selected terrain tile (or the
//             camera target when terraSlopeAtTarget); the wind direction at
//             the crown of a selected TerraWeave tree.
//   EROSION : authored erosion zones (map "erosionZones") outlined on the
//             ground, coloured by coefficient (blue protected .. red fast),
//             with "xK soil" labels; the selected zone is highlighted.
#pragma once
#include "Editor.h"
#include "engine/render/Math3D.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace aiverse::editor {

struct NatureGizmoSettings {
    bool physisWind = true;      // wind field arrows (needs a PHYSIS frame)
    bool physisFire = true;      // fire spread arrows
    bool nereidFlow = true;      // selected water: downstream arrows
    bool nereidAllWater = false; // every water body, not only the selection
    bool terraSlope = true;      // selected terrain tile: downhill arrows
    bool terraSlopeAtTarget = false; // downhill arrows around the camera target, no selection needed
    bool terraTreeWind = true;   // selected tree: wind at the crown
    bool erosionZones = true;    // authored erosion zones (map erosionZones): outline + coefficient
    float radiusMeters = 48;     // field sampling radius around the target
    float spacingMeters = 6;     // field sampling step
};

inline NatureGizmoSettings& natureGizmoSettings() {
    static NatureGizmoSettings settings;
    return settings;
}

namespace nature_gizmo_detail {

struct Painter {
    ImDrawList* draw;
    const Viewport& vp;
    const render::RTSCamera& camera;
    bool project(render::float3 p, ImVec2& out) const {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
            std::abs(p.x) > 1e6f || std::abs(p.y) > 1e6f || std::abs(p.z) > 1e6f) return false;
        render::float2 s{};
        if (!camera.worldToScreen(p, vp.pointsW, vp.pointsH, s)) return false;
        if (!std::isfinite(s.x) || !std::isfinite(s.y)) return false;
        out = {vp.originX + s.x, vp.originY + s.y};
        return true;
    }
    // World-space arrow from `a` to `b`, screen-space head; dark outline so it
    // reads over grass, water and sky.
    void arrow(render::float3 a, render::float3 b, ImU32 color, float thickness = 2.f) const {
        ImVec2 pa, pb;
        if (!project(a, pa) || !project(b, pb)) return;
        const float dx = pb.x - pa.x, dy = pb.y - pa.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 3.f) return;
        const float ux = dx / len, uy = dy / len;
        const float head = std::clamp(len * 0.35f, 5.f, 14.f);
        const ImVec2 l{pb.x - ux * head - uy * head * 0.55f, pb.y - uy * head + ux * head * 0.55f};
        const ImVec2 r{pb.x - ux * head + uy * head * 0.55f, pb.y - uy * head - ux * head * 0.55f};
        const ImU32 shadow = IM_COL32(0, 0, 0, 140);
        draw->AddLine(pa, pb, shadow, thickness + 2.f);
        draw->AddTriangleFilled(pb, l, r, shadow);
        draw->AddLine(pa, pb, color, thickness);
        draw->AddTriangleFilled(pb, l, r, color);
    }
    void line(render::float3 a, render::float3 b, ImU32 color, float thickness = 1.5f) const {
        ImVec2 pa, pb;
        if (project(a, pa) && project(b, pb)) draw->AddLine(pa, pb, color, thickness);
    }
};

inline ImU32 speedColor(float t, int alpha = 230) {
    // calm cyan -> strong white -> gale orange
    t = std::clamp(t, 0.f, 1.f);
    const float r = t < .5f ? 80 + 350 * t : 255;
    const float g = t < .5f ? 200 + 110 * t : 255 - 160 * (t - .5f);
    const float b = t < .5f ? 240 : 240 - 400 * (t - .5f);
    return IM_COL32(int(std::clamp(r, 0.f, 255.f)), int(std::clamp(g, 0.f, 255.f)),
                    int(std::clamp(b, 0.f, 255.f)), alpha);
}

inline float ground(Editor& ed, float x, float z) {
    const float h = ed.surfaceHeightAt(x, z);
    return std::isfinite(h) ? h : 0.f;
}

} // namespace nature_gizmo_detail

inline void drawNatureGizmos(Editor& ed, const Viewport& vp, const render::RTSCamera& camera) {
    using namespace nature_gizmo_detail;
    auto& cfg = natureGizmoSettings();
    if (vp.pointsW <= 0 || vp.pointsH <= 0 || ed.planetMap()) return;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(ImVec2(vp.originX, vp.originY),
                       ImVec2(vp.originX + vp.pointsW, vp.originY + vp.pointsH), true);
    const Painter paint{draw, vp, camera};
    const float step = std::clamp(cfg.spacingMeters, 2.f, 32.f);
    const float radius = std::clamp(cfg.radiusMeters, 8.f, 200.f);
    const float cx = std::floor(camera.target.x / step) * step;
    const float cz = std::floor(camera.target.z / step) * step;

    // ---- PHYSIS: wind field and fire spread from the nature frame.
    if (const auto* frame = ed.physisNaturePreview().frame(); frame && frame->valid) {
        const float maxWind = std::max(1.f, frame->maxWindSpeed);
        for (float z = cz - radius; z <= cz + radius; z += step)
            for (float x = cx - radius; x <= cx + radius; x += step) {
                if ((x - cx) * (x - cx) + (z - cz) * (z - cz) > radius * radius) continue;
                const auto at = frame->sample(x, z);
                if (!at.valid) continue;
                const float speed = std::sqrt(at.windX * at.windX + at.windZ * at.windZ);
                const float y = ground(ed, x, z) + 1.2f;
                if (cfg.physisWind && speed > 0.05f) {
                    const float len = step * 0.75f * std::clamp(speed / maxWind, 0.25f, 1.f);
                    paint.arrow({x, y, z}, {x + at.windX / speed * len, y, z + at.windZ / speed * len},
                                speedColor(speed / 20.f, 200), 1.6f);
                }
                if (cfg.physisFire && at.fire > 0.05f) {
                    // Fire runs downwind; a calm fire just glows (ring).
                    const float len = step * (0.6f + 0.8f * std::clamp(at.fire, 0.f, 1.f));
                    const ImU32 fire = IM_COL32(255, int(150 - 110 * std::clamp(at.fire, 0.f, 1.f)), 40, 240);
                    if (speed > 0.3f)
                        paint.arrow({x, y + 0.6f, z}, {x + at.windX / speed * len, y + 0.6f, z + at.windZ / speed * len}, fire, 3.f);
                    else {
                        ImVec2 p;
                        if (paint.project({x, y + 0.6f, z}, p)) draw->AddCircle(p, 6.f, fire, 12, 2.5f);
                    }
                }
            }
    }

    const auto& doc = ed.doc();
    const int selected = ed.selection.kind == Selection::Entity ? ed.selection.index : -1;

    // ---- NEREID: river splines, downstream arrows and banks.
    if (cfg.nereidFlow) {
        for (size_t i = 0; i < doc.data.entities.size(); ++i) {
            const auto& e = doc.data.entities[i];
            const Json& water = e.extra["components"]["NereidWater"];
            if (!water.isObject()) continue;
            const bool isSelected = int(i) == selected;
            if (!isSelected && !cfg.nereidAllWater) continue;
            const Json& pts = water["points"];
            if (!pts.isArray() || pts.size() < 2) continue;
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
            const ImU32 bank = IM_COL32(120, 210, 255, isSelected ? 200 : 90);
            for (size_t k = 0; k + 1 < pts.size(); ++k) {
                const auto a = P(k), b = P(k + 1);
                float tx = b.x - a.x, tz = b.z - a.z;
                const float segment = std::sqrt(tx * tx + tz * tz);
                if (segment < 1e-3f) continue;
                tx /= segment; tz /= segment;
                // Bank lines (half-width either side).
                const float ha = half(k), hb = half(k + 1);
                paint.line({a.x - tz * ha, a.y, a.z + tx * ha}, {b.x - tz * hb, b.y, b.z + tx * hb}, bank);
                paint.line({a.x + tz * ha, a.y, a.z - tx * ha}, {b.x + tz * hb, b.y, b.z - tx * hb}, bank);
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
                paint.arrow(tail, tip, IM_COL32(60, int(170 + 85 * t), 255, isSelected ? 240 : 120),
                            isSelected ? 2.5f : 1.5f);
            }
        }
    }

    // ---- TERRA: selected tree (wind at the crown) and terrain slope.
    bool slopeHere = false;
    float slopeX = cx, slopeZ = cz;
    if (selected >= 0 && size_t(selected) < doc.data.entities.size()) {
        const auto& e = doc.data.entities[size_t(selected)];
        const std::string type = e.type;
        if (cfg.terraSlope && (type.rfind("TerraTerrain", 0) == 0 ||
                               e.extra["components"].has("TerraTerrain"))) {
            slopeHere = true; slopeX = e.x; slopeZ = e.z;
        }
        if (cfg.terraTreeWind && (type.rfind("TerraTree", 0) == 0 || e.extra["components"].has("TerraTree"))) {
            const float y = float(e.extra["components"]["Transform"]["y"].toDouble(ground(ed, e.x, e.z)));
            const auto wind = ed.renderer().sceneWind();
            const float speed = std::sqrt(wind.x * wind.x + wind.z * wind.z);
            const render::float3 base{e.x, y, e.z}, crown{e.x, y + 12.f, e.z};
            paint.line(base, crown, IM_COL32(200, 160, 110, 200), 2.f);
            if (speed > 0.05f) {
                const float len = 3.f + std::min(speed, 30.f) * 0.4f;
                paint.arrow(crown, {crown.x + wind.x / speed * len, crown.y, crown.z + wind.z / speed * len},
                            speedColor(speed / 20.f), 3.f);
            } else {
                ImVec2 p;
                if (paint.project(crown, p)) draw->AddCircle(p, 7.f, IM_COL32(160, 220, 255, 200), 16, 2.f);
            }
        }
    }
    if (!slopeHere && cfg.terraSlopeAtTarget) slopeHere = true;
    if (slopeHere) {
        const float sx = std::floor(slopeX / step) * step, sz = std::floor(slopeZ / step) * step;
        for (float z = sz - radius; z <= sz + radius; z += step)
            for (float x = sx - radius; x <= sx + radius; x += step) {
                if ((x - sx) * (x - sx) + (z - sz) * (z - sz) > radius * radius) continue;
                const float h = ground(ed, x, z);
                const float gx = (ground(ed, x + 1.f, z) - ground(ed, x - 1.f, z)) * .5f;
                const float gz = (ground(ed, x, z + 1.f) - ground(ed, x, z - 1.f)) * .5f;
                const float g = std::sqrt(gx * gx + gz * gz);
                if (g < 0.02f) continue;   // flatter than 2%
                const float t = std::clamp(g / 0.6f, 0.f, 1.f);   // 60% slope = red
                const float len = step * (0.35f + 0.5f * t);
                paint.arrow({x, h + 0.4f, z}, {x - gx / g * len, h + 0.4f - g * len, z - gz / g * len},
                            IM_COL32(int(120 + 135 * t), int(220 - 150 * t), 80, 210), 1.8f);
            }
    }
    // ---- Erosion zones: where the ground erodes and how fast.
    if (cfg.erosionZones) {
        const Json& zones = doc.data.extra["erosionZones"];
        if (zones.isArray()) {
            const std::string selectedKey = ed.selection.kind == Selection::MapBlock ? ed.selection.key : std::string();
            for (size_t i = 0; i < zones.size(); ++i) {
                const Json& zone = zones[i];
                const float k = float(zone["coefficient"].toDouble(1.0));
                const bool picked = selectedKey == "erosionZones[" + std::to_string(i) + "]" ||
                                    selectedKey == "erosionZones";
                // 0 = protected (blue), 1 = default (yellow), >= 3 = fast (red).
                const float t = std::clamp(k / 3.f, 0.f, 1.f);
                const int r = k <= 0.f ? 90 : int(230 + 25 * t), g = k <= 0.f ? 160 : int(210 - 170 * t),
                          b = k <= 0.f ? 255 : int(60 - 40 * t);
                const ImU32 line = IM_COL32(r, g, b, picked ? 255 : 210);
                std::vector<render::float3> ring;
                if (zone.getStr("shape", zone.has("points") ? "polygon" : "circle") == "polygon") {
                    for (const auto& p : zone["points"].items())
                        if (p.isArray() && p.size() == 2) {
                            const float x = float(p[0].toDouble(0)), z = float(p[1].toDouble(0));
                            ring.push_back({x, ground(ed, x, z) + 0.3f, z});
                        }
                } else {
                    const float zx = float(zone["x"].toDouble(0)), zz = float(zone["z"].toDouble(0));
                    const float rad = std::max(0.5f, float(zone["radius"].toDouble(1)));
                    for (int a = 0; a < 48; ++a) {
                        const float ang = float(a) * 6.2831853f / 48.f;
                        const float x = zx + std::cos(ang) * rad, z = zz + std::sin(ang) * rad;
                        ring.push_back({x, ground(ed, x, z) + 0.3f, z});
                    }
                }
                if (ring.size() < 3) continue;
                // Densify edges so outlines follow the terrain.
                render::float3 centre{0, 0, 0};
                for (size_t a = 0; a < ring.size(); ++a) {
                    const auto& p0 = ring[a];
                    const auto& p1 = ring[(a + 1) % ring.size()];
                    const int steps = std::clamp(int(std::hypot(p1.x - p0.x, p1.z - p0.z) / 2.f), 1, 64);
                    render::float3 prev = p0;
                    for (int s2 = 1; s2 <= steps; ++s2) {
                        const float f = float(s2) / float(steps);
                        const float x = p0.x + (p1.x - p0.x) * f, z = p0.z + (p1.z - p0.z) * f;
                        const render::float3 cur{x, ground(ed, x, z) + 0.3f, z};
                        paint.line(prev, cur, IM_COL32(0, 0, 0, 120), picked ? 5.f : 3.5f);
                        paint.line(prev, cur, line, picked ? 3.f : 2.f);
                        prev = cur;
                    }
                    centre.x += p0.x; centre.y += p0.y; centre.z += p0.z;
                }
                centre.x /= float(ring.size()); centre.y /= float(ring.size()); centre.z /= float(ring.size());
                ImVec2 at;
                if (paint.project({centre.x, centre.y + 1.f, centre.z}, at)) {
                    char label[96];
                    const std::string soil = zone.getStr("soil", "");
                    std::snprintf(label, sizeof label, "%s x%.2g%s%s", zone.getStr("id", "zone").c_str(), double(k),
                                  soil.empty() ? "" : " ", soil.c_str());
                    const ImVec2 size = ImGui::CalcTextSize(label);
                    draw->AddRectFilled({at.x - size.x * .5f - 4, at.y - size.y * .5f - 2},
                                        {at.x + size.x * .5f + 4, at.y + size.y * .5f + 2}, IM_COL32(0, 0, 0, 160), 4.f);
                    draw->AddText({at.x - size.x * .5f, at.y - size.y * .5f}, line, label);
                }
            }
        }
    }
    draw->PopClipRect();
}

} // namespace aiverse::editor
