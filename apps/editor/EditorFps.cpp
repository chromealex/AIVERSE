#include "Editor.h"
#include "EditorGenreUi.h"
#include "SceneRenderable.h"
#include "EditorEntityAdapter.h"
#include "EditorSceneNodes.h"
#include "EditorSelectionAdapter.h"
#include "EditorRuntimeEntityProvider.h"
#include "EditorComponentPanel.h"
#include "modules/fps/FpsModule.h"
#include <filesystem>
#include <algorithm>
#include <cmath>
namespace aiverse::editor {
namespace {
std::string quote(const std::string& s) {
#ifdef _WIN32
    // cmd.exe expands these even within quotes; refuse paths/addresses containing them.
    if(s.find_first_of("\"%\r\n!")!=std::string::npos)throw std::runtime_error("unsupported shell characters in FPS launch path");
    return "\""+s+"\"";
#else
    std::string result="'";for(char c:s)result+=c=='\''?"'\\''":std::string(1,c);return result+"'";
#endif
}
render::float3 point(const Json& j){return {float(j[0].toDouble()),float(j[1].toDouble()),float(j[2].toDouble())};}
}
// FPS exposes real ECS runtime components; an opt-in mode stores full authoritative payloads.
// Expose a typed, read-only snapshot without pretending they are DocEntity or
// RTS World rows. This uses the same fps::Module::setup as the FPS client.
const Json& Editor::fpsRuntimeSnapshot() {
    const bool current = fpsRuntimeRevision_ == doc().revision &&
        fpsRuntimeRulesRevision_ == rulesRevision_ &&
        fpsRuntimeProjectDir_ == ws_.project.dir + "\n" + doc().path;
    if (current) return fpsRuntimeCache_;
    // Do not repeatedly construct the entire simulation while the user drags
    // a Transform or paints. Refresh on release, as the RTS preview does.
    if (fpsRuntimeRevision_ != UINT64_MAX && ImGui::GetCurrentContext() &&
        (ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
         ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
         ImGui::IsMouseDown(ImGuiMouseButton_Middle)) &&
        fpsRuntimeProjectDir_ == ws_.project.dir + "\n" + doc().path)
        return fpsRuntimeCache_;
    fpsRuntimeRevision_ = doc().revision;
    fpsRuntimeRulesRevision_ = rulesRevision_;
    fpsRuntimeProjectDir_ = ws_.project.dir + "\n" + doc().path;
    fpsRuntimeCache_ = Json::object();
    Json actors = Json::array();
    if (!ws_.project.isFps()) {
        fpsRuntimeCache_.set("actors", actors);
        return fpsRuntimeCache_;
    }
    try {
        const fps::Config config = fps::readConfig(ws_.project.rulesJson);
        Simulation preview({1, config.tickRate, 0}, std::make_unique<fps::Module>());
        preview.setup(ws_.project.rulesJson, doc().toJson());
        auto* module = dynamic_cast<fps::Module*>(&preview.module());
        if (!module) throw std::runtime_error("FPS module missing");
        // Runtime actors now have real ECS identities and typed component
        // pools, which the editor inspects directly. State::actors remains
        // the authoritative compatibility storage until gameplay migration.
        World& world = preview.world();
        for (size_t i = 0; i < preview.players().size(); ++i) {
            const Entity entity = module->actorEntity(preview, uint8_t(i + 1));
            if (!world.alive(entity)) continue;
            const auto* identity = world.tryGet<fps::ActorEntityIdentity>(entity);
            const auto* transform = world.tryGet<fps::ActorEntityTransform>(entity);
            const auto* vitals = world.tryGet<fps::ActorEntityVitals>(entity);
            if (!identity || !transform || !vitals) continue;
            Json data = Json::object();
            data.set("slot", Json::number(int64_t(identity->slot)));
            data.set("team", Json::number(int64_t(identity->team)));
            data.set("life", Json::number(int64_t(identity->life)));
            data.set("ecsEntity", Json::number(int64_t(entity.packed())));
            Json position = Json::array(), velocity = Json::array();
            for (const Fixed value : {transform->position.x, transform->position.y,
                                      transform->position.z})
                position.push(Json::number(value.toDouble(), 3));
            for (const Fixed value : {transform->velocity.x, transform->velocity.y,
                                      transform->velocity.z})
                velocity.push(Json::number(value.toDouble(), 3));
            data.set("position", position);
            data.set("velocity", velocity);
            data.set("yaw", Json::number(transform->yaw.toDouble(), 3));
            data.set("pitch", Json::number(transform->pitch.toDouble(), 3));
            data.set("health", Json::number(int64_t(vitals->health)));
            data.set("armor", Json::number(int64_t(vitals->armor)));
            data.set("ammo", Json::number(int64_t(vitals->ammo)));
            data.set("reserve", Json::number(int64_t(vitals->reserve)));
            Json components = Json::object();
            Json identityFields = Json::object();
            identityFields.set("slot", Json::number(int64_t(identity->slot)));
            identityFields.set("team", Json::number(int64_t(identity->team)));
            identityFields.set("life", Json::number(int64_t(identity->life)));
            components.set("ActorEntityIdentity", identityFields);
            Json transformFields = Json::object();
            transformFields.set("position", position);
            transformFields.set("velocity", velocity);
            transformFields.set("yaw", Json::number(transform->yaw.toDouble(), 3));
            transformFields.set("pitch", Json::number(transform->pitch.toDouble(), 3));
            components.set("ActorEntityTransform", transformFields);
            Json vitalsFields = Json::object();
            vitalsFields.set("health", Json::number(int64_t(vitals->health)));
            vitalsFields.set("armor", Json::number(int64_t(vitals->armor)));
            vitalsFields.set("ammo", Json::number(int64_t(vitals->ammo)));
            vitalsFields.set("reserve", Json::number(int64_t(vitals->reserve)));
            vitalsFields.set("downed", Json::fromBool(vitals->downed));
            vitalsFields.set("crouched", Json::fromBool(vitals->crouched));
            vitalsFields.set("prone", Json::fromBool(vitals->prone));
            components.set("ActorEntityVitals", vitalsFields);
            if(const auto* full=world.tryGetConst<fps::ActorRuntime>(entity)) {
                const auto& a=full->value;
                Json runtime=Json::object();
                runtime.set("slot",Json::number(int64_t(a.slot)));
                runtime.set("team",Json::number(int64_t(a.team)));
                runtime.set("life",Json::number(int64_t(a.life)));
                runtime.set("health",Json::number(int64_t(a.health)));
                runtime.set("kills",Json::number(int64_t(a.kills)));
                runtime.set("shots",Json::number(int64_t(a.shots)));
                runtime.set("hits",Json::number(int64_t(a.hits)));
                runtime.set("weaponIndex",Json::number(int64_t(a.weaponIndex)));
                runtime.set("reloadUntil",Json::number(int64_t(a.reloadUntil)));
                runtime.set("nextFire",Json::number(int64_t(a.nextFire)));
                runtime.set("lastSequence",Json::number(int64_t(a.lastSequence)));
                runtime.set("grounded",Json::fromBool(a.grounded));
                runtime.set("vehicle",Json::number(int64_t(a.vehicle)));
                components.set("ActorRuntime",std::move(runtime));
            }
            data.set("components", components);
            actors.push(std::move(data));
        }
        Json objects = Json::array();
        if (const auto* dynamic = world.resource<fps::DynamicEntityIndex>()) {
            for (const auto& [key, entity] : dynamic->byKey) {
                if (!world.alive(entity)) continue;
                const uint8_t kind = uint8_t(key >> 32);
                Json data = Json::object();
                data.set("key", Json::number(int64_t(key)));
                data.set("ecsEntity", Json::number(int64_t(entity.packed())));
                data.set("kind", Json::number(int64_t(kind)));
                Json components = Json::object();
                Vec3 position{};
                if (kind == 1) {
                    const auto* projectile = world.tryGet<fps::ProjectileEntity>(entity);
                    if (!projectile) continue;
                    position = projectile->position;
                    Json fields = Json::object();
                    fields.set("id", Json::number(int64_t(projectile->id)));
                    fields.set("owner", Json::number(int64_t(projectile->owner)));
                    fields.set("kind", Json::number(int64_t(projectile->kind)));
                    fields.set("damage", Json::number(int64_t(projectile->damage)));
                    components.set("ProjectileEntity", fields);
                    if(const auto* full=world.tryGetConst<fps::ProjectileRuntime>(entity)) {
                        Json runtime=Json::object();
                        runtime.set("id",Json::number(int64_t(full->value.id)));
                        runtime.set("order",Json::number(int64_t(full->order)));
                        runtime.set("expires",Json::number(int64_t(full->value.expires)));
                        runtime.set("owner",Json::number(int64_t(full->value.owner)));
                        runtime.set("damage",Json::number(int64_t(full->value.damage)));
                        runtime.set("programEnabled",Json::fromBool(full->value.program.enabled));
                        components.set("ProjectileRuntime",std::move(runtime));
                    }
                    data.set("title", Json::string("Projectile #" +
                        std::to_string(projectile->id)));
                } else if (kind == 2) {
                    const auto* loot = world.tryGet<fps::LootEntity>(entity);
                    if (!loot) continue;
                    position = loot->position;
                    Json fields = Json::object();
                    fields.set("id", Json::number(int64_t(loot->id)));
                    fields.set("type", Json::number(int64_t(loot->type)));
                    fields.set("count", Json::number(int64_t(loot->count)));
                    components.set("LootEntity", fields);
                    if(const auto* full=world.tryGetConst<fps::LootRuntime>(entity)) {
                        Json runtime=Json::object();
                        runtime.set("id",Json::number(int64_t(full->value.id)));
                        runtime.set("order",Json::number(int64_t(full->order)));
                        runtime.set("count",Json::number(int64_t(full->value.count)));
                        runtime.set("ammo",Json::number(int64_t(full->value.ammo)));
                        runtime.set("reserve",Json::number(int64_t(full->value.reserve)));
                        components.set("LootRuntime",std::move(runtime));
                    }
                    data.set("title", Json::string("Loot #" +
                        std::to_string(loot->id)));
                } else if (kind == 3) {
                    const auto* vehicle = world.tryGet<fps::VehicleEntity>(entity);
                    if (!vehicle) continue;
                    position = vehicle->position;
                    Json fields = Json::object();
                    fields.set("id", Json::number(int64_t(vehicle->id)));
                    fields.set("health", Json::number(int64_t(vehicle->health)));
                    fields.set("driver", Json::number(int64_t(vehicle->driver)));
                    fields.set("yaw", Json::number(vehicle->yaw.toDouble(), 3));
                    components.set("VehicleEntity", fields);
                    if(const auto* full=world.tryGetConst<fps::VehicleRuntime>(entity)) {
                        Json runtime=Json::object();
                        runtime.set("id",Json::number(int64_t(full->value.id)));
                        runtime.set("order",Json::number(int64_t(full->order)));
                        runtime.set("health",Json::number(int64_t(full->value.health)));
                        runtime.set("driver",Json::number(int64_t(full->value.driver)));
                        runtime.set("speed",Json::number(full->value.speed.toDouble(),3));
                        components.set("VehicleRuntime",std::move(runtime));
                    }
                    data.set("title", Json::string("Vehicle #" +
                        std::to_string(vehicle->id)));
                } else continue;
                Json pos = Json::array();
                for (const Fixed coordinate : {position.x, position.y, position.z})
                    pos.push(Json::number(coordinate.toDouble(), 3));
                data.set("position", pos);
                for (const auto& [name, entry] : components.members())
                    components.member(name).set("position", pos);
                data.set("components", components);
                objects.push(std::move(data));
            }
        }
        fpsRuntimeCache_.set("objects", objects);
    } catch (const std::exception& e) {
        fpsRuntimeCache_.set("error", Json::string(e.what()));
    }
    fpsRuntimeCache_.set("actors", actors);
    return fpsRuntimeCache_;
}

void Editor::startFpsPlay(bool listen,const std::string& address) {
    if(!ws_.project.isFps()||ws_.building())return;
    try {
        auto cfg=fps::readConfig(ws_.project.rulesJson);Simulation check({1,cfg.tickRate,0},std::make_unique<fps::Module>());check.setup(ws_.project.rulesJson,doc().toJson());
        std::string error;
        if(doc().dirty&&!saveMap(doc().path))return;
        if(!ws_.project.saveRules(&error)||!ws_.project.saveManifest(&error))throw std::runtime_error(error);
        namespace fs=std::filesystem;
        std::string name="aiverse_fps_client";
#ifdef _WIN32
        name+=".exe";
#endif
        std::vector<fs::path> roots{fs::path(opt_.editorExe).parent_path(),fs::path(ws_.buildTool).parent_path(),fs::path(ws_.engineDir)/"bin"};
        auto parent=fs::path(opt_.editorExe).parent_path();for(int i=0;i<4;++i){roots.push_back(parent);parent=parent.parent_path();}
        fs::path exe;std::error_code ec;
        for(const auto& root:roots)for(const auto& rel:{fs::path(name),fs::path("modules/fps")/name,fs::path("modules/fps/Release")/name})if(exe.empty()&&fs::is_regular_file(root/rel,ec))exe=root/rel;
        if(exe.empty()){status=genreText(ws_,"fps.editor.missing");ws_.log("fps",status,true);return;}
        std::string cmd="cd "+quote(ws_.engineDir)+" && "+quote(exe.string())+" --project "+quote(ws_.project.dir);
#ifdef _WIN32
        cmd="cd /d "+quote(ws_.engineDir)+" && "+quote(exe.string())+" --project "+quote(ws_.project.dir);
#endif
        if(listen)cmd+=" --listen";else if(!address.empty())cmd+=" --connect "+quote(address);
        ws_.startCommand(cmd,cmd,"fps-play");status=genreText(ws_,"fps.editor.running");
    }catch(const std::exception& e){status=genreText(ws_,"fps.editor.invalid");ws_.log("fps",e.what(),true);}
}
// FPS contributes scene geometry to the shared editor renderer. The scene
// lifecycle, post-processing, depth buffer and UI overlays are owned by
// Editor::renderScene, not by a genre-specific render pass.
void Editor::enqueueFpsSceneGeometry(const render::RTSCamera& camera, bool editOverlays) {
    using namespace render;
    if (!fpsPreviewMesh_.vb.valid()) {
        MeshBuilder mesh;
        mesh.color(rgba(255,255,255)).box({0,0.5f,0},{1,1,1});
        fpsPreviewMesh_ = mesh.upload(*dev_);
    }
    const uint64_t selectedKey = selection.kind == Selection::FpsObject ? selection.ref : 0;
    fpsSceneObjects_.clear();
    auto& instances = fpsSceneObjects_;
    const auto& fps = doc().data.fps;
    for (const auto& node : enumerateDocumentSceneNodes(doc(), true)) {
        if (!node.hasPosition) continue;
        const Json* source = fpsDocumentEntity(fps, node.ref);
        if (!source) continue;
        SceneRenderable object;
        object.entity = node.ref.fpsKey();
        object.mesh = &fpsPreviewMesh_;
        object.pickable = true;
        if (node.ref.source == DocumentEntityRef::Source::FpsCollider) {
            if ((*source)["yaw"].isNumber()) {
                const Json& half = (*source)["half"];
                if (!half.isArray() || half.size() != 3 ||
                    !half[0].isNumber() || !half[1].isNumber() ||
                    !half[2].isNumber()) continue;
                const float3 centre{float(node.position.x),
                                    float(node.position.y),
                                    float(node.position.z)};
                const float3 extents = point(half);
                if (!std::isfinite(extents.x) || !std::isfinite(extents.y) ||
                    !std::isfinite(extents.z) || extents.x <= 0 ||
                    extents.y <= 0 || extents.z <= 0) continue;
                const float angle = float((*source)["yaw"].toDouble() *
                                          6.283185307179586);
                const float cs = std::abs(std::cos(angle));
                const float sn = std::abs(std::sin(angle));
                const float3 bounds{
                    cs * extents.x + sn * extents.z,
                    extents.y,
                    sn * extents.x + cs * extents.z
                };
                object.world = mat4::translate({centre.x, centre.y - extents.y,
                                               centre.z}) *
                               mat4::rotateY(angle) *
                               mat4::scale(extents * 2.0f);
                object.tint = {0.35f, 0.42f, 0.44f, 1};
                object.boundsMin = centre - bounds;
                object.boundsMax = centre + bounds;
            } else {
                const Json& lower = (*source)["min"];
                const Json& upper = (*source)["max"];
                if (!lower.isArray() || !upper.isArray() ||
                    lower.size() < 3 || upper.size() < 3) continue;
                const float3 lo = point(lower), hi = point(upper);
                const float3 size = hi - lo;
                if (!std::isfinite(lo.x) || !std::isfinite(lo.y) ||
                    !std::isfinite(lo.z) || !std::isfinite(hi.x) ||
                    !std::isfinite(hi.y) || !std::isfinite(hi.z) ||
                    size.x <= 0 || size.y <= 0 || size.z <= 0) continue;
                const float3 base{float(node.position.x), lo.y,
                                  float(node.position.z)};
                if (length(base - camera.target) > 1000 && hi.y > 0) continue;
                object.world = mat4::translate(base) * mat4::scale(size);
                object.tint = {0.35f, 0.42f, 0.44f, 1};
                object.boundsMin = lo;
                object.boundsMax = hi;
            }
        } else if (node.ref.source == DocumentEntityRef::Source::FpsSpawn) {
            const float3 pos{float(node.position.x), float(node.position.y),
                             float(node.position.z)};
            if (!std::isfinite(pos.x) || !std::isfinite(pos.y) ||
                !std::isfinite(pos.z)) continue;
            object.world = mat4::translate(pos) * mat4::scale({0.6f, 1.8f, 0.6f});
            object.tint = {0.1f, 0.65f, 0.85f, 1};
            object.boundsMin = {pos.x - 0.3f, pos.y, pos.z - 0.3f};
            object.boundsMax = {pos.x + 0.3f, pos.y + 1.8f, pos.z + 0.3f};
        } else {
            continue;
        }
        instances.push_back(object);
    }
    // Live objects are now real ECS entity components (the FPS gameplay state
    // remains the compatibility authority). All runtime genres enter through
    // the same editor runtime-entity provider.
    for (const auto& runtime : enumerateEditorRuntimeEntities(*this)) {
        if (!runtime.hasPosition) continue;
        const bool actorSource = runtime.selection.kind == Selection::FpsActor;
        const bool dynamicSource = runtime.selection.kind == Selection::FpsRuntimeObject;
        if (!actorSource && !dynamicSource) continue;
        const float3 point = runtime.position;
        SceneRenderable object;
        if (actorSource)
            object.runtimeActor = runtime.selection.index;
        else
            object.runtimeObject = runtime.selection.ref;
        float3 size = actorSource ? float3{0.5f, 1.7f, 0.5f}
                                 : float3{0.35f, 0.35f, 0.35f};
        if (dynamicSource) {
            const uint8_t kind = uint8_t(runtime.selection.ref >> 32);
            if (kind == 3) size = {1.6f, 1.1f, 3.0f}; // vehicle marker
            if (kind == 1) size = {0.18f, 0.18f, 0.18f}; // projectile marker
        }
        object.world = mat4::translate(point) * mat4::scale(size);
        object.tint = actorSource ? float4{0.85f, 0.68f, 0.25f, 1}
                          : float4{0.75f, 0.60f, 0.85f, 1};
        object.mesh = &fpsPreviewMesh_;
        object.pickable = true;
        object.boundsMin = {point.x - size.x * 0.5f, point.y,
                            point.z - size.z * 0.5f};
        object.boundsMax = {point.x + size.x * 0.5f, point.y + size.y,
                            point.z + size.z * 0.5f};
        instances.push_back(object);
    }
    for (auto& object : instances) {
        Selection candidate;
        if (object.runtimeObject) {
            candidate.kind = Selection::FpsRuntimeObject;
            candidate.ref = object.runtimeObject;
        } else if (object.runtimeActor >= 0) {
            candidate.kind = Selection::FpsActor;
            candidate.index = object.runtimeActor;
        } else {
            candidate.kind = Selection::FpsObject;
            candidate.ref = object.entity;
        }
        object.selected = editOverlays && (selection == candidate ||
            std::find(selectionSet.begin(), selectionSet.end(), candidate) !=
                selectionSet.end());
        object.hovered = editOverlays && !object.selected && hover == candidate;
    }
    submitSceneRenderables(r3d_, instances);
    // Draw transform axes from the selected renderable, if visible.
    for (const auto& selected : instances) {
        // The gizmo belongs to the primary selected entity, not whichever
        // selected collider happens to come first in source order.
        if (!selected.selected || selected.runtimeActor >= 0 ||
            selected.entity != selectedKey) continue;
        DocumentEntityRef ref;
        DocumentTransform position;
        if (!DocumentEntityRef::fromFpsKey(selected.entity, ref) ||
            !readDocumentTransform(doc(), ref, position)) break;
        const float3 centre{float(position.x), float(position.y),
                            float(position.z)};
        const float extent = std::max(0.35f, camera.distance * 0.075f);
        const Json* source = fpsDocumentEntity(doc().data.fps, ref);
        const bool oriented = source && (*source)["yaw"].isNumber();
        if (oriented && transformTool == TransformTool::Rotate) {
            constexpr int segments = 64;
            constexpr float tau = 6.283185307179586f;
            for (int i = 0; i < segments; ++i) {
                const float a = tau * float(i) / float(segments);
                const float b = tau * float(i + 1) / float(segments);
                r3d_.line(centre + float3{std::cos(a) * extent, 0,
                                          std::sin(a) * extent},
                          centre + float3{std::cos(b) * extent, 0,
                                          std::sin(b) * extent},
                          rgba(255, 188, 80));
            }
        } else {
            r3d_.line(centre, centre + float3{extent, 0, 0}, rgba(235, 84, 84));
            r3d_.line(centre, centre + float3{0, extent, 0}, rgba(93, 215, 113));
            r3d_.line(centre, centre + float3{0, 0, extent}, rgba(80, 153, 245));
        }
        break;
    }
}

bool Editor::moveSelectedFpsSceneObject(float dx, float dy, float dz) {
    // FPS uses the same atomic/deduplicated multi-document translation as
    // RTS. The selected set may contain colliders and spawn points together.
    std::vector<DocumentEntityRef> refs;
    for (const auto& chosen : selected()) {
        DocumentEntityRef ref;
        if (!documentEntityFromSelection(chosen, ref) ||
            ref.source == DocumentEntityRef::Source::MapEntity) continue;
        refs.push_back(ref);
    }
    return translateDocumentEntities(doc(), refs, {dx, dy, dz});
}

bool Editor::isFpsSceneSourceSelected(bool spawn, size_t sourceIndex) const {
    const auto ref = spawn ? DocumentEntityRef::fpsSpawn(sourceIndex)
                           : DocumentEntityRef::fpsCollider(sourceIndex);
    const Selection candidate = selectionForDocumentEntity(ref);
    return selection == candidate ||
        std::find(selectionSet.begin(), selectionSet.end(), candidate) != selectionSet.end();
}

void Editor::selectFpsSceneSource(bool spawn, size_t sourceIndex) {
    const auto ref = spawn ? DocumentEntityRef::fpsSpawn(sourceIndex)
                           : DocumentEntityRef::fpsCollider(sourceIndex);
    const Selection chosen = selectionForDocumentEntity(ref);
    if (chosen.kind != Selection::None) select(chosen);
}

int Editor::pickFpsSceneObject(const Viewport& vp, const render::RTSCamera& camera,
                               float sx, float sy) const {
    render::float3 origin, direction;
    if (!camera.screenRay(sx - vp.originX, sy - vp.originY,
                          vp.pointsW, vp.pointsH, origin, direction)) return -1;
    return pickSceneRenderable(fpsSceneObjects_, origin, direction);
}


}
