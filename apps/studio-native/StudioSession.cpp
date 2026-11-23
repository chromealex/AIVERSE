#include <regex>
#include <fstream>
#include "apps/editor/AuthoringWater.h"
#include "StudioHumanCharacterBuild.h"
#include "StudioSession.h"
#include "libraries/include/aiverselib/world/WorldProcessesAuthoring.h"
#include "libraries/include/aiverselib/world/WorldProcessesLinkedAuthoring.h"
#include "libraries/include/aiverselib/world/WorldProcessesExtendedAuthoring.h"
#include "libraries/include/aiverselib/world/WorldProcessesExtendedLinks.h"
#include "engine/world/NereidFlowAuthoring.h"
#include "apps/editor/core/ProjectFile.h"
#include "apps/editor/core/Projects.h"
#include "engine/game/MapGrid.h"
#include "apps/editor/EditorWorldEdit.h"
#include "apps/studio-native/StudioContentDirectory.h"
#include "apps/studio-native/StudioAssetCatalog.h"
#include "apps/studio-native/StudioAssetReferences.h"
#include "apps/studio-native/StudioAssetUsage.h"
#include "apps/studio-native/StudioTools.h"
#include "engine/view/AttachmentLayout.h"
#include "apps/studio-native/StudioMoveJournal.h"
#include "apps/editor/EditorSceneNodes.h"
#include "apps/studio-native/StudioDocumentIds.h"
#include "apps/studio-native/StudioDocumentVisuals.h"
#include "apps/studio-native/StudioLightingValidation.h"
#include "engine/render/RenderLightingSettings.h"
#include "apps/studio-native/StudioFpsLightingDocument.h"
#include "apps/studio-native/StudioMaterialValidation.h"
#include "apps/studio-native/StudioShaderGraphValidation.h"
#include "apps/studio-native/StudioShaderGraphDraft.h"
#include "apps/studio-native/StudioFacialRigDocument.h"
#include "apps/studio-native/StudioHumanLookDocument.h"
#include "apps/studio-native/StudioHumanPackage.h"
#include "engine/character/vivid/MetaHumanAdapter.h"
#include <algorithm>
#include "apps/editor/core/MaterialOps.h"
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <filesystem>
#include <map>
#include <set>
#include <string_view>
#include <stdexcept>
#include <utility>
#include <tuple>

namespace aiverse::studio {
namespace {
Json str(const std::string& s) { return Json::string(s); }
Json num(int64_t n) { return Json::number(n); }
Json templateJson(const editor::TemplateInfo& t) {
    Json o=Json::object();
    o.set("id",str(t.id));o.set("name",str(t.name));o.set("description",str(t.description));
    o.set("genre",str(t.genre));o.set("maps",num(int64_t(t.maps)));o.set("source",str(t.source));
    o.set("official",Json::fromBool(t.official));
    if(!t.version.empty())o.set("version",str(t.version));
    if(t.size)o.set("size",num(int64_t(t.size)));
    if(!t.previewUrl.empty())o.set("preview",str(t.previewUrl));
    return o;
}
Json prop(const char* key, const char* label, const char* type, Json value, bool editable = true) {
    Json r = Json::object();
    r.set("key", str(key)); r.set("label", str(label));
    r.set("type", str(type)); r.set("value", std::move(value));
    r.set("editable", Json::fromBool(editable));
    return r;
}
Json numericProp(const char* key, const char* label, Json value, bool integer = false) {
    Json r = prop(key, label, "number", std::move(value));
    const bool owner = std::string(key) == "owner";
    r.set("min", Json::number(owner ? int64_t(-1) : int64_t(-1000000)));
    r.set("max", Json::number(owner ? int64_t(10000) : int64_t(1000000)));
    r.set("step", Json::number(owner ? int64_t(1) : int64_t(0))); // overwritten below for floating fields
    if (!owner) r.set("step", Json::number(0.1, 3));
    if (integer) r.set("integer", Json::fromBool(true));
    return r;
}
std::string nodeId(const editor::MapDoc& map,const editor::DocumentEntityRef& ref) { return studioDocumentId(map,ref); }
// Resolve native picker/CLI inputs before locating the project-local journal.
// Workspace supports directories, manifest/project files and sample template IDs.
std::string recoveredProjectDir(const editor::Workspace& ws,const std::string& input) {
    std::string error;
    std::string directory=editor::resolveProjectDir(input,&error);
    if (directory.empty() && !editor::isProjectFilePath(input)) {
        const auto sample=std::filesystem::path(ws.gamesDir())/input;
        if (std::filesystem::exists(sample/"game.json")) directory=sample.string();
    }
    if (directory.empty()) throw std::runtime_error(error.empty()?"Cannot resolve project":error);
    directory=std::filesystem::weakly_canonical(directory).string();
    StudioMoveJournal::recover(directory);
    return directory;
}

}
StudioSession::StudioSession(const std::string& engineRoot, const std::string& project) {
    ws_.engineDir = engineRoot;
    if (!project.empty()) {
        const auto directory=recoveredProjectDir(ws_,project);
        std::string error;
        if (!ws_.openGame(directory, &error)) throw std::runtime_error(error.empty() ? "Cannot open project" : error);
        // A project opened from the CLI follows the same content policy as
        // projects chosen through the native Open Project panel.
        (void)studioContentRoot(ws_.project.dir);
    }
}
int StudioSession::indexFor(const std::string& id) const {
    const int index=studioMapEntityIndex(id,ws_.map);
   if(index<0)throw std::runtime_error("Invalid or missing entity ID");
   return index;
}
Json StudioSession::propertiesFor(const std::string& entityId) const {
    if (entityId.empty()) throw std::runtime_error("Missing document entity ID");
    Json fields = Json::array();
    for (const auto& node : editor::enumerateDocumentSceneNodes(ws_.map)) {
        if (nodeId(ws_.map,node.ref) != entityId) continue;
        if(node.ref.isLight()) {
            const auto& light=*editor::documentLight(ws_.map,node.ref);
            fields.push(prop("name","Name","text",str(light.getStr("name"))));
            auto type=prop("type","Light type","text",str(light.getStr("type","point")));
            type.set("options",Json::parse(R"([{"label":"Point","value":"point"},{"label":"Spot","value":"spot"}])"));fields.push(type);
            for(const auto& [key,label,def,lo,hi]:std::vector<std::tuple<const char*,const char*,double,double,double>>{
                {"x","Position X",0,-1000000,1000000},
                {"height",ws_.map.data.worldSpace?"Position Y":"Height above terrain",1,-1000,10000},{"z","Position Z",0,-1000000,1000000},{"radius","Radius",4,.001,100000},{"intensity","Intensity",1.5,0,10000}}) {
                auto field=numericProp(key,label,Json::number(light[key].toDouble(def),3));
                field.set("min",Json::number(lo));field.set("max",Json::number(hi));fields.push(field);
            }
            fields.push(prop("nightOnly","Night only","boolean",Json::fromBool(light.getBool("nightOnly"))));
            fields.push(prop("electric","Electric","boolean",Json::fromBool(light.getBool("electric"))));
        } else if (node.ref.source == editor::DocumentEntityRef::Source::MapEntity) {
            const auto& e = ws_.map.data.entities.at(node.ref.index);
            fields.push(prop("type", "Type", "text", str(e.type)));
            fields.push(numericProp("x", "Position X", Json::number(double(e.x), 4)));
            fields.push(numericProp("z", "Position Z", Json::number(double(e.z), 4)));
            fields.push(numericProp("owner", "Owner", num(int64_t(e.owner)), true));
           if(editor::authoredEntity(e)) {
               const auto& transform=e.extra["components"]["Transform"];
               fields.push(numericProp("y","Position Y",transform.has("y")?transform["y"]:Json::number(0.0)));
               if(!e.extra["components"].has("NereidWater")) {
                   fields.push(numericProp("yaw","Rotation Y",transform.has("yaw")?transform["yaw"]:Json::number(0.0)));
                   auto scale=numericProp("scale","Uniform scale",transform.has("scale")?transform["scale"]:Json::number(1.0));scale.set("min",Json::number(.001));scale.set("max",Json::number(10000.0));fields.push(scale);
               }
               fields.push(prop("asset","Model","text",str(editor::authoredModelAsset(e)),false));
           }
        } else if (node.hasPosition) {
            fields.push(numericProp("x", "Position X", Json::number(node.position.x, 3)));
            fields.push(numericProp("y", "Position Y", Json::number(node.position.y, 3)));
            fields.push(numericProp("z", "Position Z", Json::number(node.position.z, 3)));
        }
        return fields;
    }
    throw std::runtime_error("Unknown document object");
}

namespace {
Json component(const char* id,const char* title,Json properties,const char* note="") {
    Json c=Json::object();
    c.set("id",str(id));c.set("title",str(title));c.set("properties",std::move(properties));
    if(*note)c.set("note",str(note));
    return c;
}
Json readOnly(Json field){field.set("editable",Json::fromBool(false));return field;}
// Read-only display of an arbitrary authored component value.
Json genericField(const std::string& key,const Json& value) {
    Json r=Json::object();
    r.set("key",str(key));r.set("label",str(key));r.set("editable",Json::fromBool(false));
    if(value.isNumber()){r.set("type",str("number"));r.set("value",value);}
    else if(value.isString()){r.set("type",str("text"));r.set("value",value);}
    else if(value.boolean(false)==value.boolean(true)){r.set("type",str("boolean"));r.set("value",value);}
    else {r.set("type",str("text"));r.set("value",str(value.dump(-1)));}
    return r;
}
// Validate map-object references against current authored components, not a
// runtime entity index or an untrusted browser list. Duplicate target IDs fail.
bool validWorldProcessDocumentRef(const editor::MapDoc& map,std::string_view componentId,
                                  std::string_view field,const Json& value) {
    if(!lib::worldprocess::linked::isReference(componentId,field))return true;
    if(!value.isString())return false;
    if(value.str().empty())return true; // allowed while the component is disabled
    const auto target=lib::worldprocess::linked::targetType(componentId);
    size_t found=0;
    for(const auto& entity:map.data.entities)
        if(entity.id==value.str() &&
           entity.extra["components"].has(std::string(target)))++found;
    return found==1;
}
// A Scene/Hierarchy deletion cannot leave a survivor with an orphaned
// persistent ECHO/FRACTURE/AETHER reference. Batch deletion is permitted
// when ALL referring entities are deleted in the same atomic map edit.
void validateWorldProcessEntityDeletion(const editor::MapDoc& map,
                                        const std::vector<int>& indexes) {
    std::set<int> removed(indexes.begin(),indexes.end());
    std::set<std::string> removedIds;
    for(int index:indexes){
        if(index<0||size_t(index)>=map.data.entities.size())
            throw std::runtime_error("Invalid map entity deletion index");
        const auto& id=map.data.entities[size_t(index)].id;
        if(!id.empty())removedIds.insert(id);
    }
    if(removedIds.empty())return;
    for(size_t i=0;i<map.data.entities.size();++i){
        if(removed.count(int(i)))continue;
        const auto& components=map.data.entities[i].extra["components"];
        if(!components.isObject())continue;
        for(const auto& [name,value]:components.members()){
            if(!lib::worldprocess::linked::supports(name)||!value.isObject())continue;
            for(const auto& [key,ref]:value.members()){
                if(!lib::worldprocess::linked::isReference(name,key)||
                   !ref.isString()||!removedIds.count(ref.str()))continue;
                throw std::runtime_error(
                    "Map object '"+ref.str()+"' is referenced by "+name+
                    " on '"+map.data.entities[i].id+
                    "'; clear or relink that field before deleting the target");
            }
        }
    }
}
// Map "facing" is clockwise degrees seen from above; Transform rotation Y is
// right-handed (counter-clockwise). Both normalized to [0,360).
double studioFacingToYaw(double facing){double y=std::fmod(360.0-std::fmod(facing,360.0),360.0);return y<0?y+360.0:y;}
bool studioModelAssetPath(const std::string& path) {
    if(path.rfind("content/",0)!=0 || path.size()>512 || path.find("..")!=std::string::npos || path.find('\\')!=std::string::npos)
        return false;
    std::string ext=std::filesystem::path(path).extension().string();
    std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
    return ext==".glb" || ext==".gltf" || ext==".obj";
}
}
// Legacy map unit -> authored entity with an explicit Transform that keeps it
// exactly where/how it stands now (terrain height, runtime facing).
editor::DocEntity StudioSession::promotedAuthored(int index) const {
    const auto& source=ws_.map.data.entities.at(size_t(index));
    auto candidate=source;
    if(editor::authoredEntity(source))return candidate;
    Json t=Json::object();
    t.set("y",Json::number(source.at.isNull()?double(terrainPreviewHeight(ws_.map.data,source.x,source.z,
        float(ws_.project.rules.levelHeight.toDouble()))):0.0,4));
    if(source.extra["facing"].isNumber())t.set("yaw",Json::number(studioFacingToYaw(source.extra["facing"].toDouble()),3));
    auto components=candidate.extra["components"].isObject()?candidate.extra["components"]:Json::object();
    components.set("Transform",t);candidate.extra.set("components",components);
    return candidate;
}
Json StudioSession::componentCatalog(const std::string& entityId) const {
    Json out=Json::array();
    auto item=[&](const char* id,const char* title,const char* group,const char* hint,const char* action,bool available,const char* reason) {
        Json o=Json::object();o.set("id",str(id));o.set("title",str(title));o.set("group",str(group));o.set("hint",str(hint));
        o.set("action",str(action));o.set("available",Json::fromBool(available));if(reason&&*reason)o.set("reason",str(reason));out.push(o);
    };
    if(entityId.compare(0,7,"entity:")!=0) {
        item("Transform","Transform","Core","Position, rotation, scale","native",false,"Already on this entity");
        return out;
    }
    const auto& e=ws_.map.data.entities.at(size_t(indexFor(entityId)));
    const auto& comps=e.extra["components"];
    const bool water=comps.has("NereidWater");
    item("Transform","Transform","Core","Position, rotation, scale","native",false,"Already on this entity");
    item("MeshRenderer","Mesh Renderer","Rendering","Model and material slots (built-in cube by default)","native",
        !comps.has("StaticModel") && !water,comps.has("StaticModel")?"Already on this entity":water?"Water entities are drawn by NEREID":
        (editor::authoredEntity(e)?"":"Replaces the look from rules.json for this entity only"));
    item("Light","Point Light","Rendering","Adds a light entity at this position","light",true,"");
    item("NereidWater","NEREID Water","World","Rivers, lakes and sea are authored in World › NEREID Water","water",false,"Create water from the NEREID Water panel");
    item("Unit","Unit","Gameplay","Type, owner and stats come from rules.json","native",false,"Defined by the entity type");
    for(const auto& name:{"EchoZone","FractureMember","AetherGasCell",
                          "EchoPortal","EchoEmitter","EchoReceiver",
                          "FractureBond","AetherDuct","AetherFlux",
                          "MorphNode","CrowdAgent","ImprintCell","CircuitNode",
                          "GranularField","EvolveRegion","SocietyPerson",
                          "MorphSpring","MorphForce","CrowdSlot","CrowdPreference",
                          "ImprintLink","ImprintDeposit","CircuitWire","CircuitGate",
                          "GranularSource","EvolvePressure","SocietyContact","SocietyMessage"}) {
        const bool present=comps.has(name);
        item(name,name,"World Processes",
             "Authoring data; native game setup must install the ECS component",
             "native",!present,present?"Already on this entity":"");
    }
    return out;
}
bool StudioSession::documentHas(const std::string& id) const {
    for (const auto& node : editor::enumerateDocumentSceneNodes(ws_.map))
        if (nodeId(ws_.map,node.ref) == id) return true;
    return false;
}
std::vector<std::string> StudioSession::selectedObjectIds() const {
    std::vector<std::string> out;
    if(selected_.empty())return out;
    for(const auto& id:selection_)if(id!=selected_ && documentHas(id) && std::find(out.begin(),out.end(),id)==out.end())out.push_back(id);
    out.push_back(selected_);
    return out;
}
void StudioSession::dropOrdinalFlags() {
    auto prune=[](std::set<std::string>& set){for(auto it=set.begin();it!=set.end();)it=it->rfind("entity:@",0)==0?std::next(it):set.erase(it);};
    prune(hidden_);prune(locked_);
}
Json StudioSession::inspectorFor(const std::string& entityId) const {
    if (entityId.empty()) throw std::runtime_error("Missing document entity ID");
    for (const auto& node : editor::enumerateDocumentSceneNodes(ws_.map)) {
        if (nodeId(ws_.map,node.ref) != entityId) continue;
        Json result=Json::object(),components=Json::array();
        result.set("id",str(entityId));result.set("name",str(node.label));
        if(node.ref.isLight()) {
            const auto& light=*editor::documentLight(ws_.map,node.ref);
            result.set("kind",str("Light"));
            Json transform=Json::array(),lightFields=Json::array();
            for(const auto& field:propertiesFor(entityId).items()) {
                const auto key=field.getStr("key");
                if(key=="name")continue;
                if(key=="x"||key=="z"||key=="height")transform.push(field); else lightFields.push(field);
            }
            components.push(component("Transform","Transform",transform));
            components.push(component("Light","Light",lightFields));
            result.set("nameEditable",Json::fromBool(true));
            (void)light;
        } else if(node.ref.source==editor::DocumentEntityRef::Source::MapEntity) {
            const auto& e=ws_.map.data.entities.at(node.ref.index);
            const bool authored=editor::authoredEntity(e);
            result.set("kind",str(e.type));
            const auto& comps=e.extra["components"];
            Json transform=Json::array();
            // Full 3D Transform for every map entity. Legacy units show their ground
            // height and runtime facing; the first Y/rotation/scale edit writes an
            // explicit components.Transform (see setProperty).
            const double levelHeight=ws_.project.rules.levelHeight.toDouble();
            const double ground=e.at.isNull()?terrainPreviewHeight(ws_.map.data,e.x,e.z,float(levelHeight)):0.0;
            const auto rotation=authored?editor::authoredRotationDegrees(e):
                std::array<double,3>{0.0,studioFacingToYaw(e.extra["facing"].toDouble(0)),0.0};
            const auto scale=authored?editor::authoredScale3(e):std::array<double,3>{1.0,1.0,1.0};
            const bool water=comps.has("NereidWater");
            transform.push(numericProp("x","Position X",Json::number(double(e.x),4)));
            transform.push(numericProp("y","Position Y",authored?Json::number(comps["Transform"]["y"].toDouble(0),4):Json::number(ground,4)));
            transform.push(numericProp("z","Position Z",Json::number(double(e.z),4)));
            const char* rotKeys[3]={"rotationX","rotationY","rotationZ"};
            const char* rotLabels[3]={"Rotation X","Rotation Y","Rotation Z"};
            const char* scaleKeys[3]={"scaleX","scaleY","scaleZ"};
            const char* scaleLabels[3]={"Scale X","Scale Y","Scale Z"};
            for(int i=0;i<3;++i){
                auto r=numericProp(rotKeys[i],rotLabels[i],Json::number(rotation[size_t(i)],3));
                r.set("min",Json::number(int64_t(-360000)));r.set("max",Json::number(int64_t(360000)));r.set("step",Json::number(int64_t(1)));
                if(water)r=readOnly(r);
                transform.push(r);
            }
            for(int i=0;i<3;++i){
                auto sc=numericProp(scaleKeys[i],scaleLabels[i],Json::number(scale[size_t(i)],4));
                sc.set("min",Json::number(.001));sc.set("max",Json::number(10000.0));
                if(water)sc=readOnly(sc);
                transform.push(sc);
            }
            components.push(component("Transform","Transform",transform));
            // Mesh Renderer: authored StaticModel (editable) or the type's view (read-only).
            const Json& view=ws_.project.rulesJson["types"][e.type]["view"];
            Json mesh=Json::array();
            const Json library=editor::loadMaterialLibrary(ws_);
            Json options=Json::array();
            {Json none=Json::object();none.set("label",str("Default (owner colour)"));none.set("value",str(""));options.push(none);}
            for(const auto& [name,value]:library.members()){(void)value;Json o=Json::object();o.set("label",str(name));o.set("value",str(name));options.push(o);}
            // A Transform alone (authored pose) keeps the type's look: only StaticModel overrides it.
            if(comps.has("StaticModel")) {
                const std::string asset=editor::authoredModelAsset(e);
                // No mesh = the built-in cube the Scene actually draws.
                Json field=prop("asset","Mesh","asset",str(asset.empty()?"builtin:cube":asset),authored);
                Json kinds=Json::array();kinds.push(str("model"));field.set("assetKinds",kinds);
                mesh.push(field);
                const Json& overrides=comps["StaticModel"]["materials"];
                std::vector<std::string> slots;
                if(materialSlots_ && asset.rfind("content/",0)==0)slots=materialSlots_(asset);
                if(slots.empty())slots.push_back("*");
                for(const auto& [name,value]:overrides.members())
                    if(std::find(slots.begin(),slots.end(),name)==slots.end())slots.push_back(name);
                for(const auto& slot:slots) {
                    const std::string key="material:"+slot;
                    const std::string label=slot=="*"?"Material (all slots)":"Material · "+slot;
                    Json f=prop(key.c_str(),label.c_str(),"text",str(overrides[slot].isString()?overrides[slot].str():""),authored);
                    f.set("options",options);mesh.push(f);
                }
                components.push(component("MeshRenderer","Mesh Renderer",mesh));
            } else {
                const Json& model=view["model"];
                std::string path=model.isString()?model.str():model.isArray()&&model.size()?
                    (model[size_t(0)].isString()?model[size_t(0)].str():model[size_t(0)].getStr("file")):"";
                std::string look=path;
                if(look.empty()) {
                    // RTS types without a model are drawn as built-in procedural shapes (game and Scene).
                    const Json& type=ws_.project.rulesJson["types"][e.type];
                    const bool structure=type.has("structure") && !type["structure"].isNull() && type["structure"].boolean(true);
                    look=type.isObject() ? "Shape · "+view.getStr("shape",structure?(type.has("resourceNode")?"mine":"structure"):"generic")+" (built-in)"
                                         : std::string("Cube (built-in)");
                }
                mesh.push(readOnly(prop("asset","Mesh","text",str(look))));
                for(const auto& [slot,ref]:view["materials"].members())
                    mesh.push(readOnly(prop(("material:"+slot).c_str(),(slot=="*"?std::string("Material (all slots)"):"Material · "+slot).c_str(),
                        "text",str(ref.isString()?ref.str():ref.dump(-1)))));
                components.push(component("MeshRenderer","Mesh Renderer",mesh,
                    ("Defined by entity type '"+e.type+"' in rules.json.").c_str()));
            }
            Json unit=Json::array();
            unit.push(readOnly(prop("type","Type","text",str(e.type))));
            unit.push(numericProp("owner","Owner",num(int64_t(e.owner)),true));
            components.push(component("Unit","Unit",unit));
            for(const auto& [name,value]:comps.members()) {
                if(name=="Transform"||name=="StaticModel")continue;
                Json fields=Json::array();
                const bool standalone=lib::worldprocess::authoring::supports(name) &&
                    lib::worldprocess::authoring::validate(name,value);
                const bool linked=lib::worldprocess::linked::supports(name) &&
                    lib::worldprocess::linked::validate(name,value);
                const bool extended=lib::worldprocess::extended::supports(name) &&
                    lib::worldprocess::extended::validate(name,value);
                const bool extendedLinked=lib::worldprocess::extendedLinks::supports(name) &&
                    lib::worldprocess::extendedLinks::validate(name,value);
                const bool authorable=standalone||linked||extended||extendedLinked;
                if(value.isObject())for(const auto& [key,item]:value.members()) {
                    Json field=genericField(key,item);
                    if(authorable) {
                        field.set("editable",Json::fromBool(true));
                        if(linked&&lib::worldprocess::linked::isReference(name,key)) {
                            Json options=Json::array();
                            Json empty=Json::object();
                            empty.set("label",str("Unassigned"));empty.set("value",str(""));
                            options.push(std::move(empty));
                            const auto target=lib::worldprocess::linked::targetType(name);
                            for(const auto& candidate:ws_.map.data.entities){
                                if(candidate.id.empty() ||
                                   !candidate.extra["components"].has(std::string(target)))continue;
                                Json option=Json::object();
                                const std::string label=candidate.extra.getStr("displayName",candidate.id);
                                option.set("label",str(label+" · "+candidate.id));
                                option.set("value",str(candidate.id));
                                options.push(std::move(option));
                            }
                            field.set("options",std::move(options));
                            field.set("entityReference",Json::fromBool(true));
                            field.set("description",str("Stable map object ID; resolved to a real ECS Entity on native game setup."));
                        }
                        if(item.isNumber()) {
                            int64_t minimum=0,maximum=aether::kMaxCellMass;
                            if(linked){
                                maximum=lib::worldprocess::linked::targetType(name)=="EchoZone"?
                                    echo::kMaxIntensity:
                                    lib::worldprocess::linked::targetType(name)=="FractureMember"?
                                    fracture::kMaxBondStrength:aether::kMaxDuctRate;
                                if(name=="AetherFlux")minimum=-aether::kMaxDuctRate;
                                if(key=="strength"||key=="intensity")minimum=1;
                            }
                            if(extended){
                                if(name=="MorphNode"){
                                    if(key=="restX"||key=="restY"||key=="restZ"){
                                        minimum=-morph::kPositionLimit;maximum=morph::kPositionLimit;
                                    }else maximum=1000;
                                }else if(name=="CrowdAgent"){
                                    if(key=="x"||key=="z"||key=="goalX"||key=="goalZ"){
                                        minimum=-crowd::kCoord;maximum=crowd::kCoord;
                                    }else maximum=key=="radius"?crowd::kMaxRadius:crowd::kMaxSpeed;
                                }else if(name=="ImprintCell"){
                                    maximum=key=="footprintFade"||key=="scentFade"||key=="residueFade"?
                                        imprint::kMaxRate:imprint::kMaxLevel;
                                }else if(name=="CircuitNode")maximum=circuit::kMaxUnits;
                                else if(name=="GranularField"){
                                    maximum=granular::kMaxCellMass;
                                    if(key=="capacity")minimum=1;
                                }else if(name=="EvolveRegion")maximum=evolve::kMaxState;
                                else if(name=="SocietyPerson")maximum=key=="credibility"?1000:1000000;
                            }
                            field.set("min",Json::number(minimum));
                            field.set("max",Json::number(maximum));
                            field.set("step",Json::number(int64_t(1)));
                            field.set("integer",Json::fromBool(true));
                        }
                    }
                    fields.push(std::move(field));
                }
                else fields.push(genericField("value",value));
                components.push(component(name.c_str(),name.c_str(),fields,
                    authorable?"Map authoring only; applies on next native game setup.":"Read-only in Studio."));
            }
        } else {
            result.set("kind",str(node.ref.source==editor::DocumentEntityRef::Source::ActorCollider?"Collider":"Spawn"));
            Json transform=Json::array();
            if(node.hasPosition) {
                transform.push(numericProp("x","Position X",Json::number(node.position.x,3)));
                transform.push(numericProp("y","Position Y",Json::number(node.position.y,3)));
                transform.push(numericProp("z","Position Z",Json::number(node.position.z,3)));
            }
            components.push(component("Transform","Transform",transform));
            for(const auto& descriptor:editor::documentComponents(ws_.map,node.ref)) {
                if(descriptor.kind==editor::DocumentComponentKind::Transform)continue;
                Json data,fields=Json::array();
                if(editor::readDocumentComponent(ws_.map,node.ref,descriptor.kind,data))
                    for(const auto& [key,item]:data.members())fields.push(genericField(key,item));
                components.push(component(descriptor.id,descriptor.title,fields,"Read-only in Studio."));
            }
        }
        result.set("components",components);
        return result;
    }
    throw std::runtime_error("Unknown document object");
}
Json StudioSession::setMeshRenderer(int index,const std::string& key,const Json& value) {
    if(index<0)throw std::runtime_error("Unknown document object");
    const auto& source=ws_.map.data.entities[size_t(index)];
    if(!editor::authoredEntity(source))
        throw std::runtime_error("This entity's mesh is defined by its type in rules.json");
    if(!value.isString())throw std::runtime_error("Mesh Renderer values are text");
    auto candidate=source;
    Json components=candidate.extra["components"];
    Json model=components["StaticModel"].isObject()?components["StaticModel"]:Json::object();
    if(key=="asset") {
        std::string path=value.str();
        if(path=="builtin:cube")path.clear(); // default primitive: no explicit asset
        if(!path.empty() && path!="builtin:sphere" && path.rfind("content/",0)!=0) {
            // The shared asset picker returns stable GUIDs; documents keep content paths.
            if(!studioValidGuid(path))throw std::runtime_error("Mesh must be a content/ path or asset GUID");
            StudioAssetCatalog catalog(ws_.project.dir);
            const auto entry=catalog.byGuid(path);
            if(!entry || entry->directory)throw std::runtime_error("Asset GUID not found; refresh Assets");
            path="content/"+entry->path;
        }
        if(path=="builtin:sphere") model.set("asset",str(path));
        else if(!path.empty()) {
            if(!studioModelAssetPath(path))throw std::runtime_error("Mesh must be a .glb, .gltf or .obj file in content/");
            if(!std::filesystem::is_regular_file(std::filesystem::path(ws_.project.dir)/path))
                throw std::runtime_error("Mesh file does not exist: "+path);
            model.set("asset",str(path));
        } else model.erase("asset");
    } else if(key.rfind("material:",0)==0 && key.size()>9 && key.size()<=200) {
        const std::string slot=key.substr(9);
        Json materials=model["materials"].isObject()?model["materials"]:Json::object();
        if(value.str().empty())materials.erase(slot);
        else {
            if(!editor::loadMaterialLibrary(ws_).has(value.str()))
                throw std::runtime_error("Unknown material: "+value.str());
            materials.set(slot,value);
        }
        if(materials.size())model.set("materials",materials); else model.erase("materials");
    } else throw std::runtime_error("Unknown Mesh Renderer property");
    // An empty StaticModel stays: it is the explicit Mesh Renderer drawing the built-in cube.
    components.set("StaticModel",model);
    candidate.extra.set("components",components);
    if(candidate.extra!=source.extra) {
        ws_.map.beginEdit();ws_.map.data.entities[size_t(index)]=std::move(candidate);ws_.map.touched();
    }
    return snapshot();
}
Json StudioSession::snapshot() const {
    Json result = Json::object();
    Json project = Json::object();
    project.set("name", str(ws_.project.manifest.getStr("name", "No project opened")));
    project.set("path", str(ws_.project.dir));
    project.set("dirty", Json::fromBool(ws_.map.dirty || ws_.project.rulesDirty || ws_.project.manifestDirty));
    result.set("project", std::move(project));
    Json scene = Json::object();
    scene.set("name", str(ws_.map.data.name));
    Json rows = Json::array();
    const auto nodes = editor::enumerateDocumentSceneNodes(ws_.map);
    for (const auto& node : nodes) {
        Json row = Json::object();
        row.set("id", str(nodeId(ws_.map,node.ref)));
        row.set("name", str(node.label));
        // UI receives only generic display kind; document source names remain native.
        row.set("kind", str(node.ref.source == editor::DocumentEntityRef::Source::MapEntity
            ? (node.ref.index < ws_.map.data.entities.size() ? ws_.map.data.entities[node.ref.index].type : "Entity")
            : node.ref.isLight()?"Light":node.ref.source == editor::DocumentEntityRef::Source::ActorCollider ? "Collider" : "Spawn"));
        row.set("visible", Json::fromBool(!hidden_.count(nodeId(ws_.map,node.ref))));
        if(locked_.count(nodeId(ws_.map,node.ref)))row.set("locked",Json::fromBool(true));
        if(node.ref.source==editor::DocumentEntityRef::Source::MapEntity && node.ref.index<ws_.map.data.entities.size())
            row.set("owner",num(int64_t(ws_.map.data.entities[node.ref.index].owner)));
        rows.push(std::move(row));
    }
    scene.set("entities", std::move(rows));
    result.set("scene", std::move(scene));
    Json fields = Json::array(), componentViews=Json::array(), entityFields=Json::array();
    bool valid = false;
    if (!selected_.empty()) {
        for (const auto& node : nodes) {
            if (nodeId(ws_.map,node.ref) != selected_) continue;
            valid = true;
            fields = propertiesFor(selected_);
            if(node.ref.isLight()) {
                // Light entities expose Entity/Transform/Light components over the
                // same canonical document object; no separate light state exists.
                for(const auto& descriptor:editor::documentComponents(ws_.map,node.ref)) {
                    Json component=Json::object(),properties=Json::array();
                    component.set("id",str(descriptor.id));component.set("title",str(descriptor.title));
                    for(const auto& field:fields.items()) {
                        const auto key=field.getStr("key");
                        if(key=="name")continue;
                        const bool transform=key=="x" || key=="z" || key=="height";
                        if(transform==(descriptor.kind==editor::DocumentComponentKind::Transform))properties.push(field);
                    }
                    component.set("properties",properties);componentViews.push(component);
                }
                entityFields.push(fields[0]);fields=Json::array();
            }
            break;
        }
    }
    result.set("selectedId", valid ? str(selected_) : Json());
    {Json ids=Json::array();for(const auto& id:selectedObjectIds())ids.push(str(id));result.set("selectedIds",std::move(ids));}
    result.set("properties", std::move(fields));
    result.set("components",std::move(componentViews));
    result.set("entityProperties",std::move(entityFields));
    if(valid) { try { result.set("inspector",inspectorFor(selected_)); } catch(const std::exception&) {} }
    Json logs = Json::array();
    const auto& activity = ws_.activity;
    for (size_t i = activity.size() > 100 ? activity.size() - 100 : 0; i < activity.size(); ++i) {
        Json row = Json::object();
        row.set("level", str(activity[i].error ? "error" : "info"));
        row.set("text", str(activity[i].text));
        row.set("time", str(activity[i].source));
        row.set("source", str(activity[i].source));
        logs.push(std::move(row));
    }
    result.set("logs", std::move(logs));
    Json history = Json::object();
    history.set("canUndo", Json::fromBool(ws_.map.canUndo()));
    history.set("canRedo", Json::fromBool(ws_.map.canRedo()));
    result.set("history", std::move(history));
    result.set("mode", str("edit"));
    result.set("previewMaterial", previewMaterialName_.empty() ? Json() : str(previewMaterialName_));
    return result;
}
void StudioSession::refreshMaterialPreview() {
    if (previewMaterialName_.empty()) {
        previewMaterialFactors_ = Json();
        ++previewMaterialRevision_;
        return;
    }
    Json request=Json::object();
    request.set("name",str(previewMaterialName_));
    const Json result=ws_.call("material_get",request,"studio");
    if(result.has("error")) throw std::runtime_error(result.getStr("error"));
    const Json& resolved=result["resolved"];
    if(!resolved.isObject()) throw std::runtime_error("Resolved PBR preview material missing");
    previewMaterialFactors_=resolved;
    ++previewMaterialRevision_;
}
Json StudioSession::saveMapAs(const std::string& path) {
    if (ws_.project.dir.empty() || path.empty()) throw std::runtime_error("Open a project and choose a map file");
    namespace fs=std::filesystem;
    const auto root=fs::weakly_canonical(ws_.project.dir);
    const auto target=fs::weakly_canonical(path);
    const auto relative=target.lexically_relative(root);
    if (relative.empty() || relative.is_absolute() || *relative.begin()!="maps" || target.extension()!=".json")
        throw std::runtime_error("Choose a .json map inside the current project maps folder");
    Json args=Json::object();args.set("file",str(target.string()));args.set("addToGame",Json::fromBool(true));
    const auto result=ws_.call("map_save",args,"studio");
    if (result.has("error")) throw std::runtime_error(result.getStr("error"));
    std::string error;
    if (ws_.project.rulesDirty && !ws_.project.saveRules(&error)) throw std::runtime_error(error);
    if (ws_.project.manifestDirty && !ws_.project.saveManifest(&error)) throw std::runtime_error(error);
    return snapshot();
}
Json StudioSession::openProject(const std::string& path) {
    if (path.empty()) throw std::runtime_error("Project path is empty");
    const auto directory=recoveredProjectDir(ws_,path);
    std::string error;
    if (!ws_.openGame(directory, &error))
        throw std::runtime_error(error.empty() ? "Unable to open project" : error);
    (void)studioContentRoot(ws_.project.dir);
    pendingAssetTransfer_.reset();
    clearSelection();hidden_.clear();locked_.clear();
    facialPreview_.reset(); // project identity may change; never reuse prior GUID
    previewMaterialName_.clear();
    refreshMaterialPreview();
    return snapshot();
}
Json StudioSession::previewExternalAssets(const std::vector<std::filesystem::path>& files,
                                          const std::string& destination) {
    if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
    pendingAssetTransfer_=StudioTransferPlan::external(ws_.project.dir,destination,files);
    return pendingAssetTransfer_->describe();
}
int StudioSession::selectedEntityIndex() const {
    if (selected_.empty()) return -1;
    try { return indexFor(selected_); }
    catch (const std::exception&) { return -1; }
}
namespace {
// Build panel targets (aiversebuild --target); "all" is left to the command line.
const char* const kBuildTargets[][2] = {
    {"macos", "macOS (.app)"}, {"ios", "iOS"}, {"android", "Android"}, {"web", "Web (HTML5)"},
    {"windows", "Windows (.exe)"}, {"linux", "Linux"}};
bool knownBuildTarget(const std::string& target) {
    for (const auto& t : kBuildTargets) if (target == t[0]) return true;
    return false;
}
}
// Publish / Build: aiversebuild runs in the background through the Workspace build runner (the same
// one the legacy Build window and the MCP build_start use); the panel polls the log by offset.
namespace {
std::string shellQuote(const std::string& s) {
    std::string o="'";
    for(char c:s){if(c=='\'')o+="'\\''";else o+=c;}
    return o+"'";
}
std::string clipText(const std::string& s,size_t n){return s.size()<=n?s:s.substr(0,n)+"…";}
}
// Agent link, protocol 2 (docs/mcp-agent-link.md): hello + token, then {"method":"call"} lines from
// aiversemcp. Every call runs through ws_.call with source "agent": validated, logged, undoable; each
// MapDoc snapshot it adds becomes a History step "Agent · <op>" and an entry of the panel's change list.
bool StudioSession::pollAgentLink() {
    if(!agentServer_.listening())return false;
    bool changed=false;
    auto reply=[](const Json& id,Json result){Json r=Json::object();r.set("id",id);r.set("result",std::move(result));return r;};
    auto error=[](const std::string& why){Json e=Json::object();e.set("error",str(why));return e;};
    std::vector<int> dropped;
    for(auto& m:agentServer_.poll()){
        if(std::find(dropped.begin(),dropped.end(),m.client)!=dropped.end())continue;
        Json msg;
        try{msg=Json::parse(m.line);}catch(...){agentServer_.drop(m.client);dropped.push_back(m.client);continue;}
        if(!msg.isObject()){agentServer_.drop(m.client);dropped.push_back(m.client);continue;}
        const Json& id=msg["id"];
        const std::string method=msg.getStr("method");
        if(method=="hello"){
            if(!agentServer_.tokenPublished()||!editor::net::tokenEquals(msg.getStr("token"),agentServer_.token())){
                agentServer_.send(m.client,reply(id,error("agent link: wrong token")).dump(-1));
                agentServer_.drop(m.client);dropped.push_back(m.client);continue;
            }
            agentServer_.setAuthenticated(m.client);
            Json ok=Json::object();ok.set("ok",Json::fromBool(true));ok.set("protocol",num(int64_t(2)));
            agentServer_.send(m.client,reply(id,ok).dump(-1));
            continue;
        }
        if(!agentServer_.authenticated(m.client)){
            agentServer_.send(m.client,reply(id,error("agent link: send hello with the token first")).dump(-1));
            agentServer_.drop(m.client);dropped.push_back(m.client);continue;
        }
        if(method!="call"){agentServer_.send(m.client,reply(id,error("unknown method")).dump(-1));continue;}
        const std::string name=msg["params"].getStr("name");
        const Json args=msg["params"]["arguments"].isObject()?msg["params"]["arguments"]:Json::object();
        if(name=="view_screenshot"){
            // Answered by the host with the Scene's next frame (finishAgentScreenshot).
            if(!agentShots_.empty()){agentServer_.send(m.client,reply(id,error("another screenshot is pending; retry after its reply")).dump(-1));continue;}
            agentShots_.push_back({m.client,id});
            agentLines_.push_back("→ view_screenshot");
            continue;
        }
        syncHistoryLabels();
        const size_t before=ws_.map.undoDepth(),entities=ws_.map.data.entities.size();
        const bool wasDirty=ws_.map.dirty||ws_.project.rulesDirty||ws_.project.manifestDirty;
        const Json result=ws_.call(name,args,"agent");
        const bool failed=result.has("error");
        const bool undoable=ws_.map.undoDepth()>before;
        if(undoable){
            for(size_t i=before;i<ws_.map.undoDepth();++i)undoLabels_.push_back("Agent · "+studioToolLabel(name));
            redoLabels_.clear();
        }
        syncHistoryLabels();
        if(ws_.map.data.entities.size()!=entities){clearSelection();dropOrdinalFlags();}
        const bool dirty=ws_.map.dirty||ws_.project.rulesDirty||ws_.project.manifestDirty;
        const bool edit=!failed&&(undoable||dirty!=wasDirty||!studioToolReadOnly(name));
        if(edit)changed=true;
        if(edit||failed){
            Json c=Json::object();
            c.set("op",str(name));c.set("args",str(clipText(args.dump(-1),160)));
            c.set("ok",Json::fromBool(!failed));c.set("undoable",Json::fromBool(undoable));
            c.set("position",num(int64_t(undoLabels_.size())));
            if(failed)c.set("error",str(clipText(result.getStr("error"),300)));
            agentChanges_.push(std::move(c));
        }
        agentServer_.send(m.client,reply(id,result).dump(-1));
    }
    for(auto& line:agentRun_.takeLines())agentLines_.push_back(line);
    if(agentLines_.size()>3000)agentLines_.erase(agentLines_.begin(),agentLines_.begin()+1000);
    return changed;
}
namespace {
bool brushPathKind(const std::string& k){return k=="road"||k=="river";}
const char* brushLabel(const std::string& k){
    return k=="terrain"?"Terrain brush":k=="paint"?"Paint terrain":k=="tiles"?"Paint tiles":k=="field"?"Paint field":
           k=="objects"?"Scatter objects":k=="road"?"Draw road":k=="river"?"Draw river":"Brush";
}
}
StudioLuaContext StudioSession::luaContext() const {
    StudioLuaContext c;
    const auto ids=selectedObjectIds();
    c.selectionCount=int(ids.size());
    c.selectionIndex=ids.empty()?-1:indexFor(ids.back());
    c.objectCount=int(ws_.map.data.entities.size());
    c.mapWidth=ws_.map.data.w;c.mapHeight=ws_.map.data.h;
    return c;
}
namespace {
// Stable handle key of a water body: its numeric id (water_network), else its position in the list.
std::string waterKey(const Json& body,size_t index){
    if(body["id"].isNumber())return std::to_string(body["id"].integer());
    const std::string s=body.getStr("id");
    return s.empty()?"#"+std::to_string(index):s;
}
}
Json StudioSession::splineHandles() {
    Json out=Json::object(),handles=Json::array(),segments=Json::array();
    auto seg=[&](double x0,double z0,double x1,double z1){Json s=Json::array();for(double v:{x0,z0,x1,z1})s.push(Json::number(v,3));segments.push(std::move(s));};
    if(splineEdit_=="road"){
        const Json net=ws_.call("road_network",Json::object(),"editor");
        std::map<int64_t,std::pair<double,double>> at;
        for(const auto& n:net["nodes"].items()){
            const int64_t id=n["id"].integer();at[id]={n["x"].toDouble(),n["z"].toDouble()};
            Json h=Json::object();h.set("id",str("road:"+std::to_string(id)));h.set("x",n["x"]);h.set("z",n["z"]);handles.push(std::move(h));
        }
        for(const auto& e:net["edges"].items()){
            const auto a=at.find(e["a"].integer()),b=at.find(e["b"].integer());
            if(a==at.end()||b==at.end())continue;
            const Json& c1=e["c1"];const Json& c2=e["c2"];
            if(c1.isArray()&&c2.isArray()&&c1.size()>=2&&c2.size()>=2){
                // Bezier a, c1, c2, b sampled in 12 pieces.
                double px=a->second.first,pz=a->second.second;
                for(int i=1;i<=12;++i){
                    const double t=i/12.0,u=1-t;
                    const double x=u*u*u*a->second.first+3*u*u*t*c1[0].toDouble()+3*u*t*t*c2[0].toDouble()+t*t*t*b->second.first;
                    const double z=u*u*u*a->second.second+3*u*u*t*c1[1].toDouble()+3*u*t*t*c2[1].toDouble()+t*t*t*b->second.second;
                    seg(px,pz,x,z);px=x;pz=z;
                }
            } else seg(a->second.first,a->second.second,b->second.first,b->second.second);
        }
    } else if(splineEdit_=="river"){
        const Json net=ws_.call("water_network",Json::object(),"editor");
        size_t bi=0;
        for(const auto& body:net["bodies"].items()){
            const std::string key=waterKey(body,bi);
            const Json& pts=body.has("points")?body["points"]:body["shore"];
            for(size_t i=0;i<pts.size();++i){
                Json h=Json::object();h.set("id",str("water:"+key+":"+std::to_string(i)));
                h.set("x",pts[i][0]);h.set("z",pts[i][1]);handles.push(std::move(h));
                if(i>0)seg(pts[i-1][0].toDouble(),pts[i-1][1].toDouble(),pts[i][0].toDouble(),pts[i][1].toDouble());
            }
            ++bi;
        }
    }
    out.set("kind",str(splineEdit_));out.set("handles",handles);out.set("segments",segments);
    return out;
}
bool StudioSession::splineMove(const std::string& id, double x, double z, std::string* error) {
    if(!std::isfinite(x)||!std::isfinite(z)){if(error)*error="invalid position";return false;}
    syncHistoryLabels();
    const size_t before=ws_.map.undoDepth();
    Json r;std::string label;
    if(id.rfind("road:",0)==0){
        Json p=Json::object();p.set("node",num(int64_t(std::stoll(id.substr(5)))));
        p.set("x",Json::number(x,3));p.set("z",Json::number(z,3));
        r=ws_.call("road_node_set",p,"studio");label="Move road node";
    } else if(id.rfind("water:",0)==0){
        const size_t colon=id.rfind(':');
        const std::string key=id.substr(6,colon-6);const size_t index=size_t(std::stoul(id.substr(colon+1)));
        const Json net=ws_.call("water_network",Json::object(),"editor");
        size_t bi=0;Json body;
        for(const auto& b:net["bodies"].items()){
            if(waterKey(b,bi)==key){body=b;break;}
            ++bi;
        }
        if(!body.isObject()){if(error)*error="water body not found";return false;}
        const char* field=body.has("points")?"points":"shore";
        Json pts=body[field];
        if(index>=pts.size()){if(error)*error="water point not found";return false;}
        Json pt=pts[index];
        Json moved=Json::array();moved.push(Json::number(x,2));moved.push(Json::number(z,2));
        for(size_t k=2;k<pt.size();++k)moved.push(pt[k]);   // keep the width
        pts.at(index)=std::move(moved);
        body.set(field,std::move(pts));
        Json p=Json::object();p.set("body",std::move(body));
        r=ws_.call("water_set",p,"studio");label="Move river point";
    } else {if(error)*error="unknown handle";return false;}
    if(r.has("error")){if(error)*error=r.getStr("error");return false;}
    if(ws_.map.undoDepth()>before){
        ws_.map.mergeUndoSince(before);
        while(undoLabels_.size()>before)undoLabels_.pop_back();
        undoLabels_.push_back(label);redoLabels_.clear();
    }
    syncHistoryLabels();
    return true;
}
Json StudioSession::sceneBrushJson() const {
    const auto& b=brush_;
    Json o=Json::object();
    o.set("armed",Json::fromBool(b.armed));o.set("kind",str(b.kind));o.set("op",str(b.op));
    o.set("radius",Json::number(b.radius,3));o.set("strength",Json::number(b.strength,3));
    o.set("layer",num(int64_t(b.layer)));o.set("tile",str(b.tile));o.set("field",str(b.field));o.set("fieldMode",str(b.fieldMode));
    o.set("value",Json::number(b.value,3));o.set("type",str(b.type));o.set("owner",num(int64_t(b.owner)));
    o.set("count",num(int64_t(b.count)));o.set("spread",Json::number(b.spread,3));
    o.set("roadType",str(b.roadType));o.set("width",Json::number(b.width,3));
    return o;
}
bool StudioSession::strokeDab(double x, double z, std::string* error) {
    if(!stroking_)return false;
    const auto& b=brush_;
    const double d=std::hypot(x-strokeX_,z-strokeY_);
    if(brushPathKind(b.kind)){
        // Path tools: a point every two cells; the op runs once on release.
        if(d<2.0 && strokePoints_.size()>0)return true;
        Json pt=Json::array();pt.push(Json::number(x,2));pt.push(Json::number(z,2));
        if(b.kind=="river")pt.push(Json::number(b.width,2));
        strokePoints_.push(std::move(pt));strokeX_=x;strokeY_=z;
        return true;
    }
    // Dabs closer than about a third of the radius add little but cost a full map snapshot each.
    const double spacing=b.kind=="objects"?std::max(1.0,b.radius):b.kind=="tiles"||b.kind=="field"?std::max(1.0,b.radius*0.5):b.radius*0.33;
    if(d<spacing && strokeDabs_>0)return true;
    Json p=Json::object();std::string op;
    if(b.kind=="terrain"){op="terrain_sculpt";p.set("op",str(b.op));p.set("x",Json::number(x,3));p.set("y",Json::number(z,3));
        p.set("radius",Json::number(b.radius,3));p.set("strength",Json::number(b.strength,3));}
    else if(b.kind=="paint"){op="terrain_paint_material";p.set("layer",num(int64_t(b.layer)));p.set("x",Json::number(x,3));p.set("y",Json::number(z,3));
        p.set("radius",Json::number(b.radius,3));p.set("strength",Json::number(std::min(1.0,b.strength),3));}
    else if(b.kind=="tiles"){op="tile_paint";p.set("terrain",str(b.tile));p.set("x",num(int64_t(std::floor(x))));p.set("y",num(int64_t(std::floor(z))));
        p.set("radius",num(int64_t(std::lround(b.radius))));}
    else if(b.kind=="field"){op="field_paint";p.set("id",str(b.field));p.set("x",num(int64_t(std::floor(x))));p.set("z",num(int64_t(std::floor(z))));
        p.set("radius",num(int64_t(std::lround(b.radius))));p.set("value",Json::number(b.value,3));p.set("mode",str(b.fieldMode));}
    else if(b.kind=="objects"){op="entity_place";p.set("type",str(b.type));p.set("owner",num(int64_t(b.owner)));
        p.set("x",Json::number(x,3));p.set("z",Json::number(z,3));
        if(b.count>1){p.set("count",num(int64_t(b.count)));p.set("spread",Json::number(b.spread,3));}}
    else {if(error)*error="Unknown brush";return false;}
    const Json r=ws_.call(op,p,"editor");   // "editor": no Console line per dab; one line per stroke at the end
    if(r.has("error")){if(error)*error=r.getStr("error");return false;}
    strokeX_=x;strokeY_=z;++strokeDabs_;
    return true;
}
bool StudioSession::strokeBegin(double x, double z, std::string* error) {
    if(!brush_.armed||ws_.project.dir.empty())return false;
    const auto& b=brush_;
    if((b.kind=="tiles"&&b.tile.empty())||(b.kind=="field"&&b.field.empty())||(b.kind=="objects"&&b.type.empty())||(b.kind=="road"&&b.roadType.empty())){
        if(error)*error=std::string("choose a ")+(b.kind=="tiles"?"tile terrain":b.kind=="field"?"field":b.kind=="objects"?"type":"road type")+" first";
        return false;
    }
    syncHistoryLabels();
    strokeDepth_=ws_.map.undoDepth();stroking_=true;strokeDabs_=0;strokePoints_=Json::array();
    strokeX_=x+1e9;strokeY_=z+1e9;   // the first dab always applies
    if(!strokeDab(x,z,error)){stroking_=false;return false;}
    return true;
}
bool StudioSession::strokeEnd(std::string* error) {
    if(!stroking_)return true;
    stroking_=false;
    const auto& b=brush_;
    bool ok=true;
    if(brushPathKind(b.kind)){
        if(strokePoints_.size()<2){if(error)*error="drag further to draw a "+b.kind;return false;}
        Json p=Json::object();std::string op;
        if(b.kind=="road"){op="road_add";p.set("points",strokePoints_);p.set("type",str(b.roadType));p.set("curve",str("smooth"));}
        else {op="water_set";Json body=Json::object();body.set("kind",str("river"));body.set("points",strokePoints_);p.set("body",std::move(body));}
        const Json r=ws_.call(op,p,"editor");
        if(r.has("error")){if(error)*error=r.getStr("error");ok=false;}
        else strokeDabs_=int(strokePoints_.size());
    }
    if(ws_.map.undoDepth()>strokeDepth_){
        ws_.map.mergeUndoSince(strokeDepth_);
        while(undoLabels_.size()>strokeDepth_)undoLabels_.pop_back();
        undoLabels_.push_back(brushLabel(b.kind));redoLabels_.clear();
        ws_.log("studio",std::string(brushLabel(b.kind))+(brushPathKind(b.kind)?" · "+std::to_string(strokeDabs_)+" points":" · "+std::to_string(strokeDabs_)+" dabs"));
    }
    syncHistoryLabels();
    return ok;
}
void StudioSession::finishAgentScreenshot(const std::string& pngBase64, int width, int height, const std::string& why) {
    for(const auto& shot:agentShots_){
        Json r=Json::object(),result=Json::object();
        if(pngBase64.empty())result.set("error",str(why.empty()?std::string("nothing to capture"):why));
        else {
            result.set("png_base64",str(pngBase64));
            result.set("width",num(int64_t(width)));result.set("height",num(int64_t(height)));
            result.set("view",str("Studio Scene panel as the user sees it (camera unchanged)"));
        }
        r.set("id",shot.id);r.set("result",std::move(result));
        agentServer_.send(shot.client,r.dump(-1));
    }
    agentShots_.clear();
}
Json StudioSession::agentRequest(const std::string& method, const Json& params) {
    namespace fs=std::filesystem;
    if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
    if(!agentServer_.listening()&&!agentServer_.listenAvailable())
        throw std::runtime_error("Could not open a local port for the agent link");
    if(method=="studio.agentStop"){agentRun_.stop();}
    else if(method=="studio.agentReset"){
        if(agentRun_.running())throw std::runtime_error("Stop the agent first");
        agentSession_.clear();agentLines_.clear();agentChanges_=Json::array();agentRunStart_=-1;
    } else if(method=="studio.agentStart"){
        const std::string prompt=params.getStr("prompt");
        if(prompt.empty()||prompt.size()>20000)throw std::runtime_error("Write a request for the agent (up to 20000 characters)");
        if(agentRun_.running())throw std::runtime_error("The agent is still working");
        if(mcpTool_.empty()||!fs::exists(mcpTool_))throw std::runtime_error("aiversemcp was not found next to Studio (build the aiversemcp target)");
        if(editor::agentCliPath("claude-code").empty())throw std::runtime_error("Claude Code CLI (claude) was not found: install it and sign in, then try again");
        if(!agentRun_.sessionId().empty())agentSession_=agentRun_.sessionId();
        syncHistoryLabels();
        if(agentSession_.empty()||agentRunStart_<0)agentRunStart_=int64_t(undoLabels_.size());
        const fs::path dir=fs::path(ws_.logsDir())/"studio-agent";
        std::error_code ec;fs::create_directories(dir,ec);
        // aiversemcp of this build, pointed at this Studio's port; strict: the user's global "aiverse" entry
        // (the ImGui editor's port) is not loaded for this run.
        Json server=Json::object(),argv=Json::array(),servers=Json::object(),config=Json::object();
        server.set("command",str(mcpTool_));
        for(const std::string& a:{std::string("--engine"),ws_.engineDir,std::string("--port"),std::to_string(agentServer_.port())})argv.push(str(a));
        server.set("args",argv);servers.set("aiverse",server);config.set("mcpServers",servers);
        const fs::path configFile=dir/"mcp.json",taskFile=dir/"task.md",logFile=dir/"run.log";
        {std::ofstream(configFile)<<config.dump(2);}
        {
            std::ofstream task(taskFile);
            if(agentSession_.empty())
                task<<"You are the agent inside AIVERSE Studio. The user is looking at the open project and map.\n"
                      "Project: "<<ws_.project.dir<<"\nMap: "<<(ws_.map.path.empty()?std::string("(unsaved)"):ws_.map.path)<<"\n"
                      "Change the game only through the aiverse MCP tools (mcp__aiverse__*): they act on this Studio session, "
                      "every map edit is one undo step in the History panel and shows in the Scene at once. The project is "
                      "already open: do not call open_game, map_open or map_new unless asked. Do not save the map; the user "
                      "decides. Keep edits small and say at the end, in two or three sentences, what you changed.\n\n";
            // What the user is looking at right now: the selection and recent errors of the Console.
            const auto ids=selectedObjectIds();
            if(!ids.empty()){
                task<<"Selected in the Scene:";
                for(size_t i=0;i<ids.size()&&i<20;++i){
                    const int index=indexFor(ids[i]);
                    if(index>=0&&index<int(ws_.map.data.entities.size())){
                        const auto& e=ws_.map.data.entities[size_t(index)];
                        task<<" #"<<index<<" "<<e.type<<" (owner "<<e.owner<<", x "<<e.x<<", z "<<e.z<<")"<<(i+1<ids.size()?";":"");
                    } else task<<" "<<ids[i]<<(i+1<ids.size()?";":"");
                }
                task<<"\n";
            }
            std::vector<std::string> errors;
            for(auto it=ws_.activity.rbegin();it!=ws_.activity.rend()&&errors.size()<8;++it)if(it->error)errors.push_back(it->source+": "+it->text);
            if(!errors.empty()){task<<"Recent errors in the Studio Console (newest first):\n";for(const auto& e:errors)task<<"- "<<clipText(e,300)<<"\n";}
            task<<"Request:\n"<<prompt<<"\n";
        }
        std::string extra="--mcp-config "+shellQuote(configFile.string())+" --strict-mcp-config";
        if(!agentSession_.empty())extra+=" --resume "+shellQuote(agentSession_);
        std::string err;
        if(!agentRun_.start("claude-code",ws_.project.dir,taskFile.string(),logFile.string(),&err,extra))throw std::runtime_error(err);
        agentLines_.push_back("› "+clipText(prompt,2000));
        ws_.log("agent","Агент: "+clipText(prompt,120));
    }
    for(auto& line:agentRun_.takeLines())agentLines_.push_back(line);
    Json out=Json::object(),lines=Json::array();
    const int64_t from=std::max<int64_t>(0,params["from"].integer(0));
    for(size_t i=size_t(std::min<int64_t>(from,int64_t(agentLines_.size())));i<agentLines_.size();++i)lines.push(str(agentLines_[i]));
    out.set("lines",lines);out.set("total",num(int64_t(agentLines_.size())));
    out.set("running",Json::fromBool(agentRun_.running()));
    out.set("port",num(int64_t(agentServer_.port())));
    out.set("clients",num(int64_t(agentServer_.clients())));
    out.set("changes",agentChanges_);
    out.set("startPosition",num(agentRunStart_));
    static const std::string cli=editor::agentCliPath("claude-code");   // a login-shell lookup: once per run of Studio
    out.set("cli",str(cli));
    out.set("mcp",Json::fromBool(!mcpTool_.empty()));
    out.set("conversation",Json::fromBool(!agentSession_.empty()||!agentRun_.sessionId().empty()));
    if(agentRun_.finished())out.set("result",str(agentRun_.result()));
    return out;
}
Json StudioSession::buildRequest(const std::string& method, const Json& params) {
    namespace fs = std::filesystem;
    if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
    const fs::path builds = fs::path(ws_.project.dir) / "Builds";
    if (method == "studio.buildInfo") {
        Json out = Json::object(), targets = Json::array();
        for (const auto& t : kBuildTargets) {
            Json v = Json::object();
            v.set("id", str(t[0])); v.set("name", str(t[1]));
            std::error_code ec;
            v.set("built", Json::fromBool(fs::is_directory(builds / t[0], ec)));
            targets.push(std::move(v));
        }
        out.set("targets", targets);
        std::error_code ec;
        const bool toolFound = !ws_.buildTool.empty() && fs::is_regular_file(ws_.buildTool, ec);
        out.set("toolFound", Json::fromBool(toolFound));
        out.set("outDir", str(builds.string()));
        out.set("running", Json::fromBool(ws_.building()));
        out.set("label", str(ws_.runLabel()));
        out.set("exitCode", Json::number(int64_t(ws_.buildResult())));
        out.set("runs", Json::number(int64_t(ws_.runCount())));
        out.set("dirty", Json::fromBool(ws_.map.dirty || ws_.project.rulesDirty || ws_.project.manifestDirty));
        const Json& m = ws_.project.manifest;
        Json identity = Json::object();
        identity.set("name", str(m.getStr("name"))); identity.set("id", str(m.getStr("id")));
        identity.set("version", str(m.getStr("version"))); identity.set("build", Json::number(m["build"].integer(0)));
        identity.set("orientation", str(m.getStr("orientation")));
        out.set("identity", std::move(identity));
        return out;
    }
    const std::string target = params.getStr("target");
    if (method == "studio.buildToolchain") {
        if (!knownBuildTarget(target)) throw std::runtime_error("Unknown build target");
        return ws_.toolchainCheck(target);
    }
    if (method == "studio.buildStart") {
        if (!knownBuildTarget(target)) throw std::runtime_error("Unknown build target");
        const std::string config = params.getStr("config", "Release");
        if (config != "Release" && config != "Debug") throw std::runtime_error("Unknown build configuration");
        // A build packs the files on disk: never save implicitly from here.
        if (ws_.map.dirty || ws_.project.rulesDirty || ws_.project.manifestDirty)
            throw std::runtime_error("Save the project before building");
        if (params.getBool("estimate"))
            ws_.startBuild(target, config, "--estimate --out '" + (builds / ".temp" / "estimate").string() + "'");
        else ws_.startBuild(target, config, params.getBool("installMissing") ? "--install-missing" : "");
        Json out = Json::object(); out.set("started", Json::fromBool(true));
        return out;
    }
    if (method == "studio.buildLog") {
        const std::string log = ws_.buildLog();
        // Offsets are UTF-8 bytes (the log is Russian/English text): never split a character,
        // the reply must stay valid UTF-8. The client continues from `next`.
        size_t from = size_t(std::clamp<int64_t>(params["from"].integer(0), 0, int64_t(log.size())));
        while (from < log.size() && (static_cast<unsigned char>(log[from]) & 0xC0) == 0x80) ++from;
        constexpr size_t kChunk = 256 * 1024;
        size_t end = std::min(log.size(), from + kChunk);
        while (end < log.size() && end > from && (static_cast<unsigned char>(log[end]) & 0xC0) == 0x80) --end;
        Json out = Json::object();
        out.set("from", Json::number(int64_t(from)));
        out.set("next", Json::number(int64_t(end)));
        out.set("text", str(log.substr(from, end - from)));
        out.set("size", Json::number(int64_t(log.size())));
        out.set("running", Json::fromBool(ws_.building()));
        out.set("label", str(ws_.runLabel()));
        out.set("exitCode", Json::number(int64_t(ws_.buildResult())));
        out.set("runs", Json::number(int64_t(ws_.runCount())));
        out.set("logFile", str(ws_.lastLogFile()));
        return out;
    }
    // studio.buildEstimate: the size report of the last estimate run for `target`.
    if (!knownBuildTarget(target)) throw std::runtime_error("Unknown build target");
    const fs::path report = builds / ".temp" / "estimate" / target / "size-report.json";
    std::error_code ec;
    if (!fs::is_regular_file(report, ec)) throw std::runtime_error("No size estimate yet for " + target);
    const Json rep = Json::parseFile(report.string());
    Json out = Json::object();
    out.set("target", str(target));
    out.set("pack", rep["pack"]); out.set("atlas", rep["atlas"]);
    if (rep.has("split")) out.set("split", rep["split"]);
    std::vector<const Json*> all;
    for (const auto& f : rep["files"].items()) all.push_back(&f);
    std::stable_sort(all.begin(), all.end(), [](const Json* a, const Json* b) { return (*a)["storedBytes"].integer() > (*b)["storedBytes"].integer(); });
    Json files = Json::array();
    for (size_t i = 0; i < all.size() && i < 25; ++i) files.push(*all[i]);
    out.set("heaviest", files);
    return out;
}
std::string StudioSession::historyLabel(const std::string& method, const Json& params) {
    const Json& values=params["values"];
    auto has=[&](std::initializer_list<const char*> keys){for(const char* k:keys)if(values.has(k)||params.getStr("key")==k)return true;return false;};
    if(method=="studio.setProperties"||method=="studio.setProperty") {
        if(has({"rotationX","rotationY","rotationZ"}))return "Rotate";
        if(has({"scaleX","scaleY","scaleZ"}))return "Scale";
        if(has({"x","y","z"}))return "Move";
        const std::string key=params.getStr("key");
        const std::string comp=params.getStr("componentId");
        return key.empty() ? "Edit "+(comp.empty()?std::string("properties"):comp) : "Set "+key;
    }
    static const std::pair<const char*,const char*> names[]={
        {"studio.entityCreate","Create object"},{"studio.entityDelete","Delete"},{"studio.entitiesDelete","Delete"},
        {"studio.entityDuplicate","Duplicate"},{"studio.entitiesDuplicate","Duplicate"},{"studio.entityMove","Reorder"},
        {"studio.assetPlace","Place asset"},{"studio.componentAdd","Add component"},{"studio.componentRemove","Remove component"},
        {"studio.lightAdd","Add light"},{"studio.lightUpdate","Edit light"},{"studio.lightDelete","Delete light"},
        {"studio.lightingSet","Edit lighting"},{"studio.renderLightingSet","Edit render lighting"},
        {"studio.nereidFlowSet","Edit water"},{"studio.keepPlayChanges","Keep play changes"},{"studio.entitiesAlign","Align"},{"studio.newMap","New map"}};
    for(const auto& [m,label]:names)if(method==m)return label;
    if(method=="studio.op")return studioToolLabel(params.getStr("op"));
    return "Edit";
}
void StudioSession::syncHistoryLabels() {
    // Edits made outside this session (agent tools) or a trimmed MapDoc stack keep the counts honest.
    const size_t u=ws_.map.undoDepth(),r=ws_.map.redoDepth();
    while(undoLabels_.size()>u)undoLabels_.erase(undoLabels_.begin());
    while(undoLabels_.size()<u)undoLabels_.insert(undoLabels_.begin(),"Edit");
    while(redoLabels_.size()>r)redoLabels_.erase(redoLabels_.begin());
    while(redoLabels_.size()<r)redoLabels_.insert(redoLabels_.begin(),"Edit");
}
Json StudioSession::historyJson() const {
    Json out=Json::object(),steps=Json::array();
    for(const auto& l:undoLabels_){Json s=Json::object();s.set("label",str(l));s.set("done",Json::fromBool(true));steps.push(std::move(s));}
    for(auto it=redoLabels_.rbegin();it!=redoLabels_.rend();++it){Json s=Json::object();s.set("label",str(*it));s.set("done",Json::fromBool(false));steps.push(std::move(s));}
    out.set("steps",steps);out.set("position",Json::number(int64_t(undoLabels_.size())));
    return out;
}
Json StudioSession::dispatch(const Json& request) {
    const std::string method=request.getStr("method");
    if(method=="studio.history"||method=="studio.historyJump") {
        Json reply=Json::object();reply.set("id",request["id"]);
        try {
            syncHistoryLabels();
            if(method=="studio.historyJump") {
                // Undo / redo until `position` steps are applied (one step per call of the MapDoc).
                const int64_t target=request["params"]["position"].integer(-1);
                if(target<0||target>int64_t(undoLabels_.size()+redoLabels_.size()))throw std::runtime_error("Invalid history position");
                while(int64_t(undoLabels_.size())>target && ws_.map.canUndo()){ws_.map.undo();redoLabels_.push_back(undoLabels_.back());undoLabels_.pop_back();}
                while(int64_t(undoLabels_.size())<target && ws_.map.canRedo()){ws_.map.redo();undoLabels_.push_back(redoLabels_.back());redoLabels_.pop_back();}
                clearSelection();dropOrdinalFlags();
                Json out=historyJson();out.set("snapshot",snapshot());
                reply.set("result",std::move(out));
            } else reply.set("result",historyJson());
        } catch(const std::exception& e){reply.set("error",str(e.what()));}
        return reply;
    }
    syncHistoryLabels();
    const size_t before=ws_.map.undoDepth();
    Json reply=dispatchImpl(request);
    if(reply.has("error"))return reply;
    if(method=="studio.undo"){ if(!undoLabels_.empty()){redoLabels_.push_back(undoLabels_.back());undoLabels_.pop_back();} }
    else if(method=="studio.redo"){ if(!redoLabels_.empty()){undoLabels_.push_back(redoLabels_.back());redoLabels_.pop_back();} }
    else if(method=="studio.openMap"||method=="studio.newMap"||method=="studio.openProject"){undoLabels_.clear();redoLabels_.clear();}
    else if(ws_.map.undoDepth()>before) {
        // A new edit (one or more MapDoc snapshots) drops the redo branch.
        for(size_t i=before;i<ws_.map.undoDepth();++i)undoLabels_.push_back(historyLabel(method,request["params"]));
        redoLabels_.clear();
    }
    syncHistoryLabels();
    return reply;
}
Json StudioSession::dispatchImpl(const Json& request) {
    Json reply = Json::object();
    reply.set("id", request["id"]);
    try {
        if (!request.isObject() || !request["id"].isNumber()) throw std::runtime_error("Invalid request envelope");
        const std::string method = request.getStr("method");
        Json params = request["params"];
        if((method=="studio.lightUpdate" || method=="studio.lightDelete") && params.has("entityId")) {
            if(!params["entityId"].isString())throw std::runtime_error("Invalid light entity ID");
            bool found=false;
            for(const auto& node:editor::enumerateDocumentSceneNodes(ws_.map))
                if(node.ref.isLight() && nodeId(ws_.map,node.ref)==params.getStr("entityId")) {
                    params.set("index",num(int64_t(node.ref.index)));found=true;break;
                }
            if(!found)throw std::runtime_error("Light entity no longer exists");
        }
        if (method == "studio.hubTemplates") {   // StudioHost runs this off the UI thread; kept for other hosts
            reply.set("result",hubTemplatesJson(ws_.engineDir,params));
            return reply;
        }
        if (method == "studio.hub") {
            // Project hub: recent projects (shared with the ImGui editor and aiversemcp) and the engine templates.
            Json out=Json::object(),recent=Json::array(),templates=Json::array();
            for (const auto& p:editor::recentProjects()) {
                // Test fixtures and other temporary folders are not projects anyone wants to reopen.
                if (p.dir.find("/tmp/")!=std::string::npos||p.dir.find("/var/folders/")!=std::string::npos||p.dir.find("/T/")!=std::string::npos) continue;
                Json o=Json::object();o.set("dir",str(p.dir));o.set("name",str(p.name));
                o.set("lastOpened",num(int64_t(p.lastOpened)));o.set("exists",Json::fromBool(p.exists));
                recent.push(std::move(o));
            }
            // official catalog templates from the package cache (instant); the hub then asks
            // studio.hubTemplates for a fresh list and for the rest of the catalog
            for (const auto& t:editor::hubTemplates(ws_.engineDir,true))templates.push(templateJson(t));
            out.set("recent",recent);out.set("templates",templates);out.set("defaultLocation",str(editor::defaultProjectsDir()));
            reply.set("result",std::move(out));
            return reply;
        }
        if (method == "studio.snapshot") {
            reply.set("result", snapshot());
        } else if(method=="studio.humanPreviewAnimation") {
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            if(params.has("settings"))facialPreview_.previewAnimation=vivid::HumanPreviewAnimation::fromJson(params["settings"]);
            reply.set("result",facialPreview_.previewAnimation.toJson());
        } else if(method=="studio.humanStudioLighting") {
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            if(params.has("settings"))facialPreview_.studioLighting=vivid::HumanStudioLighting::fromJson(params["settings"]);
            reply.set("result",facialPreview_.studioLighting.describe());
        } else if(method=="studio.humanFaceFitScan" || method=="studio.humanHeadBodyImport" || method=="studio.humanWardrobeInfo" || method=="studio.humanWardrobeFromAsset" || method=="studio.humanCharacterBuild" || method=="studio.humanCharacterBuildStatus") {
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            if(method=="studio.humanCharacterBuildStatus") {
                if(humanBuild_.valid()&&humanBuild_.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
                    try{humanBuildResult_=humanBuild_.get();}catch(const std::exception& e){humanBuildResult_=Json::object();humanBuildResult_.set("error",Json::string(e.what()));}
                }
                auto result=humanBuildProject_==ws_.project.dir?humanBuildResult_:Json::object();
                result.set("running",Json::fromBool(humanBuild_.valid()));
                if(!humanBuild_.valid()&&result.has("path")) {StudioAssetCatalog catalog(ws_.project.dir);catalog.rebuild();const auto asset=catalog.byPath(result.getStr("path"));if(asset)result.set("guid",Json::string(asset->guid));}
                reply.set("result",result);
            } else {
                StudioAssetCatalog catalog(ws_.project.dir);catalog.rebuild();const auto source=StudioFacialPreviewBinding::checkedModelPath(catalog,params.getStr("guid"));
                if(method=="studio.humanFaceFitScan" || method=="studio.humanHeadBodyImport") {
                    if(humanBuild_.valid())throw std::runtime_error("A character operation is already running");
                    const auto root=ws_.project.dir;humanBuildProject_=root;humanBuildResult_=Json::object();
                    if(method=="studio.humanFaceFitScan") {
                        const auto scan=StudioFacialPreviewBinding::checkedModelPath(catalog,params.getStr("scanGuid"));
                        const auto face=vivid::HumanFaceShape::fromJson(params["face"]);
                        if(!params["distance"].isNumber()||!params["strength"].isNumber())throw std::invalid_argument("Scan fit requires numeric limits");
                        const float distance=float(params["distance"].toDouble()),strength=float(params["strength"].toDouble());
                        humanBuild_=std::async(std::launch::async,[root,source,scan,face,distance,strength]{auto reader=humanCharacterReader(studioContentRoot(root));auto head=render::loadModel(source,reader),target=render::loadModel(scan,reader);if(!head||!target)throw std::runtime_error("Cannot import scan/template");auto result=Json::object();result.set("face",vivid::faceShapeFromScan(*head,*target,face,distance,strength).toJson());return result;});
                    } else {
                        const auto head=StudioFacialPreviewBinding::checkedModelPath(catalog,params.getStr("headGuid"));
                        auto wardrobe=Json::parse(R"({"version":1,"hiddenMeshes":[],"items":[]})");wardrobe.set("hiddenMeshes",params["hiddenMeshes"]);
                        auto item=Json::parse(R"({"id":"head","asset":"","slot":"head","enabled":true,"bone":"","translation":[0,0,0],"rotation":[0,0,0],"scale":[1,1,1],"finish":"source","hideMeshes":[],"masks":[],"independentRig":true})");
                        item.set("asset",Json::string(head));item.set("bone",params["bone"]);wardrobe.member("items").push(item);vivid::validateHumanWardrobe(wardrobe);
                        const auto name=params.getStr("name");if(!validStudioHumanLookName(name))throw std::invalid_argument("Invalid imported character name");const Json editable=params;
                        humanBuild_=std::async(std::launch::async,[root,source,wardrobe,name,editable]{return buildStudioHumanCharacter(root,source,wardrobe,vivid::HumanAppearance{},name,512,editable);});
                    }
                    auto result=Json::object();result.set("running",Json::fromBool(true));reply.set("result",result);
                } else if(method=="studio.humanWardrobeFromAsset") {
                    std::string bytes;const auto reader=humanCharacterReader(studioContentRoot(ws_.project.dir));
                    const auto path=(std::filesystem::path(source).parent_path()/"wardrobe.json").generic_string();
                    if(!reader(path,bytes)||bytes.size()>128*1024)throw std::runtime_error("No valid wardrobe.json beside this model");
                    auto recipe=Json::parse(bytes);vivid::validateHumanWardrobe(recipe);
                    if(recipe.has("sockets"))for(size_t i=0;i<recipe["sockets"].size();++i){auto& socket=recipe.member("sockets").at(i);for(const auto* field:{"translation","rotation","scale"})if(!socket.has(field))socket.set(field,Json::parse(std::string(field)=="scale"?"[1,1,1]":"[0,0,0]"));}
                    for(size_t i=0;i<recipe["items"].size();++i){auto& item=recipe.member("items").at(i);for(const auto* field:{"translation","rotation","scale"})if(!item.has(field))item.set(field,Json::parse(std::string(field)=="scale"?"[1,1,1]":"[0,0,0]"));if(!item.has("finish"))item.set("finish",Json::string("source"));auto asset=catalog.byPath(item.getStr("asset"));if(!asset||asset->importer!="model")throw std::runtime_error("Wardrobe model not found in project");item.set("asset",Json::string(asset->guid));for(size_t j=0;j<item["masks"].size();++j){auto& mask=item.member("masks").at(j);auto texture=catalog.byPath(mask.getStr("texture"));if(!texture||texture->importer!="texture")throw std::runtime_error("Wardrobe mask not found in project");mask.set("texture",Json::string(texture->guid));}}
                    reply.set("result",recipe);
                } else if(method=="studio.humanWardrobeInfo") {
                    // Importing a full character takes seconds: answer repeat
                    // questions from a cache keyed by path + file time.
                    std::error_code ec;const auto file=studioContentRoot(ws_.project.dir)/source;
                    const auto stamp=std::to_string(static_cast<long long>(std::filesystem::last_write_time(file,ec).time_since_epoch().count()));
                    const auto key=ws_.project.dir+"|"+source+"|"+stamp;
                    auto cached=wardrobeInfoCache_.find(key);
                    if(cached==wardrobeInfoCache_.end()) {
                        auto model=render::loadModel(source,humanCharacterReader(studioContentRoot(ws_.project.dir)));
                        if(!model)throw std::runtime_error("Cannot inspect character model");
                        if(wardrobeInfoCache_.size()>16)wardrobeInfoCache_.clear();
                        cached=wardrobeInfoCache_.emplace(key,vivid::inspectHumanWardrobe(*model)).first;
                    }
                    reply.set("result",cached->second);
                } else {
                    if(humanBuild_.valid())throw std::runtime_error("A character build is already running; check its status");
                    const auto look=vivid::HumanAppearance::fromJson(params["look"]);auto wardrobe=params["wardrobe"];vivid::validateHumanWardrobe(wardrobe);
                    const std::string name=params.getStr("name");if(!validStudioHumanLookName(name))throw std::invalid_argument("Invalid character name");
                    for(size_t wi=0;wi<wardrobe["items"].size();++wi) {
                        auto& item=wardrobe.member("items").at(wi);
                        if(!item.getBool("enabled"))continue;
                        item.set("asset",Json::string(StudioFacialPreviewBinding::checkedModelPath(catalog,item.getStr("asset"))));
                        for(size_t mi=0;mi<item["masks"].size();++mi) {
                            auto& mask=item.member("masks").at(mi);
                            const auto asset=catalog.byGuid(mask.getStr("texture"));if(!asset||asset->directory||asset->importer!="texture")throw std::runtime_error("Body mask must be a project texture");
                            mask.set("texture",Json::string(asset->path));
                        }
                    }
                    if(!params["textureSize"].isNumber()||params["textureSize"].toDouble()!=double(params.getInt("textureSize")))throw std::invalid_argument("Invalid texture resolution");
                    const auto root=ws_.project.dir;const int size=int(params.getInt("textureSize"));const Json editable=params;
                    humanBuildProject_=root;humanBuildResult_=Json::object();
                    humanBuild_=std::async(std::launch::async,[root,source,wardrobe,look,name,size,editable]{return buildStudioHumanCharacter(root,source,wardrobe,look,name,size,editable);});
                    auto result=Json::object();result.set("running",Json::fromBool(true));reply.set("result",result);
                }
            }
        } else if(method=="studio.humanFacePick") {
            // Face-region picking in the viewport (Face → Blend): the next
            // clicks report the head point instead of sculpting.
            const bool on=params.getBool("enabled");
            if(on && (!facialPreview_.face() || facialPreview_.face()->meshes.empty()))
                throw std::runtime_error("Сначала выберите меши лица: «Лицо → Пропорции и импорт»");
            sculpt_.regionPick=on;if(on)sculpt_.armed=false;
            auto result=Json::object();result.set("enabled",Json::fromBool(on));reply.set("result",std::move(result));
        } else if(method=="studio.humanPackageList" || method=="studio.humanPackageExport" || method=="studio.humanPackageImport") {
            // Portable character package (look + picture + built character).
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            if(method=="studio.humanPackageList")reply.set("result",listStudioHumanPackages(ws_.project.dir));
            else if(method=="studio.humanPackageExport")reply.set("result",exportStudioHumanPackage(ws_.project.dir,params.getStr("look"),params.getStr("character")));
            else reply.set("result",importStudioHumanPackage(ws_.project.dir,params.getStr("name")));
        } else if(method=="studio.humanMetaHuman") {
            // MetaHuman adapter: inspect a project model (permitted MetaHuman
            // export), return its report plus the VIVID look roles and face meshes.
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            StudioAssetCatalog catalog(ws_.project.dir);catalog.rebuild();
            const auto relative=StudioFacialPreviewBinding::checkedModelPath(catalog,params.getStr("guid"));
            const auto key=ws_.project.dir+"|"+relative;
            if(metaHumanKey_!=key){metaHumanKey_=key;metaHumanResult_=Json();
                const auto content=studioContentRoot(ws_.project.dir);
                metaHumanJob_=std::async(std::launch::async,[content,relative]{
                    auto model=render::loadModel(relative,humanCharacterReader(content));
                    if(!model)throw std::runtime_error("Не удалось загрузить модель "+relative);
                    const auto report=vivid::adaptMetaHuman(*model);
                    auto result=report.toJson();result.set("look",report.look());result.set("face",report.face());
                    result.set("rig",vivid::buildMetaHumanRig(*model).toJson());
                    return result;});}
            if(metaHumanResult_.isNull()&&metaHumanJob_.valid()&&metaHumanJob_.wait_for(std::chrono::seconds(0))==std::future_status::ready){
                try{metaHumanResult_=metaHumanJob_.get();}catch(...){metaHumanKey_.clear();throw;}}
            if(metaHumanResult_.isNull()){auto pending=Json::object();pending.set("pending",Json::fromBool(true));reply.set("result",std::move(pending));}
            else reply.set("result",metaHumanResult_);
        } else if(method=="studio.humanPlaceInGame") {
            // "To the game": the built character becomes an RTS unit type
            // (view.model) and, when a map is open, one unit is placed.
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            const std::string type=params.getStr("type");
            if(type.empty()||type.size()>48||!std::all_of(type.begin(),type.end(),[](unsigned char c){return std::isalnum(c)||c=='_';})||std::isdigit((unsigned char)type[0]))
                throw std::invalid_argument("Имя типа: латиница, цифры и _ (до 48 символов)");
            const std::string asset=params.getStr("path");
            namespace fs=std::filesystem;
            const fs::path relative(asset);
            if(asset.empty()||relative.is_absolute()||asset.find("..")!=std::string::npos||(relative.extension()!=".glb"&&relative.extension()!=".gltf"))
                throw std::invalid_argument("Нужна модель персонажа .glb из content");
            const fs::path file=studioContentRoot(ws_.project.dir)/relative;
            if(!fs::is_regular_file(file))throw std::runtime_error("Модель персонажа не найдена: "+asset);
            const auto viewPath=fs::relative(file,fs::path(ws_.project.dir)).generic_string();
            auto view=Json::object();view.set("model",Json::string(viewPath));view.set("height",Json::number(1.8));
            auto def=Json::object();def.set("view",view);
            if(ws_.project.rules.findType(type)<0){
                def.set("name",Json::string(params.getStr("name").empty()?type:params.getStr("name")));
                auto move=Json::object();move.set("speed",Json::number(3.));def.set("move",move);
                auto health=Json::object();health.set("max",Json::number(100.));def.set("health",health);
            }
            auto value=Json::object();value.set(type,def);
            auto args=Json::object();args.set("path",Json::string("types"));args.set("value",value);
            const Json merged=ws_.call("rules_merge",args,"studio");
            if(merged.has("error"))throw std::runtime_error(merged.getStr("error"));
            bool placed=false;
            if(!ws_.map.data.worldSpace && ws_.map.data.w>0) {
                auto at=Json::object();at.set("type",Json::string(type));
                at.set("x",Json::number(double(ws_.map.data.w)/2));at.set("z",Json::number(double(ws_.map.data.h)/2));at.set("owner",Json::number(1.));
                const Json put=ws_.call("entity_place",at,"studio");
                placed=!put.has("error");
                if(placed)selectOnly("entity:"+std::to_string(ws_.map.data.entities.size()-1));
            }
            auto result=Json::object();result.set("type",Json::string(type));result.set("model",Json::string(viewPath));result.set("placed",Json::fromBool(placed));
            reply.set("result",std::move(result));
        } else if(method=="studio.humanTryOn") {
            // Live wardrobe try-on in the preview; null takes everything off.
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            reply.set("result",facialPreview_.setWardrobe(params["wardrobe"]));
        } else if(method=="studio.humanClipList" || method=="studio.humanClipAdd" || method=="studio.humanClipRemove") {
            // External animation clips of the shown character, kept in the
            // engine's clip sidecar "<model>.clips.json" (bound by bone path).
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            if(!facialPreview_.bound()||facialPreview_.creatorMode())throw std::runtime_error("Покажите персонажа во вьюпорте");
            namespace fs=std::filesystem;
            const fs::path content=studioContentRoot(ws_.project.dir);
            const fs::path model=content/facialPreview_.path();
            const fs::path sidecar=model.string()+".clips.json";
            if(fs::is_symlink(fs::symlink_status(sidecar)))throw std::runtime_error("Clip sidecar cannot be a symlink");
            Json doc=Json::object();
            if(fs::is_regular_file(sidecar)){std::ifstream in(sidecar,std::ios::binary);std::string b((std::istreambuf_iterator<char>(in)),{});if(b.size()>256*1024)throw std::runtime_error("Clip sidecar too large");doc=Json::parse(b);}
            if(!doc.has("format")){doc.set("format",Json::string("aiverse-clips"));doc.set("version",Json::number(int64_t(1)));doc.set("keepSource",Json::fromBool(true));doc.set("clips",Json::array());}
            auto write=[&]{{std::ofstream out(sidecar.string()+".pending",std::ios::binary|std::ios::trunc);out<<doc.dump(2)<<"\n";if(!out)throw std::runtime_error("Cannot write clip sidecar");}fs::rename(sidecar.string()+".pending",sidecar);facialPreview_.reload();};
            if(method=="studio.humanClipAdd") {
                std::string file=params.getStr("file");const std::string name=params.getStr("name");
                if(!file.empty()&&file.find('/')==std::string::npos&&!fs::is_regular_file(content/file)){ // a GUID from the asset picker
                    StudioAssetCatalog catalog(ws_.project.dir);catalog.rebuild();file=StudioFacialPreviewBinding::checkedModelPath(catalog,file);}
                const fs::path rel(file);
                if(file.empty()||rel.is_absolute()||file.find("..")!=std::string::npos||!fs::is_regular_file(content/rel))throw std::invalid_argument("Выберите файл анимации из content");
                if(name.empty()||name.size()>64)throw std::invalid_argument("Имя клипа: 1..64 символа");
                if(doc["clips"].size()>=64)throw std::runtime_error("Не больше 64 клипов");
                for(const auto& c:doc["clips"].items())if(c.getStr("name")==name)throw std::runtime_error("Клип «"+name+"» уже есть");
                auto c=Json::object();c.set("name",Json::string(name));
                c.set("file",Json::string(fs::relative(content/rel,model.parent_path()).generic_string()));
                if(!params.getStr("clip").empty())c.set("clip",Json::string(params.getStr("clip")));
                doc.member("clips").push(c);write();
            } else if(method=="studio.humanClipRemove") {
                auto& list=doc.member("clips").itemsMut();const auto before=list.size();
                std::erase_if(list,[&](const Json& c){return c.getStr("name")==params.getStr("name")&&c.has("file");});
                if(list.size()==before)throw std::runtime_error("Нет такого внешнего клипа");
                write();
            }
            auto result=Json::object();auto ext=Json::array();
            for(const auto& c:doc["clips"].items())if(c.has("file")){auto e=Json::object();e.set("name",c["name"]);e.set("file",c["file"]);ext.push(e);}
            result.set("external",ext);reply.set("result",std::move(result));
        } else if(method=="studio.humanRuler") {
            facialPreview_.showRuler=params.getBool("enabled");
            auto result=Json::object();result.set("enabled",Json::fromBool(facialPreview_.showRuler));reply.set("result",std::move(result));
        } else if(method=="studio.humanCompare") {
            // Before/after: show the untouched source model while held.
            reply.set("result",facialPreview_.setCompareOriginal(params.getBool("original")));
        } else if(method=="studio.humanCamera") {
            const auto view=params.getStr("view");
            if(view!="face"&&view!="torso"&&view!="full")throw std::invalid_argument("Camera view is face, torso or full");
            facialPreview_.cameraView=view;++facialPreview_.cameraSerial;
            auto result=Json::object();result.set("view",Json::string(view));reply.set("result",std::move(result));
        } else if(method=="studio.humanSculpt" || method=="studio.humanSculptStroke" || method=="studio.humanSculptUndo") {
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            if(method=="studio.humanSculpt") {
                auto next=sculpt_;
                if(params.has("enabled"))next.armed=params.getBool("enabled");
                if(params.has("radius")){const double r=params["radius"].toDouble(-1);if(!(r>=.01&&r<=1))throw std::invalid_argument("Brush radius must be 0.01..1");next.radius=float(r);}
                if(params.has("strength")){const double v=params["strength"].toDouble(-1);if(!(v>0&&v<=.25))throw std::invalid_argument("Brush strength must be 0..0.25");next.strength=float(v);}
                if(params.has("mode")){const auto m=params.getStr("mode");if(m!="push"&&m!="pull")throw std::invalid_argument("Brush mode is push or pull");next.pull=m=="pull";}
                if(params.has("symmetry"))next.symmetry=params.getBool("symmetry");
                if(next.armed && (!facialPreview_.face() || facialPreview_.face()->meshes.empty()))
                    throw std::runtime_error("Сначала выберите меши лица: «Лицо → Пропорции и импорт»");
                if(next.armed)next.regionPick=false;
                sculpt_=next;
            } else if(method=="studio.humanSculptStroke") {
                // Sent by the native viewport after a hit test on the face meshes.
                auto vec=[&](const char* key){const auto& a=params[key];
                    if(!a.isArray()||a.size()!=3||!a[0].isNumber()||!a[1].isNumber()||!a[2].isNumber())throw std::invalid_argument(std::string("Sculpt ")+key+" must be [x,y,z]");
                    return render::float3{float(a[0].toDouble()),float(a[1].toDouble()),float(a[2].toDouble())};};
                vivid::HumanFaceHit hit;hit.point=vec("point");hit.normal=vec("normal");
                const auto stroke=vivid::humanSculptLandmark(hit,sculpt_.radius,sculpt_.pull?-sculpt_.strength:sculpt_.strength);
                facialPreview_.sculptStroke(stroke);
                // Symmetric sculpt: the mirrored stroke lands on the other half
                // (skipped on the centre line, where it would double the push).
                if(sculpt_.symmetry && std::abs(stroke.from.x-.5f)>.02f)
                    facialPreview_.sculptStroke(StudioFacialPreviewBinding::mirrored(stroke));
            } else if(params.getBool("all")) facialPreview_.sculptClear();
            else facialPreview_.sculptUndo();
            auto result=Json::object();
            result.set("armed",Json::fromBool(sculpt_.armed));result.set("radius",Json::number(double(sculpt_.radius),4));
            result.set("strength",Json::number(double(sculpt_.strength),4));result.set("mode",Json::string(sculpt_.pull?"pull":"push"));
            result.set("symmetry",Json::fromBool(sculpt_.symmetry));
            result.set("strokes",Json::number(int64_t(facialPreview_.face()?facialPreview_.face()->landmarks.size():0)));
            result.set("preview",facialPreview_.describe());
            reply.set("result",std::move(result));
        } else if(method.rfind("studio.humanCreator",0)==0) {
            // Digital Human Creator window (docs/vivid.md § Studio Human Creator).
            // Every mutation is validated all-or-nothing; the result is always
            // the full creator state so the window can redraw from one reply.
            std::string error;bool ok=true;
            auto creatorNum=[&](const char* key,double lo,double hi)->double {
                const auto& v=params[key];
                if(!v.isNumber()||!std::isfinite(v.toDouble())||v.toDouble()<lo||v.toDouble()>hi)
                    throw std::invalid_argument(std::string("Human Creator: invalid ")+key);
                return v.toDouble();
            };
            const std::string which=params.getStr("target","dna");
            if(method=="studio.humanCreatorInfo") {
            } else if(method=="studio.humanCreatorSet") {
                ok=humanCreator_.set(params.getStr("key"),params["value"],&error);
            } else if(method=="studio.humanCreatorPreset") {
                ok=humanCreator_.preset(params.getStr("id"),&error);
            } else if(method=="studio.humanCreatorUndo" || method=="studio.humanCreatorRedo") {
                const bool undo=method=="studio.humanCreatorUndo";
                if(which=="dna")ok=undo?humanCreator_.undo():humanCreator_.redo();
                else if(which=="rig")ok=undo?humanCreator_.rigUndo():humanCreator_.rigRedo();
                else if(which=="skin")ok=undo?humanCreator_.skinUndo():humanCreator_.skinRedo();
                else throw std::invalid_argument("Human Creator: undo target must be dna, rig or skin");
                if(!ok)error="Nothing to "+std::string(undo?"undo":"redo");
            } else if(method=="studio.humanCreatorRoute") {
                const auto index=std::size_t(creatorNum("index",0,256));
                if(params.getBool("remove"))ok=humanCreator_.removeRoute(index),error=ok?"":"Route index out of range";
                else ok=humanCreator_.setRoute(index,params.getStr("channel"),params.getStr("target"),creatorNum("gain",0,4),&error);
            } else if(method=="studio.humanCreatorRig") {
                const auto& t=params["translation"];
                if(!t.isArray()||t.size()!=3||!t[0].isNumber()||!t[1].isNumber()||!t[2].isNumber())
                    throw std::invalid_argument("Human Creator: translation must be [x,y,z]");
                ok=humanCreator_.setRig(params.getStr("node"),{float(t[0].toDouble()),float(t[1].toDouble()),float(t[2].toDouble())},&error);
            } else if(method=="studio.humanCreatorSkin") {
                const auto vertex=std::uint32_t(creatorNum("vertex",0,double(std::numeric_limits<std::uint32_t>::max())));
                if(params.getBool("erase"))ok=humanCreator_.eraseSkin(vertex),error=ok?"":"No weight edit on that vertex";
                else ok=humanCreator_.setSkin(vertex,params.getStr("joint"),creatorNum("weight",0,1),&error);
            } else if(method=="studio.humanCreatorNew") {
                vivid::HumanCreatorWorkbench fresh;humanCreator_=std::move(fresh);
            } else if(method=="studio.humanCreatorSave") {
                if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
                vivid::saveHumanCreator(ws_.project.dir,params.getStr("name"),humanCreator_.toDocument());
            } else if(method=="studio.humanCreatorLoad") {
                if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
                ok=humanCreator_.loadDocument(vivid::readHumanCreator(ws_.project.dir,params.getStr("name")),&error);
            } else if(method=="studio.humanCreatorList") {
                if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
                reply.set("result",vivid::listHumanCreators(ws_.project.dir));
                return reply;
            } else if(method=="studio.humanCreatorPreview") {
                if(params.getBool("enabled",true)) {
                    facialPreview_.bindCreator(humanCreator_.toDocument());
                    previewMaterialName_.clear();refreshMaterialPreview();
                } else if(facialPreview_.creatorMode()) facialPreview_.reset();
            } else throw std::invalid_argument("Unknown Human Creator method");
            if(!ok)throw std::invalid_argument(error.empty()?"Human Creator edit rejected":error);
            // Live preview follows the document after every accepted edit.
            if(facialPreview_.creatorMode() && method!="studio.humanCreatorPreview" && method!="studio.humanCreatorInfo")
                facialPreview_.bindCreator(humanCreator_.toDocument());
            auto info=humanCreator_.info();
            info.set("preview",facialPreview_.describe());
            reply.set("result",std::move(info));
        } else if(method=="studio.humanAppearanceInfo" || method=="studio.humanAppearanceApply" ||
                  method=="studio.humanAppearanceSave" || method=="studio.humanAppearanceLoad" ||
                  method=="studio.humanAppearanceList" || method=="studio.humanAppearanceFromAsset" ||
                  method=="studio.humanAppearanceThumbnails") {
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            if(method=="studio.humanAppearanceFromAsset") {
                StudioAssetCatalog catalog(ws_.project.dir);catalog.rebuild();
                const auto relative=StudioFacialPreviewBinding::checkedModelPath(catalog,params.getStr("guid"));
                const auto path=studioContentRoot(ws_.project.dir)/std::filesystem::path(relative).parent_path()/"appearance.json";
                if(std::filesystem::is_symlink(std::filesystem::symlink_status(path)) ||
                   !std::filesystem::is_regular_file(path) || std::filesystem::file_size(path)>128*1024)
                    throw std::runtime_error("No appearance.json beside this model; import a profile");
                std::ifstream in(path,std::ios::binary);
                if(!in)throw std::runtime_error("Cannot read model appearance");
                std::string bytes((std::istreambuf_iterator<char>(in)),{});
                const auto look=vivid::HumanAppearance::fromJson(Json::parse(bytes));
                auto result=Json::object();result.set("name",Json::string("model profile"));result.set("look",look.toJson());reply.set("result",std::move(result));
            } else if(method=="studio.humanAppearanceThumbnails") {
                reply.set("result",readStudioHumanLookThumbnails(ws_.project.dir));
            } else if(method=="studio.humanAppearanceSave") {
                reply.set("result",saveStudioHumanLook(ws_.project.dir,params.getStr("name"),params["look"]));
            } else if(method=="studio.humanAppearanceLoad") {
                reply.set("result",readStudioHumanLook(ws_.project.dir,params.getStr("name")));
            } else if(method=="studio.humanAppearanceList" || method=="studio.humanAppearanceFromAsset") {
                reply.set("result",listStudioHumanLooks(ws_.project.dir));
            } else {
                if(method=="studio.humanAppearanceApply") {
                    const auto& rev=params["revision"];
                    if(!rev.isNumber() || rev.toDouble()<0 || rev.toDouble()!=double(rev.integer()))
                        throw std::invalid_argument("Invalid appearance revision");
                    StudioAssetCatalog catalog(ws_.project.dir);catalog.rebuild();
                    facialPreview_.bindAppearance(catalog,params.getStr("guid"),uint64_t(rev.integer()),params["look"],params.has("face")?&params["face"]:nullptr);
                    previewMaterialName_.clear();refreshMaterialPreview();
                }
                auto info=Json::object();info.set("schema",vivid::HumanAppearance::schema());info.set("faceSchema",vivid::HumanFaceShape::schema());info.set("face",facialPreview_.face()?facialPreview_.face()->toJson():Json());
                info.set("look",facialPreview_.appearance()?facialPreview_.appearance()->toJson():vivid::HumanAppearance{}.toJson());
                info.set("preview",facialPreview_.describe());reply.set("result",std::move(info));
            }
        } else if(method=="studio.facialPreviewBind" ||
                  method=="studio.facialPreviewControls" ||
                  method=="studio.facialPreviewInfo") {
            if(ws_.project.dir.empty())
                throw std::runtime_error("Open a project first");
            if(method=="studio.facialPreviewBind") {
                if(!params["guid"].isString())
                    throw std::invalid_argument("Facial preview selection requires asset GUID string");
                const std::string guid=params.getStr("guid");
                // A clear operation detaches all staged controls without
                // touching the Scene GPU device or the document's saved rig.
                if(guid.empty()) {
                    facialPreview_.reset();
                    reply.set("result",facialPreview_.describe());
                } else {
                    StudioAssetCatalog catalog(ws_.project.dir);
                    catalog.rebuild();
                    // Facial mode takes precedence over a previously opened
                    // material sphere, without mutating map/gameplay data.
                    Json result=facialPreview_.bind(catalog,guid,params["rig"]);
                    previewMaterialName_.clear();
                    refreshMaterialPreview();
                    reply.set("result",std::move(result));
                }
            } else if(method=="studio.facialPreviewControls") {
                const Json& revision=params["revision"];
                if(!revision.isNumber() || revision.toDouble()<0 ||
                   revision.toDouble()!=double(revision.integer()))
                    throw std::runtime_error("Invalid facial selection revision");
                reply.set("result",facialPreview_.controls(
                    uint64_t(revision.integer()),params["values"],
                    params["vectors"]));
            } else {
                reply.set("result",facialPreview_.describe());
            }
        } else if (method == "studio.facialRigList" ||
                   method == "studio.facialRigGet" ||
                   method == "studio.facialRigValidate" ||
                   method == "studio.facialRigSave") {
            // Project-bound authoring only. Native side is the sole owner of
            // filesystem access; browser controls pass a bounded simple name,
            // never arbitrary source/destination paths.
            if(method!="studio.facialRigValidate" && ws_.project.dir.empty())
                throw std::runtime_error("Open a project first");
            if(method=="studio.facialRigList") {
                reply.set("result",listStudioFacialRigs(ws_.project.dir));
            } else if(method=="studio.facialRigGet") {
                const std::string name=params.getStr("name");
                reply.set("result",readStudioFacialRig(ws_.project.dir,name));
            } else {
                const Json& doc=params["rig"];
                std::string reason;
                if(!validStudioFacialRigDraft(doc,&reason))
                    throw std::runtime_error(reason);
                if(method=="studio.facialRigValidate") {
                    Json validated=Json::object();
                    validated.set("valid",Json::fromBool(true));
                    validated.set("morphs",Json::number(int64_t(doc["morphs"].size())));
                    validated.set("ikUnits",Json::number(int64_t(doc["rig"].size())));
                    // This is schema validation only, not model channel or
                    // native MotionWeave compiler verification.
                    validated.set("runtimeVerified",Json::fromBool(false));
                    reply.set("result",std::move(validated));
                } else {
                    const std::string name=params.getStr("name");
                    reply.set("result",saveStudioFacialRig(ws_.project.dir,name,doc));
                }
            }
        } else if (method == "studio.shaderGraphs" || method == "studio.shaderGraphGet" ||
                   method == "studio.shaderGraphValidate" || method == "studio.shaderGraphSave" ||
                   method == "studio.shaderGraphCatalog") {
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            if (method == "studio.shaderGraphCatalog") {
                const Json raw=ws_.call("shader_graph_catalog",Json::object(),"studio");
                if(raw.has("error")) throw std::runtime_error(raw.getStr("error"));
                Json result=Json::object(),catalog=Json::array();
                for(const Json& op:raw["nodes"].items()) {
                    if(catalog.size()>=256) break;
                    if(!op.isObject() || !validStudioShaderGraphName(op.getStr("id")))continue;
                    Json allowed=Json::object();
                    allowed.set("id",op["id"]);
                    allowed.set("inputs",op["inputs"]);
                    allowed.set("domains",op["domains"]);
                    catalog.push(std::move(allowed));
                }
                result.set("nodes",std::move(catalog));
                reply.set("result",std::move(result));
            } else if (method == "studio.shaderGraphs") {
                const Json raw=ws_.call("shader_graph_list",Json::object(),"studio");
                if(raw.has("error")) throw std::runtime_error(raw.getStr("error"));
                Json names=Json::array();
                const Json& candidates=raw["graphs"];
                const size_t count=std::min(size_t(128),candidates.size());
                for(size_t i=0;i<count;++i) {
                    if(candidates[i].isString() &&
                       validStudioShaderGraphName(candidates[i].str()))
                        names.push(candidates[i]);
                }
                Json result=Json::object();
                result.set("graphs",std::move(names));
                reply.set("result",std::move(result));
            } else if (method == "studio.shaderGraphValidate" || method == "studio.shaderGraphSave") {
                const Json& draft=params["graph"];
                if (!validStudioShaderGraphDraft(draft))
                    throw std::runtime_error("Invalid or oversized Shader Graph draft");
                Json request=Json::object();
                request.set("graph",draft);
                if(method=="studio.shaderGraphSave") {
                    const std::string name=params.getStr("name");
                    if(!validStudioShaderGraphName(name))
                        throw std::runtime_error("Invalid Shader Graph filename");
                    request.set("name",str(name));
                }
                const Json raw=ws_.call(method=="studio.shaderGraphSave"?
                    "shader_graph_set":"shader_graph_validate",request,"studio");
                if(raw.has("error")) throw std::runtime_error(raw.getStr("error"));
                Json result=Json::object();
                result.set("graph",raw["graph"]);
                result.set("nodes",raw["nodes"]);
                result.set("samples",raw["samples"]);
                // Shader source output is deliberately NOT passed to browser.
                reply.set("result",std::move(result));
            } else {
                const std::string name=params.getStr("name");
                if (!validStudioShaderGraphName(name))
                    throw std::runtime_error("Invalid Shader Graph name");
                Json request=Json::object();
                request.set("name",str(name));
                // Existing ShaderGraphOps validates the project-local name/path,
                // 256 KiB graph budget and the entire node graph via compiler.
                const Json raw=ws_.call("shader_graph_get",request,"studio");
                if(raw.has("error")) throw std::runtime_error(raw.getStr("error"));
                Json result=Json::object();
                result.set("graph",raw["graph"]);
                result.set("nodes",raw["nodes"]);
                result.set("samples",raw["samples"]);
                // Do NOT serialize raw.glsl/msl/vertex* to JS.
                reply.set("result",std::move(result));
            }
        } else if (method == "studio.materialPreview") {
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            const std::string name=params.getStr("name");
            if(!name.empty() && !validStudioMaterialName(name))
                throw std::runtime_error("Invalid material name");
            if(!name.empty() && !editor::loadMaterialLibrary(ws_)[name].isObject())
                throw std::runtime_error("Unknown material");
            previewMaterialName_=name;
            refreshMaterialPreview();
            Json result=Json::object();
            result.set("active",Json::fromBool(!name.empty()));
            result.set("name",str(name));
            reply.set("result",std::move(result));
        } else if (method == "studio.materials" || method == "studio.materialPatch" ||
                   method == "studio.materialCreate") {
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            if (method == "studio.materialCreate") {
                const std::string name=params.getStr("name");
                const std::string preset=params.getStr("preset");
                if (!validStudioMaterialName(name) || !validStudioMaterialPreset(preset))
                    throw std::runtime_error("Invalid material name or preset");
                const Json existing=editor::loadMaterialLibrary(ws_);
                if (existing.has(name)) throw std::runtime_error("A material with this name already exists");

                Json material=Json::object();
                Json color=Json::array();
                const double rgb[3] = {
                    preset=="metal" ? .78 : preset=="glass" ? .8 : .65,
                    preset=="metal" ? .8 : preset=="glass" ? .92 : .68,
                    preset=="metal" ? .85 : preset=="glass" ? 1.0 : .72
                };
                for(double v:rgb) color.push(Json::number(v,3));
                material.set("color",std::move(color));
                material.set("metallic",Json::number(preset=="metal" ? 1.0 : 0.0,3));
                material.set("roughness",Json::number(preset=="metal" ? .32 : preset=="glass" ? .08 : .85,3));
                material.set("alpha",str(preset=="glass" ? "blend" : "opaque"));
                if (preset=="glass") material.set("doubleSided",Json::fromBool(true));
                Json request=Json::object();
                request.set("name",str(name));
                request.set("material",std::move(material));
                const Json saved=ws_.call("material_set",request,"studio");
                if (saved.has("error")) throw std::runtime_error(saved.getStr("error"));
            } else if (method == "studio.materialPatch") {
                const std::string name=params.getStr("name");
                if (!validStudioMaterialName(name))
                    throw std::runtime_error("Invalid material name");
                const Json& patch=params["patch"];
                if (!patch.isObject() || patch.size()==0 || patch.size()>24)
                    throw std::runtime_error("Material patch requires 1..24 safe fields");
                const Json library=editor::loadMaterialLibrary(ws_);
                if (!library[name].isObject())
                    throw std::runtime_error("Only existing library materials can be edited");
                Json validatedPatch = Json::object();
                // Runtime materials.json stores project-relative texture paths.
                // The HTML5 picker sends immutable GUIDs; resolve/validate them
                // at the native trust boundary, never accept an arbitrary path.
                for(const auto& [key,value]:patch.members()) {
                    if (key=="sssColor") {
                        if(!value.isString())throw std::runtime_error("SSS color must be linear RGB");
                        const auto rgb=parseStudioSssColor(value.str());
                        if(!rgb)throw std::runtime_error("SSS color requires three values from 0 to 1");
                        Json color=Json::array();
                        for(double channel:*rgb)color.push(Json::number(channel));
                        validatedPatch.set(key,std::move(color));
                        continue;
                    }
                    if (studioMaterialTextureKey(key)) {
                        if (!value.isString())
                            throw std::runtime_error("Texture reference must be an asset GUID");
                        if (value.str().empty()) {
                            validatedPatch.set(key, str(""));
                            continue;
                        }
                        if (!studioValidGuid(value.str()))
                            throw std::runtime_error("Texture reference requires a valid asset GUID");
                        StudioAssetCatalog catalog(ws_.project.dir);
                        catalog.rebuild();
                        const auto asset = catalog.byGuid(value.str());
                        if (!asset || asset->directory || asset->importer != "texture")
                            throw std::runtime_error("Texture GUID not found or is not a texture");
                        validatedPatch.set(key, str("content/" + asset->path));
                        continue;
                    }
                    validatedPatch.set(key, value);
                    if (value.isNumber()) {
                        if (!validStudioMaterialNumber(key,value.toDouble()))
                            throw std::runtime_error("Unsupported material number: "+key);
                    } else if (value.isString()) {
                        if (!validStudioMaterialText(key,value.str()))
                            throw std::runtime_error("Unsupported material text: "+key);
                    } else if (value.type()==Json::Type::Bool) {
                        if (!validStudioMaterialBoolean(key))
                            throw std::runtime_error("Unsupported material boolean: "+key);
                    } else throw std::runtime_error("Unsupported material value type: "+key);
                }
                Json request=Json::object();
                request.set("name",str(name));
                request.set("material",std::move(validatedPatch));
                request.set("merge",Json::fromBool(true));
                // material_set validates keys/base and writes materials.json atomically.
                // Unlike MapDoc edits this is immediate disk save without Undo:
                // explicit Apply is required in the frontend.
                const Json result=ws_.call("material_set",request,"studio");
                if (result.has("error")) throw std::runtime_error(result.getStr("error"));
            }
            if (method!="studio.materials" && !previewMaterialName_.empty()) {
                // Refresh preview when the selected disk material was modified,
                // including inherited/base changes.
                refreshMaterialPreview();
            }
            const Json library=editor::loadMaterialLibrary(ws_);
            Json result=Json::object();
            result.set("library",library);
            reply.set("result",std::move(result));
        } else if(method=="studio.nereidFlow"||method=="studio.nereidFlowSet"){
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            if(ws_.map.data.worldSpace)throw std::runtime_error("NEREID authoring currently requires a flat map");
            if(method=="studio.nereidFlowSet"){
                if(!params["mode"].isString())throw std::runtime_error("Choose current mode");
                editor::editAuthoredWaterFlow(ws_.map,params);
            }
            reply.set("result",world::nereidFlowState(editor::authoredWaterDocument(ws_.map)));
        } else if(method=="studio.renderLighting" || method=="studio.renderLightingSet") {
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            const Json current=ws_.map.toJson();
            Json settings=render::renderLightingSettings(current["renderLighting"]);
            if(method=="studio.renderLightingSet") {
                const Json& patch=params["patch"];
                if(!patch.isObject() || patch.size()==0 || patch.size()>10)
                    throw std::runtime_error("Lighting patch requires 1..10 settings");
                for(const auto& [key,value]:patch.members())settings.set(key,value);
                settings=render::renderLightingSettings(settings); // validate BEFORE beginEdit
                if(!(settings==current["renderLighting"])) {
                    if(ws_.map.data.worldSpace) {
                        Json next=current;next.set("renderLighting",settings);
                        Json input=Json::object();input.set("map",next);
                        const auto updated=ws_.call("actor_set",input,"studio");
                        if(updated.has("error"))throw std::runtime_error(updated.getStr("error"));
                    } else {
                        ws_.map.beginEdit();ws_.map.data.extra.set("renderLighting",settings);ws_.map.touched();
                    }
                }
            }
            reply.set("result",settings);
        } else if(method=="studio.wind"||method=="studio.windSet"){
            // Presentation-only preview: never touches the map or project files.
            if(method=="studio.windSet")applyStudioWindPatch(wind_,params);
            reply.set("result",studioWindJson(wind_,ws_.project.dir,windSceneStats_));
        } else if (method == "studio.lighting" || method == "studio.lightingSet" ||
                   method == "studio.lightAdd" || method == "studio.lightUpdate" ||
                   method == "studio.lightDelete") {
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            if (ws_.map.data.worldSpace) {
                // FPS maps serialize MapData.fps, NOT MapData.extra. Apply to a copy,
                // then validate/commit the full candidate through actor_set.
                const Json currentDocument = ws_.map.toJson();
                if (method != "studio.lighting") {
                    const Json proposed = editFpsLighting(currentDocument,method,params);
                    if (!(proposed == currentDocument)) {
                        Json input=Json::object();
                        input.set("map",proposed);
                        const Json updated=ws_.call("actor_set",input,"studio");
                        if (updated.has("error")) throw std::runtime_error(updated.getStr("error"));
                    }
                }
                reply.set("result",fpsLightingView(ws_.map.toJson()));
                return reply;
            }
            if (method == "studio.lightingSet") {
                const std::string key = params.getStr("key");
                const Json& value = params["value"];
                if (!value.isNumber() || !validLightingScalar(key, value.toDouble()))
                    throw std::runtime_error("Invalid lighting parameter");
                Json input = Json::object();
                input.set(key, value);
                const Json updated = ws_.call("lighting_set", input, "studio");
                if (updated.has("error")) throw std::runtime_error(updated.getStr("error"));
            } else if (method == "studio.lightAdd") {
                Json input = Json::object();
                // New authored lights go to map center for flat maps; FPS origin otherwise.
                // Optional x/z (Inspector › Add Component › Light at the entity); else map center.
                const double x = params["x"].isNumber() ? params["x"].toDouble() : ws_.map.data.worldSpace ? 0.0 : double(ws_.map.data.w) * 0.5;
                const double z = params["z"].isNumber() ? params["z"].toDouble() : ws_.map.data.worldSpace ? 0.0 : double(ws_.map.data.h) * 0.5;
                if (!validLightScalar("x",x) || !validLightScalar("z",z))
                    throw std::runtime_error("Map coordinates out of range");
                input.set("x", Json::number(x,3));
                input.set("z", Json::number(z,3));
                const Json updated = ws_.call("light_add", input, "studio");
                if (updated.has("error")) throw std::runtime_error(updated.getStr("error"));
            } else if (method == "studio.lightUpdate" || method == "studio.lightDelete") {
                const Json& rawIndex=params["index"];
                if (!rawIndex.isNumber() || rawIndex.toDouble() != double(rawIndex.integer()))
                    throw std::runtime_error("Light index must be an integer");
                const int64_t index = rawIndex.integer(-1);
                const Json& list = ws_.map.data.extra["lights"];
                if (index < 0 || !list.isArray() || uint64_t(index) >= list.size())
                    throw std::runtime_error("Unknown light index");
                Json input = Json::object();
                input.set("index", Json::number(index));
                if (method == "studio.lightUpdate") {
                    const std::string key = params.getStr("key");
                    const Json& value = params["value"];
                    if (value.isNumber()) {
                        if (!validLightScalar(key,value.toDouble()))
                            throw std::runtime_error("Invalid light numeric property");
                    } else if (value.isString()) {
                        if (!validLightText(key,value.str()))
                            throw std::runtime_error("Invalid light string property");
                    } else if (value.type() == Json::Type::Bool) {
                        if (!validLightBoolean(key))
                            throw std::runtime_error("Invalid light boolean property");
                    } else throw std::runtime_error("Invalid light property type");
                    input.set(key,value);
                }
                const Json updated = ws_.call(method == "studio.lightDelete" ? "light_delete" : "light_update",input,"studio");
                if (updated.has("error")) throw std::runtime_error(updated.getStr("error"));
            }
            const Json current = ws_.call("light_list",Json::object(),"studio");
            if (current.has("error")) throw std::runtime_error(current.getStr("error"));
            reply.set("result",current);
        } else if (method == "studio.ops") {
            // Tool panel (Terrain, Roads, PostFX, ...): its Workspace ops with their JSON schemas.
            const StudioToolGroup* group=studioToolGroup(params.getStr("panel"));
            if (!group) throw std::runtime_error("Unknown tool panel");
            Json ops=Json::array();
            for (const char* name:group->ops)
                for (const auto& op:ws_.ops())
                    if (op.name==name) {
                        Json o=Json::object();
                        o.set("name",str(op.name));o.set("description",str(op.description));
                        o.set("schema",op.schema);o.set("readOnly",Json::fromBool(studioToolReadOnly(op.name)));
                        ops.push(std::move(o));
                    }
            Json out=Json::object();out.set("panel",str(group->panel));out.set("ops",ops);
            reply.set("result",std::move(out));
        } else if (method == "studio.op") {
            const std::string op=params.getStr("op");
            if (!studioToolAllowed(op)) throw std::runtime_error("This operation is not available in Studio panels: "+op);
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            const Json input=params["params"].isObject()?params["params"]:Json::object();
            const bool wasDirty=ws_.map.dirty||ws_.project.rulesDirty||ws_.project.manifestDirty;
            const size_t depth=ws_.map.undoDepth(),entityCount=ws_.map.data.entities.size();
            const Json result=ws_.call(op,input,"studio");
            if (result.has("error")) throw std::runtime_error(result.getStr("error"));
            const bool dirty=ws_.map.dirty||ws_.project.rulesDirty||ws_.project.manifestDirty;
            const bool changed=!studioToolReadOnly(op)||ws_.map.undoDepth()!=depth||dirty!=wasDirty;
            if (ws_.map.data.entities.size()!=entityCount) { clearSelection(); dropOrdinalFlags(); }
            Json out=Json::object();
            out.set("op",str(op));out.set("result",result);out.set("changed",Json::fromBool(changed));
            reply.set("result",std::move(out));
        } else if (method == "studio.inputGet" || method == "studio.inputSet") {
            // Input actions and contexts (input/actions.json, InputFlow asset); presentation only, never the simulation.
            namespace ecfg=platform::enhanced_config;
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            const std::filesystem::path file=std::filesystem::path(ws_.project.dir)/"input"/"actions.json";
            std::string error;
            if (method=="studio.inputSet") {
                ecfg::Asset asset;
                if (!ecfg::readAsset(params["asset"],asset,&error)) throw std::runtime_error(error);
                if (!ecfg::saveAssetFile(file,asset,&error)) throw std::runtime_error(error);
            }
            Json out=studioInputNames();
            std::error_code ec;
            const bool exists=std::filesystem::exists(file,ec);
            ecfg::Asset asset;
            if (exists && !ecfg::loadAssetFile(file,asset,&error)) out.set("error",str(error));
            out.set("asset",ecfg::writeAsset(asset));
            out.set("exists",Json::fromBool(exists));out.set("path",str("input/actions.json"));
            reply.set("result",std::move(out));
        } else if (method == "studio.assetAudit") {
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            const auto audit=studioAssetAudit(ws_.project.dir,ws_.engineDir);
            Json rows=Json::array();
            for (const auto& r:audit.rows){Json o=Json::object();o.set("source",str(r.source));o.set("reference",str(r.reference));rows.push(std::move(o));}
            Json out=Json::object();
            out.set("missing",rows);out.set("scanned",num(int64_t(audit.scanned)));out.set("truncated",Json::fromBool(audit.truncated));
            reply.set("result",std::move(out));
        } else if (method == "studio.assetUsage") {
            // Find References (path) or every content file no project file names (no path).
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            const auto files=studioProjectTextFiles(ws_.project.dir);
            Json out=Json::object();
            const std::string path=params.getStr("path");
            if (!path.empty()) {
                if (path.size()>500 || path.find("..")!=std::string::npos) throw std::runtime_error("Invalid asset path");
                out.set("path",str(path));out.set("mentions",studioAssetMentions(files,path));
            } else {
                StudioAssetCatalog catalog(ws_.project.dir);
                catalog.rebuild();
                const auto named=studioNamedContent(files);
                Json unused=Json::array();
                for (const auto& entry:catalog.all()) {
                    if (entry.directory || named.count(entry.path)) continue;
                    if (entry.path.size()>5 && entry.path.compare(entry.path.size()-5,5,".meta")==0) continue;
                    unused.push(str(entry.path));
                }
                out.set("unused",unused);out.set("scannedFiles",num(int64_t(files.size())));
            }
            reply.set("result",std::move(out));
        } else if (method == "studio.assetReferences") {
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            StudioAssetCatalog catalog(ws_.project.dir);
            catalog.rebuild();
            const auto entries=catalog.all();
            reply.set("result",studioReferenceGraphJson(studioCollectAssetReferences(
                editor::loadMaterialLibrary(ws_),ws_.project.rulesJson,entries)));
        } else if (method == "studio.assets") {
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            // Browser input cannot specify a filesystem directory. This scan
            // sees ONLY regular files beneath project/content (no symlinks).
            // Return content-relative paths, so the UI's root is "content".
            StudioAssetCatalog catalog(ws_.project.dir);
            catalog.rebuild(); // .meta is source of truth, index is disposable
            Json r = Json::object();
            r.set("root", str("content"));
            Json entries = Json::array(), folders = Json::array();
            for (const auto& asset : catalog.all()) {
                Json item = Json::object();
                item.set("path", str(asset.path));
                item.set("guid", str(asset.guid));
                item.set("importer",str(asset.importer));
                item.set("bytes", Json::number(asset.bytes));
                if(asset.directory)folders.push(std::move(item));
                else entries.push(std::move(item));
            }
            r.set("files",std::move(entries));
            r.set("folders",std::move(folders));
            reply.set("result", std::move(r));
        } else if (method == "studio.assetMetaPatch") {
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            const std::string guid=params.getStr("guid");
            const std::string key=params.getStr("key");
            if(!studioValidGuid(guid)||key.size()>40||!params.has("value"))
                throw std::runtime_error("Invalid asset import setting request");
            StudioAssetCatalog catalog(ws_.project.dir);
            const auto updated=catalog.patchSettings(guid,key,params["value"]);
            Json detail=Json::object();
            detail.set("guid",str(updated.guid));
            detail.set("path",str(updated.path));
            detail.set("importer",str(updated.importer));
            detail.set("directory",Json::fromBool(updated.directory));
            detail.set("bytes",Json::number(updated.bytes));
            detail.set("settings",updated.settings);
            reply.set("result",std::move(detail));
        } else if (method == "studio.assetFolderCreate") {
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            const std::string path=studioCreateContentFolder(ws_.project.dir,
                params.getStr("parent"),params.getStr("name"));
            Json r=Json::object();
            r.set("path",str(path));reply.set("result",std::move(r));
        } else if (method == "studio.assetMovePreview") {
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            const Json& paths=params["paths"];
            if(!paths.isArray()||paths.size()<1||paths.size()>64)
                throw std::runtime_error("Select between 1 and 64 assets");
            std::vector<std::string> sources;
            for(const Json& path:paths.items()){
                if(!path.isString())throw std::runtime_error("Invalid source path");
                sources.push_back(path.str());
            }
            pendingAssetTransfer_=StudioTransferPlan::internal(ws_.project.dir,
                params.getStr("destination"),sources);
            Json preview=pendingAssetTransfer_->describe();
            StudioAssetCatalog catalog(ws_.project.dir);
            catalog.rebuild();
            const auto dependencies=studioCollectAssetReferences(
                editor::loadMaterialLibrary(ws_),ws_.project.rulesJson,catalog.all());
            preview.set("references",studioMoveReferenceImpact(preview,dependencies));
            reply.set("result",std::move(preview));
        } else if (method == "studio.assetTransferCommit") {
            if(!pendingAssetTransfer_)throw std::runtime_error("No active asset transfer preview");
            const auto token=params.getStr("token");
            const Json& raw=params["decisions"];
            if(!raw.isArray()||raw.size()>64)throw std::runtime_error("Invalid conflict decisions");
            std::vector<StudioTransferDecision> decisions;
            for(const Json& d:raw.items()){
                if(!d.isObject()||!d["id"].isNumber()||!d["action"].isString())
                    throw std::runtime_error("Invalid conflict decision");
                decisions.push_back({int(d["id"].integer()),d.getStr("action")});
            }
            // Server-side safeguard: a browser can change decision values
            // after preview. Never move a rules model reference that the
            // reference graph cannot rewrite safely across included rules files.
            Json currentPlan=pendingAssetTransfer_->describe();
            if (currentPlan.getStr("source")=="internal") {
                StudioAssetCatalog dependencyCatalog(ws_.project.dir);
                dependencyCatalog.rebuild();
                const auto dependencies=studioCollectAssetReferences(
                    editor::loadMaterialLibrary(ws_),ws_.project.rulesJson,
                    dependencyCatalog.all());
                const Json impacted=studioMoveReferenceImpact(currentPlan,dependencies);
                for (const auto& reference:impacted.items()) {
                    if (reference.getBool("writable")) continue;
                    const int id=int(reference["id"].integer());
                    std::string action;
                    for (const auto& item:currentPlan["items"].items())
                        if (int(item["id"].integer())==id)
                            action=item.getStr("conflict").empty()?"import":"skip";
                    for (const auto& decision:decisions)
                        if (decision.id==id) action=decision.action;
                    if (action!="skip")
                        throw std::runtime_error("Asset move blocked: rules model reference "+
                            reference.getStr("owner")+" / "+reference.getStr("field")+
                            " cannot yet be rewritten safely. Skip the item or edit its rules origin first.");
                }
            }
            // Preload material document before touching assets, so malformed
            // JSON cannot trigger a partially completed internal move.
            const bool internal=currentPlan.getStr("source")=="internal";
            Json library=internal ? editor::loadMaterialLibrary(ws_) : Json::object();
            std::map<std::string,std::string> oldPathByGuid;
            if (internal) {
                StudioAssetCatalog beforeCatalog(ws_.project.dir);
                beforeCatalog.rebuild();
                for (const auto& asset:beforeCatalog.all())
                    if (!asset.directory) oldPathByGuid.emplace(asset.guid,asset.path);
            }
            std::size_t repaired=0;
            // This hook executes while internal file/.meta moves are still
            // reversible. If the material write throws, apply() reverses
            // every completed rename before returning an error.
            const auto repair=[&]() {
                StudioAssetCatalog afterCatalog(ws_.project.dir);
                afterCatalog.rebuild();
                std::map<std::string,std::string> relocated;
                for (const auto& asset:afterCatalog.all()) {
                    if (asset.directory) continue;
                    const auto found=oldPathByGuid.find(asset.guid);
                    if (found!=oldPathByGuid.end() && found->second!=asset.path)
                        relocated.emplace("content/"+found->second,"content/"+asset.path);
                }
                const std::size_t updates=studioRemapMaterialReferences(library,relocated);
                if (updates) editor::saveMaterialLibrary(ws_,library,
                    "Studio Assets: repair material paths after move");
                repaired=updates;
            };
            const std::function<void()> commitHook =
                internal ? std::function<void()>(repair) : std::function<void()>{};
            Json result=pendingAssetTransfer_->apply(token,decisions,commitHook);
            pendingAssetTransfer_.reset();
            if (internal) result.set("repairedMaterialReferences",num(int64_t(repaired)));
            reply.set("result",std::move(result));
        } else if (method == "studio.assetByGuid") {
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            const std::string guid=params.getStr("guid");
            if(!studioValidGuid(guid))throw std::runtime_error("Invalid asset GUID");
            StudioAssetCatalog catalog(ws_.project.dir);
            const auto entry=catalog.byGuid(guid);
            if(!entry)throw std::runtime_error("Asset GUID not found; refresh Assets");
            Json item=Json::object();
            item.set("guid",str(entry->guid));
            item.set("path",str(entry->path));
            item.set("importer",str(entry->importer));
            item.set("directory",Json::fromBool(entry->directory));
            item.set("bytes",Json::number(entry->bytes));
            item.set("settings",entry->settings);
            reply.set("result",std::move(item));
        } else if (method == "studio.select") {
            // mode: replace (default) | add | toggle; `ids` selects several at once (marquee).
            const std::string mode=params.getStr("mode","replace");
            if(mode!="replace" && mode!="add" && mode!="toggle")throw std::runtime_error("Invalid selection mode");
            std::vector<std::string> ids;
            if(params["ids"].isArray()) {
                if(params["ids"].size()>100000)throw std::runtime_error("Selection too large");
                for(const auto& v:params["ids"].items()){if(!v.isString())throw std::runtime_error("Invalid selection");ids.push_back(v.str());}
            } else {
                const Json& id = params["id"];
                if (!id.isNull() && !id.isString()) throw std::runtime_error("Invalid selection");
                if(id.isString() && !id.str().empty())ids.push_back(id.str());
            }
            for(const auto& id:ids)if(!documentHas(id))throw std::runtime_error("Selection refers to an unknown document entity");
            std::vector<std::string> current=mode=="replace"?std::vector<std::string>{}:selectedObjectIds();
            for(const auto& id:ids) {
                const auto at=std::find(current.begin(),current.end(),id);
                if(mode=="toggle" && at!=current.end()){current.erase(at);continue;}
                if(at!=current.end())current.erase(at);
                current.push_back(id); // last = primary
            }
            if(current.empty())clearSelection();
            else {selected_=current.back();current.pop_back();selection_=std::move(current);}
            reply.set("result", snapshot());
        } else if (method == "studio.entitiesDelete" || method == "studio.entitiesDuplicate") {
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            if (ws_.map.data.worldSpace) throw std::runtime_error("This operation requires a map entity document");
            std::vector<int> indices;
            for(const auto& v:params["entityIds"].items()) {
                if(!v.isString() || v.str().compare(0,7,"entity:")!=0)continue; // lights/colliders: not map entities
                const int index=indexFor(v.str());
                if(std::find(indices.begin(),indices.end(),index)==indices.end())indices.push_back(index);
            }
            if(indices.empty())throw std::runtime_error("Select map entities first");
            if(method=="studio.entitiesDuplicate") {
                const auto copies=editor::worldedit::duplicate(ws_.map,indices);
                if(copies.empty())throw std::runtime_error("Unable to duplicate entities");
                std::vector<std::string> next;
                for(const int c:copies)next.push_back(studioDocumentId(editor::DocumentEntityRef::mapEntity(size_t(c)),ws_.map));
                selected_=next.back();next.pop_back();selection_=std::move(next);
            } else {
                validateWorldProcessEntityDeletion(ws_.map,indices);
                std::sort(indices.rbegin(),indices.rend()); // highest first: lower indices stay valid
                const size_t depth=ws_.map.undoDepth();
                try {
                    for(const int index:indices) {
                        Json args=Json::object(); args.set("index",num(index));
                        const Json result=ws_.call("entity_delete",args,"studio");
                        if (result.has("error")) throw std::runtime_error(result.getStr("error"));
                    }
                } catch(...) { ws_.map.abandonSince(depth); throw; }
                ws_.map.mergeUndoSince(depth);
                clearSelection();dropOrdinalFlags();
            }
            reply.set("result",snapshot());
        } else if (method == "studio.entityFlags") {
            // Editor-only Hierarchy flags (not saved): hide from the Scene, lock against picking.
            for(const auto& v:params["entityIds"].items()) {
                if(!v.isString() || !documentHas(v.str()))throw std::runtime_error("Unknown document entity");
                for(const auto& [key,set]:{std::pair<const char*,std::set<std::string>*>{"hidden",&hidden_},{"locked",&locked_}}) {
                    const Json& flag=params[key];
                    if(flag.isNull())continue;
                    if(!(flag.boolean(false)==flag.boolean(true)))throw std::runtime_error("Flags must be booleans");
                    if(flag.boolean())set->insert(v.str()); else set->erase(v.str());
                }
            }
            reply.set("result",snapshot());
        } else if (method == "studio.assetPlace") {
            // Drop a model from Assets into the Scene: a new authored entity with
            // Transform + StaticModel. Needs a scenery type in rules.json.
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            if (ws_.map.data.worldSpace) throw std::runtime_error("This operation requires a map entity document");
            const std::string asset=params.getStr("asset");
            for(const char* key:{"x","z"}){const double v=params[key].toDouble(NAN);if(!std::isfinite(v)||std::abs(v)>1000000)throw std::runtime_error("Drop position out of range");}
            std::string type;
            for(const char* candidate:{"prop","props","decor","decoration","scenery","doodad","static"})
                if(ws_.project.rules.findType(candidate)>=0){type=candidate;break;}
            if(type.empty() && !selected_.empty() && selected_.compare(0,7,"entity:")==0)
                type=ws_.map.data.entities.at(size_t(indexFor(selected_))).type;
            if(type.empty())throw std::runtime_error("Add a scenery type (\"prop\") to rules.json, or select an entity whose type to use");
            Json place=Json::object();place.set("type",str(type));place.set("x",params["x"]);place.set("z",params["z"]);place.set("owner",num(int64_t(0)));
            const size_t depth=ws_.map.undoDepth();
            try {
                const Json placed=ws_.call("entity_place",place,"studio");
                if(placed.has("error"))throw std::runtime_error(placed.getStr("error"));
                const int index=int(ws_.map.data.entities.size())-1;
                auto candidate=promotedAuthored(index);
                auto components=candidate.extra["components"];components.set("StaticModel",Json::object());candidate.extra.set("components",components);
                ws_.map.beginEdit();ws_.map.data.entities[size_t(index)]=std::move(candidate);ws_.map.touched();
                const std::string id=studioDocumentId(ws_.map,editor::DocumentEntityRef::mapEntity(size_t(index)));
                if(!asset.empty())setMeshRenderer(index,"asset",str(asset)); // validates the path
                selectOnly(id);
            } catch(...) { ws_.map.abandonSince(depth); throw; }
            ws_.map.mergeUndoSince(depth);
            reply.set("result",snapshot());
        } else if (method == "studio.project") {
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            Json state=Json::object(),maps=Json::array();
            for (const auto& item:ws_.project.manifest["maps"].items()) {
                Json row=Json::object();row.set("file",str(item.getStr("file")));
                row.set("title",str(item.getStr("title",item.getStr("file"))));maps.push(std::move(row));
            }
            state.set("maps",std::move(maps));state.set("currentMap",str(ws_.map.path.empty()?"":ws_.relative(ws_.map.path)));
            state.set("canCreateMap",Json::fromBool(!ws_.map.data.worldSpace));
            state.set("maxMapSize",num(game::kMaxMapSize));reply.set("result",std::move(state));
        } else if (method == "studio.openMap" || method == "studio.newMap") {
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            if (method == "studio.openMap") {
                const auto file=params.getStr("file");bool listed=false;
                for (const auto& item:ws_.project.manifest["maps"].items()) listed|=item.getStr("file")==file;
                if (file.empty() || !listed) throw std::runtime_error("Choose a map from the current project");
                namespace fs=std::filesystem;
                const auto root=fs::weakly_canonical(ws_.project.dir);
                const auto path=fs::weakly_canonical(root/file);
                const auto relative=path.lexically_relative(root);
                if (relative.empty() || relative.is_absolute() || *relative.begin()=="..")
                    throw std::runtime_error("Map path escapes the project");
                editor::MapDoc draft;std::string error;
                if (!draft.load(path.string(),&error)) throw std::runtime_error(error);
                if (ws_.map.data.worldSpace!=draft.data.worldSpace) throw std::runtime_error("Map format does not match the project");
                draft.revision=ws_.map.revision+1;
                ws_.map=std::move(draft);
                ws_.note({editor::Change::Map,0,0,-1,-1,-1,"Opened map"});
            } else {
                if (ws_.map.data.worldSpace) throw std::runtime_error("World-space map creation requires a world-space document template");
                for (const char* key:{"width","height"}) {
                    const double value=params[key].toDouble();
                    if (!params[key].isNumber() || !std::isfinite(value) || value!=std::floor(value) || value<8 || value>game::kMaxMapSize)
                        throw std::runtime_error("Invalid map dimensions");
                }
                const auto name=params.getStr("name");
                if (name.empty() || name.size()>256) throw std::runtime_error("Map name is required (maximum 256 bytes)");
                Json args=Json::object();for (const char* key:{"width","height","name"})args.set(key,params[key]);
                const auto result=ws_.call("map_new",args,"studio");
                if (result.has("error")) throw std::runtime_error(result.getStr("error"));
                ws_.map.touched();
            }
            clearSelection();hidden_.clear();locked_.clear();previewMaterialName_.clear();refreshMaterialPreview();
            reply.set("result",snapshot());
        } else if (method == "studio.entityCatalog") {
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            Json catalog=Json::object(), types=Json::array();
            for (const auto& type : ws_.project.rules.types) {
                Json row=Json::object(); row.set("id",str(type.id)); row.set("name",str(type.name));
                types.push(std::move(row));
            }
            catalog.set("types",std::move(types));
            catalog.set("mapEntities",Json::fromBool(!ws_.map.data.worldSpace));
            reply.set("result",std::move(catalog));
        } else if (method == "studio.entityCreate" || method == "studio.entityDelete" ||
                   method == "studio.entityDuplicate") {
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            if (ws_.map.data.worldSpace) throw std::runtime_error("This operation requires a map entity document");
            if (method == "studio.entityCreate") {
                const std::string type=params.getStr("type");
                if (ws_.project.rules.findType(type)<0) throw std::runtime_error("Unknown entity type");
                for (const char* key : {"x","z","owner"}) {
                    if (!params[key].isNumber()) throw std::runtime_error("Expected numeric entity field");
                    const double value=params[key].toDouble();
                    if (!std::isfinite(value) || value < -1000000 || value > 1000000)
                        throw std::runtime_error("Entity field out of range");
                }
                const double owner=params["owner"].toDouble();
                if (owner!=std::floor(owner) || owner < -1 || owner > 10000)
                    throw std::runtime_error("Invalid owner");
                Json args=Json::object();
                for (const char* key : {"type","x","z","owner"}) args.set(key,params[key]);
                const Json result=ws_.call("entity_place",args,"studio");
                if (result.has("error")) throw std::runtime_error(result.getStr("error"));
                selectOnly("entity:"+std::to_string(ws_.map.data.entities.size()-1));
            } else {
                const std::string id=params.getStr("entityId");
                if (selected_.empty() || id!=selected_) throw std::runtime_error("Selection changed while editing");
                const int index=indexFor(id);
                if (method == "studio.entityDelete") {
                    validateWorldProcessEntityDeletion(ws_.map,{index});
                    Json args=Json::object(); args.set("index",num(index));
                    const Json result=ws_.call("entity_delete",args,"studio");
                    if (result.has("error")) throw std::runtime_error(result.getStr("error"));
                    clearSelection();dropOrdinalFlags();
                } else {
                    const auto copies=editor::worldedit::duplicate(ws_.map,{index});
                    if (copies.empty()) throw std::runtime_error("Unable to duplicate entity");
                    selectOnly(studioDocumentId(editor::DocumentEntityRef::mapEntity(size_t(copies.front())),ws_.map));
                }
            }
            reply.set("result",snapshot());
        } else if (method == "studio.entityMove") {
            // Hierarchy drag: reorder authored map entities (document order), undoable.
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            if (ws_.map.data.worldSpace) throw std::runtime_error("This operation requires a map entity document");
            const int from=indexFor(params.getStr("entityId"));
            auto& entities=ws_.map.data.entities;
            int to=int(entities.size());
            if (params["targetId"].isString()) {
                to=indexFor(params.getStr("targetId"));
                if (params.getStr("placement")=="after") ++to;
            }
            if (to!=from && to!=from+1) {
                ws_.map.beginEdit();
                // Roads reference entities by index: remap them with the permutation.
                std::vector<int> order(entities.size());
                for (size_t i=0;i<order.size();++i) order[i]=int(i);
                order.erase(order.begin()+from);
                if (to>from) --to;
                order.insert(order.begin()+to,from);
                std::vector<int> newIndex(order.size());
                decltype(ws_.map.data.entities) reordered;reordered.reserve(entities.size());
                for (size_t k=0;k<order.size();++k){newIndex[size_t(order[k])]=int(k);reordered.push_back(std::move(entities[size_t(order[k])]));}
                entities=std::move(reordered);
                for (auto& [a,b]:ws_.map.data.roads){a=newIndex[size_t(a)];b=newIndex[size_t(b)];}
                ws_.map.touched();
                selectOnly(studioDocumentId(ws_.map,editor::DocumentEntityRef::mapEntity(size_t(to))));dropOrdinalFlags();
            }
            reply.set("result",snapshot());
        } else if (method == "studio.save") {
            const Json r = ws_.call("map_save", Json::object(), "studio");
            if (r.has("error")) throw std::runtime_error(r.getStr("error"));
            std::string error;
            if (ws_.project.rulesDirty && !ws_.project.saveRules(&error)) throw std::runtime_error(error);
            if (ws_.project.manifestDirty && !ws_.project.saveManifest(&error)) throw std::runtime_error(error);
            reply.set("result", snapshot());
        } else if (method == "studio.undo") {
            if (!ws_.map.canUndo()) throw std::runtime_error("Nothing to undo");
            ws_.map.undo();
            clearSelection();dropOrdinalFlags();
            reply.set("result", snapshot());
        } else if (method == "studio.redo") {
            if (!ws_.map.canRedo()) throw std::runtime_error("Nothing to redo");
            ws_.map.redo();
            clearSelection();dropOrdinalFlags();
            reply.set("result", snapshot());
        } else if (method == "studio.propertiesFor") {
            reply.set("result", propertiesFor(params.getStr("entityId")));
        } else if (method == "studio.inspect") {
            reply.set("result", inspectorFor(params.getStr("entityId")));
        } else if (method == "studio.rules" || method == "studio.rulesSet") {
            // Rules form: unit types as written (own fields) and resolved (with "extends").
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            if (method == "studio.rulesSet") {
                const std::string path=params.getStr("path");
                static const std::regex allowed(R"(^types\.[A-Za-z0-9_\-]+((\.[A-Za-z0-9_]+)|(\[[0-9]{1,4}\])){0,6}$)");
                if (path.size()>200 || !std::regex_match(path,allowed)) throw std::runtime_error("Only unit type fields can be edited here");
                Json args=Json::object();args.set("path",str(path));
                const bool erase=params["value"].isNull();
                if (!erase) args.set("value",params["value"]);
                const Json changed=ws_.call(erase?"rules_delete":"rules_set",args,"studio");
                if (changed.has("error")) throw std::runtime_error(changed.getStr("error"));
            }
            Json own=Json::object(),resolvedArgs=Json::object();own.set("path",str("types"));
            resolvedArgs.set("path",str("types"));resolvedArgs.set("resolved",Json::fromBool(true));
            const Json types=ws_.call("rules_get",own,"studio"), resolved=ws_.call("rules_get",resolvedArgs,"studio"),
                       list=ws_.call("rules_list",Json::object(),"studio");
            for (const auto* r:{&types,&resolved,&list}) if (r->has("error")) throw std::runtime_error(r->getStr("error"));
            Json out=Json::object();
            out.set("types",types["value"]);out.set("resolved",resolved["value"]);
            out.set("resources",list["resources"]);out.set("summary",list["types"]);
            out.set("dirty",Json::fromBool(ws_.project.rulesDirty));
            reply.set("result",std::move(out));
        } else if (method == "studio.stats") {
            // Statistics: map contents and Scene timing (the legacy Stats window's editor part).
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            const auto& d=ws_.map.data;
            std::map<std::string,int64_t> byType;std::map<int,int64_t> byOwner;
            for (const auto& e:d.entities){++byType[e.type];++byOwner[e.owner];}
            Json types=Json::array();
            std::vector<std::pair<std::string,int64_t>> sorted(byType.begin(),byType.end());
            std::sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.second>b.second||(a.second==b.second&&a.first<b.first);});
            for (const auto& [t,n]:sorted){Json o=Json::object();o.set("type",str(t));o.set("count",num(n));types.push(std::move(o));}
            Json owners=Json::array();
            for (const auto& [o,n]:byOwner){
                Json j=Json::object();j.set("owner",num(int64_t(o)));j.set("count",num(n));
                j.set("name",str(o==0?std::string("Neutral"):o<0?std::string("Hostile"):o<=int(d.players.size())?d.players[size_t(o-1)].name:"Player "+std::to_string(o)));
                owners.push(std::move(j));
            }
            Json out=Json::object();
            out.set("map",str(ws_.map.path.empty()?std::string("(unsaved)"):ws_.relative(ws_.map.path)));
            out.set("width",num(int64_t(d.w)));out.set("height",num(int64_t(d.h)));
            out.set("entities",num(int64_t(d.entities.size())));out.set("players",num(int64_t(d.players.size())));
            out.set("roads",num(int64_t(d.roads.size())));
            out.set("types",types);out.set("owners",owners);
            out.set("ruleTypes",num(int64_t(ws_.project.rules.types.size())));
            out.set("undoSteps",num(int64_t(ws_.map.undoDepth())));
            out.set("dirty",Json::fromBool(ws_.map.dirty||ws_.project.rulesDirty||ws_.project.manifestDirty));
            out.set("sceneFrameMs",Json::number(sceneFrameMs_,2));out.set("sceneFrames",num(int64_t(sceneFrames_)));
            reply.set("result",std::move(out));
        } else if (method == "studio.extensions" || method == "studio.extensionAction" || method == "studio.extensionCreate") {
            // Extensions panel: the project's declarative Lua editor API (StudioLuaPanel.h).
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            if (method == "studio.extensionCreate") {
                std::string err;
                if (!StudioLuaPanel::createExample(ws_.project.dir,&err)) throw std::runtime_error(err);
            }
            if (method == "studio.extensionAction") {
                const std::string id=params.getStr("id");
                if (id.empty()||id.size()>128) throw std::runtime_error("Invalid widget id");
                Json command=lua_.action(ws_.project.dir,luaContext(),id,params.has("value")?&params["value"]:nullptr);
                // select_entity names a map index; the panel selects by Studio id.
                if (command.isObject() && command.getStr("command")=="select_entity") {
                    const int64_t index=command["index"].integer(-1);
                    if (index>=0 && index<int64_t(ws_.map.data.entities.size()))
                        command.set("entityId",str(studioDocumentId(ws_.map,editor::DocumentEntityRef::mapEntity(size_t(index)))));
                }
                Json out=Json::object();out.set("command",command);
                reply.set("result",std::move(out));
            } else {
                Json out=lua_.window(ws_.project.dir,luaContext());
                out.set("gizmos",lua_.gizmos(ws_.project.dir,luaContext()));
                reply.set("result",std::move(out));
            }
        } else if (method == "studio.attachmentsGet" || method == "studio.attachmentsSet") {
            // Attachments: an attachment layout JSON (parent model + children on named sockets), read and
            // written through the engine codec, so invalid layouts are refused with its message.
            namespace fs=std::filesystem;
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            const std::string rel=params.getStr("path","attachments.json");
            const fs::path relPath=fs::path(rel).lexically_normal();
            if (rel.empty()||rel.size()>300||relPath.is_absolute()||rel.find("..")!=std::string::npos||relPath.extension()!=".json")
                throw std::runtime_error("Choose a .json file inside the project");
            const fs::path file=fs::path(ws_.project.dir)/relPath;
            view::AttachmentLayout layout;
            if (method=="studio.attachmentsSet") {
                view::AttachmentLayoutCodec::read(params["document"],layout);   // throws on invalid data
                const Json canonical=view::AttachmentLayoutCodec::write(layout);
                std::error_code ec;fs::create_directories(file.parent_path(),ec);
                std::ofstream out(file,std::ios::binary|std::ios::trunc);
                if (!out || !(out<<canonical.dump(2)<<"\n")) throw std::runtime_error("Unable to write "+rel);
            }
            Json result=Json::object();
            std::error_code ec;
            const bool exists=fs::is_regular_file(file,ec);
            result.set("path",str(relPath.generic_string()));result.set("exists",Json::fromBool(exists));
            if (exists) {
                try { view::AttachmentLayoutCodec::read(Json::parseFile(file.string()),layout); result.set("document",view::AttachmentLayoutCodec::write(layout)); }
                catch (const std::exception& e) { result.set("error",str(e.what())); }
            } else {
                Json empty=Json::object();empty.set("parent",str(""));empty.set("attachments",Json::array());
                result.set("document",std::move(empty));
            }
            reply.set("result",std::move(result));
        } else if (method == "studio.keepPlayChanges") {
            // Play → map: positions and resource amounts of map entities as the running game has them now,
            // optionally removing the ones that died. Matched by index AND type (a map edited during play
            // is not touched where they disagree). One History step "Keep play changes".
            const Json& states=params["entities"];
            if (!states.isArray() || states.size()>200000) throw std::runtime_error("Invalid play state");
            const bool removeDead=params.getBool("removeDead");
            auto& d=ws_.map.data;
            std::vector<size_t> dead;
            int moved=0,amounts=0,skipped=0;
            bool begun=false;
            auto begin=[&]{if(!begun){ws_.map.beginEdit();begun=true;}};
            for (const auto& st:states.items()) {
                const int64_t i=st["index"].integer(-1);
                if (i<0 || i>=int64_t(d.entities.size())) {++skipped;continue;}
                auto& e=d.entities[size_t(i)];
                if (st.getBool("removed")) { if(removeDead)dead.push_back(size_t(i)); continue; }
                if (st.has("type") && st.getStr("type")!=e.type) {++skipped;continue;}
                if (st["x"].isNumber() && st["z"].isNumber()) {
                    const float x=float(st["x"].toDouble()),z=float(st["z"].toDouble());
                    if (std::isfinite(x)&&std::isfinite(z)&&std::hypot(x-e.x,z-e.z)>0.01f) {
                        begin();e.x=x;e.z=z;
                        if (e.extra["components"]["Transform"].isObject()) {
                            Json comps=e.extra["components"],tr=comps["Transform"];
                            tr.set("x",Json::number(double(x),3));tr.set("z",Json::number(double(z),3));
                            comps.set("Transform",std::move(tr));e.extra.set("components",std::move(comps));
                        }
                        ++moved;
                    }
                }
                if (st["amount"].isNumber()) {
                    const float a=float(st["amount"].toDouble());
                    // A map entity without an amount uses its type's default: only a real change is kept.
                    const float current=e.amount>=0?e.amount:float(st["defaultAmount"].toDouble(double(a)));
                    if (std::isfinite(a)&&a>=0&&std::abs(a-current)>0.01f) {begin();e.amount=a;++amounts;}
                }
            }
            int removed=0;
            if (!dead.empty()) {
                std::sort(dead.rbegin(),dead.rend());
                begin();
                for (size_t idx:dead) {
                    // Roads are entity index pairs: drop the ones that end here, shift the rest.
                    std::vector<std::pair<int,int>> roads;
                    for (auto [a,b]:d.roads) {
                        if (a==int(idx)||b==int(idx)) continue;
                        roads.push_back({a>int(idx)?a-1:a,b>int(idx)?b-1:b});
                    }
                    d.roads=std::move(roads);
                    d.entities.erase(d.entities.begin()+long(idx));
                    ++removed;
                }
                clearSelection();dropOrdinalFlags();
            }
            if (begun) { ws_.map.touched(); ws_.log("studio","Keep play changes: "+std::to_string(moved)+" moved, "+std::to_string(amounts)+" amounts, "+std::to_string(removed)+" removed"); }
            Json out=Json::object();
            out.set("moved",num(int64_t(moved)));out.set("amounts",num(int64_t(amounts)));out.set("removed",num(int64_t(removed)));
            out.set("skipped",num(int64_t(skipped)));out.set("snapshot",snapshot());
            reply.set("result",std::move(out));
        } else if (method == "studio.splineEdit") {
            // Roads / Water panels: show node handles in the Scene ("road" | "river" | "" off).
            const std::string k=params.getStr("kind");
            if (k!="road"&&k!="river"&&!k.empty()) throw std::runtime_error("Unknown spline kind");
            splineEdit_=ws_.project.dir.empty()?std::string():k;
            if (!splineEdit_.empty()) brush_.armed=false;
            reply.set("result",splineHandles());
        } else if (method == "studio.sceneBrush") {
            // Arm / disarm the Scene brush and set its tool and settings (session only).
            auto& b=brush_;
            static const std::set<std::string> kinds={"terrain","paint","tiles","field","objects","road","river"};
            static const std::set<std::string> ops={"raise","lower","smooth","flatten","noise","erode"};
            static const std::set<std::string> modes={"set","add","max","erase"};
            auto number=[&](const char* k,double lo,double hi){const double v=params[k].toDouble();
                if(!params[k].isNumber()||!std::isfinite(v)||v<lo||v>hi)throw std::runtime_error(std::string(k)+" must be "+std::to_string(lo)+".."+std::to_string(hi));return v;};
            auto text=[&](const char* k){const std::string v=params.getStr(k);if(v.size()>128)throw std::runtime_error(std::string(k)+" is too long");return v;};
            if (params.has("kind")) { const std::string k=params.getStr("kind"); if(!kinds.count(k))throw std::runtime_error("Unknown brush tool"); b.kind=k; }
            if (params.has("op")) { const std::string op=params.getStr("op"); if(!ops.count(op))throw std::runtime_error("Unknown brush op"); b.op=op; }
            if (params.has("radius")) b.radius=number("radius",0.25,64);
            if (params.has("strength")) b.strength=number("strength",0,20);
            if (params.has("layer")) b.layer=int(number("layer",0,15));
            if (params.has("tile")) b.tile=text("tile");
            if (params.has("field")) b.field=text("field");
            if (params.has("fieldMode")) { const std::string m=params.getStr("fieldMode"); if(!modes.count(m))throw std::runtime_error("Unknown field mode"); b.fieldMode=m; }
            if (params.has("value")) b.value=number("value",-1000,1000);
            if (params.has("type")) b.type=text("type");
            if (params.has("owner")) b.owner=int(number("owner",-1,16));
            if (params.has("count")) b.count=int(number("count",1,50));
            if (params.has("spread")) b.spread=number("spread",0,32);
            if (params.has("roadType")) b.roadType=text("roadType");
            if (params.has("width")) b.width=number("width",1,200);
            if (params.has("armed")) { b.armed=params.getBool("armed")&&!ws_.project.dir.empty(); if (b.armed) splineEdit_.clear(); }
            reply.set("result",sceneBrushJson());
        } else if (method == "studio.agentInfo" || method == "studio.agentStart" || method == "studio.agentStop" || method == "studio.agentReset") {
            reply.set("result", agentRequest(method, params));
        } else if (method == "studio.buildInfo" || method == "studio.buildToolchain" || method == "studio.buildStart" ||
                   method == "studio.buildLog" || method == "studio.buildEstimate") {
            reply.set("result", buildRequest(method, params));
        } else if (method == "studio.componentCatalog") {
            reply.set("result", componentCatalog(params.getStr("entityId")));
        } else if (method == "studio.componentAdd" || method == "studio.componentRemove") {
            const std::string entityId=params.getStr("entityId"),componentId=params.getStr("componentId");
            if(entityId.compare(0,7,"entity:")!=0)throw std::runtime_error("Components can be changed on map entities only");
            const int index=indexFor(entityId);
            const auto& source=ws_.map.data.entities[size_t(index)];
            auto candidate=source;
            if(method=="studio.componentAdd") {
                if(componentId=="MeshRenderer") {
                    if(source.extra["components"].has("StaticModel"))throw std::runtime_error("Mesh Renderer is already on this entity");
                    if(source.extra["components"].has("NereidWater"))throw std::runtime_error("Water entities are drawn by NEREID");
                    candidate=promotedAuthored(index);
                    auto components=candidate.extra["components"];components.set("StaticModel",Json::object());
                    candidate.extra.set("components",components);
                } else if(lib::worldprocess::authoring::supports(componentId) ||
                          lib::worldprocess::linked::supports(componentId) ||
                          lib::worldprocess::extended::supports(componentId) ||
                          lib::worldprocess::extendedLinks::supports(componentId)) {
                    if(source.extra["components"].has(componentId))
                        throw std::runtime_error("World-process component is already present");
                    candidate=promotedAuthored(index);
                    auto components=candidate.extra["components"];
                    components.set(componentId,
                        lib::worldprocess::authoring::supports(componentId) ?
                        lib::worldprocess::authoring::defaults(componentId) :
                        lib::worldprocess::linked::supports(componentId) ?
                        lib::worldprocess::linked::defaults(componentId) :
                        lib::worldprocess::extended::supports(componentId) ?
                        lib::worldprocess::extended::defaults(componentId) :
                        lib::worldprocess::extendedLinks::defaults(componentId));
                    candidate.extra.set("components",components);
                } else throw std::runtime_error("This component cannot be added here");
            } else {
                const char* id=componentId=="MeshRenderer"?"StaticModel":componentId.c_str();
                if(componentId=="Transform"||componentId=="Unit"||componentId=="NereidWater")
                    throw std::runtime_error(componentId+" cannot be removed");
                // Deleting a target component must not silently leave a
                // linked World Process referencing the same stable map ID.
                if(lib::worldprocess::authoring::supports(componentId) && !source.id.empty()){
                    for(const auto& other:ws_.map.data.entities)
                        for(const char* kind:{"EchoPortal","EchoEmitter","EchoReceiver",
                                            "FractureBond","AetherDuct","AetherFlux"}){
                            if(lib::worldprocess::linked::targetType(kind)!=componentId)continue;
                            const auto& linkedDoc=other.extra["components"][kind];
                            if(!linkedDoc.isObject())continue;
                            for(const auto& [key,value]:linkedDoc.members())
                                if(lib::worldprocess::linked::isReference(kind,key) &&
                                   value.str()==source.id)
                                    throw std::runtime_error(
                                        "This component is referenced by "+std::string(kind)+
                                        "; clear its link before removal");
                        }
                }
                if(!source.extra["components"].has(id))throw std::runtime_error("Component is not on this entity");
                auto components=candidate.extra["components"];components.erase(id);candidate.extra.set("components",components);
            }
            ws_.map.beginEdit();ws_.map.data.entities[size_t(index)]=std::move(candidate);ws_.map.touched();
            reply.set("result",snapshot());
        } else if (method == "studio.entitiesAlign") {
            // Align / distribute / drop to ground for the selected map entities: ONE undo step.
            const std::string mode=params.getStr("mode");
            std::vector<std::string> ids;
            for(const auto& v:params["entityIds"].items())if(v.isString()&&v.str().rfind("entity:",0)==0)ids.push_back(v.str());
            if(ids.size()>10000)throw std::runtime_error("Too many entities");
            static const std::set<std::string> modes={"minX","centerX","maxX","minZ","centerZ","maxZ","distributeX","distributeZ","ground"};
            if(!modes.count(mode))throw std::runtime_error("Unknown align mode");
            const bool distribute=mode.rfind("distribute",0)==0;
            if(ids.size()<(mode=="ground"?1u:distribute?3u:2u))throw std::runtime_error(distribute?"Select at least three objects":"Select at least two objects");
            struct Item{std::string id;double x,z;};
            std::vector<Item> items;
            for(const auto& id:ids){const auto& e=ws_.map.data.entities.at(size_t(indexFor(id)));items.push_back({id,double(e.x),double(e.z)});}
            const bool onX=mode.back()=='X';
            auto coord=[&](const Item& i){return onX?i.x:i.z;};
            double lo=1e300,hi=-1e300;for(const auto& i:items){lo=std::min(lo,coord(i));hi=std::max(hi,coord(i));}
            std::vector<std::pair<std::string,Json>> edits;   // id -> {key: value}
            if(mode=="ground") {
                const float level=float(ws_.project.rules.levelHeight.toDouble());
                for(const auto& i:items){Json v=Json::object();v.set("y",Json::number(double(terrainPreviewHeight(ws_.map.data,float(i.x),float(i.z),level)),4));edits.push_back({i.id,v});}
            } else if(distribute) {
                std::sort(items.begin(),items.end(),[&](const Item& a,const Item& b){return coord(a)<coord(b);});
                for(size_t k=0;k<items.size();++k){Json v=Json::object();
                    v.set(onX?"x":"z",Json::number(lo+(hi-lo)*double(k)/double(items.size()-1),4));edits.push_back({items[k].id,v});}
            } else {
                const double target=mode.rfind("min",0)==0?lo:mode.rfind("max",0)==0?hi:(lo+hi)*.5;
                for(const auto& i:items){Json v=Json::object();v.set(onX?"x":"z",Json::number(target,4));edits.push_back({i.id,v});}
            }
            const size_t depth=ws_.map.undoDepth();
            try {
                for(const auto& [id,values]:edits) for(const auto& [key,value]:values.members()) {
                    Json nested=Json::object(),input=Json::object();
                    nested.set("id",request["id"]);nested.set("method",str("studio.setProperty"));
                    input.set("entityId",str(id));input.set("key",str(key));input.set("value",value);
                    nested.set("params",input);
                    const auto updated=dispatchImpl(nested);
                    if(updated.has("error"))throw std::runtime_error(updated.getStr("error"));
                }
            } catch(...) { ws_.map.abandonSince(depth); throw; }
            ws_.map.mergeUndoSince(depth);
            reply.set("result",snapshot());
        } else if (method == "studio.setProperties") {
            // Several properties of one component as ONE undo step (paste / reset).
            const Json& values=params["values"];
            if(!values.isObject() || values.members().empty())throw std::runtime_error("Missing property values");
            if(values.members().size()>64)throw std::runtime_error("Too many properties");
            std::vector<std::string> targets;
            if(params["entityIds"].isArray()){for(const auto& v:params["entityIds"].items())if(v.isString())targets.push_back(v.str());}
            else targets.push_back(params.getStr("entityId"));
            if(targets.empty() || targets.size()>10000)throw std::runtime_error("Invalid entity list");
            const std::string componentId=params.getStr("componentId");
            if(lib::worldprocess::linked::supports(componentId) ||
               lib::worldprocess::extendedLinks::supports(componentId)) {
                // Validate the complete new component on every target BEFORE
                // mutating the map. Switching both bond ends in one paste
                // must not fail merely because its intermediate state is
                // temporarily invalid; still commit as ONE Undo snapshot.
                std::vector<std::pair<size_t,editor::DocEntity>> updates;
                std::set<int> seen;
                for(const auto& target:targets) {
                    if(target.rfind("entity:",0)!=0)
                        throw std::runtime_error("Linked authoring requires a map entity");
                    const int index=indexFor(target);
                    if(!seen.insert(index).second)
                        throw std::runtime_error("Duplicate linked authoring target");
                    const auto& source=ws_.map.data.entities[size_t(index)];
                    const Json& original=source.extra["components"][componentId];
                    if(!original.isObject())
                        throw std::runtime_error("Component is not authored on this object");
                    Json edited=original;
                    for(const auto& [key,value]:values.members()) {
                        if(!lib::worldprocess::linked::validValue(componentId,key,value) ||
                           !validWorldProcessDocumentRef(ws_.map,componentId,key,value))
                            throw std::runtime_error("Invalid linked map reference or value: "+key);
                        edited.set(key,value);
                    }
                    std::string error;
                    if(!lib::worldprocess::linked::validate(componentId,edited,&error))
                        throw std::runtime_error(error);
                    if(edited!=original) {
                        auto candidate=source;
                        Json components=candidate.extra["components"];
                        components.set(componentId,std::move(edited));
                        candidate.extra.set("components",std::move(components));
                        updates.push_back({size_t(index),std::move(candidate)});
                    }
                }
                if(!updates.empty()) {
                    ws_.map.beginEdit();
                    for(auto& [index,entity]:updates)
                        ws_.map.data.entities[index]=std::move(entity);
                    ws_.map.touched();
                }
            }else{
                const size_t depth=ws_.map.undoDepth();
                try {
                    for(const auto& target:targets) for(const auto& [key,value]:values.members()) {
                        Json nested=Json::object(),input=Json::object();
                        nested.set("id",request["id"]);nested.set("method",str("studio.setProperty"));
                        input.set("entityId",str(target));input.set("key",str(key));input.set("value",value);
                        if(params.has("componentId"))input.set("componentId",params["componentId"]);
                        nested.set("params",input);
                        const auto updated=dispatchImpl(nested);
                        if(updated.has("error"))throw std::runtime_error(key+": "+updated.getStr("error"));
                    }
                } catch(...) { ws_.map.abandonSince(depth); throw; }
                ws_.map.mergeUndoSince(depth);
            }
            reply.set("result",snapshot());
        } else if (method == "studio.setProperty") {
            const std::string entityId = params.getStr("entityId");
            // A pinned Inspector may edit its own explicitly addressed entity while
            // the global selection points elsewhere. Both document branches below
            // validate the target ID against the current project before writing.
            if (entityId.empty()) throw std::runtime_error("Missing document entity ID");
            const std::string key = params.getStr("key");
            const Json& value = params["value"];
            const std::string componentId=params.getStr("componentId");
            if(lib::worldprocess::authoring::supports(componentId) ||
               lib::worldprocess::linked::supports(componentId) ||
               lib::worldprocess::extended::supports(componentId) ||
               lib::worldprocess::extendedLinks::supports(componentId)) {
                if(entityId.compare(0,7,"entity:")!=0)
                    throw std::runtime_error("Authoring applies only to map entities");
                const int index=indexFor(entityId);
                const auto& source=ws_.map.data.entities[size_t(index)];
                const Json& existing=source.extra["components"][componentId];
                if(!existing.isObject())throw std::runtime_error("Component is not authored on this object");
                Json staged=existing;
                std::string error;
                if(lib::worldprocess::extendedLinks::supports(componentId)){
                    if(!validWorldProcessDocumentRef(ws_.map,componentId,key,value))
                        throw std::runtime_error("Invalid extended link map target");
                    if(!lib::worldprocess::extendedLinks::edit(componentId,key,value,staged,&error))
                        throw std::runtime_error(error);
                } else if(lib::worldprocess::linked::supports(componentId)) {
                    if(!validWorldProcessDocumentRef(ws_.map,componentId,key,value))
                        throw std::runtime_error("Unknown or incompatible linked map object");
                    if(!lib::worldprocess::linked::edit(componentId,key,value,staged,&error))
                        throw std::runtime_error(error);
                } else if(lib::worldprocess::extended::supports(componentId)) {
                    if(!lib::worldprocess::extended::edit(componentId,key,value,staged,&error))
                        throw std::runtime_error(error);
                } else if(!lib::worldprocess::authoring::edit(componentId,key,value,staged,&error))
                    throw std::runtime_error(error);
                if(staged!=existing) {
                    auto candidate=source;
                    Json components=candidate.extra["components"];
                    components.set(componentId,std::move(staged));
                    candidate.extra.set("components",std::move(components));
                    ws_.map.beginEdit();
                    ws_.map.data.entities[size_t(index)]=std::move(candidate);
                    ws_.map.touched();
                }
                reply.set("result",snapshot());return reply;
            }
            if(componentId=="MeshRenderer") {
                reply.set("result",setMeshRenderer(indexFor(entityId),key,value));
                return reply;
            }
            if(entityId.compare(0,6,"light:")==0) {
                bool found=false;size_t index=0;
                for(const auto& node:editor::enumerateDocumentSceneNodes(ws_.map))
                    if(node.ref.isLight() && nodeId(ws_.map,node.ref)==entityId){index=node.ref.index;found=true;break;}
                if(!found)throw std::runtime_error("Light entity no longer exists");
                const std::string owner=key=="name"?"Entity":
                    (key=="x" || key=="z" || key=="height")?"Transform":"Light";
                if(params.has("componentId") && params.getStr("componentId")!=owner)
                    throw std::runtime_error("Property does not belong to this component");
                if(owner=="Entity") {
                    Json nested=Json::object(),input=Json::object();
                    nested.set("id",request["id"]);nested.set("method",str("studio.lightUpdate"));
                    input.set("index",num(int64_t(index)));input.set("key",str(key));input.set("value",value);nested.set("params",input);
                    const auto updated=dispatchImpl(nested);
                    if(updated.has("error"))throw std::runtime_error(updated.getStr("error"));
                } else {
                    editor::MapDoc draft;draft.data=ws_.map.data;
                    const auto ref=editor::DocumentEntityRef::light(index,ws_.map.data.worldSpace);
                    bool changed=false;
                    if(owner=="Transform") {
                        if(!value.isNumber() || !validLightScalar(key,value.toDouble()))
                            throw std::runtime_error("Invalid light transform");
                        editor::DocumentTransform position;
                        if(!editor::readDocumentTransform(draft,ref,position))throw std::runtime_error("Transform unavailable");
                        double& coordinate=key=="x"?position.x:key=="z"?position.z:position.y;
                        if(coordinate==value.toDouble()){reply.set("result",snapshot());return reply;}
                        coordinate=value.toDouble();changed=editor::setDocumentPosition(draft,ref,position);
                    } else {
                        const auto* current=editor::documentLight(draft,ref);
                        if(current && (*current)[key]==value){reply.set("result",snapshot());return reply;}
                        Json patch=Json::object();patch.set(key,value);
                        changed=editor::writeDocumentComponent(draft,ref,editor::DocumentComponentKind::Light,patch);
                    }
                    if(!changed)throw std::runtime_error("Invalid component property");
                    if(ws_.map.data.worldSpace) {
                        Json args=Json::object();args.set("map",draft.toJson());
                        const auto updated=ws_.call("actor_set",args,"studio");
                        if(updated.has("error"))throw std::runtime_error(updated.getStr("error"));
                    } else {ws_.map.beginEdit();ws_.map.data=std::move(draft.data);ws_.map.touched();}
                }
                reply.set("result",snapshot());return reply;
            }
            if (entityId.compare(0, 7, "entity:") != 0) {
                // Non-map document sources use the shared editor transform adapter.
                // actor_set validates the COMPLETE edited document atomically before
                // committing it with the existing MapDoc undo snapshots.
                if (!ws_.map.data.worldSpace ||
                    (key != "x" && key != "y" && key != "z") || !value.isNumber())
                    throw std::runtime_error("Unsupported document property");
                const double number = value.toDouble();
                if (!std::isfinite(number) || number < -1000000 || number > 1000000)
                    throw std::runtime_error("Invalid coordinate");

                bool found = false;
                editor::DocumentEntityRef ref;
                editor::DocumentTransform target{};
                for (const auto& node : editor::enumerateDocumentSceneNodes(ws_.map)) {
                    if (nodeId(ws_.map,node.ref) != entityId) continue;
                    if (node.ref.source == editor::DocumentEntityRef::Source::MapEntity ||
                        !node.hasPosition)
                        throw std::runtime_error("Document position is unavailable");
                    ref = node.ref;
                    target = node.position;
                    found = true;
                    break;
                }
                if (!found) throw std::runtime_error("Unknown document object");
                const double previous = key == "x" ? target.x : key == "y" ? target.y : target.z;
                if (previous == number) {
                    reply.set("result", snapshot());
                    return reply;
                }
                if (key == "x") target.x = number;
                else if (key == "y") target.y = number;
                else target.z = number;

                editor::MapDoc draft;
                draft.data.extra = ws_.map.toJson(); draft.data.worldSpace = true;
                if (!editor::setDocumentPosition(draft, ref, target))
                    throw std::runtime_error("Document transform validation failed");
                if (draft.toJson() == ws_.map.toJson()) {
                    reply.set("result", snapshot());
                    return reply; // serialized 3-decimal position unchanged
                }
                Json args = Json::object();
                args.set("map", draft.toJson());
                const Json op = ws_.call("actor_set", args, "studio");
                if (op.has("error")) throw std::runtime_error(op.getStr("error"));
                reply.set("result", snapshot());
                return reply;
            }
            const int index = indexFor(entityId);
            if(editor::authoredTransformKey(key)) {
               const auto& source=ws_.map.data.entities[size_t(index)];
               const bool legacy=!editor::authoredEntity(source);
               auto candidate=promotedAuthored(index);
               candidate=editor::editAuthoredPlacement(candidate,key,value);
               // The RTS runtime reads "facing" for the initial rotation: keep it in sync.
               if((key=="rotationY"||key=="yaw") && (legacy || source.extra.has("facing")))
                   candidate.extra.set("facing",Json::number(studioFacingToYaw(editor::authoredRotationDegrees(candidate)[1]),3));
               if(candidate.extra!=source.extra){ws_.map.beginEdit();ws_.map.data.entities[size_t(index)]=std::move(candidate);ws_.map.touched();}
               reply.set("result",snapshot());return reply;
           }
           if (key == "type") {
                if (!value.isString() || value.str().empty()) throw std::runtime_error("Invalid type");
            } else if (key == "facing") {
                if (!value.isNumber() || !std::isfinite(value.toDouble()) ||
                    value.toDouble() < 0 || value.toDouble() > 360) throw std::runtime_error("Rotation must be 0..360 degrees");
            } else if (key == "x" || key == "z" || key == "owner") {
                if (!value.isNumber()) throw std::runtime_error("Expected number");
                const double v = value.toDouble();
                if (!(v >= -1000000 && v <= 1000000)) throw std::runtime_error("Value out of range");
                if (key == "owner" && (v != double(value.integer()) || v < -1 || v > 10000))
                    throw std::runtime_error("Invalid owner");
            } else throw std::runtime_error("Property not editable");
            Json changes = Json::object(); changes.set(key, value);
            Json op = Json::object();
            op.set("index", num(index));
            op.set("set", std::move(changes));
            const Json r = ws_.call("entity_update", op, "studio");
            if (r.has("error")) throw std::runtime_error(r.getStr("error"));
            reply.set("result", snapshot());
        } else throw std::runtime_error("Unknown Studio method");
    } catch (const std::exception& ex) {
        reply.set("error", str(ex.what()));
    }
    return reply;
}
// Template packages of the catalog: {all, query, refresh}. all=false: the start-screen list (official catalog
// templates + engine templates the catalog does not replace). Runs aiverse_pkg.py (network): call off the UI thread.
Json hubTemplatesJson(const std::string& engineDir, const Json& params) {
    std::string error;
    Json list=Json::array();
    const bool all=params.getBool("all");
    const auto found=all ? editor::catalogTemplates(engineDir,true,params.getStr("query"),false,params.getBool("refresh"),&error)
                         : editor::hubTemplates(engineDir,false,&error);
    for(const auto& t:found)list.push(templateJson(t));
    Json out=Json::object();out.set("templates",list);
    if(!error.empty())out.set("error",str(error));
    return out;
}
}
