#pragma once
#include "Workspace.h"
#include "DocumentLightIdentity.h"
#include "apps/editor/EditorSceneNodes.h"
#include "apps/editor/DocumentEntityIdentity.h"
#include "engine/render/RenderLightingSettings.h"
#include <stdexcept>

namespace aiverse::editor {
inline DocumentEntityRef resolveSceneEntity(const Workspace& ws,const Json& params) {
    if(!params["entityId"].isString())throw std::runtime_error("entityId is required");
    for(const auto& node:enumerateDocumentSceneNodes(ws.map,ws.project.isFps()))
        if(documentEntityId(ws.map,node.ref)==params.getStr("entityId"))return node.ref;
    throw std::runtime_error("Unknown scene entity ID");
}
inline Json sceneEntityView(const MapDoc& map,const EditorSceneNode& node) {
    Json result=Json::object(),components=Json::array();
    result.set("entityId",Json::string(documentEntityId(map,node.ref)));
    result.set("name",Json::string(node.label));
    result.set("stableId",Json::fromBool(node.ref.isLight()));
    for(const auto& descriptor:documentComponents(map,node.ref)) {
        Json data,component=Json::object();
        if(!readDocumentComponent(map,node.ref,descriptor.kind,data))continue;
        component.set("id",Json::string(descriptor.id));component.set("data",data);
        component.set("editable",Json::fromBool(descriptor.editable));components.push(component);
    }
    result.set("components",components);return result;
}
inline void commitSceneDocument(Workspace& ws,MapDoc& draft) {
    if(ws.project.isFps()) {
        Json args=Json::object();args.set("map",draft.toJson());
        const auto result=ws.call("fps_set",args,"scene-entity");
        if(result.has("error"))throw std::runtime_error(result.getStr("error"));
    } else {ws.map.beginEdit();ws.map.data=std::move(draft.data);ws.map.touched();}
}
inline bool componentPatch(MapDoc& map,DocumentEntityRef ref,DocumentComponentKind kind,const Json& patch) {
    if(!patch.isObject() || patch.size()==0)throw std::runtime_error("Nonempty component patch required");
    for(const auto& [key,value]:patch.members()) {
        (void)value;
        const bool valid=kind==DocumentComponentKind::Light ||
            (kind==DocumentComponentKind::Transform && (key=="position" || key=="rotationY" || key=="scale")) ||
            (kind==DocumentComponentKind::Spawn && key=="position") ||
            (kind==DocumentComponentKind::Collider && (key=="min" || key=="max"));
        if(!valid)throw std::runtime_error("Property does not belong to this component");
    }
    if(ref.source==DocumentEntityRef::Source::MapEntity && patch.has("rotationY") && patch.size()>1)
        throw std::runtime_error("Legacy map rotation must be edited separately");
    Json before;readDocumentComponent(map,ref,kind,before);
    if(kind!=DocumentComponentKind::Light)
        for(const auto& [key,value]:patch.members()) {
            (void)value;
            if(!before.has(key))throw std::runtime_error("Field is not supported by this component instance");
        }
    if(writeDocumentComponent(map,ref,kind,patch))return true;
    for(const auto& [key,value]:patch.members())
        if(!before.has(key) || before[key]!=value)throw std::runtime_error("Invalid or unsupported component patch");
    return false;
}
inline void registerSceneEntityOps(Workspace& ws) {
    auto add=[&](const char* name,const char* description,const char* schema,Workspace::OpFn operation) {
        ws.registerOp({name,description,Json::parse(schema),[&ws,operation](const Json& p){
            if(ws.project.dir.empty())throw std::runtime_error("Open a project with open_game first");
            return operation(p);
        },false});
    };
    add("scene_entity_list","List document entities with identity and real component data. Light IDs persist; other legacy IDs are provisional.","{}",[&ws](const Json&){
        Json result=Json::object(),entities=Json::array();
        for(const auto& node:enumerateDocumentSceneNodes(ws.map,ws.project.isFps()))entities.push(sceneEntityView(ws.map,node));
        result.set("entities",entities);return result;
    });
    add("scene_entity_get","Read one scene entity and its components by entityId.",R"({"properties":{"entityId":{"type":"string"}},"required":["entityId"]})",[&ws](const Json& p){
        const auto ref=resolveSceneEntity(ws,p);
        for(const auto& node:enumerateDocumentSceneNodes(ws.map,ws.project.isFps()))
            if(node.ref.source==ref.source && node.ref.index==ref.index)return sceneEntityView(ws.map,node);
        throw std::runtime_error("Scene entity disappeared");
    });
    add("scene_component_set","Patch a declared editable component by entityId and componentId; native validation and Undo history, explicit map_save to persist.",R"({"properties":{"entityId":{"type":"string"},"componentId":{"type":"string"},"patch":{"type":"object"}},"required":["entityId","componentId","patch"]})",[&ws](const Json& p){
        const auto ref=resolveSceneEntity(ws,p);MapDoc draft;draft.data=ws.map.data;
        bool found=false,changed=false;
        for(const auto& component:documentComponents(ws.map,ref))if(p.getStr("componentId")==component.id) {
            if(!component.editable)throw std::runtime_error("Component is read-only");
            changed=componentPatch(draft,ref,component.kind,p["patch"]);found=true;break;
        }
        if(!found)throw std::runtime_error("Unknown component ID");
        if(changed)commitSceneDocument(ws,draft);
        Json result=Json::object();result.set("changed",Json::fromBool(changed));return result;
    });
    add("scene_light_create","Create a Light entity with Transform and Light components in RTS or FPS. Position is [x,height,z]; RTS height is relative to terrain.",R"({"properties":{"name":{"type":"string"},"position":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},"light":{"type":"object"}}})",[&ws](const Json& p){
        if(p.has("name") && (!p["name"].isString() || p.getStr("name").size()>128))throw std::runtime_error("Invalid entity name");
        MapDoc draft;draft.data=ws.map.data;
        auto& document=ws.project.isFps()?draft.data.fps:draft.data.extra;
        if(!document["lights"].isArray())document.set("lights",Json::array());
        auto& lights=document.member("lights");const size_t index=lights.size();
        Json light=Json::parse(R"({"x":0,"z":0,"height":1,"type":"point","radius":4,"intensity":1.5,"color":[1,0.8,0.6]})");
        if(p.has("name"))light.set("name",p["name"]);
        lights.push(light);ensureDocumentLightIds(document);
        const auto ref=DocumentEntityRef::light(index,ws.project.isFps());
        if(p.has("position")){Json patch=Json::object();patch.set("position",p["position"]);componentPatch(draft,ref,DocumentComponentKind::Transform,patch);}
        if(p.has("light"))componentPatch(draft,ref,DocumentComponentKind::Light,p["light"]);
        const auto id=documentEntityId(draft,ref);commitSceneDocument(ws,draft);
        Json result=Json::object();result.set("entityId",Json::string(id));return result;
    });
    add("scene_light_delete","Delete a Light entity by persistent ID; rejects other entity kinds. Undoable until explicit map_save.",R"({"properties":{"entityId":{"type":"string"}},"required":["entityId"]})",[&ws](const Json& p){
        const auto ref=resolveSceneEntity(ws,p);if(!ref.isLight())throw std::runtime_error("Expected a Light entity");
        MapDoc draft;draft.data=ws.map.data;
        auto& document=ws.project.isFps()?draft.data.fps:draft.data.extra;
        document.member("lights").eraseAt(ref.index);commitSceneDocument(ws,draft);
        Json result=Json::object();result.set("deleted",Json::fromBool(true));return result;
    });
    add("scene_render_lighting_get","Read validated map GI and direct lighting settings (renderLighting v1).","{}",[&ws](const Json&){
        return render::renderLightingSettings((ws.project.isFps()?ws.map.data.fps:ws.map.data.extra)["renderLighting"]);
    });
    add("scene_render_lighting_set","Patch map renderLighting v1 settings; native validation, Undo history, explicit map_save. Does not imply hardware support.",R"({"properties":{"patch":{"type":"object"}},"required":["patch"]})",[&ws](const Json& p){
        if(!p["patch"].isObject() || p["patch"].size()==0)throw std::runtime_error("Nonempty settings patch required");
        MapDoc draft;draft.data=ws.map.data;auto& document=ws.project.isFps()?draft.data.fps:draft.data.extra;
        auto settings=render::renderLightingSettings(document["renderLighting"]);
        for(const auto& [key,value]:p["patch"].members())settings.set(key,value);
        settings=render::renderLightingSettings(settings);
        if(settings!=document["renderLighting"]){document.set("renderLighting",settings);commitSceneDocument(ws,draft);}
        return settings;
    });
}
}
