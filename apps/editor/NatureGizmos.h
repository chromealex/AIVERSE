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
#include "apps/editor/core/NatureFieldArrows.h"
#include "apps/editor/core/RiverFlowGizmos.h"
#include "apps/editor/core/TerrainSlopeArrows.h"
#include "apps/editor/core/TreeWindGizmo.h"
#include "apps/editor/core/ErosionZoneGizmo.h"
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
    const render::Camera& camera;
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



inline float ground(Editor& ed, float x, float z) {
    const float h = ed.surfaceHeightAt(x, z);
    return std::isfinite(h) ? h : 0.f;
}

} // namespace nature_gizmo_detail

inline void drawNatureGizmos(Editor& ed, const Viewport& vp, const render::Camera& camera) {
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
        natureFieldArrows(*frame,cx,cz,radius,step,cfg.physisWind,cfg.physisFire,
            [&](float x,float z,float dx,float dz,bool fire,float strength){
                const float y=ground(ed,x,z)+(fire?1.8f:1.2f);
                const auto color=fire?IM_COL32(255,int(150-110*strength),40,240):windGizmoColor(strength,200);
                if(dx||dz)paint.arrow({x,y,z},{x+dx,y,z+dz},color,fire?3.f:1.6f);
                else {ImVec2 p;if(paint.project({x,y,z},p))draw->AddCircle(p,6.f,color,12,2.5f);}
            });
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
            riverFlowGizmos(e,[&](render::float3 a,render::float3 b){
                paint.line(a,b,IM_COL32(120,210,255,isSelected?200:90));
            },[&](auto tail,auto tip,float speed){
                paint.arrow(tail,tip,IM_COL32(60,int(170+85*speed),255,isSelected?240:120),isSelected?2.5f:1.5f);
            });
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
        if(cfg.terraTreeWind && isWindGizmoTree(e)){
            treeWindGizmo(e,ed.renderer().sceneWind(),[&](float x,float z){return ground(ed,x,z);},
                [&](auto a,auto b,auto color){paint.line(a,b,color,2.f);},
                [&](auto a,auto b,auto color){paint.arrow(a,b,color,3.f);},
                [&](auto crown,auto color){ImVec2 p;if(paint.project(crown,p))draw->AddCircle(p,7.f,color,16,2.f);});
        }
    }
    if (!slopeHere && cfg.terraSlopeAtTarget) slopeHere = true;
    if (slopeHere) {
        terrainSlopeArrows(slopeX,slopeZ,radius,step,[&](float x,float z){return ground(ed,x,z);},
            [&](auto a,auto b,float t){paint.arrow(a,b,IM_COL32(int(120+135*t),int(220-150*t),80,210),1.8f);});
    }
    // ---- Erosion zones: where the ground erodes and how fast.
    if (cfg.erosionZones) {
        const Json& zones = doc.data.extra["erosionZones"];
        if (zones.isArray()) {
            const std::string selectedKey = ed.selection.kind == Selection::MapBlock ? ed.selection.key : std::string();
            for (size_t i = 0; i < zones.size(); ++i) {
                const Json& zone = zones[i];
                const bool picked = selectedKey == "erosionZones[" + std::to_string(i) + "]" ||
                                    selectedKey == "erosionZones";
                erosionZoneGizmo(zone,picked,[&](float x,float z){return ground(ed,x,z);},
                    [&](auto a,auto b,auto color){
                        paint.line(a,b,IM_COL32(0,0,0,120),picked?5.f:3.5f);
                        paint.line(a,b,color,picked?3.f:2.f);
                    },[&](auto centre,const std::string& text,auto color){
                        ImVec2 at;
                        if(!paint.project(centre,at))return;
                        const ImVec2 size=ImGui::CalcTextSize(text.c_str());
                        draw->AddRectFilled({at.x-size.x*.5f-4,at.y-size.y*.5f-2},
                                            {at.x+size.x*.5f+4,at.y+size.y*.5f+2},IM_COL32(0,0,0,160),4);
                        draw->AddText({at.x-size.x*.5f,at.y-size.y*.5f},color,text.c_str());
                    });
            }
        }
    }
    draw->PopClipRect();
}

} // namespace aiverse::editor
