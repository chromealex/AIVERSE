// PHYSIS Stage 22 — marquee, configurable pivots and 3D world-space field gizmos.
// Authoring writes ONLY through MapDoc Undo/Redo, never live Simulation World.
// No genre-specific identity or Box3D interaction; existing Scene selection
// stays authoritative unless an explicitly enabled PHYSIS marker was clicked.
#pragma once
#include "Editor.h"
#include "EditorUtil.h"
#include "PhysisAuthoring.h"
#include "PhysisGizmoGeometry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <utility>

namespace aiverse::editor {

struct PhysisFieldEditorState {
    bool showMarkers = true;
    bool editMode = false;
    bool dragging = false;
    bool dragEdited = false;
    bool draggingRadius = false;
    bool snapEnabled = false;
    float snapStepMeters = 5.f;
    float groupYawDegrees = 0.f;
    float groupUniformScale = 1.f;
    int gizmoMode = 0; // 0: XYZ move, 1: radius, 2: Y rotation, 3: uniform scale
    int pivotMode = 0;
    float customPivot[3] = {};
    bool show3DAxes = true;
    int dragAxis = -1;
    double dragAxisStart = 0.0;
    int dragTransform = 0; // 0 no, 2 yaw, 3 scale
    Json dragBaseline;
    physis_authoring::FieldPivot dragTransformPivot{};
    double dragRingInitialAngle = 0;
    double dragRingInitialRadius = 1;
    std::uint64_t dragKnownRevision = 0;
    bool boxSelectEnabled = false;
    bool boxSelecting = false;
    bool boxAdditive = false;
    ImVec2 boxStart{}, boxEnd{};
    double dragRawY = 0;
    std::string selectedId;
    std::vector<std::string> selectedIds; // sorted authored IDs
    std::vector<physis_authoring::FieldPosition> dragOriginal;
    double dragAnchorX = 0, dragAnchorY = 0, dragAnchorZ = 0;
    double dragAppliedX = 0, dragAppliedY = 0, dragAppliedZ = 0;
    std::string draftId;
    std::string error;
    std::string rawDraft;
    Json draftField;
    double grabOffsetX = 0, grabOffsetZ = 0;
    float lastMouseY = 0;
};

inline PhysisFieldEditorState& physisFieldEditorState() {
    static PhysisFieldEditorState state;
    return state;
}

inline bool physisFieldPosition(const Json& field, render::float3& position) {
    const Json& coords = field["center"];
    if (!coords.isArray() || coords.size()!=3) return false;
    for (std::size_t axis=0;axis<3;++axis)
        if (!coords[axis].isNumber() ||
            !std::isfinite(coords[axis].toDouble()) ||
            std::abs(coords[axis].toDouble()) > 1000000) return false;
    position = {float(coords[0].toDouble()),float(coords[1].toDouble()),
                float(coords[2].toDouble())};
    return true;
}

inline void setPhysisFieldSelection(std::vector<std::string> ids) {
    auto& state = physisFieldEditorState();
    std::sort(ids.begin(),ids.end());
    ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
    state.selectedIds = std::move(ids);
    state.selectedId = state.selectedIds.empty() ? std::string() : state.selectedIds.back();
    state.draftId.clear();
    state.error.clear();
    state.dragging = false;
    state.draggingRadius = false;
    state.dragEdited = false;
    state.dragOriginal.clear();
    state.dragAxis=-1;
    state.dragTransform=0;
    state.dragBaseline=Json{};
}

// Ctrl/Shift toggles membership; a plain selection replaces it.
inline void selectPhysisField(const std::string& id, bool toggle = false) {
    auto& state = physisFieldEditorState();
    if (!toggle) {
        if (state.selectedIds.size() == 1 && state.selectedId == id) return;
        setPhysisFieldSelection(id.empty() ? std::vector<std::string>{} :
                                             std::vector<std::string>{id});
        return;
    }
    if (id.empty()) return;
    auto next = state.selectedIds;
    const auto it = std::find(next.begin(),next.end(),id);
    if (it == next.end()) next.push_back(id);
    else next.erase(it);
    setPhysisFieldSelection(std::move(next));
}

inline bool physisFieldSelected(const std::string& id) {
    const auto& ids = physisFieldEditorState().selectedIds;
    return std::binary_search(ids.begin(),ids.end(),id);
}


inline bool physisCurrentPivot(const Editor& ed,physis_authoring::FieldPivot& result) {
    const auto& st=physisFieldEditorState();
    return physis_authoring::computeFieldPivot(
        physis_authoring::read(ed.doc()),st.selectedIds,st.selectedId,
        static_cast<physis_authoring::FieldPivotMode>(st.pivotMode),
        {double(st.customPivot[0]),double(st.customPivot[1]),
         double(st.customPivot[2])},result);
}
inline render::float3 physisPivot3(const physis_authoring::FieldPivot& p) {
    return {float(p.x),float(p.y),float(p.z)};
}
inline std::vector<physis_gizmo::ScreenMarker> physisVisibleMarkers(
    const Json& config,const Viewport& vp,const render::RTSCamera& camera) {
    std::vector<physis_gizmo::ScreenMarker> output;
    for (const char* key:{"windZones","storms","gravityFields"}) {
        const Json& fields=config[key];
        if (!fields.isArray()) continue;
        for (const auto& entry:fields.items()) {
            render::float3 position{};
            render::float2 point{};
            if (!physisFieldPosition(entry,position) ||
                !camera.worldToScreen(position,vp.pointsW,vp.pointsH,point) ||
                !std::isfinite(point.x)||!std::isfinite(point.y)) continue;
            const double x=vp.originX+point.x,y=vp.originY+point.y;
            if (x>=vp.originX && x<=vp.originX+vp.pointsW &&
                y>=vp.originY && y<=vp.originY+vp.pointsH)
                output.push_back({entry.getStr("id"),x,y});
        }
    }
    return output;
}

inline float physisHandleLength(const render::RTSCamera& camera) {
    return std::clamp(camera.distance*0.28f,2.0f,160.0f);
}
inline bool physisAxisScreen(const render::RTSCamera& camera,const Viewport& vp,
                             render::float3 pivot,int axis,ImVec2& a,ImVec2& b) {
    const auto direction=physis_gizmo::worldAxis(axis);
    const float length=physisHandleLength(camera);
    render::float2 from{},to{};
    if (!camera.worldToScreen(pivot,vp.pointsW,vp.pointsH,from) ||
        !camera.worldToScreen({pivot.x+direction.x*length,
                               pivot.y+direction.y*length,
                               pivot.z+direction.z*length},
                              vp.pointsW,vp.pointsH,to) ||
        !std::isfinite(from.x)||!std::isfinite(from.y)||
        !std::isfinite(to.x)||!std::isfinite(to.y))
        return false;
    a={vp.originX+from.x,vp.originY+from.y};
    b={vp.originX+to.x,vp.originY+to.y};
    return true;
}
inline bool physisCaptureGroupOrigins(Editor& ed) {
    auto& st=physisFieldEditorState();
    st.dragOriginal.clear();
    const Json config=physis_authoring::read(ed.doc());
    for (const auto& id:st.selectedIds) {
        std::string collection;
        std::size_t index=0;
        Json entry;
        if (!physis_authoring::findField(config,id,collection,index,&entry) ||
            !entry["center"].isArray() || entry["center"].size()!=3) {
            st.dragOriginal.clear();
            st.error="PHYSIS field in the selection is missing";
            return false;
        }
        const Json& v=entry["center"];
        st.dragOriginal.push_back({id,v[0].toDouble(),v[1].toDouble(),v[2].toDouble()});
    }
    return !st.dragOriginal.empty();
}

inline bool physisFieldRadius(const Json& field, double& radius) {
    const Json& value = field["outerRadiusMeters"].isNull() ?
                        field["radiusMeters"] : field["outerRadiusMeters"];
    if (!value.isNumber()) return false;
    radius = value.toDouble();
    return std::isfinite(radius) && radius >= 0.0 && radius <= 100000.0;
}

inline bool physisRadiusHandleScreen(const render::RTSCamera& camera,
                                      const Viewport& vp, render::float3 center,
                                      double radius, render::float2& projected) {
    if (!camera.worldToScreen({center.x + float(std::max(1.0, radius)),
                                center.y, center.z}, vp.pointsW, vp.pointsH, projected))
        return false;
    return std::isfinite(projected.x) && std::isfinite(projected.y);
}

inline void drawPhysisFieldMarkers(Editor& ed, const Viewport& vp,
                                   const render::RTSCamera& camera) {
    auto& state = physisFieldEditorState();
    if (!state.showMarkers || !physis_authoring::read(ed.doc()).isObject()) return;
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect({vp.originX,vp.originY},
                       {vp.originX+vp.pointsW,vp.originY+vp.pointsH},true);
    const Json config = physis_authoring::read(ed.doc());
    for (const char* key : {"windZones","storms","gravityFields"}) {
        const auto& entries = config[key];
        if (!entries.isArray()) continue;
        for (const auto& entry : entries.items()) {
            render::float3 position;
            if (!physisFieldPosition(entry,position)) continue;
            render::float2 projected;
            if (!camera.worldToScreen(position,vp.pointsW,vp.pointsH,projected))
                continue;
            const ImVec2 p(vp.originX+projected.x,vp.originY+projected.y);
            if (p.x < vp.originX || p.x > vp.originX+vp.pointsW ||
                p.y < vp.originY || p.y > vp.originY+vp.pointsH) continue;
            const bool selected = physisFieldSelected(entry.getStr("id"));
            const bool active = entry.getStr("id")==state.selectedId;
            const bool wind = std::string(key)=="windZones";
            const bool storm = std::string(key)=="storms";
            const ImU32 color = wind ? IM_COL32(90,219,243,245) :
                                storm ? IM_COL32(255,192,93,245) :
                                        IM_COL32(186,142,255,245);
            draw->AddCircleFilled(p, selected?9.f:6.f,
                                  IM_COL32(16,22,35,225), 18);
            draw->AddCircle(p, selected?9.f:6.f,color,18,selected?2.7f:1.8f);
            if (selected) {
                draw->AddLine({p.x-14,p.y},{p.x+14,p.y},color,1);
                draw->AddLine({p.x,p.y-14},{p.x,p.y+14},color,1);
                double radius = 0;
                // Limit ring geometry when many fields are selected.
                if ((state.selectedIds.size() <= 16 || active) &&
                    physisFieldRadius(entry, radius)) {
                    // World-space X/Z circle: unlike a screen-space ellipse, this
                    // projects correctly through the existing Scene camera.
                    ImVec2 previous{};
                    bool previousValid = false;
                    for (int pointIndex = 0; pointIndex <= 64; ++pointIndex) {
                        const double angle = double(pointIndex) * 6.283185307179586 / 64.0;
                        render::float2 screen{};
                        const bool valid = camera.worldToScreen({
                            position.x + float(radius * std::cos(angle)), position.y,
                            position.z + float(radius * std::sin(angle))},
                            vp.pointsW, vp.pointsH, screen) &&
                            std::isfinite(screen.x) && std::isfinite(screen.y);
                        if (valid && previousValid)
                            draw->AddLine(previous, {vp.originX+screen.x, vp.originY+screen.y},
                                          color, 1.5f);
                        if (valid) previous = {vp.originX+screen.x, vp.originY+screen.y};
                        previousValid = valid;
                    }
                    render::float2 handle{};
                    if (active && state.gizmoMode == 1 &&
                        physisRadiusHandleScreen(camera, vp, position, radius, handle)) {
                        const ImVec2 hp{vp.originX+handle.x, vp.originY+handle.y};
                        draw->AddLine(p, hp, color, 1.2f);
                        draw->AddCircleFilled(hp, 6.5f, color, 16);
                        draw->AddCircle(hp, 6.5f, IM_COL32(18,23,35,245), 16, 1.6f);
                    }
                }
            }
            const char* symbol = wind ? "W" : storm ? "S" : "G";
            draw->AddText({p.x+10,p.y-8},color,symbol);
        }
    }
    if (state.show3DAxes && state.editMode && state.gizmoMode==0 &&
        !state.selectedIds.empty()) {
        physis_authoring::FieldPivot p{};
        if (physisCurrentPivot(ed,p)) {
            const render::float3 pivot=physisPivot3(p);
            const ImU32 colors[]={IM_COL32(248,93,100,245),
                                  IM_COL32(90,220,115,245),
                                  IM_COL32(96,149,249,245)};
            for (int axis=0;axis<3;++axis) {
                ImVec2 a{},b{};
                if (!physisAxisScreen(camera,vp,pivot,axis,a,b)) continue;
                draw->AddLine(a,b,colors[axis],3.0f);
                draw->AddCircleFilled(b,5.f,colors[axis],12);
                draw->AddText({b.x+7.f,b.y-7.f},colors[axis],
                              axis==0?"X":axis==1?"Y":"Z");
            }
        }
    }
    if ((state.gizmoMode==2 || state.gizmoMode==3) &&
        state.editMode && !state.selectedIds.empty()) {
        physis_authoring::FieldPivot p{};
        if (physisCurrentPivot(ed,p)) {
            const auto pivot=physisPivot3(p);
            const float radius=std::clamp(camera.distance*0.4f,3.0f,180.0f);
            const ImU32 color=state.gizmoMode==2 ?
                IM_COL32(255,192,88,240):IM_COL32(95,225,205,240);
            ImVec2 previous{};
            bool previousValid=false;
            for (int i=0;i<=64;++i) {
                const double angle=double(i)*6.283185307179586/64.0;
                render::float2 projected{};
                const bool valid=camera.worldToScreen({
                       pivot.x+radius*float(std::cos(angle)),pivot.y,
                       pivot.z+radius*float(std::sin(angle))},
                       vp.pointsW,vp.pointsH,projected) &&
                       std::isfinite(projected.x)&&std::isfinite(projected.y);
                if (valid && previousValid)
                    draw->AddLine(previous,
                        {vp.originX+projected.x,vp.originY+projected.y},color,2.8f);
                if (valid) previous={vp.originX+projected.x,vp.originY+projected.y};
                previousValid=valid;
            }
        }
    }
    if (state.boxSelecting) {
        // Rect drawing must be normalized for backwards marquee drags.
        const ImVec2 a{std::clamp(std::min(state.boxStart.x,state.boxEnd.x),
                                 vp.originX,vp.originX+vp.pointsW),
                       std::clamp(std::min(state.boxStart.y,state.boxEnd.y),
                                 vp.originY,vp.originY+vp.pointsH)};
        const ImVec2 b{std::clamp(std::max(state.boxStart.x,state.boxEnd.x),
                                 vp.originX,vp.originX+vp.pointsW),
                       std::clamp(std::max(state.boxStart.y,state.boxEnd.y),
                                 vp.originY,vp.originY+vp.pointsH)};
        draw->AddRectFilled(a,b,IM_COL32(100,180,255,40));
        draw->AddRect(a,b,IM_COL32(130,190,255,230),0,0,1.5f);
    }
    draw->PopClipRect();
}

// Return true only for mouse interactions owned by an opted-in PHYSIS marker.
// This keeps the legacy FPS and RTS selection/drag behavior intact otherwise.
// Optional Scene interaction. Ctrl/Shift-click toggles PHYSIS membership;
// an unmodified click on a selected group member moves the entire group.
inline bool processPhysisFieldGizmo(Editor& ed, const Viewport& vp,
                                    const render::RTSCamera& camera) {
    auto& state=physisFieldEditorState();
    const ImGuiIO& io=ImGui::GetIO();
    if (!state.editMode || !state.showMarkers || ed.playing()) {
        state.dragging=false;
        state.draggingRadius=false;
        state.dragAxis=-1;
        state.dragTransform=0;
        state.dragBaseline=Json{};
        state.boxSelecting=false;
        state.dragOriginal.clear();
        return false;
    }
    if (state.boxSelecting) {
        state.boxEnd=io.MousePos;
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) ||
            !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const float dx=std::abs(state.boxEnd.x-state.boxStart.x);
            const float dy=std::abs(state.boxEnd.y-state.boxStart.y);
            if (dx>=4.f || dy>=4.f) {
                const auto visible=physisVisibleMarkers(physis_authoring::read(ed.doc()),vp,camera);
                auto chosen=physis_gizmo::idsInScreenRect(visible,
                    state.boxStart.x,state.boxStart.y,state.boxEnd.x,state.boxEnd.y);
                if (state.boxAdditive)
                    chosen.insert(chosen.end(),state.selectedIds.begin(),state.selectedIds.end());
                setPhysisFieldSelection(std::move(chosen));
                if (!state.selectedIds.empty())
                    ed.select({Selection::MapProps,-1,{}});
            } else if (!state.boxAdditive) selectPhysisField("");
            state.boxSelecting=false;
        }
        return true;
    }
    if (state.dragging) {
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) ||
            !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            state.dragging=false;
            state.draggingRadius=false;
            state.dragEdited=false;
            state.dragAxis=-1;
            state.dragTransform=0;
            state.dragBaseline=Json{};
            state.dragOriginal.clear();
            return true;
        }
        if (state.dragTransform!=0) {
            if (ed.doc().revision!=state.dragKnownRevision) {
                state.dragging=false;
                state.dragTransform=0;
                state.dragBaseline=Json{};
                state.error="PHYSIS map was changed during the gizmo drag";
                return true;
            }
            render::float3 hit{};
            const auto& pivot=state.dragTransformPivot;
            if (camera.screenToGround(io.MousePos.x-vp.originX,
                        io.MousePos.y-vp.originY,vp.pointsW,vp.pointsH,
                        hit,float(pivot.y))) {
                const double dx=double(hit.x)-pivot.x;
                const double dz=double(hit.z)-pivot.z;
                const double radius=std::hypot(dx,dz);
                if (radius>0.0001) {
                    double yaw=0,scale=1;
                    if (state.dragTransform==2) {
                        const double angle=std::atan2(dz,dx);
                        const double delta=angle-state.dragRingInitialAngle;
                        yaw=std::atan2(std::sin(delta),std::cos(delta)) *
                            (180.0/3.14159265358979323846);
                    } else {
                        scale=radius/state.dragRingInitialRadius;
                        scale=std::clamp(scale,0.01,100.0);
                    }
                    const auto rev=ed.doc().revision;
                    if (physis_authoring::transformFields(
                            ed.doc(),state.selectedIds,pivot.x,pivot.y,pivot.z,
                            yaw,scale,&state.error,&state.dragBaseline,
                            !state.dragEdited)) {
                        if (ed.doc().revision!=rev) {
                            state.dragEdited=true;
                            state.dragKnownRevision=ed.doc().revision;
                        }
                        state.draftId.clear();
                        state.error.clear();
                    }
                }
            }
            state.lastMouseY=io.MousePos.y;
            return true;
        }
        std::string collection;
        std::size_t index=0;
        Json field;
        if (!physis_authoring::findField(physis_authoring::read(ed.doc()),
                    state.selectedId,collection,index,&field)) {
            state.dragging=false;
            state.dragOriginal.clear();
            return true;
        }
        render::float3 current{};
        if (!physisFieldPosition(field,current)) {
            state.dragging=false;
            state.dragOriginal.clear();
            return true;
        }
        render::float3 hit{};
        if (state.draggingRadius) {
            // Radius handle affects the active field only, not the whole group.
            if (camera.screenToGround(io.MousePos.x-vp.originX,
                    io.MousePos.y-vp.originY,vp.pointsW,vp.pointsH,hit,current.y)) {
                const double rawRadius=std::hypot(double(hit.x)-double(current.x),
                                                   double(hit.z)-double(current.z));
                double radius=state.snapEnabled ?
                    physis_authoring::snapMeters(rawRadius,double(state.snapStepMeters)) :
                    rawRadius;
                const double minimum=collection=="storms" ?
                    field["eyeRadiusMeters"].toDouble()+0.001 : 1.0;
                radius=std::clamp(radius,minimum,100000.0);
                double previous=0;
                if (physisFieldRadius(field,previous) &&
                    std::abs(radius-previous)>=0.0005) {
                    const auto revision=ed.doc().revision;
                    if (physis_authoring::resizeField(ed.doc(),state.selectedId,
                                  radius,!state.dragEdited,&state.error)) {
                        if (ed.doc().revision!=revision) state.dragEdited=true;
                        state.draftId.clear();
                        state.error.clear();
                    }
                }
            }
        } else if (!state.dragOriginal.empty()) {
            // Targets derive from the initial drag snapshot, never from
            // previously rounded values; relative offsets remain invariant.
            // Keep already modified axes when switching between X/Z and
            // Shift+Y during one continuous drag.
            double x=state.dragAppliedX;
            double y=state.dragAppliedY;
            double z=state.dragAppliedZ;
            if (state.dragAxis>=0) {
                render::float3 origin{},direction{};
                double parameter=0;
                const render::float3 pivot{float(state.dragAnchorX),
                                            float(state.dragAnchorY),
                                            float(state.dragAnchorZ)};
                if (camera.screenRay(io.MousePos.x-vp.originX,
                                     io.MousePos.y-vp.originY,vp.pointsW,vp.pointsH,
                                     origin,direction) &&
                    physis_gizmo::closestRayAxisParameter(origin,direction,pivot,
                                                         state.dragAxis,parameter)) {
                    const double delta=parameter-state.dragAxisStart;
                    const double start=state.dragAxis==0 ? state.dragAnchorX :
                        state.dragAxis==1 ? state.dragAnchorY:state.dragAnchorZ;
                    const double value=state.snapEnabled ?
                        physis_authoring::snapMeters(start+delta,double(state.snapStepMeters)) :
                        start+delta;
                    if (state.dragAxis==0) x=value;
                    else if (state.dragAxis==1) y=value;
                    else z=value;
                }
            } else if (io.KeyShift) {
                state.dragRawY += double(state.lastMouseY-io.MousePos.y) *
                                  std::max(0.01,double(camera.distance)*0.002);
                y=state.dragRawY;
                if (state.snapEnabled)
                    y=physis_authoring::snapMeters(y,double(state.snapStepMeters));
            } else if (camera.screenToGround(io.MousePos.x-vp.originX,
                       io.MousePos.y-vp.originY,vp.pointsW,vp.pointsH,
                       hit,float(state.dragAnchorY))) {
                x=double(hit.x)+state.grabOffsetX;
                z=double(hit.z)+state.grabOffsetZ;
                if (state.snapEnabled) {
                    x=physis_authoring::snapMeters(x,double(state.snapStepMeters));
                    z=physis_authoring::snapMeters(z,double(state.snapStepMeters));
                }
            }
            const double dx=x-state.dragAnchorX;
            const double dy=y-state.dragAnchorY;
            const double dz=z-state.dragAnchorZ;
            if (std::abs(dx)>0.0005 || std::abs(dy)>0.0005 ||
                std::abs(dz)>0.0005 || state.dragEdited) {
                std::vector<physis_authoring::FieldPosition> targets;
                targets.reserve(state.dragOriginal.size());
                for (const auto& p:state.dragOriginal)
                    targets.push_back({p.id,p.x+dx,p.y+dy,p.z+dz});
                const auto revision=ed.doc().revision;
                if (physis_authoring::moveFields(ed.doc(),targets,
                          !state.dragEdited,&state.error)) {
                    if (ed.doc().revision!=revision) {
                        state.dragEdited=true;
                        state.dragAppliedX=x;
                        state.dragAppliedY=y;
                        state.dragAppliedZ=z;
                    }
                    state.draftId.clear();
                    state.error.clear();
                }
            }
        }
        state.lastMouseY=io.MousePos.y;
        return true;
    }
    if (!vp.hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
        io.KeySuper || io.KeyAlt || io.WantTextInput ||
        ImGui::IsMouseDown(ImGuiMouseButton_Right))
        return false;
    const bool additive=io.KeyCtrl || io.KeyShift;
    const Json config=physis_authoring::read(ed.doc());

    // World-space ring selection: rotate around Y or uniformly scale source
    // geometry, deriving every frame from a fixed baseline map snapshot.
    if (!additive && (state.gizmoMode==2 || state.gizmoMode==3) &&
        !state.selectedIds.empty() &&
        (state.gizmoMode==3 || state.selectedIds.size()>1)) {
        physis_authoring::FieldPivot p{};
        if (physisCurrentPivot(ed,p)) {
            const render::float3 at=physisPivot3(p);
            const float radius=std::clamp(camera.distance*0.4f,3.0f,180.0f);
            double best=10.0*10.0;
            ImVec2 prior{};
            bool priorValid=false;
            for (int i=0;i<=64;++i) {
                const double angle=double(i)*6.283185307179586/64.0;
                render::float2 projection{};
                const bool valid=camera.worldToScreen({
                    at.x+radius*float(std::cos(angle)),at.y,
                    at.z+radius*float(std::sin(angle))},vp.pointsW,vp.pointsH,
                    projection)&&std::isfinite(projection.x)&&std::isfinite(projection.y);
                if (valid) {
                    const ImVec2 here{vp.originX+projection.x,vp.originY+projection.y};
                    if (priorValid)
                        best=std::min(best,physis_gizmo::segmentDistanceSq(
                             io.MousePos.x,io.MousePos.y,
                             prior.x,prior.y,here.x,here.y));
                    prior=here;
                }
                priorValid=valid;
            }
            if (best<10.0*10.0) {
                render::float3 hit{};
                if (camera.screenToGround(io.MousePos.x-vp.originX,
                         io.MousePos.y-vp.originY,vp.pointsW,vp.pointsH,hit,at.y)) {
                    const double dx=double(hit.x)-p.x,dz=double(hit.z)-p.z;
                    const double radial=std::hypot(dx,dz);
                    if (radial>0.0001) {
                        state.dragTransform=state.gizmoMode;
                        state.dragBaseline=config;
                        state.dragTransformPivot=p;
                        state.dragRingInitialAngle=std::atan2(dz,dx);
                        state.dragRingInitialRadius=radial;
                        state.dragKnownRevision=ed.doc().revision;
                        state.dragging=true;
                        state.draggingRadius=false;
                        state.dragAxis=-1;
                        state.dragEdited=false;
                        state.dragOriginal.clear();
                        state.lastMouseY=io.MousePos.y;
                        ed.select({Selection::MapProps,-1,{}});
                        return true;
                    }
                }
            }
        }
    }

    if (!additive && state.show3DAxes && state.gizmoMode==0 &&
        !state.selectedIds.empty()) {
        physis_authoring::FieldPivot p{};
        if (physisCurrentPivot(ed,p)) {
            int bestAxis=-1;
            double nearest=9.0*9.0;
            const render::float3 at=physisPivot3(p);
            for (int axis=0;axis<3;++axis) {
                ImVec2 a{},b{};
                if (!physisAxisScreen(camera,vp,at,axis,a,b)) continue;
                const double x=a.x+(b.x-a.x)*0.22;
                const double y=a.y+(b.y-a.y)*0.22;
                const double ds=physis_gizmo::segmentDistanceSq(
                    io.MousePos.x,io.MousePos.y,x,y,b.x,b.y);
                if (ds<nearest) {nearest=ds;bestAxis=axis;}
            }
            if (bestAxis>=0) {
                render::float3 origin{},direction{};
                double parameter=0;
                if (camera.screenRay(io.MousePos.x-vp.originX,
                                     io.MousePos.y-vp.originY,vp.pointsW,vp.pointsH,
                                     origin,direction) &&
                    physis_gizmo::closestRayAxisParameter(origin,direction,at,
                                                          bestAxis,parameter) &&
                    physisCaptureGroupOrigins(ed)) {
                    ed.select({Selection::MapProps,-1,{}});
                    state.dragging=true;
                    state.draggingRadius=false;
                    state.dragAxis=bestAxis;
                    state.dragAxisStart=parameter;
                    state.dragEdited=false;
                    state.dragAnchorX=p.x;state.dragAnchorY=p.y;state.dragAnchorZ=p.z;
                    state.dragAppliedX=p.x;state.dragAppliedY=p.y;state.dragAppliedZ=p.z;
                    state.dragRawY=p.y;
                    state.lastMouseY=io.MousePos.y;
                    return true;
                }
            }
        }
    }

    // Resize handle has priority, except when toggling selection.
    if (!additive && state.gizmoMode==1 && !state.selectedId.empty()) {
        std::string collection;
        std::size_t index=0;
        Json selected;
        render::float3 center{};
        double radius=0;
        render::float2 screen{};
        if (physis_authoring::findField(config,state.selectedId,collection,index,&selected) &&
            physisFieldPosition(selected,center) && physisFieldRadius(selected,radius) &&
            physisRadiusHandleScreen(camera,vp,center,radius,screen)) {
            const double dx=double(screen.x+vp.originX-io.MousePos.x);
            const double dy=double(screen.y+vp.originY-io.MousePos.y);
            if (dx*dx+dy*dy<14.0*14.0) {
                ed.select({Selection::MapProps,-1,{}});
                state.dragging=true;
                state.draggingRadius=true;
                state.dragEdited=false;
                state.dragOriginal.clear();
                state.error.clear();
                return true;
            }
        }
    }

    std::string picked;
    float nearest=15.f*15.f;
    render::float3 chosen{};
    for (const char* key:{"windZones","storms","gravityFields"}) {
        const auto& fields=config[key];
        if (!fields.isArray()) continue;
        for (const auto& entry:fields.items()) {
            render::float3 position{};
            render::float2 screen{};
            if (!physisFieldPosition(entry,position) ||
                !camera.worldToScreen(position,vp.pointsW,vp.pointsH,screen))
                continue;
            const float dx=screen.x+vp.originX-io.MousePos.x;
            const float dy=screen.y+vp.originY-io.MousePos.y;
            const float d=dx*dx+dy*dy;
            if (d<nearest) {nearest=d;chosen=position;picked=entry.getStr("id");}
        }
    }
    if (picked.empty()) {
        if (state.boxSelectEnabled) {
            state.boxSelecting=true;
            state.boxAdditive=additive;
            state.boxStart=state.boxEnd=io.MousePos;
            return true;
        }
        return false;
    }
    if (additive) {
        selectPhysisField(picked,true);
        ed.select({Selection::MapProps,-1,{}});
        return true;
    }
    if (!physisFieldSelected(picked))
        selectPhysisField(picked);
    else if (state.selectedId!=picked) {
        // Make this member the pivot without clearing other selected fields.
        state.selectedId=picked;
        state.draftId.clear();
        state.error.clear();
    }
    ed.select({Selection::MapProps,-1,{}});
    if (state.gizmoMode==1) return true;

    render::float3 ground{};
    if (camera.screenToGround(io.MousePos.x-vp.originX,
                io.MousePos.y-vp.originY,vp.pointsW,vp.pointsH,
                ground,chosen.y)) {
        state.grabOffsetX=double(chosen.x-ground.x);
        state.grabOffsetZ=double(chosen.z-ground.z);
    } else state.grabOffsetX=state.grabOffsetZ=0;

    if (!physisCaptureGroupOrigins(ed)) return true;
    state.dragAxis=-1;
    state.dragAnchorX=chosen.x;
    state.dragAnchorY=chosen.y;
    state.dragAnchorZ=chosen.z;
    state.dragAppliedX=chosen.x;
    state.dragAppliedY=chosen.y;
    state.dragAppliedZ=chosen.z;
    state.dragRawY=chosen.y;
    state.dragging=true;
    state.draggingRadius=false;
    state.dragEdited=false;
    state.lastMouseY=io.MousePos.y;
    return true;
}

inline void drawPhysisFieldProperties(Editor& ed) {
    using namespace physis_authoring;
    auto& state = physisFieldEditorState();
    std::string collection;
    std::size_t index = 0;
    Json selected;
    if (!findField(read(ed.doc()), state.selectedId, collection, index, &selected))
        return;
    ui::section(ICON_PENCIL,"PHYSIS — параметры поля");
    if (state.selectedIds.size()>1) {
        ImGui::TextDisabled("Выделено: %zu. Радиус изменяет лишь активное поле.",
                            state.selectedIds.size());
        ImGui::TextDisabled("Для редактирования свойств выберите одно поле без Ctrl/Shift.");
        return;
    }

    if (state.draftId!=state.selectedId) {
        state.draftId=state.selectedId;
        state.draftField=selected;
        state.rawDraft=selected.dump(2);
    }
    ImGui::Text("Выбрано: %s",state.selectedId.c_str());
    Json draft=state.draftField;
    render::float3 position;
    if (physisFieldPosition(draft,position)) {
        float coords[]={position.x,position.y,position.z};
        if (ImGui::InputFloat3("Позиция XYZ, м",coords))
            draft.set("center",point(coords[0],coords[1],coords[2]));
    }
    float radius=float(draft["radiusMeters"].toDouble(
                 draft["outerRadiusMeters"].toDouble(100)));
    const bool storm=collection=="storms";
    if (ImGui::DragFloat("Радиус, м",&radius,1.f,1.f,100000.f)) {
        if (storm) draft.set("outerRadiusMeters",Json::number(double(radius),3));
        else draft.set("radiusMeters",Json::number(double(radius),3));
    }
    if (collection=="windZones") {
        float velocity[3]={};
        const auto& v=draft["velocity"];
        if (v.isArray() && v.size()==3)
            for(int i=0;i<3;++i)velocity[i]=float(v[size_t(i)].toDouble());
        if (ImGui::DragFloat3("Ветер, м/с",velocity,0.2f,-200.f,200.f))
            draft.set("velocity",point(velocity[0],velocity[1],velocity[2]));
    }
    if (collection=="storms") {
        const char* kinds[]={"Tornado","Hurricane"};
        int kind=draft.getStr("kind")=="Hurricane" ? 1 : 0;
        if (ImGui::Combo("Тип шторма",&kind,kinds,2))
            draft.set("kind",Json::string(kinds[kind]));
        for (const char* key : {"eyeRadiusMeters","heightMeters","tangentialWind","updraft"}) {
            float value=float(draft[key].toDouble());
            if (ImGui::DragFloat(key,&value,0.2f,0.f,100000.f))
                draft.set(key,Json::number(double(value),3));
        }
    }
    if (collection=="gravityFields") {
        const char* modes[]={"Uniform","Attract","Repel"};
        int kind=draft.getStr("mode")=="Attract" ? 1 :
                 draft.getStr("mode")=="Repel" ? 2 : 0;
        if (ImGui::Combo("Тип гравитации",&kind,modes,3))
            draft.set("mode",Json::string(modes[kind]));
        float acceleration[3]={};
        const auto& v=draft["acceleration"];
        if (v.isArray() && v.size()==3)
            for(int i=0;i<3;++i)acceleration[i]=float(v[size_t(i)].toDouble());
        if (ImGui::DragFloat3("Ускорение м/с²",acceleration,0.1f,-200.f,200.f))
            draft.set("acceleration",point(acceleration[0],acceleration[1],acceleration[2]));
        float strength=float(draft["radialMagnitude"].toDouble(10));
        if (ImGui::DragFloat("Радиальная сила",&strength,0.1f,0.f,200.f))
            draft.set("radialMagnitude",Json::number(double(strength),3));
    }
    if (!(draft==state.draftField)) {
        state.draftField=draft;
        state.rawDraft=draft.dump(2);
    }

    if (ImGui::CollapsingHeader("Дополнительные параметры JSON")) {
        ui::inputMultiline("##physisFieldJson",state.rawDraft,ImVec2(-1,150));
    }
    if (ImGui::Button("Применить изменения поля")) {
        try {
            const Json value=Json::parse(state.rawDraft);
            if (replaceField(ed.doc(),state.selectedId,value,&state.error)) {
                state.draftId.clear();
                state.error.clear();
            }
        } catch(const std::exception& error) {
            state.error=error.what();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Удалить поле")) {
        if (removeField(ed.doc(),state.selectedId,&state.error))
            selectPhysisField("");
    }
}

inline void drawPhysisFieldsPanel(Editor& ed) {
    using namespace physis_authoring;
    auto& state = physisFieldEditorState();
    const Json config = read(ed.doc());
    ui::section(ICON_GLOBE,"PHYSIS — объекты и Gizmo");
    if (!config.isObject()) {
        ui::hint("Создайте PHYSIS в свойствах карты, затем добавляйте поля.");
        if (ImGui::Button("Открыть свойства карты##physisProps")) {
            ed.select({Selection::MapProps,-1,{}});
            ed.focusWindow("inspector");
        }
        return;
    }
    ImGui::Checkbox("Показать маркеры в Сцене", &state.showMarkers);
    ImGui::Checkbox("Редактировать маркеры мышью", &state.editMode);
    if (state.editMode) {
        ImGui::Checkbox("Выделение рамкой##physis", &state.boxSelectEnabled);
        ImGui::Checkbox("Оси X/Y/Z##physis", &state.show3DAxes);
        ImGui::TextDisabled("Рамка с пустого места; Shift/Ctrl добавляет.");
        ImGui::Combo("Gizmo##physis", &state.gizmoMode,
                     "Перемещение\0Радиус\0Поворот Y\0Масштаб\0");
        ImGui::Checkbox("Привязка к сетке##physis", &state.snapEnabled);
        if (state.snapEnabled) {
            ImGui::DragFloat("Шаг привязки, м##physis", &state.snapStepMeters,
                             0.25f, 0.25f, 1000.f, "%.2f");
            state.snapStepMeters = std::clamp(state.snapStepMeters, 0.25f, 1000.f);
        }
        const char* hints[]={
            "Группа: тяните X/Y/Z или центр (Shift — высота).",
            "Радиус меняет только активное поле.",
            "Тяните кольцо поворота Y; требуется несколько полей.",
            "Тяните кольцо равномерного масштаба геометрии."
        };
        ImGui::TextDisabled("%s",hints[std::clamp(state.gizmoMode,0,3)]);
        ImGui::TextDisabled("Ctrl/Shift + клик — добавить или убрать поле из группы.");
        const char* pivots[]={"Активное поле","Средняя точка","Центр границ","Начало мира","Заданный"};
        ImGui::Combo("Pivot##physis",&state.pivotMode,pivots,5);
        if (state.pivotMode==4) ImGui::InputFloat3("Pivot XYZ, м##physis",state.customPivot);
        if (!state.selectedIds.empty()) {
            physis_authoring::FieldPivot p{};
            if (physisCurrentPivot(ed,p))
                ImGui::TextDisabled("Pivot: %.2f, %.2f, %.2f м",p.x,p.y,p.z);
        }
    }

    const double x = std::clamp(double(ed.sceneCam.target.x),-1000000.0,1000000.0);
    const double y = std::clamp(double(ed.sceneCam.target.y),-1000000.0,1000000.0);
    const double z = std::clamp(double(ed.sceneCam.target.z),-1000000.0,1000000.0);
    const auto add = [&](aiverse::physis::AuthoredFieldKind kind) {
        std::string id;
        if (addField(ed.doc(),kind,x,y,z,id,&state.error)) {
            selectPhysisField(id);
            state.editMode = true;
            state.showMarkers = true;
        }
    };
    if (ImGui::SmallButton("+ Ветер")) add(aiverse::physis::AuthoredFieldKind::Wind);
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Смерч/ураган")) add(aiverse::physis::AuthoredFieldKind::Storm);
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Гравитация")) add(aiverse::physis::AuthoredFieldKind::Gravity);

    ImGui::TextDisabled("Выделено PHYSIS: %zu",state.selectedIds.size());
    if (ImGui::SmallButton("Выделить всё##physis")) {
        std::vector<std::string> all;
        for (const char* key:{"windZones","storms","gravityFields"}) {
            const auto& fields=read(ed.doc())[key];
            if (!fields.isArray()) continue;
            for (const auto& entry:fields.items()) all.push_back(entry.getStr("id"));
        }
        setPhysisFieldSelection(std::move(all));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Снять выделение##physis"))
        selectPhysisField("");

    ImGui::BeginDisabled(state.selectedIds.empty());
    if (ImGui::Button("Дублировать выбранные##physis")) {
        std::vector<std::string> copies;
        const double offset=state.snapEnabled ? double(state.snapStepMeters) : 5.0;
        if (duplicateFields(ed.doc(),state.selectedIds,offset,0,offset,
                            copies,&state.error)) {
            setPhysisFieldSelection(std::move(copies));
            state.error.clear();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Удалить выбранные##physis")) {
        if (removeFields(ed.doc(),state.selectedIds,&state.error))
            setPhysisFieldSelection({});
    }
    ImGui::EndDisabled();

    if (!state.selectedIds.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("Поворот и размер зон вокруг активного маркера");
        ImGui::DragFloat("Поворот вокруг Y, град##physisGroup",
                         &state.groupYawDegrees,1.f,-360.f,360.f,"%.1f");
        ImGui::DragFloat("Масштаб геометрии##physisGroup",
                         &state.groupUniformScale,0.01f,0.01f,20.f,"%.2f");
        if (ImGui::Button("Применить к выбранным##physisGroup")) {
            physis_authoring::FieldPivot pivot{};
            if (computeFieldPivot(read(ed.doc()),state.selectedIds,state.selectedId,
                static_cast<FieldPivotMode>(state.pivotMode),
                {double(state.customPivot[0]),double(state.customPivot[1]),
                 double(state.customPivot[2])},pivot,&state.error)) {
                if (transformFields(ed.doc(),state.selectedIds,
                     pivot.x,pivot.y,pivot.z,
                     double(state.groupYawDegrees),double(state.groupUniformScale),
                     &state.error)) {
                    state.draftId.clear();
                    state.error.clear();
                }
            }
        }
        ImGui::TextDisabled("Меняются позиции и размеры полей, но не скорости и силы.");
    }
    if (!state.error.empty()) ui::toneText(ui::Tone::Error,"%s",state.error.c_str());

    if (ImGui::BeginChild("##physisFields",ImVec2(0,140),ImGuiChildFlags_Borders)) {
        for (const char* key : {"windZones","storms","gravityFields"}) {
            const auto& fields=read(ed.doc())[key];
            if (!fields.isArray()) continue;
            for (const auto& entry : fields.items()) {
                const std::string id=entry.getStr("id");
                if (ImGui::Selectable((id+"##physisField").c_str(),
                                      physisFieldSelected(id))) {
                    const auto& io=ImGui::GetIO();
                    selectPhysisField(id,io.KeyCtrl || io.KeyShift);
                    ed.select({Selection::MapProps,-1,{}});
                }
            }
        }
    }
    ImGui::EndChild();
    // Prune stale IDs after Undo/Redo, deletion or map switch.
    std::vector<std::string> surviving;
    for (const auto& id:state.selectedIds) {
        std::string collection;
        std::size_t index=0;
        if (findField(read(ed.doc()),id,collection,index)) surviving.push_back(id);
    }
    if (surviving != state.selectedIds) {
        const std::string active=state.selectedId;
        setPhysisFieldSelection(std::move(surviving));
        if (physisFieldSelected(active)) state.selectedId=active;
    }
    drawPhysisFieldProperties(ed);

}

} // namespace aiverse::editor
