// AIVERSE LENS camera scene authoring asset (cameras.json).
// This is an explicit project data format, not implicit ECS memory persistence.
// Camera runtime presentation state and GPU handles are deliberately omitted.
#pragma once
#include "../data/Json.h"
#include "CameraComponents.h"
#include "CameraRigComponents.h"
#include "CameraViewportComponents.h"
#include "SceneHierarchy.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace aiverse {
struct CameraNameComponent {
    uint64_t stableId=0;
    std::string name="Camera";
};
inline Json cameraVecJson(Vec3 v) {
    Json a=Json::array();
    a.push(Json::number(v.x));a.push(Json::number(v.y));a.push(Json::number(v.z));
    return a;
}
inline Vec3 cameraVecFromJson(const Json& v,Vec3 fallback={}) {
    if(!v.isArray() || v.size()!=3)return fallback;
    return {v[0].fx(fallback.x),v[1].fx(fallback.y),v[2].fx(fallback.z)};
}
inline Json cameraRotationJson(Rotation r) {
    Json a=Json::array();
    a.push(Json::number(r.x));a.push(Json::number(r.y));
    a.push(Json::number(r.z));a.push(Json::number(r.w));
    return a;
}
inline Rotation cameraRotationFromJson(const Json& v) {
    if(!v.isArray()||v.size()!=4)return {};
    return {v[0].fx(),v[1].fx(),v[2].fx(),v[3].fx(1_fx)};
}
inline Json cameraSceneToJson(const World& world) {
    Json root=Json::object();root.set("version",Json::number(int64_t(1)));
    Json cameras=Json::array();
    world.eachExistingConst<CameraComponent>([&](Entity entity,const CameraComponent& camera){
        if(!world.alive(entity))return;
        Json item=Json::object();
        const auto* name=world.tryGetConst<CameraNameComponent>(entity);
        item.set("id",Json::number(int64_t(name?name->stableId:entity.packed())));
        item.set("name",Json::string(name?name->name:"Camera"));
        if(const auto* xf=world.tryGetConst<LocalTransform>(entity)) {
            item.set("position",cameraVecJson(xf->position));
            item.set("rotation",cameraRotationJson(xf->rotation));
        }
        Json view=Json::object();
        view.set("projection",Json::string(camera.projection==CameraProjection::Orthographic?"ortho":"perspective"));
        view.set("fov",Json::number(camera.verticalFov));
        view.set("orthographicHalfHeight",Json::number(camera.orthographicHalfHeight));
        view.set("near",Json::number(camera.nearPlane));
        view.set("far",Json::number(camera.farPlane));
        view.set("priority",Json::number(int64_t(camera.priority)));
        view.set("visibleMask",Json::number(int64_t(camera.visibilityMask)));
        view.set("enabled",Json::fromBool(camera.enabled));
        item.set("camera",std::move(view));
        if(const auto* o=world.tryGetConst<CameraOutputComponent>(entity)){
            Json out=Json::object();
            out.set("kind",Json::string(o->kind==CameraOutputKind::Texture?"texture":"screen"));
            out.set("key",Json::number(int64_t(o->targetAssetKey)));
            out.set("width",Json::number(int64_t(o->width)));
            out.set("height",Json::number(int64_t(o->height)));
            out.set("hdr",Json::fromBool(o->hdr));
            out.set("depth",Json::fromBool(o->depth));
            item.set("output",std::move(out));
        }
        if(const auto* update=world.tryGetConst<CameraUpdateComponent>(entity)){
            Json j=Json::object();
            j.set("mode",Json::number(int64_t(update->mode)));
            j.set("everyNFrames",Json::number(int64_t(update->everyNFrames)));
            j.set("rateHz",Json::number(int64_t(update->rateHz)));
            item.set("update",std::move(j));
        }
        if(const auto* vp=world.tryGetConst<CameraViewportComponent>(entity)){
            Json j=Json::object();
            j.set("x",Json::number(vp->x));j.set("y",Json::number(vp->y));
            j.set("width",Json::number(vp->width));j.set("height",Json::number(vp->height));
            j.set("enabled",Json::fromBool(vp->enabled));
            item.set("viewport",std::move(j));
        }
        if(const auto* stack=world.tryGetConst<CameraStackComponent>(entity)){
            Json j=Json::object();j.set("order",Json::number(int64_t(stack->order)));
            j.set("opacity",Json::number(stack->opacity));
            j.set("overlay",Json::fromBool(stack->overlay));
            item.set("stack",std::move(j));
        }
        if(const auto* rig=world.tryGetConst<CameraRigComponent>(entity)){
            Json j=Json::object();j.set("mode",Json::number(int64_t(rig->mode)));
            j.set("offset",cameraVecJson(rig->offset));
            j.set("distance",Json::number(rig->distance));
            j.set("yaw",Json::number(rig->yaw));j.set("pitch",Json::number(rig->pitch));
            j.set("smoothing",Json::number(rig->smoothing));
            j.set("trackTarget",Json::fromBool(rig->trackTarget));
            j.set("enabled",Json::fromBool(rig->enabled));
            item.set("rig",std::move(j));
        }
        if(const auto* lens=world.tryGetConst<CameraLensComponent>(entity)){
            Json j=Json::object();
            j.set("physicalFov",Json::fromBool(lens->physicalFov));
            j.set("focalLengthMm",Json::number(lens->focalLengthMm));
            j.set("sensorHeightMm",Json::number(lens->sensorHeightMm));
            j.set("fNumber",Json::number(lens->fNumber));
            j.set("focusDistance",Json::number(lens->focusDistance));
            j.set("shutterSeconds",Json::number(lens->shutterSeconds));
            j.set("iso",Json::number(lens->iso));item.set("lens",std::move(j));
        }
        cameras.push(std::move(item));
    });
    root.set("cameras",std::move(cameras));
    return root;
}
inline bool loadCameraScene(World& world,SceneHierarchy& hierarchy,const Json& json,
                            std::vector<Entity>* created=nullptr,std::string* error=nullptr) {
    auto fail=[&](const char* message){
        if(error)*error=message;
        return false;
    };
    if(!json.isObject()||json.getInt("version")!=1||
       !json["cameras"].isArray()||json["cameras"].size()>256)
        return fail("Invalid AIVERSE camera scene version/count");
    struct Item {
        LocalTransform transform{};
        CameraComponent camera{};
        CameraOutputComponent output{};
        CameraUpdateComponent update{};
        CameraViewportComponent viewport{};
        CameraStackComponent stack{};
        CameraRigComponent rig{};
        CameraLensComponent lens{};
        CameraNameComponent name{};
    };
    std::vector<Item> items;
    std::unordered_set<uint64_t> ids;
    for(const Json& input:json["cameras"].items()) {
        if(!input.isObject())return fail("Invalid camera object");
        Item it;
        const int64_t id=input.getInt("id");
        if(id<=0 || !ids.insert(uint64_t(id)).second)return fail("Duplicate/invalid camera id");
        it.name={uint64_t(id),input.getStr("name","Camera")};
        it.transform.position=cameraVecFromJson(input["position"]);
        it.transform.rotation=cameraRotationFromJson(input["rotation"]);
        const Json& view=input["camera"];
        if(!view.isObject())return fail("Missing camera settings");
        it.camera.projection=view.getStr("projection")=="ortho"?
            CameraProjection::Orthographic:CameraProjection::Perspective;
        it.camera.verticalFov=view.getFx("fov",it.camera.verticalFov);
        it.camera.orthographicHalfHeight=view.getFx("orthographicHalfHeight",it.camera.orthographicHalfHeight);
        it.camera.nearPlane=view.getFx("near",it.camera.nearPlane);
        it.camera.farPlane=view.getFx("far",it.camera.farPlane);
        it.camera.priority=int32_t(std::clamp<int64_t>(view.getInt("priority"),-10000,10000));
        it.camera.visibilityMask=uint32_t(view.getInt("visibleMask",UINT32_MAX));
        it.camera.enabled=view.getBool("enabled",true);
        if(it.camera.verticalFov<=0_fx || it.camera.verticalFov>=Fixed::fromInt(3) ||
           it.camera.orthographicHalfHeight<=0_fx ||
           it.camera.nearPlane<=0_fx || it.camera.farPlane<=it.camera.nearPlane)
            return fail("Invalid camera clip/projection");
        const Json& out=input["output"];
        if(out.isObject()) {
            it.output.kind=out.getStr("kind")=="texture"?CameraOutputKind::Texture:CameraOutputKind::Screen;
            it.output.targetAssetKey=uint64_t(std::max<int64_t>(0,out.getInt("key")));
            it.output.width=uint16_t(std::clamp<int64_t>(out.getInt("width"),0,8192));
            it.output.height=uint16_t(std::clamp<int64_t>(out.getInt("height"),0,8192));
            it.output.hdr=out.getBool("hdr",false);
            it.output.depth=out.getBool("depth",true);
            if(it.output.kind==CameraOutputKind::Texture&&!it.output.targetAssetKey)
                return fail("Texture camera requires output key");
        }
        const Json& update=input["update"];
        if(update.isObject()) {
            it.update.mode=CameraUpdateMode(std::clamp<int64_t>(update.getInt("mode"),0,4));
            it.update.everyNFrames=uint16_t(std::clamp<int64_t>(update.getInt("everyNFrames",1),1,65535));
            it.update.rateHz=uint16_t(std::clamp<int64_t>(update.getInt("rateHz",30),1,1000));
        }
        const Json& vp=input["viewport"];
        if(vp.isObject()) {
            it.viewport.x=vp.getFx("x");it.viewport.y=vp.getFx("y");
            it.viewport.width=vp.getFx("width",1_fx);it.viewport.height=vp.getFx("height",1_fx);
            it.viewport.enabled=vp.getBool("enabled");
        }
        const Json& stack=input["stack"];
        if(stack.isObject()){
            it.stack.order=int16_t(std::clamp<int64_t>(stack.getInt("order"),-1000,1000));
            it.stack.opacity=stack.getFx("opacity",1_fx);
            it.stack.overlay=stack.getBool("overlay");
        }
        const Json& rig=input["rig"];
        if(rig.isObject()) {
            it.rig.mode=CameraRigMode(std::clamp<int64_t>(rig.getInt("mode"),0,4));
            it.rig.offset=cameraVecFromJson(rig["offset"],it.rig.offset);
            it.rig.distance=rig.getFx("distance",it.rig.distance);
            it.rig.yaw=rig.getFx("yaw");it.rig.pitch=rig.getFx("pitch");
            it.rig.smoothing=rig.getFx("smoothing",it.rig.smoothing);
            it.rig.trackTarget=rig.getBool("trackTarget",true);
            it.rig.enabled=rig.getBool("enabled",true);
        }
        const Json& lens=input["lens"];
        if(lens.isObject()) {
            it.lens.physicalFov=lens.getBool("physicalFov");
            it.lens.focalLengthMm=lens.getFx("focalLengthMm",it.lens.focalLengthMm);
            it.lens.sensorHeightMm=lens.getFx("sensorHeightMm",it.lens.sensorHeightMm);
            it.lens.fNumber=lens.getFx("fNumber",it.lens.fNumber);
            it.lens.focusDistance=lens.getFx("focusDistance",it.lens.focusDistance);
            it.lens.shutterSeconds=lens.getFx("shutterSeconds",it.lens.shutterSeconds);
            it.lens.iso=lens.getFx("iso",it.lens.iso);
        }
        items.push_back(std::move(it));
    }
    // All validation above is complete before a single Entity is created.
    for(const auto& c:items) {
        const Entity entity=world.create();
        if(!hierarchy.attach(world,entity,c.transform))
            return fail("Scene hierarchy rejected camera entity");
        world.add<CameraComponent>(entity,c.camera);
        world.add<CameraOutputComponent>(entity,c.output);
        world.add<CameraUpdateComponent>(entity,c.update);
        world.add<CameraNameComponent>(entity,c.name);
        world.add<CameraViewportComponent>(entity,c.viewport);
        world.add<CameraStackComponent>(entity,c.stack);
        world.add<CameraRigComponent>(entity,c.rig);
        world.add<CameraLensComponent>(entity,c.lens);
        if(created)created->push_back(entity);
    }
    hierarchy.update(world);
    return true;
}
} // namespace aiverse
