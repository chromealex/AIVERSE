#include "engine/platform/EnhancedInputPresets.h"
#include "StudioHierarchyGroups.h"
#include "apps/editor/core/BuildFixes.h"
#include "StudioOrientedCollider.h"
#include "StudioEntityNesting.h"
#include "aiverselib/world/EvolveAreaScaleMath.h"
#include <regex>
#include "apps/editor/core/GraphAssetTransaction.h"
#include <fstream>
#include "apps/editor/AuthoringWater.h"
#include "StudioHumanCharacterBuild.h"
#include "StudioSession.h"
#include "engine/platform/WindowSettings.h"
#include "engine/actor/ActorModule.h"
#include "StudioPlanetForestDocument.h"
#include "libraries/include/aiverselib/world/WorldProcessesAuthoring.h"
#include "libraries/include/aiverselib/world/WorldProcessesLinkedAuthoring.h"
#include "libraries/include/aiverselib/world/WorldProcessesExtendedAuthoring.h"
#include "libraries/include/aiverselib/world/WorldProcessesExtendedLinks.h"
#include "libraries/include/aiverselib/world/GranularDocument.h"
#include "apps/editor/core/TerraWeavePlanetCavernsDoc.h"
#include "engine/world/NereidFlowAuthoring.h"
#include "apps/editor/core/ProjectFile.h"
#include "engine/view/game/GameTerrainRender.h"
#include "apps/editor/core/Projects.h"
#include "StudioGameWizard.h"
#include "engine/game/MapGrid.h"
#include "apps/editor/EditorWorldEdit.h"
#include "apps/studio-native/StudioContentDirectory.h"
#include "apps/studio-native/StudioProjectDirectory.h"
#include "apps/studio-native/StudioAssetCatalog.h"
#include "apps/studio-native/StudioBuildingGeneration.h"
#include "apps/studio-native/StudioBuildingMaterialPublish.h"
#include "apps/studio-native/StudioBuildingDependencies.h"
#include "apps/studio-native/StudioBuildingRegeneration.h"
#include "engine/render/Model.h"
#include "apps/studio-native/StudioAssetReferences.h"
#include "apps/studio-native/StudioAssetUsage.h"
#include "apps/studio-native/StudioTools.h"
#include "apps/studio-native/StudioAgentHistoryPolicy.h"
#include "apps/studio-native/StudioHistoryCoordinator.h"
#include "apps/editor/core/ProjectHistoryPersistence.h"
#include "engine/pack/Sha256.h"
#include "engine/view/AttachmentLayout.h"
#include "apps/studio-native/StudioMoveJournal.h"
#include "apps/editor/core/ProjectAssetWriteLock.h"
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
#include "apps/editor/core/DocumentWorldPreview.h"
#include <charconv>
#include "engine/world/WorldPersistenceDiagnostics.h"
#include "engine/world/SimulationLod.h"
#include "libraries/include/aiverselib/mechanics/SpectraSourceDocument.h"
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

// Json retains number lexemes. Keep the native baseline intact across WebView number conversion.
Json graphOriginal(const Json& expected) {
    if (!expected.isString()) return expected;
    const auto& source = expected.str();
    if (source.size() > 1024 * 1024) throw std::invalid_argument("Original graph exceeds bridge limit");
    auto graph = Json::parse(source);
    if (!graph.isObject()) throw std::invalid_argument("Original graph object required");
    return graph;
}
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
    // Material writes inside an asset transaction may themselves use a document
    // journal. Resolve that inner transaction before rolling back the outer move.
    editor::recoverProjectFileEdits(directory);
    studioRecoverAssetJournal(directory);
    editor::ProjectAssetWriteLock projectLock(directory);
    StudioMoveJournal::recover(directory);
    return directory;
}

}
StudioSession::StudioSession(const std::string& engineRoot, const std::string& project) {
    ws_.engineDir = engineRoot;
    ws_.setSpectraAssetCheckHandler([this](const Json& params){
        Json request=Json::object();request.set("method",str("studio.spectraAssetCheck"));
        request.set("params",params);request.set("id",num(0));
        const Json response=dispatchImpl(request);
        if(response.has("error"))throw std::runtime_error(response.getStr("error"));
        return response["result"];
    });
    if (!project.empty()) {
        const auto directory=recoveredProjectDir(ws_,project);
        std::string error;
        if (!ws_.openGame(directory, &error)) throw std::runtime_error(error.empty() ? "Cannot open project" : error);
        // A project opened from the CLI follows the same content policy as
        // projects chosen through the native Open Project panel.
        (void)studioContentRoot(ws_.project.dir);
        loadAssetHistory();
        try { const auto pending=StudioMaterialPublicationJournal::inspect(ws_.project.dir);if(pending.size())ws_.log("studio","Pending material publications: "+pending.dump(-1),true); } catch(const std::exception& e){ws_.log("studio",e.what(),true);}
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
    const auto target=lib::worldprocess::extendedLinks::supports(componentId)?
        lib::worldprocess::extendedLinks::referenceTarget(componentId,field):
        lib::worldprocess::linked::isReference(componentId,field)?
        lib::worldprocess::linked::targetType(componentId):std::string_view{};
    if(target.empty())return true;
    if(!value.isString())return false;
    if(value.str().empty())return true; // allowed while disabled or unbound
    size_t found=0;
    for(const auto& entity:map.data.entities)
        if(entity.id==value.str() &&
           (target=="Entity"||
            entity.extra["components"].has(std::string(target))))++found;
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
            if(!(lib::worldprocess::linked::supports(name)||
                 lib::worldprocess::extendedLinks::supports(name))||!value.isObject())continue;
            for(const auto& [key,ref]:value.members()){
                if(!(lib::worldprocess::linked::isReference(name,key)||
                     lib::worldprocess::extendedLinks::isReference(name,key))||
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
// Validate a published graph through the shared Workspace compiler before
// writing a source declaration. Preview-only drafts are not placeable.
Json requireStudioSpectraGraph(editor::Workspace& ws,const std::string& name){
    if(!lib::mechanics::validSpectraSourceName(name))throw std::runtime_error("Invalid SpectraFX effect name");
    Json args=Json::object();args.set("name",str(name));
    const auto result=ws.call("fx_graph_get",args,"studio");
    if(result.has("error")||!result["validation"].getBool("ok")||result["graph"].getInt("version")!=2)
        throw std::runtime_error("Choose a published valid SpectraFX Graph v2");
    return result["graph"];
}
spectra::InstanceParameterTable studioSpectraParameterSchema(const Json& graph){
    spectra::InstanceParameterTable schema;std::vector<std::string> errors;
    if(!spectra::loadInstanceParameterTable(graph["instanceParameters"],schema,errors))
        throw std::runtime_error(errors.empty()?"Invalid instance parameter schema":errors.front());
    return schema;
}
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
    item("SpectraSource","VFX Renderer","Rendering","Authored SpectraFX source (native host presentation binding required)","native",
        !comps.has("SpectraSource"),comps.has("SpectraSource")?"Already on this entity":"");
    item("Light","Point Light","Rendering","Adds a light entity at this position","light",true,"");
    item("NereidWater","NEREID Water","World","Rivers, lakes and sea are authored in World › NEREID Water","water",false,"Create water from the NEREID Water panel");
    item("Unit","Unit","Gameplay","Type, owner and stats come from rules.json","native",false,"Defined by the entity type");
    for(const auto& name:{"EchoZone","FractureMember","AetherGasCell",
                          "EchoPortal","EchoEmitter","EchoReceiver",
                          "FractureBond","AetherDuct","AetherFlux",
                          "MorphNode","CrowdAgent","ImprintCell","CircuitNode",
                          "GranularField","EvolveRegion","EvolveArea","SocietyPerson",
                          "MorphSpring","MorphForce","CrowdSlot","CrowdPreference",
                          "ImprintLink","ImprintDeposit","ImprintTracker","CircuitWire","CircuitGate","CircuitSwitch","CircuitDevice",
                          "GranularSource","EvolvePressure","SocietyContact","SocietyMessage","SocietyNpcTrigger"}) {
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
bool StudioSession::selectedRoad(const std::vector<std::string>& ids, int& a, int& b, bool& linked) const {
    const auto& map = ws_.map;
    if (ids.size() != 2 || map.data.worldSpace || map.data.planet()) return false;
    for (const auto& id : ids) if (locked_.contains(id) || hidden_.contains(id)) return false;
    a = studioMapEntityIndex(ids[0], map);
    b = studioMapEntityIndex(ids[1], map);
    if (a < 0 || b < 0 || a == b) return false;
    linked = std::any_of(map.data.roads.begin(), map.data.roads.end(), [&](const auto& road) {
        return (road.first == a && road.second == b) || (road.first == b && road.second == a);
    });
    return true;
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
            const auto properties = propertiesFor(entityId);
            for(const auto& field:properties.items()) {
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
            if(e.at.isNull()){
                transform.push(numericProp("x","Position X",Json::number(double(e.x),4)));
                transform.push(numericProp("y","Position Y",authored?Json::number(comps["Transform"]["y"].toDouble(0),4):Json::number(ground,4)));
                transform.push(numericProp("z","Position Z",Json::number(double(e.z),4)));
            }
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
            if(!e.at.isNull()){
                Json location=Json::array(),bodyOptions=Json::array();
                for(const auto& body:ws_.map.data.bodies)if(body.surface()){
                    Json option=Json::object();
                    option.set("label",str(body.id));option.set("value",str(body.id));
                    bodyOptions.push(std::move(option));
                }
                Json bodyField=prop("body","Body","text",str(e.at.getStr("body")));
                bodyField.set("options",std::move(bodyOptions));
                location.push(std::move(bodyField));
                auto face=numericProp("face","Cube face",Json::number(e.at.getInt("face",0)),true);
                face.set("min",Json::number(int64_t(0)));face.set("max",Json::number(int64_t(5)));
                face.set("step",Json::number(int64_t(1)));location.push(std::move(face));
                const int bi=ws_.map.data.findBody(e.at.getStr("body"));
                const int n=bi>=0?ws_.map.data.bodies[size_t(bi)].faceSize:0;
                for(const char* key:{"x","y"}){
                    auto field=numericProp(key,key[0]=='x'?"Face X (cells)":"Face Y (cells)",e.at[key]);
                    field.set("min",Json::number(int64_t(0)));field.set("max",Json::number(int64_t(n)));
                    field.set("step",Json::number(0.01,2));location.push(std::move(field));
                }
                components.push(component("SurfacePlacement","Planet placement",location,
                    "Canonical authored body/face coordinates. Scene surface move or numeric editing."));
            }
            components.push(component("Transform","Transform",transform));
            if(ws_.map.data.planet()&&!e.id.empty()){
                const auto forest=studioPlanetForestDocument(ws_.map,ws_.project.rulesJson);
                Json fields=Json::array();
                size_t count=0;
                for(const auto& plant:forest["forest"]["plants"].items())if(plant.getStr("entity")==e.id){
                    if(count++>=16)break;
                    const auto key=std::to_string(plant.getInt("species"))+":"+std::to_string(plant.getInt("key"));
                    fields.push(prop(key.c_str(),"Species / plant key","text",str(key),false));
                }
                if(fields.size())components.push(component("PlanetForestMarker","Planet forest marker",fields,
                    "Read-only placement links. Edit the forest document in Rules > Planet forest."));
            }
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
                if(name=="SpectraSource"){
                    Json fields=Json::array();
                    fields.push(prop("effect","Effect","text",value["effect"]));
                    auto scale=numericProp("scale","Effect scale",value["scale"]);
                    scale.set("min",Json::number(.001));scale.set("max",Json::number(100.0));fields.push(scale);
                    fields.push(prop("enabled","Enabled","boolean",value["enabled"]));
                    fields.push(prop("killOnDetach","Kill on detach","boolean",value["killOnDetach"]));
                    const auto effect=value.getStr("effect");
                    if(lib::mechanics::validSpectraSourceName(effect)){
                        try{
                            namespace fs=std::filesystem;
                            const auto dir=fs::path(ws_.project.dir)/"fx";
                            const auto file=dir/"graphs"/(effect+".vfx.json");
                            if(!fs::is_symlink(fs::symlink_status(dir))&&!fs::is_symlink(fs::symlink_status(dir/"graphs"))&&
                               fs::is_regular_file(fs::symlink_status(file))&&fs::file_size(file)<=512*1024){
                                std::ifstream input(file,std::ios::binary);
                                const auto graph=Json::parse(std::string(std::istreambuf_iterator<char>(input),{}));
                                for(const auto& [name,def]:studioSpectraParameterSchema(graph)){
                                    const auto key="parameter:"+name;
                                    auto field=numericProp(key.c_str(),name.c_str(),value["parameters"].has(name)?value["parameters"][name]:Json::number(double(def.initial)));
                                    field.set("min",Json::number(double(def.minimum)));
                                    field.set("max",Json::number(double(def.maximum)));
                                    fields.push(std::move(field));
                                }
                            }
                        }catch(const std::exception&){/* invalid graph: keep base inspector accessible */}
                    }
                    fields.push(readOnly(numericProp("revision","Revision",value["revision"],true)));
                    components.push(component("SpectraSource","VFX Renderer",fields,"Authored source; runtime host binding required."));
                    continue;
                }
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
                Json display=value;
                if(extended&&name=="EvolveArea"){
                    if(!display.has("offsetXcm"))display.set("offsetXcm",Json::number(int64_t(0)));
                    if(!display.has("offsetZcm"))display.set("offsetZcm",Json::number(int64_t(0)));
                }
                if(extended&&name=="MorphNode"){
                    if(!display.has("gravityImpulseMilli"))display.set("gravityImpulseMilli",Json::number(int64_t(0)));
                    if(!display.has("gravityRelative"))display.set("gravityRelative",Json::fromBool(true));
                }
                if(display.isObject())for(const auto& [key,item]:display.members()) {
                    Json field=genericField(key,item);
                    if(authorable) {
                        field.set("editable",Json::fromBool(true));
                        if(extended&&name=="GranularField"&&value.has("width")&&
                           (key=="width"||key=="height"||key=="mass"||key=="solid"))
                            field.set("editable",Json::fromBool(false));
                        if((linked&&lib::worldprocess::linked::isReference(name,key)) ||
                           (extendedLinked&&lib::worldprocess::extendedLinks::isReference(name,key))) {
                            Json options=Json::array();
                            Json empty=Json::object();
                            empty.set("label",str("Unassigned"));empty.set("value",str(""));
                            options.push(std::move(empty));
                            const auto target=extendedLinked?
                                lib::worldprocess::extendedLinks::referenceTarget(name,key):
                                lib::worldprocess::linked::targetType(name);
                            for(const auto& candidate:ws_.map.data.entities){
                                if(candidate.id.empty() ||
                                   (target!="Entity" &&
                                    !candidate.extra["components"].has(std::string(target))))continue;
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
                            if(extendedLinked){
                                if(name=="MorphSpring") {
                                    minimum=key=="stiffness"?0:-morph::kPositionLimit;
                                    maximum=key=="stiffness"?1000:morph::kPositionLimit;
                                }else if(name=="MorphForce") {
                                    minimum=-morph::kForceLimit;maximum=morph::kForceLimit;
                                }else if(name=="CrowdSlot") {
                                    minimum=-crowd::kCoord;maximum=crowd::kCoord;
                                }else if(name=="ImprintLink"||name=="ImprintDeposit")
                                    maximum=imprint::kMaxRate;
                                else if(name=="CircuitGate")maximum=3;
                                else if(name=="GranularSource") {
                                    minimum=key=="delta"?-granular::kMaxSourceRate:0;
                                    maximum=key=="delta"?granular::kMaxSourceRate:granular::kMaxDimension-1;
                                }else if(name=="EvolvePressure")maximum=evolve::kMaxRate;
                                else if(name=="SocietyContact")maximum=1000;
                            }
                            if(extended){
                                if(name=="MorphNode"){
                                    if(key=="restX"||key=="restY"||key=="restZ"){
                                        minimum=-morph::kPositionLimit;maximum=morph::kPositionLimit;
                                    }else maximum=key=="gravityImpulseMilli"?1000000:1000;
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
                                else if(name=="EvolveArea"){
                                    const bool offset=key=="offsetXcm"||key=="offsetZcm";
                                    minimum=offset?-evolve::kAreaCoordinateCm:50;
                                    maximum=offset?evolve::kAreaCoordinateCm:evolve::kAreaMaxHalfExtentCm;
                                }
                                else if(name=="SocietyPerson")maximum=key=="credibility"?1000:1000000;
                            }
                            field.set("min",Json::number(minimum));
                            field.set("max",Json::number(maximum));
                            field.set("step",Json::number(int64_t(1)));
                            field.set("integer",Json::fromBool(true));
                        }
                    }
                    if(ws_.map.data.planet()&&(name=="CrowdAgent"||name=="CrowdSlot")&&
                       key!="enabled"){
                        field=readOnly(field);
                        field.set("description",str("Planet movement uses real placement and GameCore navigation; legacy planar field is inactive."));
                    }
                    if(ws_.map.data.planet()&&name=="EvolveArea")
                        field.set("description",str("Local east/south chart in centimetres, not global XZ."));
                    if(name=="MorphNode"&&key=="gravityImpulseMilli")
                        field.set("description",str("Opt-in PHYSIS load scale, 0 disables; 1000 = one impulse unit per m/s²."));
                    if(name=="MorphNode"&&key=="gravityRelative")
                        field.set("description",str("Subtract frame acceleration for free-fall deformation."));
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
                if(editor::readDocumentComponent(ws_.map,node.ref,descriptor.kind,data)){
                    if(std::string_view(descriptor.id)=="OrientedBoxCollider"){
                        Json pose;
                        if(editor::readDocumentComponent(ws_.map,node.ref,editor::DocumentComponentKind::Transform,pose)){
                            fields.push(numericProp("rotationY","Yaw (degrees)",pose["rotationY"]));
                            for(size_t axis=0;axis<3;++axis){
                                const auto key=std::string("scale")+"XYZ"[axis];
                                const auto label=std::string("Size ")+"XYZ"[axis];
                                auto dimension=numericProp(key.c_str(),label.c_str(),pose["scale"][axis]);
                                dimension.set("min",Json::number(0.001,3));
                                dimension.set("max",num(axis==1?200000:4096));
                                fields.push(std::move(dimension));
                            }
                            auto box=component(descriptor.id,descriptor.title,fields);
                            box.set("scope",graphDocumentScope());components.push(std::move(box));
                            continue;
                        }
                    }
                    if(std::string_view(descriptor.id)=="BoxCollider" && data["min"].size()==3 && data["max"].size()==3){
                        for(const auto* bound:{"min","max"})for(size_t axis=0;axis<3;++axis){
                            const auto key=std::string(bound)+"XYZ"[axis];
                            const auto label=std::string(bound==std::string("min")?"Minimum ":"Maximum ")+"XYZ"[axis];
                            fields.push(numericProp(key.c_str(),label.c_str(),data[bound][axis]));
                        }
                        auto box=component(descriptor.id,descriptor.title,fields);
                        box.set("scope",graphDocumentScope());
                        components.push(std::move(box));
                        continue;
                    }
                    for(const auto& [key,item]:data.members())fields.push(genericField(key,item));
                }
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
Json StudioSession::graphDocumentScope() const {
    Json scope=Json::object();
    scope.set("projectPath",str(ws_.project.dir));
    scope.set("mapPath",str(ws_.map.path));
    scope.set("epoch",str(std::to_string(graphDocumentEpoch_)));
    scope.set("revision",str(std::to_string(ws_.map.revision)));
    return scope;
}
// WebKit and WebView2 may reorder object members in bridge requests.
bool sameDocumentScope(const Json& supplied,const Json& current) {
    return supplied.isObject() && supplied.size()==current.size() &&
        std::all_of(current.members().begin(),current.members().end(),[&](const auto& field){
            return supplied[field.first]==field.second;
        });
}
Json StudioSession::mapProperties(const Json& params,bool write) {
    auto& doc=ws_.map;
    auto& data=doc.data;
    if(ws_.project.dir.empty() || !sameDocumentScope(params["scope"],graphDocumentScope()))
        throw std::runtime_error("Map changed. Reload its properties.");
    if(write){
        const auto& patch=params["changes"];
        if(!patch.isObject())throw std::runtime_error("Expected map property changes");
        for(const auto& [key,value]:patch.members()){
            if(key=="name" && value.isString() && !value.str().empty())continue;
            if(data.worldSpace)throw std::runtime_error("Unsupported world-space scene property: "+key);
            if(key=="roadsOnly" && value.type()==Json::Type::Bool)continue;
            if(key=="victory" && (value.isNull() || value.isObject()))continue;
            if(key=="size" && value.isArray() && value.size()==2 && !data.planet()){
                const auto valid=[](const Json& n){return n.isNumber() && n.toDouble()>=8 && n.toDouble()<=game::kMaxMapSize && n.toDouble()==n.integer();};
                if(valid(value[0]) && valid(value[1]))continue;
            }
            throw std::runtime_error("Invalid map property: "+key);
        }
        const bool resize=patch.has("size") && (patch["size"][0].integer()!=data.w || patch["size"][1].integer()!=data.h);
        const bool victory=patch.has("victory") && (patch["victory"].isNull()?data.extra.has("victory"):patch["victory"]!=data.extra["victory"]);
        if(resize || victory || (patch.has("name") && patch.getStr("name")!=data.name) ||
           (patch.has("roadsOnly") && patch.getBool("roadsOnly")!=data.roadsOnly)){
            if(resize)doc.resize(int(patch["size"][0].integer()),int(patch["size"][1].integer()));
            else doc.beginEdit();
            if(patch.has("name")){
                data.name=patch.getStr("name");
                if(data.worldSpace)data.extra.set("name",patch["name"]);
            }
            if(patch.has("roadsOnly"))data.roadsOnly=patch.getBool("roadsOnly");
            if(victory){
                if(patch["victory"].isNull())data.extra.erase("victory");
                else data.extra.set("victory",patch["victory"]);
            }
        }
        return snapshot();
    }
    Json out=Json::object();
    out.set("scope",graphDocumentScope());out.set("name",str(data.name));
    out.set("width",num(data.w));out.set("height",num(data.h));out.set("grid",Json::fromBool(!data.worldSpace && !data.planet()));
    if(data.worldSpace){
        Json world=Json::object();world.set("colliders",num(data.extra["colliders"].size()));
        world.set("spawns",num(data.extra["spawns"].size()));
        out.set("worldSpace",std::move(world));
    }
    out.set("maxSize",num(game::kMaxMapSize));out.set("entities",num(data.entities.size()));out.set("players",num(data.players.size()));
    out.set("areas",data.extra["areas"].isObject()?data.extra["areas"]:Json::object());
    out.set("roadsOnly",Json::fromBool(data.roadsOnly));out.set("victory",data.extra["victory"]);
    Json roster=Json::array(),factions=Json::array(),resources=Json::array();
    for(const auto& player:data.players){
        Json row=Json::object(),amounts=Json::object();
        row.set("name",str(player.name));row.set("team",num(player.team));
        row.set("waves",Json::fromBool(player.waves));row.set("faction",str(player.faction));
        if(player.hasStart){
            Json start=Json::array();start.push(Json::number(double(player.startX)));start.push(Json::number(double(player.startZ)));
            row.set("start",player.startAt.isObject()?player.startAt:std::move(start));
        }
        for(const auto& [id,amount]:player.resources)amounts.set(id,Json::number(double(amount)));
        row.set("resources",std::move(amounts));roster.push(std::move(row));
    }
    for(const auto& faction:ws_.project.rules.factions){
        Json item=Json::object();item.set("id",str(faction.id));item.set("name",str(faction.name));factions.push(std::move(item));
    }
    for(const auto& resource:ws_.project.rules.resources){
        if(resource.kind==game::ResourceKind::Capacity)continue;
        Json item=Json::object();item.set("id",str(resource.id));item.set("name",str(resource.name));
        item.set("initial",Json::number(resource.start.toDouble()));resources.push(std::move(item));
    }
    Json surfaces=Json::array();
    for(const auto& body:data.bodies)if(body.surface()){
        Json item=Json::object();item.set("id",str(body.id));item.set("size",num(body.faceSize));surfaces.push(std::move(item));
    }
    out.set("surfaces",std::move(surfaces));
    out.set("roster",std::move(roster));out.set("factions",std::move(factions));out.set("resources",std::move(resources));
    if(params.getBool("validate")){
        if(data.worldSpace)throw std::runtime_error("Grid map validation is unavailable for world-space scenes.");
        Json issues=Json::array();
        for(const auto& item:doc.validate(ws_.project.rules,ws_.project.rulesJson)){
            Json issue=Json::object();
            issue.set("error",Json::fromBool(item.error));issue.set("text",str(item.text));
            if(item.entity>=0 && size_t(item.entity)<data.entities.size())
                issue.set("entityId",str(nodeId(doc,editor::DocumentEntityRef::mapEntity(size_t(item.entity)))));
            issues.push(std::move(issue));
        }
        out.set("issues",std::move(issues));
    }
    return out;
}
Json StudioSession::mapPlayer(const Json& params){
    const auto& data=ws_.map.data;
    if(ws_.project.dir.empty() || data.worldSpace || !sameDocumentScope(params["scope"],graphDocumentScope()))
        throw std::runtime_error("Map changed. Reload the player list.");
    const auto action=params.getStr("action");
    const int count=int(data.players.size());
    const int slot=params.getInt("slot");
    if(!params["slot"].isNumber() || params["slot"].toDouble()!=slot || slot<1 ||
       (action=="add" ? slot!=count+1 || count>=16 : slot>count))
        throw std::runtime_error("Invalid player slot");
    Json args=Json::object();
    if(action=="edit"){
        const auto& changes=params["changes"];
        if(!changes.isObject() || changes.members().empty())throw std::runtime_error("Expected player changes");
        for(const auto& [key,value]:changes.members()){
            if(key=="name" && value.isString())continue;
            if(key=="team" && value.isNumber() && value.toDouble()>=std::numeric_limits<int>::min() &&
               value.toDouble()<=std::numeric_limits<int>::max() && value.toDouble()==value.integer())continue;
            if(key=="waves" && value.type()==Json::Type::Bool)continue;
            if(key=="faction" && value.isString() && (value.str().empty() || std::any_of(ws_.project.rules.factions.begin(),
               ws_.project.rules.factions.end(),[&](const auto& f){return f.id==value.str();})))continue;
            if(key=="start"){
                const auto coord=[](const Json& v){return v.isNumber() && std::isfinite(float(v.toDouble()));};
                if(value.isNull() || (!data.planet() && value.isArray() && value.size()==2 && coord(value[0]) && coord(value[1])))continue;
                if(data.planet() && value.isObject())continue; // player_set validates body, face and bounds before opening an edit.
            }
            if(key=="resources" && value.isObject()){
                for(const auto& [id,amount]:value.members()){
                    if(!amount.isNumber() || !(amount.toDouble()>=0 && amount.toDouble()<=1e6) ||
                       std::none_of(ws_.project.rules.resources.begin(),ws_.project.rules.resources.end(),
                         [&](const auto& r){return r.id==id && r.kind!=game::ResourceKind::Capacity;}))
                        throw std::runtime_error("Invalid starting resource: "+id);
                }
                continue;
            }
            throw std::runtime_error("Invalid player property: "+key);
        }
        args=changes;
    }else if(action=="remove"){
        if(count<=2)throw std::runtime_error("Keep at least two player slots");
    }else if(action!="add")throw std::runtime_error("Unknown player action");
    args.set("player",num(slot));
    const auto result=ws_.call(action=="remove"?"player_remove":"player_set",args,"studio");
    if(result.has("error"))throw std::runtime_error(result.getStr("error"));
    return snapshot();
}
bool StudioSession::prepareHierarchyPreview(const editor::WorldConfig& config,bool refresh) {
    const auto version=graphDocumentScope().dump(-1)+"|"+ws_.project.historyVersion();
    if(version!=hierarchyPreviewVersion_ || refresh){
        editor::startDocumentWorldPreview(hierarchyPreview_,ws_.map,ws_.project,config);
        selectedPreviewIds_=Json();
        hierarchyPreviewVersion_=version;
    }
    hierarchyPreview_.poll();
    if(hierarchyPreview_.busy())return false;
    auto* sim=hierarchyPreview_.sim();
    auto* module=hierarchyPreview_.module();
    if(!sim || !module || !hierarchyPreview_.error().empty())throw std::runtime_error("Hierarchy preview: "+hierarchyPreview_.error());
    return true;
}
Json StudioSession::resolvePreviewRefs(const Json& refs) {
    const auto config=studioHierarchyConfig(ws_.project.manifest);
    if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
    if(!prepareHierarchyPreview(config,false))throw std::runtime_error("Hierarchy preview is loading; retry");
    editor::WorldModel world;
    editor::readWorld(*hierarchyPreview_.sim(),*hierarchyPreview_.module(),config,hierarchyPreview_.links(),
                      hierarchyPreview_.setupRefs(),&ws_.map.data,world,{false});
    Json rows=Json::array();
    std::unordered_set<uint64_t> seen;
    for(const auto& value:refs.items()){
        uint64_t ref=0;
        if(value.isString()){
            const auto text=value.str();
            auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),ref);
            if(error!=std::errc{} || end!=text.data()+text.size())throw std::runtime_error("Invalid preview reference");
        }else if(value.isNumber() && value.toDouble()>0 && value.toDouble()<=9007199254740991. && value.toDouble()==std::floor(value.toDouble()))
            ref=uint64_t(value.integer());
        const auto* row=world.find(ref);
        if(!row)throw std::runtime_error("Unknown preview reference");
        if(!seen.insert(ref).second)continue;
        Json item=Json::object();
        item.set("ref",num(int64_t(row->ref)));
        item.set("doc",num(int64_t(row->doc)));
        item.set("id",str(row->doc>=0?nodeId(ws_.map,editor::DocumentEntityRef::mapEntity(size_t(row->doc))):"preview:"+std::to_string(ref)));
        if(row->hasPos){item.set("x",Json::number(double(row->x)));item.set("y",Json::number(double(row->y)));item.set("z",Json::number(double(row->z)));}
        rows.push(std::move(item));
    }
    return rows;
}
Json StudioSession::editWorldRegions() {
    if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
    if(!prepareHierarchyPreview(studioHierarchyConfig(ws_.project.manifest),false))
        throw std::runtime_error("Hierarchy preview is loading; retry");
    const auto& sim=*hierarchyPreview_.sim();
    const auto& ecs=sim.world();
    const auto* ledger=ecs.resourceConst<world::RegionEntityLedger>();
    if(!ledger)throw std::runtime_error("Region ownership is not enabled");
    auto result=world::ledgerStatus(*ledger,ecs);
    result.set("durabilityKnown",Json::fromBool(false));
    result.set("simulationLod",world::simulationLodDiagnosticsJson(ecs.resourceConst<world::SimulationLodDiagnostics>()));
    result.set("mode",Json::string("preview"));
    result.set("tick",Json::number(int64_t(sim.tick())));
    return result;
}
Json StudioSession::queryEditWorld(const Json& params,bool list) {
    if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
    const auto config=studioHierarchyConfig(ws_.project.manifest);
    if(!prepareHierarchyPreview(config,false))throw std::runtime_error("Hierarchy preview is loading; retry");
    auto& sim=*hierarchyPreview_.sim();
    auto& module=*hierarchyPreview_.module();
    editor::WorldModel world;
    editor::readWorld(sim,module,config,hierarchyPreview_.links(),hierarchyPreview_.setupRefs(),&ws_.map.data,world);
    if(list)return editor::listWorldRows(world,module.rules(),&ws_.map.data,params,"edit");
    auto details=editor::inspectWorldRow(sim,module,world,ws_.project.rulesJson,&ws_.map.data,params);
    if(details["row"].has("doc"))details.set("map",ws_.map.toJson()["entities"][size_t(details["row"].getInt("doc"))]);
    return details;
}
Json StudioSession::hierarchyGroups(const Json& params) {
    if(ws_.project.dir.empty() || params.getStr("expectedProject")!=ws_.project.dir ||
       !sameDocumentScope(params["scope"],graphDocumentScope()))throw std::runtime_error("Hierarchy document changed");
    const auto config=studioHierarchyConfig(ws_.project.manifest);
    const auto index=params.getInt("group",-1);
    if(params.has("group") && (index<0 || size_t(index)>=config.groupings.size()))throw std::runtime_error("Unknown Hierarchy grouping");
    Json result=Json::object();
    const bool ready=prepareHierarchyPreview(config,params.getBool("refresh"));
    result.set("pending",Json::fromBool(!ready));
    if(!ready)return result;
    auto* sim=hierarchyPreview_.sim();
    auto* module=hierarchyPreview_.module();
    editor::WorldModel world;
    editor::readWorld(*sim,*module,config,hierarchyPreview_.links(),hierarchyPreview_.setupRefs(),&ws_.map.data,world);
    std::unordered_map<uint64_t,std::string> ids;
    for(const auto& node:editor::enumerateDocumentSceneNodes(ws_.map)){
        if(node.ref.source!=editor::DocumentEntityRef::Source::MapEntity)continue;
        if(const auto* row=world.findDoc(int(node.ref.index)))ids.emplace(row->ref,nodeId(ws_.map,node.ref));
    }
    Json preview=Json::array();
    std::vector<const editor::WorldRow*> matched;
    for(const auto& row:world.rows){
        if(!ids.contains(row.ref)){
            if(row.doc>=0)continue;
            const auto id="preview:"+std::to_string(row.ref);
            ids.emplace(row.ref,id);
            Json item=Json::object(),details=Json::object(),components=Json::array();
            item.set("id",str(id));
            item.set("name",str(editor::worldRowName(world,module->rules(),&ws_.map.data,row)));
            item.set("kind",str(row.type>=0 && size_t(row.type)<module->rules().types.size() ?
                module->rules().types[size_t(row.type)].id : editor::worldKindId(row.kind)));
            item.set("owner",num(int64_t(row.owner)));
            row.eachComp([&](int c){ components.push(str(editor::worldComponents()[size_t(c)].id)); });
            details.set("components",std::move(components));
            if(row.maxHp>0)details.set("health",Json::number(double(row.hp)));
            item.set("preview",std::move(details));
            preview.push(std::move(item));
        }
        matched.push_back(&row);
    }
    result.set("preview",std::move(preview));
    if(index<0)return result;
    if(config.groupings[size_t(index)].by==std::vector<std::string>{"parent"}){
        Json parents=Json::object();
        for(const auto& row:nestedEntities(matched,sim->world()))
            if(row.parent)parents.set(ids.at(row.entity->ref),str(ids.at(row.parent)));
        result.set("parents",std::move(parents));
        return result;
    }
    Json paths=Json::object();
    for(const auto* row:matched){
        Json path=Json::array();
        for(const auto& key:config.groupings[size_t(index)].by){
            if(key=="component"){
                Json labels=Json::array();
                visitHierarchyLabels(world,module->rules(),*row,key,[&](const auto& label){ labels.push(str(label)); });
                path.push(std::move(labels));
            }else path.push(str(editor::worldGroupValue(world,module->rules(),*row,key)));
        }
        paths.set(ids.at(row->ref),std::move(path));
    }
    result.set("paths",std::move(paths));
    return result;
}
static bool scalableEvolveRegion(const editor::DocEntity& entity) {
    const auto& components = entity.extra["components"];
    return components["EvolveRegion"].isObject() &&
           lib::worldprocess::extended::validate("EvolveArea",components["EvolveArea"]);
}

Json StudioSession::snapshot() const {
    Json result = Json::object();
    Json project = Json::object();
    project.set("name", str(ws_.project.manifest.getStr("name", "No project opened")));
    project.set("path", str(ws_.project.dir));
    project.set("actorSession",Json::fromBool(ws_.project.actorSession()));
    project.set("historyVersion",str(ws_.project.historyVersion()));
    project.set("assetHistoryVersion",str(assetHistoryVersion()));
    project.set("dirty", Json::fromBool(ws_.map.dirty || ws_.project.rulesDirty || ws_.project.manifestDirty));
    result.set("project", std::move(project));
    if (!ws_.project.dir.empty()) result.set("graphDocument",graphDocumentScope());
    Json scene = Json::object();
    scene.set("name", str(ws_.map.data.name));
    scene.set("mapRegions",Json::fromBool(!ws_.map.data.worldSpace && !ws_.map.data.planet() && !ws_.project.dir.empty()));
    scene.set("mapProperties",Json::fromBool(!ws_.project.dir.empty()));
    scene.set("worldSpace",Json::fromBool(ws_.map.data.worldSpace));
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
    const auto hierarchy=studioHierarchyConfig(ws_.project.manifest);
    if(!ws_.project.dir.empty() && !ws_.map.data.worldSpace){
        Json groups=Json::array();
        for(const auto& group:hierarchy.groupings)groups.push(str(group.title));
        scene.set("groupings",std::move(groups));
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
    const auto selectedIds = selectedObjectIds();
    Json ids = Json::array();
    for (const auto& id : selectedIds) ids.push(str(id));
    result.set("selectedIds", std::move(ids));
    if(selectedPreviewIds_.size() && hierarchyPreviewVersion_==graphDocumentScope().dump(-1)+"|"+ws_.project.historyVersion()){
        result.set("selectedPreviewIds",selectedPreviewIds_);
        if(previewInspector_.isObject())result.set("previewInspector",previewInspector_);
    }
    if (!ws_.map.data.planet() && selectedIds.size() >= 2 && selectedIds.size() <= 256 &&
        std::all_of(selectedIds.begin(), selectedIds.end(), [&](const auto& id) {
            if (!id.starts_with("entity:@") || locked_.contains(id) || hidden_.contains(id)) return false;
            const int index = studioMapEntityIndex(id, ws_.map);
            return index >= 0 && scalableEvolveRegion(ws_.map.data.entities[size_t(index)]);
        })) result.set("canScaleEvolveRegions", Json::fromBool(true));
    int roadA, roadB;
    bool linked;
    if (selectedRoad(selectedIds, roadA, roadB, linked)) result.set("selectedRoad", Json::fromBool(linked));
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
    result.set("previewMaterial", previewMaterialName().empty() ? Json() : str(previewMaterialName()));
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
namespace {
void saveStudioDocuments(editor::Workspace& ws,const std::string& file,bool addToGame){
    auto map=ws.map;
    auto project=ws.project;
    auto edits=map.stageSave(project.dir,file,true);
    if(addToGame){
        const auto relative=std::filesystem::path(map.path).lexically_relative(std::filesystem::canonical(project.dir)).generic_string();
        bool listed=false;
        for(const auto& item:project.manifest["maps"].items())if(item.getStr("file")==relative)listed=true;
        if(!listed){
            if(!project.manifest["maps"].isArray())project.manifest.set("maps",Json::array());
            Json row=Json::object();row.set("file",str(relative));row.set("title",str(map.data.name));
            project.manifest.find("maps")->push(std::move(row));project.manifestDirty=true;
        }
    }
    auto projectEdits=project.stageDirtySave();
    edits.insert(edits.end(),projectEdits.begin(),projectEdits.end());
    // The journal validates all targets and rejects duplicates, including
    // absent outputs. HistoryFileBatch is intentionally existing-files only.
    const auto historyLock=map.lockAtomicSave();
    auto checkpoint=map.stageHistoryCheckpoint(edits);
    edits.push_back(std::move(checkpoint));
    editor::applyProjectFileEdits(project.dir,edits);
    map.finishAtomicSave();
    ws.map=std::move(map);ws.project=std::move(project);
    ws.log("studio","Saved map and project documents together");
}
}
Json StudioSession::saveMapAs(const std::string& path) {
    if(stroking_)throw std::runtime_error("Finish the active Scene stroke before saving");
    if(ws_.project.dir.empty()||path.empty())throw std::runtime_error("Open a project and choose a map file");
    saveStudioDocuments(ws_,path,true);
    return snapshot();
}
Json StudioSession::openProject(const std::string& path) {
    if(agentRun_.running())throw std::runtime_error("Stop the running agent before opening a project");
    strokeCancel();brush_.armed=false;brush_.pickHeight=false;brush_.fixedTile=-1;brush_.regionBorders=false;
    agentHistoryUnsafe_=true;agentBatchFenced_=true;
    if (path.empty()) throw std::runtime_error("Project path is empty");
    const auto directory=recoveredProjectDir(ws_,path);
    std::string error;
    if (!ws_.openGame(directory, &error))
        throw std::runtime_error(error.empty() ? "Unable to open project" : error);
    (void)studioContentRoot(ws_.project.dir);
    try { const auto pending=StudioMaterialPublicationJournal::inspect(ws_.project.dir);if(pending.size())ws_.log("studio","Pending material publications: "+pending.dump(-1),true); } catch(const std::exception& e){ws_.log("studio",e.what(),true);}
    ++graphDocumentEpoch_; // reopening the same project is a different document lifetime
    pendingAssetTransfer_.reset();loadAssetHistory();
    clearSelection();hidden_.clear();locked_.clear();
    rigPreview_.reset();
    facialPreview_.reset(); // project identity may change; never reuse prior GUID
    previewGraphTitle_.clear();previewGraphFactors_=Json();previewMaterialName_.clear();
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
// Explicit native adapters: never expose the generic browser dispatch or
// native external picker/purge to an agent.
Json StudioSession::agentLinkedCall(const std::string& name,const Json& args){
    try{
        if(name=="studio_history_capabilities"){
            Json out=Json::object(),rows=Json::array();
            for(const auto& op:ws_.ops()){
                Json row=Json::object();row.set("name",str(op.name));row.set("policy",str(studioAgentHistoryPolicy(op.name)));
                row.set("guiOnly",Json::fromBool(op.guiOnly));
                row.set("studioLinkOnly",Json::fromBool(op.studioLinkOnly));rows.push(std::move(row));
            }
            out.set("operations",std::move(rows));out.set("bulkUndo",str("map-only"));
            out.set("coordinatedBatch",str("map-project-assets-validated-relocation-chains"));
            out.set("atomicSave",Json::fromBool(true));out.set("assetCursorJournal",num(5));
            out.set("mapCheckpointVersion",num(2));out.set("nativeOnlyRecoveryReset",Json::fromBool(true));
            out.set("nativeOnlyPermanentPurge",Json::fromBool(true));return out;
        }
        if(name=="studio_owner_history"||name=="studio_owner_history_step"||
           name=="studio_save_documents"||name=="studio_asset_history_reset_review"){
            if(ws_.project.dir.empty()||args.getStr("expectedProject")!=ws_.project.dir)
                throw std::runtime_error("Linked Studio project changed");
            if(name=="studio_owner_history"){
                syncHistoryLabels();
                Json out=Json::object();
                out.set("project",str(ws_.project.dir));out.set("mapPath",str(ws_.map.path));
                out.set("ownerVersion",str(batchOwnerVersion()));
                auto map=historyJson();
                map.set("canUndo",Json::fromBool(ws_.map.canUndo()&&!stroking_));
                map.set("canRedo",Json::fromBool(ws_.map.canRedo()&&!stroking_));
                out.set("map",std::move(map));out.set("projectHistory",ws_.project.historyStatus());
                out.set("assets",assetHistoryJson());
                out.set("mapDirty",Json::fromBool(ws_.map.dirty));
                out.set("rulesDirty",Json::fromBool(ws_.project.rulesDirty));
                out.set("manifestDirty",Json::fromBool(ws_.project.manifestDirty));
                out.set("saveAvailable",Json::fromBool(!ws_.map.path.empty()&&!stroking_));
                out.set("hostedAgentRunning",Json::fromBool(agentRun_.running()));
                return out;
            }
            if(name=="studio_asset_history_reset_review"){
                auto out=studioReviewAssetHistoryReset(ws_.project.dir);
                out.set("version",str(assetHistoryVersion()));out.set("applyNativeOnly",Json::fromBool(true));return out;
            }
            if(!args.getBool("confirmed")||stroking_)throw std::runtime_error("Confirm the operation outside an active Scene stroke");
            Json request=Json::object();request.set("id",num(1));request.set("params",args);
            if(name=="studio_save_documents"){
                if(ws_.map.path.empty()||args.getStr("expectedMap")!=ws_.map.path||
                   args.getStr("expectedOwnerVersion")!=batchOwnerVersion())
                    throw std::runtime_error("Linked map or owner version changed; refresh owner history");
                request.set("method",str("studio.save"));
            }else{
                if(agentRun_.running())throw std::runtime_error("Stop the hosted agent before history navigation");
                const auto scope=args.getStr("scope"),direction=args.getStr("direction");
                if(direction!="undo"&&direction!="redo")throw std::runtime_error("Invalid history direction");
                if(scope=="map"){
                    if(args.getStr("expectedVersion")!=historyVersion())throw std::runtime_error("Map history changed");
                    request.set("method",str(direction=="redo"?"studio.redo":"studio.undo"));
                }else if(scope=="project"||scope=="assets"){
                    request.set("method",str(scope=="project"?"studio.projectHistoryStep":"studio.assetHistoryStep"));
                }else throw std::runtime_error("Invalid history owner");
            }
            const auto result=dispatch(request);
            if(result.has("error"))throw std::runtime_error(result.getStr("error"));
            if(name=="studio_owner_history_step"){agentHistoryUnsafe_=true;agentBatchFenced_=true;}
            Json out=Json::object();out.set("ok",Json::fromBool(true));
            out.set("ownerVersion",str(batchOwnerVersion()));return out;
        }
        if(name=="studio_agent_history_list"||name=="studio_agent_history_step"||
           name=="studio_agent_batch_preview"||name=="studio_agent_batch_apply"){
            if(ws_.project.dir.empty()||args.getStr("expectedProject")!=ws_.project.dir)
                throw std::runtime_error("Linked Studio project changed");
            if(name=="studio_agent_history_list"){
                const auto read=[&](const char* key,int64_t fallback,int64_t min,int64_t max){
                    if(!args.has(key))return fallback;
                    const auto value=args.getInt(key,-1);
                    if(!args[key].isNumber()||args[key].numberLiteral()!=std::to_string(value)||value<min||value>max)
                        throw std::runtime_error("Invalid history page");
                    return value;
                };
                const auto offset=read("offset",0,0,2000),limit=read("limit",50,1,100);
                Json out=Json::object(),rows=Json::array();
                for(size_t i=size_t(offset);i<agentChanges_.size()&&rows.size()<size_t(limit);++i)
                    rows.push(agentHistoryLink(agentChanges_[i]));
                out.set("items",std::move(rows));out.set("total",num(int64_t(agentChanges_.size())));
                out.set("project",str(ws_.project.dir));out.set("hostedAgentRunning",Json::fromBool(agentRun_.running()));
                return out;
            }
            const auto direction=args.getStr("direction");
            if(direction!="undo"&&direction!="redo")throw std::runtime_error("Invalid history direction");
            if(name=="studio_agent_batch_preview")return agentBatchPreview(direction=="redo");
            if(!args.getBool("confirmed"))throw std::runtime_error("Confirm history navigation explicitly");
            // Reuse the exact browser navigation boundary; hosted agents cannot
            // navigate their own live transaction while still running.
            Json request=Json::object();request.set("id",num(int64_t(1)));
            request.set("method",str(name=="studio_agent_history_step"?"studio.agentHistoryStep":"studio.agentBatchApply"));
            request.set("params",args);
            const auto result=dispatch(request);
            if(result.has("error"))throw std::runtime_error(result.getStr("error"));
            Json out=Json::object();out.set("ok",Json::fromBool(true));
            out.set("ownerVersion",str(batchOwnerVersion()));
            return out;
        }
        // Explicit allowlist; never translate arbitrary tool names to browser RPC.
        if(name=="studio_asset_dependencies"||name=="studio_asset_recovery_storage"||
           name=="studio_asset_trash_list"||name=="studio_asset_trash_restore_review"||
           name=="studio_asset_trash_restore"||name=="studio_asset_delete"||
           name=="studio_asset_recovery_quota_set"||name=="studio_asset_trash_cleanup_review"){
            if(ws_.project.dir.empty()||args.getStr("expectedProject")!=ws_.project.dir)
                throw std::runtime_error("Linked Studio project changed");
            const auto integer=[&](const char* key,int64_t fallback,int64_t min,int64_t max){
                if(!args.has(key))return fallback;
                const auto value=args.getInt(key,-1);
                if(!args[key].isNumber()||args[key].numberLiteral()!=std::to_string(value)||value<min||value>max)
                    throw std::runtime_error(std::string("Invalid integer argument: ")+key);
                return value;
            };
            Json params=Json::object();params.set("expectedProject",str(ws_.project.dir));
            const auto invoke=[&](const char* method,const Json& input){
                Json request=Json::object();request.set("id",num(int64_t(1)));
                request.set("method",str(method));request.set("params",input);
                const auto response=dispatchImpl(request);
                if(response.has("error"))throw std::runtime_error(response.getStr("error"));
                return response["result"];
            };
            const auto selected=[&]{
                if(!args["guid"].isString()||!studioValidGuid(args.getStr("guid")))
                    throw std::runtime_error("Select an existing asset GUID");
                StudioAssetCatalog catalog(ws_.project.dir);catalog.rebuild(false);
                const auto entry=catalog.byGuid(args.getStr("guid"));
                if(!entry)throw std::runtime_error("Asset GUID not found");
                return *entry;
            };
            if(name=="studio_asset_dependencies"){
                const auto entry=selected();
                const auto offset=integer("offset",0,0,65536),limit=integer("limit",50,1,100);
                params.set("path",str(entry.path));params.set("offset",num(offset));params.set("limit",num(limit));
                params.set("depth",num(args.has("depth")?integer("depth",1,1,16):0));
                if(args.has("direction"))params.set("direction",args["direction"]);
                if(args.has("expectedToken"))params.set("expectedToken",args["expectedToken"]);
                auto out=invoke("studio.assetDependencyPage",params);
                out.set("guid",str(entry.guid));out.set("path",str(entry.path));
                out.set("version",str(assetHistoryVersion()));
                return out;
            }
            if(name=="studio_asset_recovery_storage")return studioRecoveryStorage(ws_.project.dir);
            if(name=="studio_asset_trash_list"){
                params.set("offset",num(integer("offset",0,0,10000)));
                params.set("limit",num(integer("limit",50,1,100)));
                return invoke("studio.assetTrashList",params);
            }
            if(name=="studio_asset_trash_cleanup_review"){
                params.set("ids",args["ids"]);return invoke("studio.assetTrashBatchReview",params);
            }
            if(name=="studio_asset_recovery_quota_set"){
                if(!args.has("expectedMiB")||!args.has("quotaMiB"))throw std::runtime_error("Both quota values are required");
                params.set("expectedMiB",num(integer("expectedMiB",0,0,1048576)));
                params.set("quotaMiB",num(integer("quotaMiB",0,0,1048576)));
                return invoke("studio.assetRecoveryQuota",params);
            }
            if(name=="studio_asset_trash_restore_review"){
                params.set("id",args["id"]);params.set("action",str("restore"));
                return invoke("studio.assetTrashReview",params);
            }
            if(!args.getBool("confirmed")||args.getStr("expectedVersion")!=assetHistoryVersion())
                throw std::runtime_error("Explicit confirmation and current asset history version required");
            params.set("confirmed",Json::fromBool(true));params.set("expectedVersion",args["expectedVersion"]);
            if(name=="studio_asset_delete"){
                const auto entry=selected();
                params.set("guid",str(entry.guid));params.set("path",str(entry.path));
                const auto result=invoke("studio.assetDelete",params);
                agentAssetTransfer_.reset();agentAssetTransferGuids_.clear();
                return result;
            }
            params.set("id",args["id"]);params.set("token",args["token"]);params.set("action",str("restore"));
            const auto result=invoke("studio.assetTrashApply",params);
            agentAssetTransfer_.reset();agentAssetTransferGuids_.clear();
            return result;
        }
        if(name=="studio_asset_move_preview"||name=="studio_asset_move_commit"){
            if(ws_.project.dir.empty()||args.getStr("expectedProject")!=ws_.project.dir||
               args.getStr("expectedVersion")!=assetHistoryVersion())
                throw std::runtime_error("Linked asset project or version changed");
            StudioAssetCatalog catalog(ws_.project.dir);catalog.rebuild(false);
            Json params=args,request=Json::object();request.set("id",num(int64_t(1)));
            const bool preview=name=="studio_asset_move_preview";
            if(preview){
                agentAssetTransfer_.reset();agentAssetTransferGuids_.clear();
                const auto& guids=args["guids"];
                if(!guids.isArray()||guids.size()<1||guids.size()>64||!args["destination"].isString())
                    throw std::runtime_error("Select 1–64 existing asset GUIDs and a destination");
                Json paths=Json::array();
                for(const auto& guid:guids.items()){
                    if(!guid.isString())throw std::runtime_error("Invalid asset GUID");
                    const auto entry=catalog.byGuid(guid.str());
                    if(!entry||!agentAssetTransferGuids_.emplace(entry->guid,entry->path).second)
                        throw std::runtime_error("Missing or repeated asset GUID");
                    paths.push(str(entry->path));
                }
                params.set("paths",std::move(paths));
            }else{
                if(!agentAssetTransfer_||agentAssetTransfer_->token()!=args.getStr("token")||
                   agentAssetTransferVersion_!=assetHistoryVersion())throw std::runtime_error("Agent asset preview expired");
                for(const auto& [guid,path]:agentAssetTransferGuids_){
                    const auto entry=catalog.byGuid(guid);
                    if(!entry||entry->path!=path)throw std::runtime_error("Preview asset identity changed");
                }
                const auto& decisions=args["decisions"];
                const auto count=agentAssetTransfer_->describe()["items"].size();
                if(!decisions.isArray()||decisions.size()!=count||count>64)
                    throw std::runtime_error("Explicit decisions required for every preview item");
                std::set<int64_t> decided;
                for(const auto& d:decisions.items()){
                    const auto id=d.getInt("id",-1);
                    if(!d["id"].isNumber()||id<0||size_t(id)>=count||!decided.insert(id).second)
                        throw std::runtime_error("Invalid or repeated move decision");
                    const auto action=d.getStr("action");
                    if(action!="import"&&action!="keep-both"&&action!="skip")throw std::runtime_error("Only non-destructive moves are supported");
                }
            }
            request.set("method",str(preview?"studio.assetMovePreview":"studio.assetTransferCommit"));
            request.set("params",std::move(params));
            // Agent and browser previews never share a token or overwrite each other.
            auto browser=std::move(pendingAssetTransfer_);
            pendingAssetTransfer_=std::move(agentAssetTransfer_);
            Json response;
            try{response=dispatchImpl(request);}
            catch(...){agentAssetTransfer_.reset();pendingAssetTransfer_=std::move(browser);throw;}
            agentAssetTransfer_=std::move(pendingAssetTransfer_);pendingAssetTransfer_=std::move(browser);
            if(response.has("error")){
                agentAssetTransfer_.reset();Json out=Json::object();out.set("error",response["error"]);return out;
            }
            if(preview)agentAssetTransferVersion_=assetHistoryVersion();
            else {
                agentAssetTransfer_.reset();agentAssetTransferGuids_.clear();
                if(response["result"]["files"].size())pendingAssetTransfer_.reset();
            }
            return response["result"];
        }
        if(name=="studio_asset_history"){
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            auto out=assetHistoryJson();out.set("project",str(ws_.project.dir));return out;
        }
        if(name=="studio_asset_catalog"||name=="studio_asset_meta_patch"||name=="studio_asset_folder_create"){
            if(ws_.project.dir.empty()||args.getStr("expectedProject")!=ws_.project.dir)
                throw std::runtime_error("Linked Studio project changed");
            if(name=="studio_asset_catalog"){
                const auto offset=args.getInt("offset",0),limit=args.getInt("limit",100);
                if(offset<0||limit<1||limit>200)throw std::runtime_error("Invalid asset catalog page");
                StudioAssetCatalog catalog(ws_.project.dir);catalog.rebuild(false);
                const auto assets=catalog.all();Json rows=Json::array(),out=Json::object();
                for(size_t i=size_t(offset);i<assets.size()&&rows.size()<size_t(limit);++i){
                    const auto& e=assets[i];Json row=Json::object();
                    row.set("guid",str(e.guid));row.set("path",str(e.path));row.set("importer",str(e.importer));
                    row.set("directory",Json::fromBool(e.directory));row.set("settings",e.settings);rows.push(std::move(row));
                }
                out.set("items",std::move(rows));out.set("total",num(int64_t(assets.size())));out.set("version",str(assetHistoryVersion()));return out;
            }
            if(args.getStr("expectedVersion")!=assetHistoryVersion())throw std::runtime_error("Asset history changed; refresh first");
            Json request=Json::object();request.set("id",num(int64_t(1)));
            request.set("method",str(name=="studio_asset_meta_patch"?"studio.assetMetaPatch":"studio.assetFolderCreate"));
            request.set("params",args);
            const auto reply=dispatchImpl(request);
            if(reply.has("error")){Json error=Json::object();error.set("error",reply["error"]);return error;}
            return reply["result"];
        }
        auto result=ws_.call(name,args,"agent");
        if(!result.has("error") && (name=="map_open" || name=="map_new"))++graphDocumentEpoch_;
        return result;
    }catch(const std::exception& e){Json out=Json::object();out.set("error",str(e.what()));return out;}
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
        if(stroking_){agentServer_.send(m.client,reply(id,error("Scene stroke active; retry after release or cancel")).dump(-1));continue;}
        const Json args=msg["params"]["arguments"].isObject()?msg["params"]["arguments"]:Json::object();
        if(name=="view_screenshot"){
            // Answered by the host with the Scene's next frame (finishAgentScreenshot).
            if(!agentShots_.empty()){agentServer_.send(m.client,reply(id,error("another screenshot is pending; retry after its reply")).dump(-1));continue;}
            const auto window=args.getStr("window",args.has("device")?"game":"all");
            const bool nature=window=="physis";
            if ((args.has("window") && !args["window"].isString()) ||
                (window!="all" && window!="scene" && window!="game" && !nature)) {
                agentServer_.send(m.client,reply(id,error("Unknown Studio screenshot window")).dump(-1));
                continue;
            }
            if (window=="all" && (args.size()!=size_t(args.has("window")+args.has("clean")) ||
                (args.has("clean") && (args["clean"].type()!=Json::Type::Bool || args.getBool("clean"))))) {
                agentServer_.send(m.client,reply(id,error("Whole-window capture uses the current Studio window size and layout")).dump(-1));
                continue;
            }
            if (window=="game") {
                if ((args.has("device") && (!args["device"].isString() || args.getStr("device").empty())) ||
                    (args.has("clean") && (args["clean"].type()!=Json::Type::Bool || args.getBool("clean"))) ||
                    args.size()!=size_t(args.has("window")+args.has("device")+args.has("clean"))) {
                    agentServer_.send(m.client,reply(id,error("Game capture accepts the device preset and current Game viewport only")).dump(-1));
                    continue;
                }
            }
            if(nature && args.size()!=size_t(1 + args.has("x") + args.has("y") + args.has("z") + args.has("distance") + args.has("pitch") + args.has("yaw"))){
                agentServer_.send(m.client,reply(id,error("PHYSIS capture accepts camera coordinates and uses the current viewport size")).dump(-1));
                continue;
            }
            editor::SceneScreenshotOptions scene;
            if(window=="scene" || nature){
                try {
                    if(args.has("device"))throw std::runtime_error("device preset requires Game capture");
                    scene=editor::SceneScreenshotOptions::parse(args,true);
                } catch(const std::exception& e){
                    agentServer_.send(m.client,reply(id,error(e.what())).dump(-1));
                    continue;
                }
            }
            agentShots_.push_back({m.client,id,window,args.getStr("device"),scene,window=="scene"?sceneDocument():StudioSceneDocument{}});
            agentLines_.push_back("→ view_screenshot");
            continue;
        }
        // The authenticated, explicit native view adapter cannot touch a document
        // owner. Do not put camera navigation in the map/asset history journal.
        if(studioSceneViewAgentOperation(name)){
            Json result;
            try{
                if(!sceneViewAgent_)throw std::runtime_error("Requires an authenticated HTML5 Studio agent link with a native Scene host");
                result=sceneViewAgent_(name,args);
            }catch(const std::exception& e){result=error(e.what());}
            agentServer_.send(m.client,reply(id,result).dump(-1));continue;
        }
        if(stroking_&&brush_.kind=="caverns"){
            agentServer_.send(m.client,reply(id,error("Finish the active Caverns stroke before MCP operations")).dump(-1));continue;
        }
        syncHistoryLabels();
        const auto before=ws_.map.historyMark();const size_t entities=ws_.map.data.entities.size();
        const auto projectBefore=ws_.project.documentHistory.version(),assetBefore=assetHistory_.version();
        const auto sourceMap=ws_.map.path,sourceEpoch=ws_.map.historyEpoch();
        const bool wasDirty=ws_.map.dirty||ws_.project.rulesDirty||ws_.project.manifestDirty;
        if(name=="open_game"||name=="open_project"||name=="create_game"||name=="create_project")assetHistory_.reset();
        const auto assetProjectBefore=ws_.project.dir;
        if(!agentBatchOwnerVersion_.empty()&&agentBatchOwnerVersion_!=batchOwnerVersion())agentBatchFenced_=true;
        const Json result=agentLinkedCall(name,args);
        agentBatchOwnerVersion_=batchOwnerVersion();
        if(name=="studio_agent_history_step"||name=="studio_agent_batch_apply"||name=="studio_owner_history_step"){
            const bool ownerChanged=ws_.map.historyMark()!=before||ws_.project.documentHistory.version()!=projectBefore||
                assetHistory_.version()!=assetBefore;
            // A navigation is not a new edit. Preserve the original links
            // for redo; on any failed partial owner change fence the batch.
            if(!result.has("error")||ownerChanged)changed=true;
            if(result.has("error")&&ownerChanged){agentBatchFenced_=true;agentHistoryUnsafe_=true;}
            agentServer_.send(m.client,reply(id,result).dump(-1));continue;
        }
        if(assetProjectBefore!=ws_.project.dir){
            // The legacy open route bypasses the Studio journal recovery.
            // Never attach persisted history until native project open.
            assetHistory_=editor::ProjectHistory{};
            assetHistoryPersistenceWarning_="Reopen through Studio to load saved asset history";
        }
        const bool failed=result.has("error");
        const bool sameProject=assetProjectBefore==ws_.project.dir;
        const bool sameMap=sameProject&&sourceMap==ws_.map.path&&sourceEpoch==ws_.map.historyEpoch();
        std::vector<std::pair<std::string,std::string>> links;
        if(sameMap)labelNewHistory(before,"Agent · "+studioToolLabel(name));else syncHistoryLabels();
        if(sameMap)for(size_t i=0;i<ws_.map.undoDepth();++i)if(ws_.map.undoId(i)>before){
            const auto id=ws_.map.undoId(i);
            if(!failed)links.emplace_back("map",std::to_string(id));
            if(agentEditIds_.size()<2000)agentEditIds_.push_back(id);else agentHistoryUnsafe_=true;
        }
        const auto capture=[&](const char* scope,const editor::ProjectHistory& history,uint64_t previous){
            if(!failed&&sameProject&&history.version()==previous+1&&history.canUndo()&&
               history.next(false).id==history.versionToken())
                links.emplace_back(scope,history.next(false).id);
        };
        capture("project",ws_.project.documentHistory,projectBefore);
        capture("assets",assetHistory_,assetBefore);
        if(ws_.map.data.entities.size()!=entities){clearSelection();dropOrdinalFlags();}
        const bool dirty=ws_.map.dirty||ws_.project.rulesDirty||ws_.project.manifestDirty;
        const std::string policy=studioAgentHistoryPolicy(name);
        const bool readOnly=policy=="read"||policy=="view"||policy=="runtime"||(name=="terraweave_caverns_paint"&&args.getStr("mode")=="get");
        // Known native asset no-ops do not invent an untracked mutation.
        const bool nativeNoop=(name=="studio_asset_meta_patch"||
            (name=="studio_asset_move_commit"&&!result["files"].size()&&!result["issues"].size()))&&
            assetHistory_.version()==assetBefore&&!failed;
        const bool forestNoop=(name=="planet_forest_set"||name=="planet_forest_move")&&
            result.has("changed")&&!result.getBool("changed")&&!failed;
        const bool ownerChanged=ws_.map.historyMark()!=before||ws_.project.documentHistory.version()!=projectBefore||assetHistory_.version()!=assetBefore;
        const bool edit=!failed&&(ownerChanged||dirty!=wasDirty||(!readOnly&&!nativeNoop&&!forestNoop));
        if(edit||ownerChanged)changed=true;
        // Map-only Undo all never crosses untracked or non-map changes.
        if((edit&&(policy!="map"||links.empty()||std::any_of(links.begin(),links.end(),[](const auto& l){return l.first!="map";})))||
           (failed&&(!readOnly||ownerChanged))||!sameMap||ws_.project.dir!=agentHistoryProject_||ws_.map.path!=agentHistoryMap_)
            agentHistoryUnsafe_=true;
        if(!sameMap||(failed&&(!readOnly||ownerChanged))||
           (edit&&(links.empty()||policy=="unverified"||std::any_of(links.begin(),links.end(),[&](const auto& l){return l.first!=policy;}))))agentBatchFenced_=true;
        if(edit||failed){
            const auto add=[&](const std::string& scope,const std::string& historyId){
                Json c=Json::object();c.set("op",str(name));c.set("args",str(clipText(args.dump(-1),160)));
                c.set("ok",Json::fromBool(!failed));c.set("scope",str(scope));c.set("historyId",str(historyId));
                c.set("sourceProject",str(ws_.project.dir));c.set("sourceMap",str(ws_.map.path));
                if(failed)c.set("error",str(clipText(result.getStr("error"),300)));
                agentChanges_.push(std::move(c));
            };
            if(links.empty())add("untracked","");
            else {
                for(const auto& [scope,historyId]:links)add(scope,historyId);
                if(policy=="unverified"||std::any_of(links.begin(),links.end(),[&](const auto& l){return l.first!=policy;}))add("untracked","");
            }
            if(agentChanges_.size()>2000){agentChanges_.eraseAt(0);agentHistoryUnsafe_=true;agentBatchFenced_=true;}
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
    return k=="sparse"?"Sparse terrain brush":k=="terrain"?"Terrain brush":k=="paint"?"Paint terrain":k=="region"?"Paint region":k=="tiles"?"Paint tiles":k=="field"?"Paint field":
           k=="objects"?"Scatter objects":k=="road"?"Draw road":k=="river"?"Draw river":k=="caverns"?"Caverns brush":"Brush";
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
    if(stroking_){if(error)*error="Finish the active Scene stroke first";return false;}
    if(!std::isfinite(x)||!std::isfinite(z)){if(error)*error="invalid position";return false;}
    syncHistoryLabels();
    const auto before=ws_.map.historyMark();
    editor::MapDoc::CompoundEdit batch(ws_.map);
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
    batch.commit();
    labelNewHistory(before,label);
    return true;
}
Json StudioSession::sceneBrushJson() const {
    const auto& b=brush_;
    Json o=Json::object();
    o.set("armed",Json::fromBool(b.armed));o.set("kind",str(b.kind));o.set("op",str(b.op));
    o.set("radius",Json::number(b.radius,3));o.set("strength",Json::number(b.kind=="terrain" && b.op=="stamp"?b.stampHeight:b.strength,3));
    const auto& sparse=ws_.map.data.extra["terraweaveSparse"];
    o.set("planet",Json::fromBool(ws_.map.data.planet()));
    if(b.kind=="paint"){
        game::TerrainLook look;
        game::applyTerrainLook(look,ws_.project.rulesJson["terrain"]);
        game::applyTerrainLook(look,ws_.project.manifest["terrain"]);
        Json names=Json::array();
        for(const auto& layer:look.layers)names.push(str(layer.id));
        o.set("layerNames",std::move(names));
    }
    o.set("terrainGrid",str(b.terrainGrid));
    o.set("detailScale",num(int64_t(ws_.map.data.detailScale)));
    o.set("targetHeight",Json::number(b.targetHeight,6));o.set("pickHeight",Json::fromBool(b.pickHeight));
    o.set("sparseMode",str(b.sparseMode));
    o.set("chunkMetres",num(sparse.getInt("chunkMetres",b.chunkMetres)));
    o.set("sparseChunks",num(int64_t(sparse["chunks"].size())));
    o.set("depthCm",num(int64_t(b.depthCm)));
    o.set("stamp",str(b.stamp));o.set("angle",Json::number(b.angle,2));
    o.set("hardness",Json::number(b.hardness,3));o.set("seed",num(int64_t(b.seed)));
    o.set("layer",num(int64_t(b.layer)));o.set("tile",str(b.tile));o.set("fixedTile",num(int64_t(b.fixedTile)));o.set("field",str(b.field));o.set("fieldMode",str(b.fieldMode));o.set("fieldPlayer",num(int64_t(b.fieldPlayer)));
    o.set("regionBorders",Json::fromBool(b.regionBorders));
    o.set("regionLetter",str(b.regionLetter));
    o.set("value",Json::number(b.value,3));o.set("type",str(b.type));o.set("owner",num(int64_t(b.owner)));
    o.set("count",num(int64_t(b.count)));o.set("spread",Json::number(b.spread,3));
    o.set("roadType",str(b.roadType));o.set("width",Json::number(b.width,3));
    o.set("caveMode",str(b.caveMode));o.set("caveY",Json::number(b.caveY,3));o.set("caveSurface",Json::fromBool(b.caveSurface));o.set("caveDepth",Json::number(b.caveDepth,3));
    return o;
}
bool StudioSession::caveBrushPoint(double ox,double oy,double oz,double dx,double dy,double dz,
                                  double& x,double& y,double& z,std::string* error){
    if(!brush_.armed||brush_.kind!="caverns"||ws_.project.dir.empty())return false;
    const double length=std::sqrt(dx*dx+dy*dy+dz*dz);
    if(!std::isfinite(length)||length<1e-8||!std::isfinite(ox)||!std::isfinite(oy)||!std::isfinite(oz)||
       std::abs(ox)>1e7||std::abs(oy)>1e7||std::abs(oz)>1e7)return false;
    dx/=length;dy/=length;dz/=length;
    if(ws_.map.data.planet())try{
        if(brush_.caveRegionId!=editor::planet_caves::activeRegionId(ws_.map.data)){
            if(error)*error="Caverns region changed; rearm the brush";
            return false;
        }
    }catch(const std::exception& e){if(error)*error=e.what();return false;}
    if(!brush_.caveSurface&&brush_.caveMode!="shaft"){
        if(std::abs(dy)<1e-8)return false;
        const double t=(brush_.caveY-oy)/dy;
        if(t<=0||t>500)return false;
        x=ox+dx*t;y=brush_.caveY;z=oz+dz*t;
        if(ws_.map.data.planet()){
            try{const auto cap=editor::planet_caves::read(ws_.map.data).anchor.halfExtentCm/100.;
                return std::abs(x)<=cap&&std::abs(y)<=cap&&std::abs(z)<=cap;}
            catch(const std::exception& e){if(error)*error=e.what();return false;}
        }
        return std::abs(x)<=1e7&&std::abs(z)<=1e7;
    }
    Json p=Json::object();
    p.set("xCm",num(std::llround(ox*100)));p.set("yCm",num(std::llround(oy*100)));p.set("zCm",num(std::llround(oz*100)));
    p.set("dxQ20",num(std::llround(dx*(1<<20))));p.set("dyQ20",num(std::llround(dy*(1<<20))));p.set("dzQ20",num(std::llround(dz*(1<<20))));
    if(brush_.caveMode=="shaft")p.set("planetSurface",Json::fromBool(true));
    const auto r=ws_.call("terraweave_caverns_pick",p,"editor");
    if(r.has("error")){if(error)*error=r.getStr("error");return false;}
    if(!r.getBool("hit",false))return false;
    x=r.getInt("xCm")/100.;y=r.getInt("yCm")/100.;z=r.getInt("zCm")/100.;
    return true;
}
bool StudioSession::strokeCancel(){
    if(!stroking_&&!strokeTransaction_)return false;
    stroking_=false;strokeTransaction_.reset();strokePoints_=Json::array();
    clearSelection();dropOrdinalFlags();syncHistoryLabels();return true;
}
bool StudioSession::strokeDab(double x,double z,std::string* error,double y,bool shift,const std::string& body){
    if(!stroking_)return false;
    try{
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||std::abs(x)>1e7||std::abs(y)>1e7||std::abs(z)>1e7)
            throw std::runtime_error("Invalid stroke coordinates");
        if(strokeDabImpl(x,z,error,y,shift,body))return true;
    }catch(const std::exception& e){if(error)*error=e.what();}
    strokeCancel();return false;
}
bool StudioSession::strokeDabImpl(double x, double z, std::string* error,double y,bool shift,const std::string& body) {
    if(!stroking_)return false;
    const auto& b=brush_;
    const bool planet=ws_.map.data.planet() && (b.kind=="terrain" || b.kind=="paint");
    if(body!=strokeBody_ || planet!=!body.empty())throw std::runtime_error("Terrain brush body changed");
    Json direction=Json::array();
    if(planet){
        const int index=ws_.map.data.findBody(body);
        const double length=std::hypot(x,y,z);
        if(index<0 || length<1e-6)throw std::runtime_error("Unknown planet brush surface");
        const double scale=ws_.map.data.bodies[size_t(index)].faceSize/1.5707963267948966;
        for(double coordinate:{x,y,z})direction.push(Json::number(coordinate/length,8));
        x=x/length*scale;y=y/length*scale;z=z/length*scale;
    }
    const double d=(planet || b.kind=="caverns")?std::hypot(x-strokeX_,y-strokeHeight_,z-strokeY_):std::hypot(x-strokeX_,z-strokeY_);
    if(brushPathKind(b.kind)){
        // Path tools: a point every two cells; the op runs once on release.
        if(d<2.0 && strokePoints_.size()>0)return true;
        if(strokePoints_.size()>=4096){if(error)*error="Path stroke point limit";return false;}
        Json pt=Json::array();pt.push(Json::number(x,2));pt.push(Json::number(z,2));
        if(b.kind=="river")pt.push(Json::number(b.width,2));
        strokePoints_.push(std::move(pt));strokeX_=x;strokeY_=z;
        return true;
    }
    // Dabs closer than about a third of the radius add little but cost a full map snapshot each.
    const double spacing=b.kind=="objects"?std::max(1.0,b.radius):b.kind=="tiles"||b.kind=="field"?std::max(1.0,b.radius*0.5):b.radius*0.33;
    if(strokeDabs_>0 && (d<spacing || (b.kind=="terrain" && b.op=="stamp")))return true;
    Json p=Json::object();std::string op;
    if(b.kind=="terrain"){
        op="terrain_sculpt";
        auto mode=b.op;
        if(shift && mode=="raise")mode="lower";
        else if(shift && mode=="lower")mode="raise";
        double strength=b.strength;
        if(mode=="set")strength=b.targetHeight;
        if(mode=="stamp")strength=shift?-b.stampHeight:b.stampHeight;
        p.set("op",str(mode));p.set("x",Json::number(x,3));p.set("y",Json::number(z,3));
        if (b.op=="stamp") {
            p.set("stamp",str(b.stamp));p.set("angle",Json::number(b.angle,2));
        }
        if(b.terrainGrid!="auto")p.set("detail",Json::fromBool(b.terrainGrid=="detail"));
        p.set("seed",num(int64_t(b.seed)));
        p.set("hardness",Json::number(b.hardness,3));
        p.set("radius",Json::number(b.radius,3));p.set("strength",Json::number(strength,6));}
    else if(b.kind=="sparse"){
        op="terraweave_sparse_paint";
        auto mode=b.sparseMode;
        if(shift && mode!="create")mode=mode=="raise"?"lower":"raise";
        p.set("mode",str(mode));
        p.set("xCm",num(std::llround(x*100)));
        p.set("zCm",num(std::llround(z*100)));
        p.set("chunkMetres",num(int64_t(b.chunkMetres)));
        p.set("depthCm",num(int64_t(b.depthCm)));
        p.set("radiusCm",num(std::llround(b.radius*100)));
    }
    else if(b.kind=="paint" && (planet || b.terrainGrid=="base")){
        op="terrain_material";
        p.set("layer",num(int64_t(shift?-1:b.layer)));
        p.set("x",num(int64_t(std::floor(x))));
        p.set("y",num(int64_t(std::floor(z))));
        p.set("radius",num(int64_t(b.radius)));
    }
    else if(b.kind=="paint"){op="terrain_paint_material";p.set("hardness",Json::number(b.hardness,3));p.set("layer",num(int64_t(b.layer)));p.set("x",Json::number(x,3));p.set("y",Json::number(z,3));
        p.set("radius",Json::number(b.radius,3));p.set("strength",Json::number(std::min(1.0,b.strength),3));}
    else if(b.kind=="region"){
        const auto& map=ws_.map.data;
        if(map.worldSpace || map.planet() || !(x>=0 && x<map.w && z>=0 && z<map.h)){
            if(error)*error="Region strokes require a cell on a flat map";
            return false;
        }
        op="region_paint";
        Json circle=Json::array();
        circle.push(num(int64_t(x)));circle.push(num(int64_t(z)));circle.push(num(int64_t(b.radius)));
        p.set("circle",std::move(circle));p.set("letter",str(b.regionLetter));
    }
    else if(b.kind=="tiles"){op="tile_paint";p.set("tile",num(int64_t(b.fixedTile)));p.set("terrain",str(b.tile));p.set("x",num(int64_t(std::floor(x))));p.set("y",num(int64_t(std::floor(z))));
        p.set("radius",num(int64_t(std::lround(b.radius))));}
    else if(b.kind=="field"){op="field_paint";p.set("id",str(b.field));p.set("x",num(int64_t(std::floor(x))));p.set("z",num(int64_t(std::floor(z))));
        p.set("radius",num(int64_t(std::lround(b.radius))));p.set("value",Json::number(b.value,3));p.set("mode",str(shift?"erase":b.fieldMode));p.set("player",num(int64_t(b.fieldPlayer)));}
    else if(b.kind=="caverns"){
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||
           std::abs(x)>1e7||std::abs(y)>1e7||std::abs(z)>1e7||strokeDabs_>=256){
            if(error)*error="Caverns stroke limit or coordinate extent";return false;
        }
        if(b.caveMode=="shaft"&&strokeDabs_>0)return true;
        op="terraweave_caverns_paint";p.set("mode",str(b.caveMode=="shaft"?"planet_entrance":b.caveMode));
        if(ws_.map.data.planet())p.set("expectedRegionId",str(b.caveRegionId));
        if(b.caveMode=="shaft")p.set("depthCm",num(std::llround(b.caveDepth*100)));
        p.set("xCm",num(std::llround(x*100)));p.set("yCm",num(std::llround(y*100)));
        p.set("zCm",num(std::llround(z*100)));p.set("radiusCm",num(std::llround(b.radius*100)));
    }
    else if(b.kind=="objects"){op="entity_place";p.set("type",str(b.type));p.set("owner",num(int64_t(b.owner)));
        p.set("x",Json::number(x,3));p.set("z",Json::number(z,3));
        if(b.count>1){p.set("count",num(int64_t(b.count)));p.set("spread",Json::number(b.spread,3));}}
    else {if(error)*error="Unknown brush";return false;}
    if(planet){
        p.set("body",str(body));p.set("dir",std::move(direction));
        p.set("radius",Json::number(b.radius,3));p.erase("x");p.erase("y");
    }
    const Json r=ws_.call(op,p,"editor");   // "editor": no Console line per dab; one line per stroke at the end
    if(r.has("error")){if(error)*error=r.getStr("error");return false;}
    strokeX_=x;strokeY_=z;strokeHeight_=y;++strokeDabs_;
    return true;
}
bool StudioSession::strokeBegin(double x, double z, std::string* error,double y,bool shift,const std::string& body) {
    if(!brush_.armed||brush_.pickHeight||ws_.project.dir.empty()||stroking_)return false;
    const auto& b=brush_;
    if((b.kind=="tiles"&&b.tile.empty())||(b.kind=="field"&&b.field.empty())||(b.kind=="objects"&&b.type.empty())||(b.kind=="road"&&b.roadType.empty())){
        if(error)*error=std::string("choose a ")+(b.kind=="tiles"?"tile terrain":b.kind=="field"?"field":b.kind=="objects"?"type":"road type")+" first";
        return false;
    }
    syncHistoryLabels();
    try{
        strokeTransaction_=std::make_unique<editor::MapDoc::CompoundEdit>(ws_.map);
        strokeHistoryMark_=ws_.map.historyMark();stroking_=true;strokeDabs_=0;strokePoints_=Json::array();
        strokeX_=x+1e9;strokeY_=z+1e9;
        strokeBody_=body;
        return strokeDab(x,z,error,y,shift,body);
    }catch(const std::exception& e){if(error)*error=e.what();strokeCancel();return false;}
}
bool StudioSession::strokeEnd(std::string* error) {
    if(!stroking_)return true;
    const auto& b=brush_;
    try{
        if(brushPathKind(b.kind)){
            if(strokePoints_.size()<2)throw std::runtime_error("drag further to draw a "+b.kind);
            Json p=Json::object();std::string op;
            if(b.kind=="road"){op="road_add";p.set("points",strokePoints_);p.set("type",str(b.roadType));p.set("curve",str("smooth"));}
            else {op="water_set";Json body=Json::object();body.set("kind",str("river"));body.set("points",strokePoints_);p.set("body",std::move(body));}
            const Json r=ws_.call(op,p,"editor");
            if(r.has("error"))throw std::runtime_error(r.getStr("error"));
            strokeDabs_=int(strokePoints_.size());
        }
        strokeTransaction_->commit();
    }catch(const std::exception& e){if(error)*error=e.what();strokeCancel();return false;}
    strokeTransaction_.reset();stroking_=false;strokePoints_=Json::array();
    if(ws_.map.historyMark()!=strokeHistoryMark_){
        labelNewHistory(strokeHistoryMark_,brushLabel(b.kind));
        ws_.log("studio",std::string(brushLabel(b.kind))+(brushPathKind(b.kind)?" · "+std::to_string(strokeDabs_)+" points":" · "+std::to_string(strokeDabs_)+" dabs"));
    }
    syncHistoryLabels();return true;
}
void StudioSession::finishAgentScreenshot(const std::string& pngBase64, int width, int height, const std::string& why) {
    for(const auto& shot:agentShots_){
        Json r=Json::object(),result=Json::object();
        if(pngBase64.empty())result.set("error",str(why.empty()?std::string("nothing to capture"):why));
        else {
            result.set("png_base64",str(pngBase64));
            result.set("width",num(int64_t(width)));result.set("height",num(int64_t(height)));
            if(shot.window=="scene"){
                result.set("clean",Json::fromBool(shot.scene.clean));
                result.set("render_width",num(int64_t(width)));result.set("render_height",num(int64_t(height)));
            }
            result.set("view",str(shot.window=="all"?"HTML Studio window":shot.window=="game"?"Studio Game viewport":shot.window=="physis"?"PHYSIS 3D nature viewport":"Studio Scene viewport"));
        }
        r.set("id",shot.id);r.set("result",std::move(result));
        agentServer_.send(shot.client,r.dump(-1));
    }
    agentShots_.clear();
}
bool StudioSession::agentCanUndoAll() const{
    bool ok=agentHistoryProject_==ws_.project.dir&&agentHistoryMap_==ws_.map.path&&
        !agentHistoryUnsafe_&&!stroking_&&!agentRun_.running()&&!agentEditIds_.empty()&&
        ws_.map.undoDepth()==agentStartIds_.size()+agentEditIds_.size();
    if(!ok)return false;
    for(size_t i=0;i<agentStartIds_.size();++i)if(ws_.map.undoId(i)!=agentStartIds_[i])return false;
    for(size_t i=0;i<agentEditIds_.size();++i)if(ws_.map.undoId(agentStartIds_.size()+i)!=agentEditIds_[i])return false;
    return true;
}
std::string StudioSession::batchOwnerVersion() const{
    return historyVersion()+"|"+ws_.project.historyVersion()+"|"+assetHistoryVersion();
}
Json StudioSession::agentBatchPreview(bool redo) const{
    Json result=Json::object(),rows=Json::array();
    result.set("direction",str(redo?"redo":"undo"));
    result.set("available",Json::fromBool(false));
    try{
        if(ws_.project.dir.empty()||stroking_||agentRun_.running()||agentBatchFenced_||
           agentBatchOwnerVersion_!=batchOwnerVersion())
            throw std::runtime_error("Stop the agent and use a fully tracked conversation");
        std::map<std::string,HistoryBatchOwner> owners;
        auto& map=owners["map"];
        map.position=ws_.map.undoDepth();
        for(size_t i=0;i<ws_.map.undoDepth();++i)map.ids.push_back(std::to_string(ws_.map.undoId(i)));
        for(size_t i=ws_.map.redoDepth();i>0;--i)map.ids.push_back(std::to_string(ws_.map.redoId(i-1)));
        const auto collect=[&](const char* scope,const editor::ProjectHistory& history){
            auto& owner=owners[scope];owner.position=history.position();
            for(const auto& step:history.steps())owner.ids.push_back(step.id);
        };
        collect("project",ws_.project.documentHistory);collect("assets",assetHistory_);
        std::vector<HistoryBatchStep> links;
        for(const auto& change:agentChanges_.items()){
            if(!change.getBool("ok")){
                if(std::string(studioAgentHistoryPolicy(change.getStr("op")))!="read")throw std::runtime_error("Conversation includes a failed potentially mutating call");
                continue;
            }
            const auto scope=change.getStr("scope"),id=change.getStr("historyId");
            const auto resolved=agentHistoryLink(change);
            if(!resolved.getBool("undoable")||change.getStr("sourceProject")!=ws_.project.dir)
                throw std::runtime_error("Conversation includes untracked, evicted or foreign edits");
            if(resolved.getBool("applied")==redo)throw std::runtime_error("All tracked edits must be "+std::string(redo?"undone":"applied")+" before batch navigation");
            if(scope=="project"&&(ws_.project.rulesDirty||ws_.project.manifestDirty||ws_.project.historyRequiresReload))
                throw std::runtime_error("Save project documents before batch navigation");
            if(scope=="assets"){
                const auto it=std::find_if(assetHistory_.steps().begin(),assetHistory_.steps().end(),[&](const auto& e){return e.id==id;});
                if(it==assetHistory_.steps().end()||(!it->move&&it->files.size()!=1))
                    throw std::runtime_error("Unsupported asset history entry");
            }
            links.push_back({scope,id});
        }
        const auto plan=planHistoryBatch(links,owners,redo);
        for(const auto& step:plan){
            Json row=Json::object();row.set("scope",str(step.scope));row.set("historyId",str(step.id));rows.push(std::move(row));
        }
        result.set("steps",rows);
        result.set("mapVersion",str(historyVersion()));
        result.set("projectVersion",str(ws_.project.historyVersion()));
        result.set("assetVersion",str(assetHistoryVersion()));
        result.set("available",Json::fromBool(true));
        const auto bytes=result.dump(-1);const auto hash=pack::sha256(bytes.data(),bytes.size());
        result.set("token",str(pack::toHex(hash.data(),hash.size())));
    }catch(const std::exception& e){result.set("reason",str(e.what()));}
    return result;
}
void StudioSession::applyAgentBatch(bool redo,const std::string& expectedToken){
    const auto preview=agentBatchPreview(redo);
    if(!preview.getBool("available")||expectedToken.empty()||preview.getStr("token")!=expectedToken)
        throw std::runtime_error("Batch preview changed or unavailable: "+preview.getStr("reason"));
    // Stage all owner state before touching the filesystem. The outer batch
    // journal coordinates exact endpoint chains with existing-file edits.
    std::optional<editor::MapDoc> stagedMap;
    std::optional<editor::Project> stagedProject;
    std::optional<editor::ProjectHistory> stagedAssets;
    std::unique_ptr<StudioAssetCatalog> catalog;
    HistoryFileBatch files;
    std::vector<StudioBatchRelocation> relocations;
    std::vector<StudioBatchResourceEdit> movingResources;
    std::set<std::string> relocationEndpoints;
    std::vector<editor::ProjectHistory::MovePlan> ordinaryMoves;
    bool ordinaryChain=true;
    std::vector<editor::ProjectHistory::Step> orderedAssets;
    {
        auto probe=assetHistory_;
        for(const auto& row:preview["steps"].items())if(row.getStr("scope")=="assets"){
            const auto& entry=probe.next(redo);
            orderedAssets.push_back(entry);
            if(!entry.move||entry.move->quarantine||entry.move->replacement||entry.move->created)ordinaryChain=false;
            probe.advance(redo);
        }
    }
    const bool mixedChain=!ordinaryChain&&studioCanPlanMixedAssetChain(orderedAssets);
    std::vector<std::pair<std::string,bool>> referencePaths;
    std::vector<std::pair<std::string,bool>> removedPaths;
    for(const auto& row:preview["steps"].items()){
        const auto scope=row.getStr("scope");
        if(scope=="map"){
            if(!stagedMap)stagedMap.emplace(ws_.map);
            if(redo)stagedMap->redo();else stagedMap->undo();
        }else if(scope=="project"){
            if(!stagedProject)stagedProject.emplace(ws_.project);
            files.append(stagedProject->stageHistoryStep(redo));
        }else if(scope=="assets"){
            if(!stagedAssets){stagedAssets.emplace(assetHistory_);catalog=std::make_unique<StudioAssetCatalog>(ws_.project.dir);}
            const auto& entry=stagedAssets->next(redo);
            if(mixedChain){
                if(entry.move){
                    const auto& move=*entry.move;
                    if(!move.quarantine&&!move.replacement){
                        const auto& before=redo?move.materialsBefore:move.materialsAfter;
                        const auto& after=redo?move.materialsAfter:move.materialsBefore;
                        const auto material=std::filesystem::path(ws_.project.dir)/"materials.json";
                        if(bool(before)!=bool(after)||bool(before)!=std::filesystem::exists(material))
                            throw std::runtime_error("Material topology changed since move");
                        if(before){
                            if(!Json::parse(*after)["materials"].isObject())throw std::runtime_error("Invalid recorded material library");
                            files.append({{material,*before,*after}});
                        }
                    }
                    for(size_t i=0;i<move.files.size();i+=2){
                        const auto& f=move.files[i];
                        if(move.replacement)continue;
                        referencePaths.emplace_back(f.from,f.directory);
                        if(!move.quarantine)referencePaths.emplace_back(f.to,f.directory);
                        else if(move.created?!redo:redo){
                            if(studioDocumentNamesAsset(editor::loadMaterialLibrary(ws_),f.from,f.directory))
                                throw std::runtime_error("Asset is referenced by materials");
                            removedPaths.emplace_back(f.from,f.directory);
                        }
                    }
                }
                stagedAssets->advance(redo);continue;
            }
            if(entry.move){
                const auto& move=*entry.move;StudioBatchAssetPlan staged;
                if(ordinaryChain){
                    ordinaryMoves.push_back(move);
                    const auto& before=redo?move.materialsBefore:move.materialsAfter;
                    const auto& after=redo?move.materialsAfter:move.materialsBefore;
                    const auto material=std::filesystem::path(ws_.project.dir)/"materials.json";
                    if(bool(before)!=bool(after)||bool(before)!=std::filesystem::exists(material))
                        throw std::runtime_error("Material topology changed since move");
                    if(before){
                        if(!Json::parse(*after)["materials"].isObject())throw std::runtime_error("Invalid recorded material library");
                        files.append({{material,*before,*after}});
                    }
                    for(size_t i=0;i<move.files.size();i+=2){
                        const auto& f=move.files[i];referencePaths.emplace_back(f.from,f.directory);
                        referencePaths.emplace_back(f.to,f.directory);
                    }
                    stagedAssets->advance(redo);
                    continue;
                }
                const auto check=[&]{
                    std::vector<std::pair<std::string,bool>> paths;
                    const auto materials=editor::loadMaterialLibrary(ws_);
                    for(size_t i=0;i<move.files.size();i+=2){
                        const auto& f=move.files[i];paths.emplace_back(f.from,f.directory);
                        if(!move.quarantine)paths.emplace_back(f.to,f.directory);
                        else {
                            if(studioDocumentNamesAsset(materials,f.from,f.directory))
                                throw std::runtime_error("Asset is referenced by materials");
                            removedPaths.emplace_back(f.from,f.directory);
                        }
                    }
                    studioCheckMapAssetPaths(ws_.project.dir,ws_.map.toJson(),ws_.project.rulesJson,ws_.project.manifest,paths);
                    referencePaths.insert(referencePaths.end(),paths.begin(),paths.end());
                };
                if(move.replacement)studioReplayAssetReplacement(ws_.project.dir,move,redo,&staged);
                else if(move.quarantine)studioReplayAssetQuarantine(ws_.project.dir,move,move.created?!redo:redo,check,&staged);
                else studioReplayAssetMove(ws_.project.dir,move,redo,check,&staged);
                std::set<std::string> group;
                for(const auto& op:staged.relocations){group.insert(op.from);group.insert(op.to);}
                for(const auto& path:group)for(const auto& old:relocationEndpoints)
                    if(path==old||path.starts_with(old+"/")||old.starts_with(path+"/"))
                        throw std::runtime_error("Overlapping asset operations require individual navigation");
                relocationEndpoints.insert(group.begin(),group.end());
                relocations.insert(relocations.end(),staged.relocations.begin(),staged.relocations.end());
                files.append(staged.documents);
            }else{
                if(entry.files.size()!=1)throw std::runtime_error("Unsupported batch asset plan");
                const auto edits=redo?entry.files:editor::reverseProjectFileEdits(entry.files);
                catalog->validateMetadataEdit(edits.front());files.append(edits);
            }
            stagedAssets->advance(redo);
        }else throw std::runtime_error("Unknown batch owner");
    }
    if(mixedChain){
        auto chain=studioPlanMixedAssetChain(ws_.project.dir,orderedAssets,redo);
        relocations=std::move(chain.relocations);files.append(chain.documents);
        movingResources=std::move(chain.movingResources);
        studioCheckMapAssetPaths(ws_.project.dir,ws_.map.toJson(),ws_.project.rulesJson,ws_.project.manifest,referencePaths,false);
    }
    if(!ordinaryMoves.empty()){
        auto chain=studioPlanAssetMoveChain(ws_.project.dir,ordinaryMoves,redo);
        relocations=std::move(chain.relocations);files.append(chain.documents);
        movingResources=std::move(chain.movingResources);
        studioCheckMapAssetPaths(ws_.project.dir,ws_.map.toJson(),ws_.project.rulesJson,ws_.project.manifest,referencePaths,false);
    }
    // References must be safe in both the original and the prepared target
    // owner state. This deliberately refuses dependencies that would require
    // interleaving a map/rules write with a relocation.
    if(!referencePaths.empty()){
        const auto& map=stagedMap?*stagedMap:ws_.map;
        const auto& project=stagedProject?*stagedProject:ws_.project;
        studioCheckMapAssetPaths(project.dir,map.toJson(),project.rulesJson,project.manifest,referencePaths,ordinaryMoves.empty()&&!mixedChain);
    }
    auto plan=files.plan(); // validate EVERY exact on-disk baseline before writes
    for(const auto& edit:plan)if(edit.path.filename()=="materials.json"){
        const auto target=Json::parse(edit.after);
        for(const auto& [path,directory]:removedPaths)if(studioDocumentNamesAsset(target,path,directory))
            throw std::runtime_error("Coordinated target material references removed asset");
    }
    {
        if(stagedProject){
            auto checkpoint=editor::prepareProjectHistoryCheckpoint(ws_.project.dir,ws_.project.savedHistoryCheckpoint,stagedProject->documentHistory);
            if(!ws_.project.savedHistoryCheckpoint)ws_.project.savedHistoryCheckpoint=checkpoint.before;
            stagedProject->savedHistoryCheckpoint=checkpoint.after;stagedProject->historyPersistenceWarning.clear();
            plan.push_back(std::move(checkpoint));
        }
        studioApplyHistoryBatch(ws_.project.dir,relocations,plan,movingResources,
            stagedAssets?&assetHistory_:nullptr,stagedAssets?&*stagedAssets:nullptr);
    }
    // Publish the prepared in-memory owners only after the file commit.
    if(stagedProject)ws_.project=std::move(*stagedProject);
    if(stagedMap)ws_.map=std::move(*stagedMap);
    if(stagedAssets){
        assetHistory_=std::move(*stagedAssets);
        assetHistoryPersistenceWarning_.clear();
        try{catalog->rebuild(false);}
        catch(const std::exception& e){assetHistoryPersistenceWarning_="Asset index refresh required: "+std::string(e.what());}
    }
    pendingAssetTransfer_.reset();agentAssetTransfer_.reset();agentAssetTransferGuids_.clear();
    syncHistoryLabels();clearSelection();dropOrdinalFlags();agentHistoryUnsafe_=true;
    agentBatchOwnerVersion_=batchOwnerVersion();
}
Json StudioSession::agentHistoryLink(Json c) const{
    const auto scope=c.getStr("scope"),id=c.getStr("historyId");
    bool retained=false,applied=false,undo=false,redo=false;int64_t position=-1;
    std::string version;
    if(c.getBool("ok")&&!id.empty()&&c.getStr("sourceProject")==ws_.project.dir){
        if(scope=="map"&&c.getStr("sourceMap")==ws_.map.path){
            version=historyVersion();
            for(size_t i=0;i<ws_.map.undoDepth();++i)if(std::to_string(ws_.map.undoId(i))==id){
                retained=applied=true;position=int64_t(i+1);undo=i+1==ws_.map.undoDepth();
            }
            for(size_t i=0;i<ws_.map.redoDepth();++i)if(std::to_string(ws_.map.redoId(i))==id){
                retained=true;position=int64_t(ws_.map.undoDepth()+ws_.map.redoDepth()-i);redo=i+1==ws_.map.redoDepth();
            }
        }else if(scope=="project"||scope=="assets"){
            const auto& history=scope=="project"?ws_.project.documentHistory:assetHistory_;
            version=scope=="project"?ws_.project.historyVersion():assetHistoryVersion();
            const bool blocked=scope=="project"&&(ws_.project.rulesDirty||ws_.project.manifestDirty||ws_.project.historyRequiresReload||ws_.project.dir.empty());
            for(size_t i=0;i<history.steps().size();++i)if(history.steps()[i].id==id){
                retained=true;applied=i<history.position();position=int64_t(i+1);
                undo=!blocked&&applied&&i+1==history.position();redo=!blocked&&!applied&&i==history.position();
            }
        }
    }
    const bool safe=!stroking_&&!agentRun_.running();
    c.set("undoable",Json::fromBool(retained));c.set("applied",Json::fromBool(applied));
    c.set("canUndo",Json::fromBool(safe&&undo));c.set("canRedo",Json::fromBool(safe&&redo));
    c.set("position",num(position));c.set("historyVersion",str(version));
    return c;
}
static std::string readWizardDocument(const std::string& project, const std::string& expected, const char* name) {
    namespace fs = std::filesystem;
    if (project.empty() || expected != project) throw std::runtime_error("The active project changed");
    const auto path = fs::path(project) / name;
    if (!fs::is_regular_file(fs::symlink_status(path))) throw std::runtime_error(std::string(name)+" unavailable");
    constexpr size_t limit = 1024 * 1024;
    if (fs::file_size(path) > limit) throw std::runtime_error(std::string(name)+" exceeds 1 MiB");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error(std::string("Cannot open ")+name);
    std::string text(limit + 1, '\0');
    input.read(text.data(), text.size());
    if (!input.eof() || input.bad()) throw std::runtime_error(std::string("Cannot read ")+name);
    text.resize(size_t(input.gcount()));
    if (text.empty()) throw std::runtime_error(std::string(name)+" is empty");
    return text;
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
        agentStartIds_.clear();agentEditIds_.clear();agentHistoryUnsafe_=false;agentBatchFenced_=false;agentBatchOwnerVersion_.clear();
        agentAssetTransfer_.reset();agentAssetTransferGuids_.clear();
    } else if(method=="studio.agentStart"){
        const std::string client=params.getStr("client","claude-code");
        if(client!="claude-code" && client!="codex")throw std::runtime_error("Unknown agent client");
        if(params.has("expectedProject") && params.getStr("expectedProject")!=ws_.project.dir)
            throw std::runtime_error("The active project changed");
        const bool wizard=params.getBool("useWizardBrief");
        const std::string prompt=wizard ? readWizardDocument(ws_.project.dir,params.getStr("expectedProject"),"WIZARD_TASK.md") : params.getStr("prompt");
        if(prompt.empty()||(!wizard && prompt.size()>20000))throw std::runtime_error("Write a request for the agent (up to 20000 characters)");
        if(agentRun_.running())throw std::runtime_error("The agent is still working");
        if(mcpTool_.empty()||!fs::exists(mcpTool_))throw std::runtime_error("aiversemcp was not found next to Studio (build the aiversemcp target)");
        if(editor::agentCliPath(client).empty())throw std::runtime_error("Selected agent CLI unavailable");
        agentSession_.clear();
        if(!wizard && client=="claude-code" && agentRun_.client()==client && agentRunStart_>=0 && agentHistoryProject_==ws_.project.dir)
            agentSession_=agentRun_.sessionId();
        syncHistoryLabels();
        if(agentSession_.empty()||agentRunStart_<0){
            agentRunStart_=int64_t(undoLabels_.size());agentStartIds_.clear();agentEditIds_.clear();
            agentHistoryProject_=ws_.project.dir;agentHistoryMap_=ws_.map.path;agentHistoryUnsafe_=false;agentBatchFenced_=false;agentBatchOwnerVersion_=batchOwnerVersion();
            for(size_t i=0;i<ws_.map.undoDepth();++i)agentStartIds_.push_back(ws_.map.undoId(i));
        }
        const fs::path dir=fs::path(ws_.logsDir())/"studio-agent";
        std::error_code ec;fs::create_directories(dir,ec);
        // aiversemcp of this build, pointed at this Studio's port; strict: the user's global "aiverse" entry
        // (the ImGui editor's port) is not loaded for this run.
        Json server=Json::object(),argv=Json::array(),servers=Json::object(),config=Json::object();
        server.set("command",str(mcpTool_));
        for(const std::string& a:{std::string("--no-editor"),std::string("--engine"),ws_.engineDir,std::string("--port"),std::to_string(agentServer_.port())})argv.push(str(a));
        server.set("args",argv);servers.set("aiverse",server);config.set("mcpServers",servers);
        const fs::path configFile=dir/"mcp.json",taskFile=dir/"task.md",logFile=dir/"run.log";
        {std::ofstream(configFile)<<config.dump(2);}
        {
            std::ofstream task(taskFile);
            if(wizard) task<<"Use the aiverse MCP tools connected to this Studio project. Follow its generated game brief and report instructions.\n";
            else if(agentSession_.empty())
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
        if(client=="codex"){
            extra="--ignore-user-config -c "+shellQuote("mcp_servers.aiverse.command="+str(mcpTool_).dump())+
                  " -c "+shellQuote("mcp_servers.aiverse.args="+argv.dump());
        }
        if(!agentRun_.start(client,ws_.project.dir,taskFile.string(),logFile.string(),&err,extra))throw std::runtime_error(err);
        agentLines_.push_back("› "+clipText(prompt,2000));
        ws_.log("agent","Агент: "+clipText(prompt,120));
    }
    for(auto& line:agentRun_.takeLines())agentLines_.push_back(line);
    Json out=Json::object(),lines=Json::array();
    const int64_t from=std::max<int64_t>(0,params["from"].integer(0));
    for(size_t i=size_t(std::min<int64_t>(from,int64_t(agentLines_.size())));i<agentLines_.size();++i)lines.push(str(agentLines_[i]));
    out.set("lines",lines);out.set("total",num(int64_t(agentLines_.size())));
    const fs::path project = ws_.project.dir;
    std::error_code fileError;
    out.set("hasWizardBrief", Json::fromBool(fs::is_regular_file(fs::symlink_status(project/"WIZARD_TASK.md",fileError))));
    out.set("hasWizardReport", Json::fromBool(fs::is_regular_file(fs::symlink_status(project/"WIZARD.md",fileError))));
    if (method == "studio.agentInfo" && params.has("briefForProject"))
        out.set("wizardBrief", str(readWizardDocument(ws_.project.dir, params.getStr("briefForProject"), "WIZARD_TASK.md")));
    if (method == "studio.agentInfo" && params.has("reportForProject"))
        out.set("wizardReport", str(readWizardDocument(ws_.project.dir, params.getStr("reportForProject"), "WIZARD.md")));
    out.set("running",Json::fromBool(agentRun_.running()));
    out.set("port",num(int64_t(agentServer_.port())));
    out.set("clients",num(int64_t(agentServer_.clients())));
    Json changes=Json::array();
    for(auto c:agentChanges_.items())changes.push(agentHistoryLink(std::move(c)));
    const bool canUndoAll=agentCanUndoAll();
    out.set("changes",std::move(changes));
    out.set("startPosition",num(canUndoAll?int64_t(agentStartIds_.size()):-1));
    out.set("historyVersion",str(historyVersion()));
    out.set("batchUndo",agentBatchPreview(false));out.set("batchRedo",agentBatchPreview(true));
    static const std::string cli=editor::agentCliPath("claude-code");   // a login-shell lookup: once per run of Studio
    static const std::string codex=editor::agentCliPath("codex");
#if defined(_WIN32)
    out.set("cli",str(""));out.set("codexCli",str(""));
#else
    out.set("cli",str(cli));out.set("codexCli",str(codex));
#endif
    out.set("runClient",str(agentRun_.client()));
    out.set("mcp",Json::fromBool(!mcpTool_.empty()));
    out.set("conversation",Json::fromBool(agentRunStart_>=0 && agentHistoryProject_==ws_.project.dir && (!agentSession_.empty()||!agentRun_.sessionId().empty())));
    if(agentRun_.finished())out.set("result",str(agentRun_.result()));
    return out;
}
Json StudioSession::buildRequest(const std::string& method, const Json& params) {
    namespace fs = std::filesystem;
    if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
    const fs::path builds = fs::path(ws_.project.dir) / "Builds";
    const Json& manifest = ws_.project.manifest;
    const Json& pages = manifest["atlas"]["maxPage"];
    const bool atlasEnabled = manifest["atlas"].getBool("enable",true);
    const auto compression = manifest["packaging"].getInt("level",0);
    if (method == "studio.buildRemoveFileRule" || method == "studio.buildFileTextureSize" || method == "studio.buildFileExclusion" || method == "studio.buildFileCompression") {
        const auto scope = params.getStr("scope"), target = params.getStr("target");
        if (!knownBuildTarget(target) || (scope!="project" && scope!="target") ||
            params.getStr("project")!=ws_.project.dir || ws_.building()) throw std::runtime_error("Build settings unavailable");
        const auto& rules = scope=="project" ? manifest["packaging"]["files"] : manifest["packaging"]["targets"][target]["files"];
        Json next = Json::array();
        if (method!="studio.buildRemoveFileRule") {
            const auto path = params.getStr("path");
            const auto& level = params["level"];
            if (path.empty() || path.find_first_of("*?")!=std::string::npos) throw std::runtime_error("Expected an exact file path");
            if (method=="studio.buildFileCompression" && (!params.has("level") ||
                (!level.isNull() && (!level.isNumber() || level.toDouble()!=std::floor(level.toDouble()) ||
                    level.toDouble()<0 || level.toDouble()>22)))) throw std::runtime_error("Invalid file compression rule");
            if (method=="studio.buildFileExclusion" && (params["exclude"].type()!=Json::Type::Bool ||
                (params.getBool("exclude") && (path=="game.json" || path.rfind("engine/",0)==0 || path.rfind("_atlas/",0)==0))))
                throw std::runtime_error("Invalid file exclusion");
            if (!rules.isNull() && !rules.isArray()) throw std::runtime_error("Invalid file rule list");
            size_t index = rules.size();
            for (size_t i=0;i<rules.size();++i) if (rules[i].getStr("match")==path) index=i;
            const bool exists = index<rules.size();
            if ((exists ? rules[index].dump() : "")!=params.getStr("original"))
                throw std::runtime_error("The file rule changed. Review the current rules.");
            Json updated = exists ? rules[index] : Json::object();
            updated.set("match",str(path));
            if (method=="studio.buildFileTextureSize") {
                auto ext=fs::path(path).extension().string();
                std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
                if (path.rfind("_atlas/",0)==0 || (ext!=".png" && ext!=".jpg" && ext!=".jpeg" && ext!=".tga" && ext!=".bmp"))
                    throw std::runtime_error("Choose a source image");
                const auto& size=params["size"];
                if (!params.has("size") || (!size.isNull() && (!size.isNumber() ||
                    (size.toDouble()!=0.25 && size.toDouble()!=0.5 && size.toDouble()!=256 &&
                     size.toDouble()!=512 && size.toDouble()!=1024 && size.toDouble()!=2048))))
                    throw std::runtime_error("Invalid texture size");
                updated.erase("maxSize");
                updated.erase("scale");
                if (!size.isNull()) updated.set(size.toDouble()<1 ? "scale" : "maxSize",size);
            } else if (method=="studio.buildFileExclusion") {
                if (params.getBool("exclude")) updated.set("exclude",Json::fromBool(true));
                else updated.erase("exclude");
            } else if (level.isNull()) updated.erase("level");
            else updated.set("level",level);
            for (size_t i=0;i<rules.size();++i) {
                if (i!=index) next.push(rules[i]);
                else if (updated.size()>1) next.push(updated);
            }
            if (!exists && updated.size()>1) next.push(updated);
        } else {
        const auto index = params.getInt("index",-1);
        if (!rules.isArray() || index<0 || size_t(index)>=rules.size() || rules[size_t(index)].dump()!=params.getStr("original"))
            throw std::runtime_error("The file rule changed. Review the current rules.");
        for (size_t i=0;i<rules.size();++i) if (i!=size_t(index)) next.push(rules[i]);
        }
        Json patch = Json::object();
        patch.set("path",str(scope=="project" ? "packaging.files" : "packaging.targets."+target+".files"));
        patch.set("value",next);
        const auto saved = ws_.call("set_manifest",patch);
        if (saved.has("error")) throw std::runtime_error(saved.getStr("error"));
        return snapshot();
    }
    if (method == "studio.buildSplitLimit") {
        const auto target = params.getStr("target"), field = params.getStr("field");
        if (!knownBuildTarget(target) || ws_.building() || params.getStr("project") != ws_.project.dir ||
            (field != "chunkSize" && field != "baseSize")) throw std::runtime_error("Build settings unavailable");
        const auto& current = manifest["packaging"]["targets"][target];
        if (!params.has("original") || current[field].dump() != params["original"].dump())
            throw std::runtime_error("Package limit changed. Review the current value.");
        const auto& limit = params["bytes"];
        if (!params.has("bytes") || (!limit.isNull() && (!limit.isNumber() || limit.toDouble()<0 ||
            limit.toDouble()>9007199254740991.0 || limit.toDouble()!=std::floor(limit.toDouble()) ||
            (field=="chunkSize" && limit.toDouble()==0)))) throw std::runtime_error("Invalid package size in bytes");
        Json next = current.isObject() ? current : Json::object();
        if (limit.isNull()) next.erase(field);
        else next.set(field,limit);
        if (current[field].dump() != limit.dump()) {
            Json patch = Json::object();
            patch.set("path",str("packaging.targets." + target));
            patch.set("value",next);
            const auto saved = ws_.call("set_manifest",patch);
            if (saved.has("error")) throw std::runtime_error(saved.getStr("error"));
        }
        return snapshot();
    }
    if (method == "studio.buildSplitMode") {
        const auto target = params.getStr("target");
        if (!knownBuildTarget(target) || ws_.building() || params.getStr("project") != ws_.project.dir)
            throw std::runtime_error("Build settings unavailable");
        const auto& current = manifest["packaging"]["targets"][target];
        if (!params.has("original") || current["split"].dump() != params["original"].dump())
            throw std::runtime_error("Download settings changed. Review the current mode.");
        const auto& mode = params["mode"];
        if (!params.has("mode") || (!mode.isNull() && mode.type() != Json::Type::Bool))
            throw std::runtime_error("Invalid download mode");
        if (current["split"].dump() != mode.dump()) {
            Json next = current.isObject() ? current : Json::object();
            if (mode.isNull()) next.erase("split");
            else next.set("split", mode);
            Json patch = Json::object();
            patch.set("path", str("packaging.targets." + target));
            patch.set("value", next);
            const auto saved = ws_.call("set_manifest", patch);
            if (saved.has("error")) throw std::runtime_error(saved.getStr("error"));
        }
        return snapshot();
    }
    if (method == "studio.buildCompression") {
        if (ws_.building() || params.getStr("project")!=ws_.project.dir ||
            !params["original"].isNumber() || params["original"].toDouble()!=compression)
            throw std::runtime_error("Build settings changed. Review the current compression level.");
        const auto& level=params["level"];
        const auto value=level.toDouble();
        if (!level.isNumber() || value!=std::floor(value) || value<0 || value>22)
            throw std::runtime_error("Choose a compression level from 1 to 22, or 0 for automatic.");
        if (value!=compression) {
            Json patch=Json::object();patch.set("path",str("packaging.level"));patch.set("value",level);
            const auto saved=ws_.call("set_manifest",patch);
            if (saved.has("error")) throw std::runtime_error(saved.getStr("error"));
        }
        return snapshot();
    }
    if (method == "studio.buildGameWindow") {
        const Json& original=manifest["window"];
        const auto scope=params.getStr("scope");
        if (ws_.building() || params.getStr("project")!=ws_.project.dir ||
            params.getStr("revision")!=original.dump() ||
            (scope!="desktop" && scope!="macos" && scope!="windows" && scope!="linux"))
            throw std::runtime_error("Game window settings changed or unavailable. Reload the Build window.");
        Json next=original.isObject()?original:Json::object();
        if (params.getBool("reset")) {
            if (scope=="desktop") throw std::runtime_error("Choose a platform override to reset.");
            next.erase(scope);
        } else {
            const Json& changes=params["changes"];
            if (!changes.isObject()) throw std::runtime_error("Expected window settings.");
            Json block=scope=="desktop"?next:next[scope];
            if (!block.isObject()) block=Json::object();
            for (const auto& [key,value]:changes.members()) {
                if (key=="width" || key=="height" || key=="minWidth" || key=="minHeight") {
                    const int minimum=key=="width"?320:key=="height"?240:0;
                    const double number=value.toDouble();
                    if (!value.isNumber() || std::floor(number)!=number || number<minimum || number>16384)
                        throw std::runtime_error("Invalid window dimension: "+key);
                } else if (key=="mode") {
                    if (!value.isString()) throw std::runtime_error("Expected a window mode.");
                } else if (key=="allowFullscreen" || key=="resizable" || key=="keepAspect") {
                    if (value.type()!=Json::Type::Bool) throw std::runtime_error("Expected a boolean window setting.");
                } else throw std::runtime_error("Unknown window setting: "+key);
                block.set(key,value);
            }
            if (scope=="desktop") next=std::move(block); else next.set(scope,std::move(block));
        }
        const auto problems=platform::windowSettingsProblems(next);
        if (!problems.empty()) throw std::runtime_error(problems.front());
        if (next!=original) {
            Json patch=Json::object();patch.set("path",str("window"));patch.set("value",next);
            const Json result=ws_.call("set_manifest",patch);
            if (result.has("error")) throw std::runtime_error(result.getStr("error"));
        }
        return snapshot();
    }
    if (method == "studio.buildAtlasEnabled") {
        if (params.getStr("project") != ws_.project.dir || ws_.building() ||
            params["original"].type()!=Json::Type::Bool || params.getBool("original")!=atlasEnabled)
            throw std::runtime_error("Atlas settings changed or a build is running.");
        if (params["enabled"].type()!=Json::Type::Bool) throw std::runtime_error("Invalid atlas setting");
        if (params.getBool("enabled")!=atlasEnabled) {
            Json patch=Json::object();patch.set("path",str("atlas.enable"));patch.set("value",params["enabled"]);
            const auto saved=ws_.call("set_manifest",patch);
            if (saved.has("error")) throw std::runtime_error(saved.getStr("error"));
        }
        return snapshot();
    }
    if (method == "studio.buildAtlasPage") {
        if (params.getStr("project") != ws_.project.dir || !params.has("original") ||
            params["original"].dump() != pages.dump())
            throw std::runtime_error("Atlas settings changed. Refresh the Build panel.");
        const auto target = params.getStr("target");
        if (!knownBuildTarget(target) || ws_.building()) throw std::runtime_error("Build settings unavailable");
        const auto& size = params["size"];
        if (!params.has("size") || (!size.isNull() && (!size.isNumber() ||
            (size.toDouble()!=1024 && size.toDouble()!=2048 && size.toDouble()!=4096 && size.toDouble()!=8192))))
            throw std::runtime_error("Invalid atlas page size");
        Json next = pages.isObject() ? pages : Json::object();
        if (pages.isNumber()) next.set("default",pages);
        if (size.isNull()) {
            if (!next.erase(target)) return snapshot();
        } else next.set(target,size);
        Json patch=Json::object();patch.set("path",str("atlas.maxPage"));patch.set("value",next);
        const auto saved=ws_.call("set_manifest",patch);
        if (saved.has("error")) throw std::runtime_error(saved.getStr("error"));
        return snapshot();
    }
    if (method == "studio.buildNativeProject") {
        if (params.getStr("project") != ws_.project.dir || ws_.building())
            throw std::runtime_error("Project or build state changed");
        auto result = ws_.call("native_build", Json::object());
        if (result.has("error")) throw std::runtime_error(result.getStr("error"));
        return result;
    }
    if (method == "studio.buildInfo") {
        Json out = Json::object(), targets = Json::array();
        out.set("gameWindow",manifest["window"]);
        out.set("windowRevision",str(manifest["window"].dump()));
        auto fileRules = [](const Json& rules) {
            Json rows = Json::array();
            for (const auto& rule : rules.items()) {
                Json row = Json::object();
                row.set("rule",rule);
                row.set("original",str(rule.dump()));
                rows.push(std::move(row));
            }
            return rows;
        };
        if (ws_.project.rulesJson["nativeModules"].size()) {
            out.set("nativeModules", ws_.call("native_status", Json::object()));
        }
        out.set("rules",fileRules(manifest["packaging"]["files"]));
        for (const auto& t : kBuildTargets) {
            Json v = Json::object();
            v.set("id", str(t[0])); v.set("name", str(t[1]));
            std::error_code ec;
            for (const auto* field : {"baseSize","chunkSize"}) v.set(field,manifest["packaging"]["targets"][t[0]][field]);
            v.set("rules",fileRules(manifest["packaging"]["targets"][t[0]]["files"]));
            v.set("split",manifest["packaging"]["targets"][t[0]]["split"]);
            v.set("built", Json::fromBool(fs::is_directory(builds / t[0], ec)));
            targets.push(std::move(v));
        }
        out.set("project", str(ws_.project.dir));
        for (const auto* field : {"baseSize","chunkSize"}) out.set(field,manifest["packaging"][field]);
        out.set("splitDefault",Json::fromBool(manifest["packaging"].getBool("split",false)));
        out.set("compression",num(int64_t(compression)));
        out.set("atlasEnabled", Json::fromBool(atlasEnabled));
        out.set("atlasPages", pages);
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
    if (method == "studio.installBuildTools") {
        if (!knownBuildTarget(target)) throw std::runtime_error("Unknown build target");
        if (params.getStr("project") != ws_.project.dir) throw std::runtime_error("Build project changed");
        ws_.startTool("--install-toolchain " + target, "toolchain-" + target, false);
        Json out = Json::object();
        out.set("started", Json::fromBool(true));
        return out;
    }
    if (method == "studio.buildStart") {
        if (!knownBuildTarget(target)) throw std::runtime_error("Unknown build target");
        const std::string config = params.getStr("config", "Release");
        if (config != "Release" && config != "Debug") throw std::runtime_error("Unknown build configuration");
        // A build packs the files on disk: never save implicitly from here.
        if (ws_.map.dirty || ws_.project.rulesDirty || ws_.project.manifestDirty)
            throw std::runtime_error("Save the project before building");
        const std::string audio = params.getStr("audio", "auto");
        std::string flags;
        if (audio != "auto") {
            if (audio != "desktop" && audio != "mobile" && audio != "web" && audio != "off")
                throw std::runtime_error("Unknown audio profile");
            flags = "--audio " + audio;
        }
        if (params.getBool("estimate")) {
            const int completed = ws_.runCount() + 1;
            ws_.startBuild(target, config, "--estimate " + flags, (builds / ".temp" / "estimate").string());
            estimateRun_=completed;estimateProject_=ws_.project.dir;estimateTarget_=target;estimateManifest_=manifest.dump();
        } else ws_.startBuild(target, config, flags + (params.getBool("installMissing") ? " --install-missing" : ""));
        Json out = Json::object(); out.set("started", Json::fromBool(true));
        return out;
    }
    if (method == "studio.buildLog") {
        if (params.has("project") && params.getStr("project") != ws_.project.dir)
            throw std::runtime_error("Build log project changed");
        return ws_.buildLogChunk(params.getInt("from"),params.getInt("run",-1));
    }
    // studio.buildEstimate: the size report of the last estimate run for `target`.
    if (!knownBuildTarget(target)) throw std::runtime_error("Unknown build target");
    const fs::path report = builds / ".temp" / "estimate" / target / "size-report.json";
    std::error_code ec;
    if (!fs::is_regular_file(report, ec)) throw std::runtime_error("No size estimate yet for " + target);
    const Json rep = Json::parseFile(report.string());
    Json out = Json::object();
    const bool current = estimateProject_ == ws_.project.dir && estimateTarget_ == target &&
        estimateRun_ == ws_.runCount() && !ws_.building() && ws_.buildResult()==0 && estimateManifest_==manifest.dump();
    const auto& excluded=rep["atlas"]["excluded"];
    const auto bytes=excluded.dump(-1);
    const auto hash=pack::sha256(bytes.data(),bytes.size());
    const auto token=pack::toHex(hash.data(),hash.size());
    if (method=="studio.buildAtlasFixSizes") {
        if (!current || params.getStr("project")!=ws_.project.dir || params.getInt("run",-1)!=estimateRun_ ||
            params.getStr("token")!=token) throw std::runtime_error("Run a new size estimate before applying texture sizes.");
        const auto& existing=manifest["packaging"]["targets"][target]["files"];
        const auto rules=editor::atlasSizeFixRules(existing,excluded);
        if (!rules.size() || rules.dump()==existing.dump()) return snapshot();
        Json patch=Json::object();patch.set("path",str("packaging.targets."+target+".files"));patch.set("value",rules);
        const auto saved=ws_.call("set_manifest",patch);
        if (saved.has("error")) throw std::runtime_error(saved.getStr("error"));
        return snapshot();
    }
    if(current) {out.set("fixRun",num(int64_t(estimateRun_)));out.set("fixToken",str(token));}
    out.set("target", str(target));
    out.set("pack", rep["pack"]); out.set("atlas", rep["atlas"]);
    if (rep["fileRules"].isArray()) out.set("fileRules",rep["fileRules"]);
    if (rep.has("split")) out.set("split", rep["split"]);
    std::vector<const Json*> all;
    for (const auto& f : rep["files"].items()) all.push_back(&f);
    std::stable_sort(all.begin(), all.end(), [](const Json* a, const Json* b) { return (*a)["storedBytes"].integer() > (*b)["storedBytes"].integer(); });
    Json files = Json::array();
    for (size_t i = 0; i < all.size() && i < 25; ++i) files.push(*all[i]);
    out.set("heaviest", files);
    Json inventory = Json::array();
    for (const auto* file : all) inventory.push(*file);
    out.set("files",std::move(inventory));
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
        {"studio.selectedRoadSet","Edit movement road"},{"studio.mapRegionName","Rename map region"},{"studio.mapArea","Edit map area"},{"studio.mapPlayer","Edit map players"},{"studio.mapPropertiesSet","Edit map properties"},{"studio.entityCreate","Create object"},{"studio.entityDelete","Delete"},{"studio.entitiesDelete","Delete"},
        {"studio.entityDuplicate","Duplicate"},{"studio.entitiesDuplicate","Duplicate"},{"studio.entityMove","Reorder"},
        {"studio.assetPlace","Place asset"},{"studio.componentAdd","Add component"},{"studio.componentRemove","Remove component"},
        {"studio.lightAdd","Add light"},{"studio.lightUpdate","Edit light"},{"studio.lightDelete","Delete light"},
        {"studio.lightingSet","Edit lighting"},{"studio.renderLightingSet","Edit render lighting"},
        {"studio.nereidFlowSet","Edit water"},{"studio.keepPlayChanges","Keep play changes"},{"studio.entitiesAlign","Align"},{"studio.newMap","New map"}};
    for(const auto& [m,label]:names)if(method==m)return label;
    if(method=="studio.planetForestPlant")return "Plant on planet";
    if(method=="studio.planetForestMove")return "Move planetary plant";
    if(method=="studio.planetForestSet")return "Edit planetary forest";
    if(method=="studio.worldProcessAreaBatchMove")return "Move EVOLVE region group";
    if(method=="studio.worldProcessAreaBatchResize")return "Resize EVOLVE region group";
    if(method=="studio.worldProcessAreaSet")
        return params.getStr("componentId")=="GranularField"?
            "Resize GRANULAR field":"Resize EVOLVE gameplay area";
    if(method=="studio.op")return studioToolLabel(params.getStr("op"));
    return "Edit";
}
void StudioSession::syncHistoryLabels() {
    // MapDoc owns history identity; label lookup no longer guesses by stack depth.
    std::map<uint64_t,std::string> retained;
    auto label=[&](uint64_t id){
        const auto it=historyLabelsById_.find(id);
        const auto saved=ws_.map.historyLabel(id);
        const auto name=it==historyLabelsById_.end()?(saved.empty()?std::string("Edit"):saved):it->second;
        retained.emplace(id,name);return name;
    };
    undoLabels_.clear();redoLabels_.clear();
    for(size_t i=0;i<ws_.map.undoDepth();++i)undoLabels_.push_back(label(ws_.map.undoId(i)));
    for(size_t i=0;i<ws_.map.redoDepth();++i)redoLabels_.push_back(label(ws_.map.redoId(i)));
    historyLabelsById_=std::move(retained);
}
void StudioSession::labelNewHistory(uint64_t mark,const std::string& label){
    for(size_t i=0;i<ws_.map.undoDepth();++i)
        if(ws_.map.undoId(i)>mark){
            historyLabelsById_[ws_.map.undoId(i)]=label;
            ws_.map.setHistoryLabel(ws_.map.undoId(i),label);
        }
    syncHistoryLabels();
}
std::string StudioSession::historyVersion() const {
    std::string v=ws_.project.dir+"|"+ws_.map.path+"|"+ws_.map.historyEpoch()+"|"+std::to_string(ws_.map.revision);
    for(size_t i=0;i<ws_.map.undoDepth();++i)v+=":u"+std::to_string(ws_.map.undoId(i));
    for(size_t i=0;i<ws_.map.redoDepth();++i)v+=":r"+std::to_string(ws_.map.redoId(i));
    return v;
}
Json StudioSession::historyJson() const {
    Json out=Json::object(),steps=Json::array();
    for(const auto& l:undoLabels_){Json s=Json::object();s.set("label",str(l));s.set("done",Json::fromBool(true));steps.push(std::move(s));}
    for(auto it=redoLabels_.rbegin();it!=redoLabels_.rend();++it){Json s=Json::object();s.set("label",str(*it));s.set("done",Json::fromBool(false));steps.push(std::move(s));}
    out.set("steps",steps);out.set("position",Json::number(int64_t(undoLabels_.size())));
    out.set("version",str(historyVersion()));out.set("scope",str("map"));out.set("strokeActive",Json::fromBool(stroking_));
    out.set("persistent",Json::fromBool(!ws_.map.path.empty()));
    if(!ws_.map.historyPersistenceWarning().empty())out.set("persistenceWarning",str(ws_.map.historyPersistenceWarning()));
    return out;
}
std::string StudioSession::assetHistoryVersion() const {
    return ws_.project.dir+"|assets|"+assetHistory_.versionToken();
}
void StudioSession::loadAssetHistory(){
    assetHistory_=editor::ProjectHistory{};assetHistoryPersistenceWarning_.clear();
    try{assetHistory_=studioLoadAssetHistoryCheckpoint(ws_.project.dir);}
    catch(const std::exception& e){assetHistoryPersistenceWarning_=e.what();}
}
void StudioSession::finishAssetHistoryPublication(StudioAssetHistoryPublication& publication,bool success) noexcept{
    try{
        if(success)publication.save(assetHistory_);
        else if(!publication.cancelUnchanged(assetHistory_)){
            assetHistoryPersistenceWarning_="Asset operation did not complete; saved history fenced until next checkpoint";
            return;
        }
        assetHistoryPersistenceWarning_.clear();
    }catch(const std::exception& e){assetHistoryPersistenceWarning_=e.what();}
}
Json StudioSession::assetHistoryJson() const {
    Json out=Json::object(),steps=Json::array();
    size_t i=0;
    for(const auto& entry:assetHistory_.steps()){
        Json row=Json::object();row.set("label",str(entry.label));
        row.set("done",Json::fromBool(i++<assetHistory_.position()));steps.push(std::move(row));
    }
    out.set("scope",str("assets"));out.set("steps",std::move(steps));
    out.set("persistent",Json::fromBool(true));
    if(!assetHistoryPersistenceWarning_.empty())out.set("persistenceWarning",str(assetHistoryPersistenceWarning_));
    out.set("position",Json::number(int64_t(assetHistory_.position())));
    out.set("bytes",Json::number(int64_t(assetHistory_.bytes())));
    out.set("maxSteps",Json::number(int64_t(editor::ProjectHistory::MaxSteps)));
    out.set("maxBytes",Json::number(int64_t(editor::ProjectHistory::MaxBytes)));
    out.set("version",str(assetHistoryVersion()));out.set("strokeActive",Json::fromBool(stroking_));
    out.set("canUndo",Json::fromBool(!stroking_&&assetHistory_.canUndo()));
    out.set("canRedo",Json::fromBool(!stroking_&&assetHistory_.canRedo()));
    return out;
}
Json StudioSession::dispatch(const Json& request) {
    const std::string method=request.getStr("method");
    const bool switching=method=="studio.openProject"||method=="studio.openMap"||method=="studio.newMap"||method=="studio.hubOpen"||method=="studio.hubCreate";
    if(switching){strokeCancel();brush_.fixedTile=-1;brush_.armed=false;brush_.pickHeight=false;brush_.regionBorders=false;agentHistoryUnsafe_=true;agentBatchFenced_=true;}
    if(stroking_){
        // The stroke owns the MapDoc transaction across frames. Only explicit
        // observational requests can enter; other edits retry after release.
        static const std::set<std::string> reads={"studio.snapshot","studio.mapRegions","studio.mapProperties","studio.hierarchyGroups","studio.inspect","studio.propertiesFor","studio.history","studio.projectHistory","studio.assetHistory","studio.assetTrashList","studio.stats","studio.project","studio.agentInfo","studio.agentStop","studio.componentCatalog"};
        if(method=="studio.sceneBrush"&&request["params"].has("armed")&&!request["params"].getBool("armed",true))strokeCancel();
        else if(!reads.count(method)){
            Json reply=Json::object();reply.set("id",request["id"]);reply.set("error",str("Finish or cancel the active Scene stroke first"));return reply;
        }
    }
    if(method=="studio.agentBatchPreview"||method=="studio.agentBatchApply"){
        Json reply=Json::object();reply.set("id",request["id"]);
        try{
            if(!request.isObject()||!request["id"].isNumber())throw std::runtime_error("Invalid request envelope");
            const auto& p=request["params"];const auto direction=p.getStr("direction");
            if(direction!="undo"&&direction!="redo")throw std::runtime_error("Invalid batch direction");
            if(method=="studio.agentBatchPreview")reply.set("result",agentBatchPreview(direction=="redo"));
            else{
                if(!p.getBool("confirmed"))throw std::runtime_error("Confirm the reviewed agent history batch");
                applyAgentBatch(direction=="redo",p.getStr("token"));
                reply.set("result",snapshot());
            }
        }catch(const std::exception& e){reply.set("error",str(e.what()));}
        return reply;
    }
    if(method=="studio.agentHistoryStep"||method=="studio.agentUndoAll"){
        Json reply=Json::object();reply.set("id",request["id"]);
        try{
            if(!request.isObject()||!request["id"].isNumber()||agentRun_.running())throw std::runtime_error("Stop the agent before navigating history");
            const auto& p=request["params"];
            if(method=="studio.agentUndoAll"){
                if(!agentCanUndoAll()||p.getStr("expectedVersion")!=historyVersion())
                    throw std::runtime_error("Agent map history changed; refresh first");
                while(ws_.map.undoDepth()>agentStartIds_.size())ws_.map.undo();
                syncHistoryLabels();clearSelection();dropOrdinalFlags();agentHistoryUnsafe_=true;
            }else{
                const auto direction=p.getStr("direction"),scope=p.getStr("scope"),id=p.getStr("historyId");
                if(direction!="undo"&&direction!="redo")throw std::runtime_error("Invalid agent history direction");
                bool allowed=false;
                for(const auto& c:agentChanges_.items())if(c.getStr("scope")==scope&&c.getStr("historyId")==id&&!id.empty()){
                    const auto link=agentHistoryLink(c);
                    allowed=link.getStr("historyVersion")==p.getStr("expectedVersion")&&link.getBool(direction=="undo"?"canUndo":"canRedo");
                    if(allowed)break;
                }
                if(!allowed)throw std::runtime_error("Agent edit is not at the current history boundary; refresh History");
                if(scope=="map"){
                    if(direction=="undo")ws_.map.undo();else ws_.map.redo();
                    syncHistoryLabels();clearSelection();dropOrdinalFlags();
                }else{
                    Json nested=Json::object();nested.set("id",request["id"]);
                    nested.set("method",str(scope=="project"?"studio.projectHistoryStep":"studio.assetHistoryStep"));
                    nested.set("params",p);
                    const auto result=dispatch(nested);
                    if(result.has("error"))throw std::runtime_error(result.getStr("error"));
                }
                agentHistoryUnsafe_=true;
            }
            reply.set("result",snapshot());
        }catch(const std::exception& e){reply.set("error",str(e.what()));}
        return reply;
    }
    if(method=="studio.assetHistory"||method=="studio.assetHistoryStep"){
        Json reply=Json::object();reply.set("id",request["id"]);
        try {
            if(!request.isObject()||!request["id"].isNumber())throw std::runtime_error("Invalid request envelope");
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            const bool step=method=="studio.assetHistoryStep";
            if(step){
                const auto& params=request["params"];
                if(!params["expectedVersion"].isString()||params.getStr("expectedVersion")!=assetHistoryVersion())
                    throw std::runtime_error("Asset history changed; refresh before navigating");
                const auto direction=params.getStr("direction");
                if(direction!="undo"&&direction!="redo")throw std::runtime_error("Invalid asset history direction");
                const bool redo=direction=="redo";
                const auto& entry=assetHistory_.next(redo);
                StudioBatchAssetPlan staged;
                auto next=assetHistory_;next.advance(redo);
                if(entry.move&&entry.move->replacement){
                    studioReplayAssetReplacement(ws_.project.dir,*entry.move,redo,&staged);
                }else if(entry.move&&entry.move->quarantine){
                    const auto check=[&]{
                        std::vector<std::pair<std::string,bool>> paths;
                        const auto materials=editor::loadMaterialLibrary(ws_);
                        for(size_t i=0;i<entry.move->files.size();i+=2){
                            const auto& f=entry.move->files[i];
                            paths.emplace_back(f.from,f.directory);
                            if(studioDocumentNamesAsset(materials,f.from,f.directory))throw std::runtime_error("Asset is referenced by materials");
                        }
                        studioCheckMapAssetPaths(ws_.project.dir,ws_.map.toJson(),ws_.project.rulesJson,ws_.project.manifest,paths);
                    };
                    studioReplayAssetQuarantine(ws_.project.dir,*entry.move,entry.move->created?!redo:redo,check,&staged);
                }else if(entry.move){
                    // Recheck known read-only rules references before reversing paths.
                    const auto check=[&]{
                        StudioAssetCatalog catalog(ws_.project.dir);catalog.rebuild(false);
                        const auto refs=studioCollectAssetReferences(editor::loadMaterialLibrary(ws_),ws_.project.rulesJson,catalog.all());
                        std::vector<std::pair<std::string,bool>> paths;
                        for(size_t i=0;i<entry.move->files.size();i+=2){
                            const auto& f=entry.move->files[i];
                            paths.emplace_back(f.from,f.directory);paths.emplace_back(f.to,f.directory);
                            for(const auto& ref:refs)
                                if(!ref.writable&&(ref.path=="content/"+f.from||ref.path=="content/"+f.to||
                                    (f.directory&&(ref.path.starts_with("content/"+f.from+"/")||ref.path.starts_with("content/"+f.to+"/")))))
                                    throw std::runtime_error("Asset move history blocked by rules reference: "+ref.owner);
                        }
                        studioCheckMapAssetPaths(ws_.project.dir,ws_.map.toJson(),ws_.project.rulesJson,ws_.project.manifest,paths,false);
                    };
                    if(entry.move->resourceEdits.empty())studioReplayAssetMove(ws_.project.dir,*entry.move,redo,check,&staged);
                    else{
                        check();staged=studioPlanAssetMoveChain(ws_.project.dir,{*entry.move},redo);
                        const auto& before=redo?entry.move->materialsBefore:entry.move->materialsAfter;
                        const auto& after=redo?entry.move->materialsAfter:entry.move->materialsBefore;
                        if(bool(before)!=bool(after))throw std::runtime_error("Unsupported material topology change");
                        const auto material=std::filesystem::canonical(ws_.project.dir)/"materials.json";
                        if(before)staged.documents.push_back({material,*before,*after});
                        else if(std::filesystem::exists(material))throw std::runtime_error("Material library topology changed");
                    }
                }else{
                    if(entry.files.size()!=1)throw std::runtime_error("Unsupported asset history entry");
                    const auto plan=redo?entry.files:editor::reverseProjectFileEdits(entry.files);
                    StudioAssetCatalog catalog(ws_.project.dir);
                    catalog.validateMetadataEdit(plan.front());staged.documents=plan;
                }
                studioApplyHistoryBatch(ws_.project.dir,staged.relocations,staged.documents,staged.movingResources,
                    &assetHistory_,&next);
                assetHistory_=std::move(next);agentHistoryUnsafe_=true;
                assetHistoryPersistenceWarning_.clear();
            }
            auto result=assetHistoryJson();
            if(step)result.set("snapshot",snapshot());
            reply.set("result",std::move(result));
        }catch(const std::exception& e){
            reply.set("error",str(e.what()));
        }
        return reply;
    }
    if(method=="studio.projectHistory"||method=="studio.projectHistoryStep"){
        Json reply=Json::object();reply.set("id",request["id"]);
        try {
            if(!request.isObject()||!request["id"].isNumber())throw std::runtime_error("Invalid request envelope");
            const bool step=method=="studio.projectHistoryStep";
            auto result=ws_.call(step?"project_history_step":"project_history_status",request["params"],"studio");
            if(result.has("error"))throw std::runtime_error(result.getStr("error"));
            if(step){agentHistoryUnsafe_=true;result.set("snapshot",snapshot());}
            result.set("strokeActive",Json::fromBool(stroking_));
            reply.set("result",std::move(result));
        }catch(const std::exception& e){reply.set("error",str(e.what()));}
        return reply;
    }
    if(method=="studio.history"||method=="studio.historyJump") {
        Json reply=Json::object();reply.set("id",request["id"]);
        try {
            syncHistoryLabels();
            if(method=="studio.historyJump") {
                // Undo / redo until `position` steps are applied (one step per call of the MapDoc).
                if(request["params"].has("expectedVersion")&&request["params"].getStr("expectedVersion")!=historyVersion())
                    throw std::runtime_error("History changed; refresh before jumping");
                const int64_t target=request["params"]["position"].integer(-1);
                if(target<0||target>int64_t(undoLabels_.size()+redoLabels_.size()))throw std::runtime_error("Invalid history position");
                while(int64_t(ws_.map.undoDepth())>target && ws_.map.canUndo())ws_.map.undo();
                while(int64_t(ws_.map.undoDepth())<target && ws_.map.canRedo())ws_.map.redo();
                syncHistoryLabels();
                clearSelection();dropOrdinalFlags();
                Json out=historyJson();out.set("snapshot",snapshot());
                reply.set("result",std::move(out));
            } else reply.set("result",historyJson());
        } catch(const std::exception& e){reply.set("error",str(e.what()));}
        return reply;
    }
    syncHistoryLabels();
    const auto before=ws_.map.historyMark();
    const auto beforeEpoch=ws_.map.historyEpoch();
    Json reply=dispatchImpl(request);
    if(!reply.has("error")&&beforeEpoch==ws_.map.historyEpoch()&&method!="studio.undo"&&method!="studio.redo"&&
       method!="studio.openMap"&&method!="studio.newMap"&&method!="studio.openProject")
        labelNewHistory(before,historyLabel(method,request["params"]));
    else syncHistoryLabels();
    return reply;
}
Json StudioSession::dispatchImpl(const Json& request) {
    return dispatchImplCore(request);
}
Json StudioSession::dispatchImplCore(const Json& request) {
    Json reply = Json::object();
    reply.set("id", request["id"]);
    try {
        if (!request.isObject() || !request["id"].isNumber()) throw std::runtime_error("Invalid request envelope");
        const std::string method = request.getStr("method");
        Json params = request["params"];
        // Cover Studio content mutations including multi-step asset transfers.
        std::unique_ptr<editor::ProjectAssetWriteLock> assetLock;
        if(!ws_.project.dir.empty() && (method=="studio.assetFolderCreate"||method=="studio.assetTransferCommit"||method=="studio.assetMetaPatch"||method=="studio.buildingPublishMaterial"||method=="studio.materialCreate"||method=="studio.materialPatch"))
            assetLock=std::make_unique<editor::ProjectAssetWriteLock>(ws_.project.dir);
        if((method=="studio.lightUpdate" || method=="studio.lightDelete") && params.has("entityId")) {
            if(!params["entityId"].isString())throw std::runtime_error("Invalid light entity ID");
            bool found=false;
            for(const auto& node:editor::enumerateDocumentSceneNodes(ws_.map))
                if(node.ref.isLight() && nodeId(ws_.map,node.ref)==params.getStr("entityId")) {
                    params.set("index",num(int64_t(node.ref.index)));found=true;break;
                }
            if(!found)throw std::runtime_error("Light entity no longer exists");
        }
        // Procedural building assets: native-only computation and bounded project writes.
        // Browser requests supply data, never executable commands or filesystem paths.
        if(method=="studio.buildingSurfacePreview") {
            reply.set("result",studioBuildingSurfacePreview(ws_.engineDir,params));
            return reply;
        }
        if(method=="studio.buildingCatalog") {
            reply.set("result",studioBuildingCatalog(ws_.engineDir));
            return reply;
        }
        if(method=="studio.buildingTextureProvenance") {
            reply.set("result",studioBuildingTextureProvenance(ws_.project.dir,params));
            return reply;
        }
        if(method=="studio.buildingInspect") {
            reply.set("result",studioBuildingInspect(ws_.engineDir,params));
            return reply;
        }
        if(method=="studio.buildingStaleMaterials") {
            reply.set("result",studioBuildingStaleMaterials(ws_.project.dir));
            return reply;
        }
        if(method=="studio.buildingRecipes") {
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project");
            reply.set("result",studioBuildingRecipes(ws_.project.dir));
            return reply;
        }
        if(method=="studio.buildingReadRecipe") {
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project");
            reply.set("result",studioBuildingReadRecipe(
                ws_.engineDir,ws_.project.dir,params.getStr("recipe")));
            return reply;
        }
        if(method=="studio.buildingRegenerate") {
            if(ws_.project.dir.empty())
                throw std::runtime_error("Open a project before regeneration");
            reply.set("result",studioBuildingRegenerate(
                ws_.engineDir,ws_.project.dir,params.getStr("recipe")));
            return reply;
        }
        if(method=="studio.buildingPublishMaterial"||method=="studio.materialPublicationRetry") {
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project");
            editor::ProjectAssetWriteLock publicationLock(ws_.project.dir);
            const auto name=params.getStr("name");
            if(!validStudioMaterialName(name)||editor::loadMaterialLibrary(ws_).has(name))
                throw std::runtime_error("Invalid or existing material name");
            Json recipe=params;
            if(method=="studio.materialPublicationRetry"){
                if(params.getStr("expectedProject")!=ws_.project.dir||params.getStr("expectedDocumentEpoch")!=std::to_string(graphDocumentEpoch_))throw std::runtime_error("Material recovery document changed");
                recipe=StudioMaterialPublicationJournal::retryRecipe(ws_.project.dir,name);
                (void)StudioMaterialPublicationJournal::quarantineIsolated(ws_.project.dir,name);
            }
            auto result=studioBuildingPublishMaterial(ws_.engineDir,ws_.project.dir,recipe,
                [&](const std::string& published,const Json& material){
                    if(editor::loadMaterialLibrary(ws_).has(published))
                        throw std::runtime_error("Material already exists");
                    Json request=Json::object();request.set("name",str(published));
                    request.set("material",material);request.set("createOnly",Json::fromBool(true));
                    const auto saved=ws_.call("material_set",request,"studio");
                    if(saved.has("error"))throw std::runtime_error(saved.getStr("error"));
                });
            reply.set("result",std::move(result));return reply;
        }
        if(method=="studio.buildingGenerate") {
            reply.set("result",studioBuildingGenerate(ws_.engineDir,ws_.project.dir,params));
            return reply;
        }
        if (method == "studio.hubTemplates") {   // StudioHost runs this off the UI thread; kept for other hosts
            reply.set("result",hubTemplatesJson(ws_.engineDir,params));
            return reply;
        }
        if (method == "studio.wizardOptions") {
            Json options=editor::wizardCatalog(ws_.engineDir), clients=Json::array();
#if !defined(_WIN32)
            if(!mcpTool_.empty() && std::filesystem::is_regular_file(mcpTool_))
                for(const auto* client:{"claude-code","codex"})
                    if(editor::agentClientCanRun(client))clients.push(str(client));
#endif
            options.set("agentClients",std::move(clients));
            reply.set("result",std::move(options));
            return reply;
        }
        if (method == "studio.wizardPlan") {
            auto spec = studioWizardSpec(ws_.engineDir, params, wizardArtDir_);
            reply.set("result", editor::planGame(ws_.engineDir, spec));
            return reply;
        }
        if (method == "studio.hubForget") {
            const std::string dir = params.getStr("dir");
            if (dir.empty()) throw std::runtime_error("Choose a recent project");
            auto listed = [&dir]() {
                const auto projects = editor::recentProjects();
                return std::any_of(projects.begin(), projects.end(), [&dir](const auto& project) {
                    return project.dir == dir;
                });
            };
            if (listed()) {
                editor::forgetProject(dir);
                if (listed()) throw std::runtime_error("Could not update recent projects");
            }
            reply.set("result", Json::object());
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
                    rigPreview_.reset();
                    reply.set("result",rigPreview_.describe());
                } else {
                    StudioAssetCatalog catalog(ws_.project.dir);
                    catalog.rebuild();
                    Json result=rigPreview_.bind(catalog,guid,params["rig"]);
                    reply.set("result",std::move(result));
                }
            } else if(method=="studio.facialPreviewControls") {
                const Json& revision=params["revision"];
                if(!revision.isNumber() || revision.toDouble()<0 ||
                   revision.toDouble()!=double(revision.integer()))
                    throw std::runtime_error("Invalid facial selection revision");
                reply.set("result",rigPreview_.controls(
                    uint64_t(revision.integer()),params["values"],
                    params["vectors"]));
            } else {
                reply.set("result",rigPreview_.describe());
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
                    if(params.has("expectedDocumentEpoch") && params.getStr("expectedDocumentEpoch")!=std::to_string(graphDocumentEpoch_))
                        throw std::runtime_error("Shader graph document reopened; reload before saving");
                    if(params.getStr("expectedProject")!=ws_.project.dir)
                        throw std::runtime_error("Shader Graph project changed; reload before saving");
                    const std::string name=params.getStr("name");
                    if(!validStudioShaderGraphName(name))
                        throw std::runtime_error("Invalid Shader Graph filename");
                    const Json original=graphOriginal(params["expected"]);
                    if(!validStudioShaderGraphDraft(original))
                        throw std::runtime_error("Original Shader Graph document required; reload before saving");
                    request.set("expected",original);
                    request.set("name",str(name));
                }
                const Json raw=ws_.call(method=="studio.shaderGraphSave"?
                    "shader_graph_set":"shader_graph_validate",request,"studio");
                if(raw.has("error")) throw std::runtime_error(raw.getStr("error"));
                Json result=Json::object();
                result.set("graph",raw["graph"]);
                result.set("originalJson",str(raw["graph"].dump()));
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
                result.set("originalJson",str(raw["graph"].dump()));
                result.set("nodes",raw["nodes"]);
                result.set("samples",raw["samples"]);
                // Do NOT serialize raw.glsl/msl/vertex* to JS.
                reply.set("result",std::move(result));
            }
        } else if (method=="studio.shaderGraphPreview") {
            const auto name=params.getStr("name");
            const auto& graph=params["graph"];
            if(ws_.project.dir.empty()||params.getStr("projectPath")!=ws_.project.dir)
                throw std::runtime_error("The preview project changed");
            if(!validStudioShaderGraphName(name)||!validStudioShaderGraphDraft(graph)||(graph.getStr("domain")!="material"&&graph.getStr("domain")!="postfx"&&graph.getStr("domain")!="particle"))
                throw std::runtime_error("Expected a material, postfx or particle Shader Graph draft");
            Json request=Json::object();request.set("graph",graph);
            const auto validated=ws_.call("shader_graph_validate",request,"studio");
            if(validated.has("error"))throw std::runtime_error(validated.getStr("error"));
            previewGraphTitle_="Shader Graph / "+name;
            previewGraphFactors_=Json::object();previewGraphFactors_.set("studioGraphDraft",graph);
            ++previewGraphRevision_;
            Json out=Json::object();out.set("name",str(previewGraphTitle_));reply.set("result",std::move(out));
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
        } else if (method == "studio.materialPublicationQuarantine"||method=="studio.materialPublicationRestore") {
            if(ws_.project.dir.empty()||params.getStr("expectedProject")!=ws_.project.dir||params.getStr("expectedDocumentEpoch")!=std::to_string(graphDocumentEpoch_))throw std::runtime_error("Material recovery document changed");
            editor::ProjectAssetWriteLock lock(ws_.project.dir);
            if(method=="studio.materialPublicationQuarantine") (void)StudioMaterialPublicationJournal::quarantineIsolated(ws_.project.dir,params.getStr("id"));
            else StudioMaterialPublicationJournal::restoreQuarantine(ws_.project.dir,params.getStr("id"));
            Json result=Json::object();result.set("ok",Json::fromBool(true));reply.set("result",std::move(result));
        } else if (method == "studio.materialPublicationRollbackPreview"||method=="studio.materialPublicationRollbackEmpty") {
            if(ws_.project.dir.empty()||params.getStr("expectedProject")!=ws_.project.dir||params.getStr("expectedDocumentEpoch")!=std::to_string(graphDocumentEpoch_))throw std::runtime_error("Material recovery document changed");
            const auto name=params.getStr("name");
            if(method=="studio.materialPublicationRollbackPreview")
                reply.set("result",StudioMaterialPublicationJournal::rollbackPreview(ws_.project.dir,name,ws_.project.rulesJson,ws_.map.toJson()));
            else {
                StudioMaterialPublicationJournal::rollbackEmpty(ws_.project.dir,name,ws_.project.rulesJson,ws_.map.toJson());
                Json result=Json::object();result.set("rolledBack",Json::fromBool(true));result.set("pendingPublications",StudioMaterialPublicationJournal::inspect(ws_.project.dir));reply.set("result",std::move(result));
            }
        } else if (method == "studio.materialPublicationFinalize") {
            if(ws_.project.dir.empty()||params.getStr("expectedProject")!=ws_.project.dir||params.getStr("expectedDocumentEpoch")!=std::to_string(graphDocumentEpoch_))throw std::runtime_error("Material recovery document changed");
            StudioMaterialPublicationJournal::finalize(ws_.project.dir,params.getStr("name"));
            Json result=Json::object();result.set("finalized",Json::fromBool(true));result.set("pendingPublications",StudioMaterialPublicationJournal::inspect(ws_.project.dir));reply.set("result",std::move(result));
        } else if (method == "studio.materialPublicationRepair") {
            if(ws_.project.dir.empty()||params.getStr("expectedProject")!=ws_.project.dir||params.getStr("expectedDocumentEpoch")!=std::to_string(graphDocumentEpoch_))
                throw std::runtime_error("Material recovery document changed; refresh Studio");
            editor::ProjectAssetWriteLock recoveryLock(ws_.project.dir);
            StudioMaterialPublicationJournal::restoreMissingMetadata(ws_.project.dir,params.getStr("name"),params.getStr("filename"));
            Json result=Json::object();result.set("restored",Json::fromBool(true));
            result.set("pendingPublications",StudioMaterialPublicationJournal::inspect(ws_.project.dir));
            reply.set("result",std::move(result));
        } else if (method == "studio.materials" || method == "studio.materialPatch" ||
                   method == "studio.materialCreate" || method=="studio.materialsToFiles") {
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            auto texturePath = [&](const Json& value) {
                if (!value.isString()) throw std::runtime_error("Texture reference must be an asset GUID");
                if (value.str().empty()) return str("");
                if (!studioValidGuid(value.str())) throw std::runtime_error("Texture reference requires a valid asset GUID");
                StudioAssetCatalog catalog(ws_.project.dir);
                catalog.rebuild();
                const auto asset=catalog.byGuid(value.str());
                if (!asset || asset->directory || asset->importer!="texture")
                    throw std::runtime_error("Texture GUID not found or is not a texture");
                return str("content/"+asset->path);
            };
            if(method=="studio.materialsToFiles"){
                editor::ProjectAssetWriteLock lock(ws_.project.dir);
                const auto materials=editor::loadMaterialLibrary(ws_);
                editor::saveMaterialLibrary(ws_,materials,"Convert material library to files");
            } else if (method == "studio.materialCreate") {
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
                if (preset=="triplanar") {
                    Json definition=Json::object();
                    definition.set("colorMap",texturePath(params["colorGuid"]));
                    if (params.has("normalGuid")) definition.set("normalMap",texturePath(params["normalGuid"]));
                    if (params.has("repeatMetres")) definition.set("repeatMetres",params["repeatMetres"]);
                    const Json recipe=ws_.call("material_triplanar",definition,"studio");
                    if (recipe.has("error")) throw std::runtime_error(recipe.getStr("error"));
                    material=recipe["material"];
                }
                Json request=Json::object();
                request.set("name",str(name));
                request.set("material",std::move(material));request.set("createOnly",Json::fromBool(true));
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
                    if (key=="uvScale"||key=="uvOffset"){
                        if(!value.isString())
                            throw std::runtime_error("UV transform requires 'u,v' values");
                        const auto pair=parseStudioUvPair(value.str());
                        if(!pair)
                            throw std::runtime_error("UV transform requires two bounded finite numbers");
                        Json uv=Json::array();
                        for(double component:*pair)
                            uv.push(Json::number(component,6));
                        validatedPatch.set(key,std::move(uv));
                        continue;
                    }
                    if (studioMaterialTextureKey(key)) {
                        validatedPatch.set(key,texturePath(value));
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
                request.set("merge",Json::fromBool(true));request.set("requireExisting",Json::fromBool(true));
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
            result.set("files",editor::materialFilePaths(ws_));
            try { result.set("pendingPublications",StudioMaterialPublicationJournal::inspect(ws_.project.dir));result.set("quarantinedPublications",StudioMaterialPublicationJournal::inspectQuarantine(ws_.project.dir)); }
            catch(const std::exception& e){result.set("publicationInspectionError",str(e.what()));}
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
            if (params.has("expectedDocumentEpoch") &&
                params.getStr("expectedDocumentEpoch")!=std::to_string(graphDocumentEpoch_))
                throw std::runtime_error("Map changed; reopen Lighting before editing");
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
                const Json updated = ws_.call("lighting_set", studioMapLightingPatch(params), "studio");
                if (updated.has("error")) throw std::runtime_error(updated.getStr("error"));
            } else if (method == "studio.lightAdd") {
                const Json input=studioLightPosition(params,ws_.map.data.w*.5,ws_.map.data.h*.5);
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
                    if (!validStudioLightProperty(key,value))
                        throw std::runtime_error("Invalid light property");
                    input.set(key,value);
                }
                const Json updated = ws_.call(method == "studio.lightDelete" ? "light_delete" : "light_update",input,"studio");
                if (updated.has("error")) throw std::runtime_error(updated.getStr("error"));
            }
            const Json current = ws_.call("light_list",Json::object(),"studio");
            if (current.has("error")) throw std::runtime_error(current.getStr("error"));
            reply.set("result",current);
        } else if (method == "studio.cavernsGraphInspect") {
            // Narrow get-only capability; never expose the mixed paint operation via this route.
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            const Json scope=graphDocumentScope();
            const Json& supplied=params["scope"];
            if (!params.isObject() || params.size()!=1 || !sameDocumentScope(supplied,scope))
                throw std::runtime_error("Caverns inspection document changed; refresh Studio");
            Json out=Json::object();
            out.set("scope",scope);
            const bool registered=std::any_of(ws_.ops().begin(),ws_.ops().end(),[](const auto& op){
                return op.name=="terraweave_caverns_paint" && !op.guiOnly;
            });
            if (!registered) out.set("status",str("unavailable"));
            else if (!ws_.map.data.planet() && static_cast<const Json&>(ws_.map.data.extra)["terraweaveCaverns"].isNull())
                out.set("status",str("absent"));
            else {
                const size_t undo=ws_.map.undoDepth();
                const bool dirty=ws_.map.dirty;
                Json input=Json::object();input.set("mode",str("get"));
                const Json report=ws_.call("terraweave_caverns_paint",input,"editor");
                if (report.has("error")) throw std::runtime_error(report.getStr("error"));
                if (!(scope==graphDocumentScope()) || undo!=ws_.map.undoDepth() || dirty!=ws_.map.dirty)
                    throw std::runtime_error("Caverns read violated document scope");
                if (ws_.map.data.planet() && !report.getBool("planetary"))
                    out.set("status",str("unsupported"));
                else if (ws_.map.data.planet() && report.getStr("activeRegionId").empty())
                    out.set("status",str("absent"));
                else {
                    if(!report.getBool("ok") || report.getBool("changed") || !report["snapshot"].isObject() ||
                        (ws_.map.data.planet() && (!report["planetAnchor"].isObject() || !report["planetRegions"].isArray())))
                        throw std::runtime_error("Invalid native Caverns inspection report");
                    out.set("status",str("ready"));out.set("report",report);
                }
            }
            reply.set("result",std::move(out));
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
            if(params.has("expectedDocumentRevision") && params.getStr("expectedDocumentRevision")!=std::to_string(ws_.map.revision))
                throw std::runtime_error("Map changed. Review the current values before applying.");
            if (!studioToolAllowed(op)) throw std::runtime_error("This operation is not available in Studio panels: "+op);
            if(params.has("expectedDocumentEpoch") && params.getStr("expectedDocumentEpoch")!=std::to_string(graphDocumentEpoch_))
                throw std::runtime_error("Graph document reopened; refresh the panel");
            if(params.has("expectedProject") && params.getStr("expectedProject")!=ws_.project.dir)
                throw std::runtime_error("Studio operation project changed; reload before retrying");
            if (op.rfind("feature_readiness_",0)!=0 && ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            if((op=="graph_debug_session"||op=="graph_runtime_probe"||op=="graph_live_snapshot"||op=="graph_live_watch"||op=="graph_live_profile"||op=="graph_gpu_profile") && params.getStr("expectedProject")!=ws_.project.dir)
                throw std::runtime_error("Graph runtime project changed; reopen the panel");
            if((op.starts_with("world_save_")||op=="world_job_start"||op.starts_with("world_simulation_lod_"))&&params.getStr("expectedProject")!=ws_.project.dir)
                throw std::runtime_error("World maintenance project changed; reopen the panel");
            if((op=="terraweave_caverns_paint"||op=="terraweave_caverns_pick") && (params.getStr("expectedProject")!=ws_.project.dir ||
               !params["expectedMap"].isString() || params.getStr("expectedMap")!=(ws_.map.path.empty()?"":ws_.relative(ws_.map.path))))
                throw std::runtime_error("Caverns project/map changed; refresh the panel");
            if(op=="set_manifest" && (params.getStr("expectedProject")!=ws_.project.dir ||
               !params["params"]["expectedManifest"].isString()))
                throw std::runtime_error("Project settings save requires the loaded project and original manifest");
            if(op=="game_file_write" && (params.getStr("expectedProject")!=ws_.project.dir ||
               !params["params"]["expectedText"].isString()))
                throw std::runtime_error("Project data save requires the loaded project and original file text");
            Json input=params["params"].isObject()?params["params"]:Json::object();
            if(op=="shader_graph_apply" && input.getStr("scope")=="map" && params.has("expectedMap") &&
               (!params["expectedMap"].isString() || params.getStr("expectedMap")!=ws_.map.path))
                throw std::runtime_error("Map changed before shader application; check the current map and retry.");
            const bool graphWrite=op=="fx_graph_set" || op=="fx_graph_draft_set" || op=="texture_graph_set" || op=="animation_graph_set";
            if(graphWrite && input["expected"].isString())
                input.set("expected",graphOriginal(input["expected"]));
            if(op=="terraweave_caverns_paint" && input.getStr("mode")!="get" &&
               (stroking_ || input.getStr("expectedMapRevision")!=std::to_string(ws_.map.revision) ||
                !input["expectedRevision"].isString() || input.getBool("withinStroke",false)))
                throw std::runtime_error("Caverns Studio edit requires revision and its own undo step");
            const bool wasDirty=ws_.map.dirty||ws_.project.rulesDirty||ws_.project.manifestDirty;
            const size_t depth=ws_.map.undoDepth(),entityCount=ws_.map.data.entities.size();
            Json result=ws_.call(op,input,"studio");
            const bool graphRead=op=="fx_graph_get" || op=="fx_graph_draft_get" || op=="texture_graph_get" || op=="animation_graph_get";
            if((graphRead || graphWrite) && result["graph"].isObject())
                result.set("originalJson",str(result["graph"].dump()));
            if((op=="fx_graph_set" || op=="fx_graph_draft_set") && result.getBool("ok") && input["graph"].isObject() && !input.has("text"))
                result.set("originalJson",str(input["graph"].dump()));
            if(op=="terraweave_caverns_paint"&&!result.has("error"))result.set("mapRevisionText",str(std::to_string(ws_.map.revision)));
            if (result.has("error")) throw std::runtime_error(result.getStr("error"));
            const bool dirty=ws_.map.dirty||ws_.project.rulesDirty||ws_.project.manifestDirty;
            const bool readOnly=studioToolReadOnly(op)||(op=="terraweave_caverns_paint"&&(input.getStr("mode")=="get"||!result.getBool("changed",true)));
            const bool changed=!readOnly||ws_.map.undoDepth()!=depth||dirty!=wasDirty;
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
            Json presets=Json::object();
            presets.set("fps",ecfg::writeAsset(platform::enhanced_presets::fpsReference()));
            presets.set("rts",ecfg::writeAsset(platform::enhanced_presets::rtsReference()));
            out.set("presets",std::move(presets));
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
        } else if (method=="studio.assetDependencyPage"){
            if(ws_.project.dir.empty()||params.getStr("expectedProject")!=ws_.project.dir)
                throw std::runtime_error("Dependency project changed");
            const auto read=[&](const char* key,int64_t def,int64_t min,int64_t max){
                if(!params.has(key))return def;
                const auto v=params.getInt(key,-1);
                if(!params[key].isNumber()||params[key].numberLiteral()!=std::to_string(v)||v<min||v>max)
                    throw std::runtime_error("Invalid dependency page parameter");
                return v;
            };
            StudioAssetCatalog catalog(ws_.project.dir);catalog.rebuild(false);
            const auto entry=catalog.byPath(params.getStr("path"));
            if(!params["path"].isString()||!entry)throw std::runtime_error("Dependency asset is not indexed");
            if((params.has("direction")&&!params["direction"].isString())||
               (params.has("expectedToken")&&!params["expectedToken"].isString()))
                throw std::runtime_error("Invalid dependency page selection");
            const auto depth=read("depth",16,0,16),offset=read("offset",0,0,65536),limit=read("limit",50,1,100);
            StudioDependencyCoverage coverage;
            const auto refs=studioCollectProjectAssetReferences(ws_.project.dir,editor::loadMaterialLibrary(ws_),
                ws_.project.rulesJson,ws_.map.toJson(),ws_.project.manifest,catalog.all(),&coverage);
            auto result=studioDependencyPage(refs,entry->path,entry->directory,size_t(depth),params.getStr("direction","all"),
                size_t(offset),size_t(limit),coverage.json(),params.getStr("expectedToken"));
            result.set("path",str(entry->path));result.set("guid",str(entry->guid));
            reply.set("result",std::move(result));
        } else if (method == "studio.assetReferences") {
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            StudioAssetCatalog catalog(ws_.project.dir);
            catalog.rebuild(false);
            const auto entries=catalog.all();
            auto refs=studioCollectProjectAssetReferences(
                ws_.project.dir,editor::loadMaterialLibrary(ws_),ws_.project.rulesJson,
                ws_.map.toJson(),ws_.project.manifest,entries);
            if(params.has("path")){
                if(!params["path"].isString())throw std::runtime_error("Invalid dependency asset path");
                const auto selected=catalog.byPath(params.getStr("path"));
                if(!selected)throw std::runtime_error("Dependency asset is not indexed");
                if(params.has("depth")){
                    const auto depth=params.getInt("depth",0);
                    if(!params["depth"].isNumber()||depth<1||depth>16)
                        throw std::runtime_error("Dependency depth must be 1–16");
                    reply.set("result",studioDependencyTraversalJson(refs,selected->path,selected->directory,size_t(depth)));
                    return reply;
                }
                refs=studioAssetDependencyEdges(refs,selected->path,selected->directory);
            }else if(params.has("depth")){
                throw std::runtime_error("Dependency traversal requires an asset path");
            }
            reply.set("result",studioReferenceGraphJson(refs));
        } else if (method == "studio.projectDirectory") {
            if(ws_.project.dir.empty() || params.getStr("expectedProject")!=ws_.project.dir)
                throw std::runtime_error("The open project changed");
            if(!params["folder"].isString())throw std::runtime_error("Expected a project-relative folder");
            reply.set("result",studioProjectDirectory(ws_.project.dir,params.getStr("folder")));
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
                if(!asset.directory&&asset.importer=="model") {
                    const bool native=render::isNativeModelFormat(asset.path);
                    const auto converted=catalog.byPath(asset.path+".glb");
                    item.set("modelLoadableFormat",Json::fromBool(native||(converted&&!converted->directory)));
                }
                item.set("bytes", Json::number(asset.bytes));
                if(asset.directory)folders.push(std::move(item));
                else entries.push(std::move(item));
            }
            r.set("files",std::move(entries));
            r.set("folders",std::move(folders));
            reply.set("result", std::move(r));
        } else if (method == "studio.spectraAssetCheck") {
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            const std::string guid=params.getStr("guid");
            if(!studioValidGuid(guid))throw std::runtime_error("Invalid asset GUID");
            StudioAssetCatalog catalog(ws_.project.dir);catalog.rebuild();
            const auto entry=catalog.byGuid(guid);
            if(!entry||entry->directory){
                Json result=Json::object();result.set("guid",str(guid));result.set("ok",Json::fromBool(false));
                result.set("code",str("missing"));result.set("detail",str("Asset missing from project index"));
                result.set("gpuVerified",Json::fromBool(false));reply.set("result",std::move(result));return reply;
            }
            if(entry->importer!="model"&&entry->importer!="texture")
                throw std::runtime_error("Expected model or texture asset");
            const auto root=std::filesystem::path(ws_.project.dir)/"content";
            uint64_t remaining=64ull*1024*1024;
            render::FileReader reader=[&](const std::string& name,std::string& bytes){
                const auto rel=std::filesystem::path(name).lexically_normal();
                if(rel.empty()||rel.is_absolute())return false;
                for(const auto& part:rel)if(part=="..")return false;
                const auto indexed=catalog.byPath(rel.generic_string());
                if(!indexed||indexed->directory||indexed->bytes<0||uint64_t(indexed->bytes)>remaining)return false;
                std::error_code ec;
                const auto file=root/rel;
                auto walk=root;
                for(const auto& part:rel){
                    walk/=part;
                    if(std::filesystem::is_symlink(std::filesystem::symlink_status(walk,ec))||ec)return false;
                }
                const auto size=std::filesystem::file_size(file,ec);
                if(ec||size>remaining)return false;
                std::ifstream stream(file,std::ios::binary);
                if(!stream)return false;
                bytes.resize(size_t(size));
                if(size&&!stream.read(bytes.data(),std::streamsize(size)))return false;
                remaining-=size;
                return true;
            };
            Json result=Json::object();
            result.set("guid",str(guid));
            bool ok=false;
            std::string detail,code="decode_failed";
            try {
                if(entry->importer=="model"&&!render::isNativeModelFormat(entry->path)&&!catalog.byPath(entry->path+".glb")){
                    code="unsupported_format";detail="No native model loader or converted GLB";
                }else if(entry->importer=="model"){
                    auto model=render::loadModel(entry->path,reader);
                    ok=model&&!model->vertices.empty()&&!model->indices.empty();
                }else{
                    std::string bytes;render::Image image;
                    ok=reader(entry->path,bytes)&&render::decodeImage(bytes,image);
                }
                if(detail.empty())detail=ok?"CPU decode succeeded":"CPU decode failed or geometry empty";
                if(ok)code="ok";
            }catch(const std::exception& e){detail=e.what();}
            result.set("ok",Json::fromBool(ok));result.set("detail",str(detail));result.set("code",str(code));
            result.set("gpuVerified",Json::fromBool(false));
            reply.set("result",std::move(result));
        } else if (method == "studio.assetMetaPatch") {
            if(params.has("expectedDocumentEpoch") && params.getStr("expectedDocumentEpoch")!=std::to_string(graphDocumentEpoch_))
                throw std::runtime_error("Asset document reopened; refresh the panel");
            if(params.has("expectedProject") && params.getStr("expectedProject")!=ws_.project.dir)
                throw std::runtime_error("Asset metadata project changed; refresh the panel");
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            const std::string guid=params.getStr("guid");
            const std::string key=params.getStr("key");
            if(!studioValidGuid(guid)||key.size()>40||!params.has("value"))
                throw std::runtime_error("Invalid asset import setting request");
            StudioAssetCatalog catalog(ws_.project.dir);
            catalog.rebuild(false);
            auto next=assetHistory_;
            std::vector<editor::ProjectFileEdit> edits;
            const auto updated=catalog.patchSettings(guid,key,params["value"],&next,&edits);
            studioApplyHistoryBatch(ws_.project.dir,{},edits,{},&assetHistory_,&next);
            assetHistory_=std::move(next);assetHistoryPersistenceWarning_.clear();
            try{catalog.rebuild(false);}catch(const std::exception& e){assetHistoryPersistenceWarning_="Asset index refresh required: "+std::string(e.what());}
            agentHistoryUnsafe_=true;
            Json detail=Json::object();
            detail.set("guid",str(updated.guid));
            detail.set("path",str(updated.path));
            detail.set("importer",str(updated.importer));
            detail.set("directory",Json::fromBool(updated.directory));
            detail.set("bytes",Json::number(updated.bytes));
            detail.set("settings",updated.settings);
            reply.set("result",std::move(detail));
        } else if(method=="studio.assetRecoveryQuota"){
            if(ws_.project.dir.empty()||params.getStr("expectedProject")!=ws_.project.dir)
                throw std::runtime_error("Recovery project changed; reopen Trash");
            const auto expected=params.getInt("expectedMiB",-1),quota=params.getInt("quotaMiB",-1);
            if(!params["expectedMiB"].isNumber()||!params["quotaMiB"].isNumber()||
               params["expectedMiB"].numberLiteral()!=std::to_string(expected)||
               params["quotaMiB"].numberLiteral()!=std::to_string(quota))
                throw std::runtime_error("Recovery quota requires integer MiB");
            reply.set("result",studioSetRecoveryQuota(ws_.project.dir,expected,quota));
        } else if(method=="studio.assetHistoryResetReview"||method=="studio.assetHistoryResetApply"){
            if(ws_.project.dir.empty()||params.getStr("expectedProject")!=ws_.project.dir)
                throw std::runtime_error("Recovery project changed; reopen Trash");
            if(method=="studio.assetHistoryResetReview"){
                auto result=studioReviewAssetHistoryReset(ws_.project.dir);
                result.set("version",str(assetHistoryVersion()));reply.set("result",std::move(result));
            }else{
                if(!params.getBool("confirmed")||params.getStr("expectedVersion")!=assetHistoryVersion())
                    throw std::runtime_error("Confirm history reset against the current owner");
                studioApplyAssetHistoryReset(ws_.project.dir,params.getStr("token"));
                assetHistory_=editor::ProjectHistory{};assetHistoryPersistenceWarning_.clear();
                pendingAssetTransfer_.reset();agentAssetTransfer_.reset();agentAssetTransferGuids_.clear();
                agentHistoryUnsafe_=true;agentBatchFenced_=true;
                Json result=Json::object();result.set("ok",Json::fromBool(true));reply.set("result",std::move(result));
            }
        } else if(method=="studio.assetTrashBatchReview"||method=="studio.assetTrashBatchApply"){
            if(ws_.project.dir.empty()||params.getStr("expectedProject")!=ws_.project.dir)
                throw std::runtime_error("Recovery project changed; reopen Trash");
            const auto& raw=params["ids"];
            if(!raw.isArray()||raw.size()<1||raw.size()>50)throw std::runtime_error("Select 1–50 recovery records");
            std::vector<std::string> ids;
            for(const auto& id:raw.items()){
                if(!id.isString())throw std::runtime_error("Invalid recovery record ID");
                ids.push_back(id.str());
            }
            if(method=="studio.assetTrashBatchReview"){
                auto review=studioReviewTrashBatch(ws_.project.dir,ids,assetHistory_);
                review.set("version",str(assetHistoryVersion()));reply.set("result",std::move(review));
            }else{
                if(!params.getBool("confirmed")||params.getStr("expectedVersion")!=assetHistoryVersion())
                    throw std::runtime_error("Confirm cleanup against current asset history");
                reply.set("result",studioPurgeUnprotectedTrashBatch(ws_.project.dir,ids,params.getStr("token"),assetHistory_));
            }
        } else if (method == "studio.assetTrashList" || method == "studio.assetTrashReview" || method == "studio.assetTrashApply") {
            if(ws_.project.dir.empty()||params.getStr("expectedProject")!=ws_.project.dir)
                throw std::runtime_error("Recovery project changed; reopen Trash");
            if(method=="studio.assetTrashList"){
                const auto offset=params.getInt("offset",0),limit=params.getInt("limit",50);
                if(offset<0||limit<1)throw std::runtime_error("Invalid recovery page");
                auto result=studioAssetTrashList(ws_.project.dir,size_t(offset),size_t(limit));
                const auto protectedIds=studioProtectedTrashIds(assetHistory_);
                Json rows=Json::array();
                for(auto row:result["items"].items()){
                    row.set("historyProtected",Json::fromBool(std::binary_search(protectedIds.begin(),protectedIds.end(),row.getStr("id"))));
                    rows.push(std::move(row));
                }
                result.set("items",std::move(rows));
                if(!assetHistoryPersistenceWarning_.empty())result.set("historyWarning",str(assetHistoryPersistenceWarning_));
                reply.set("result",std::move(result));
            }else if(method=="studio.assetTrashReview"){
                if(params.getStr("action")=="purge"){
                    const auto protectedIds=studioProtectedTrashIds(assetHistory_);
                    if(std::binary_search(protectedIds.begin(),protectedIds.end(),params.getStr("id")))
                        throw std::runtime_error("Recovery record is protected by retained Undo/Redo");
                }
                auto review=studioAssetTrashReview(ws_.project.dir,params.getStr("id"),params.getStr("action"));
                review.set("version",str(assetHistoryVersion()));reply.set("result",std::move(review));
            }else{
                if(!params.getBool("confirmed")||params.getStr("expectedVersion")!=assetHistoryVersion())
                    throw std::runtime_error("Confirm recovery against the current asset history");
                const auto id=params.getStr("id"),action=params.getStr("action"),token=params.getStr("token");
                if(action=="purge"){
                    reply.set("result",studioPurgeUnprotectedTrash(ws_.project.dir,id,token,assetHistory_));
                }else if(action=="restore"){
                    StudioBatchAssetPlan staged;
                    studioAssetTrashApply(ws_.project.dir,id,action,token,&staged);
                    auto next=assetHistory_;next.reset();
                    studioApplyHistoryBatch(ws_.project.dir,staged.relocations,staged.documents,
                        staged.movingResources,&assetHistory_,&next);
                    assetHistory_=std::move(next);assetHistoryPersistenceWarning_.clear();
                    pendingAssetTransfer_.reset();agentHistoryUnsafe_=true;agentBatchFenced_=true;
                    try{StudioAssetCatalog catalog(ws_.project.dir);catalog.rebuild(false);}
                    catch(const std::exception& e){assetHistoryPersistenceWarning_="Asset index refresh required: "+std::string(e.what());}
                    Json result=Json::object();result.set("ok",Json::fromBool(true));reply.set("result",std::move(result));
                }else throw std::runtime_error("Invalid recovery action");
            }
        } else if (method == "studio.assetDelete") {
            if(ws_.project.dir.empty()||params.getStr("expectedProject")!=ws_.project.dir||
               params.getStr("expectedVersion")!=assetHistoryVersion()||!params.getBool("confirmed"))
                throw std::runtime_error("Confirm deletion in the current project and refresh asset history");
            const auto path=params.getStr("path"),guid=params.getStr("guid");
            StudioAssetCatalog catalog(ws_.project.dir);catalog.rebuild(false);
            const auto current=catalog.byGuid(guid);
            if(!current||current->path!=path)throw std::runtime_error("Asset identity changed");
            const auto check=[&]{
                studioCheckMapAssetPaths(ws_.project.dir,ws_.map.toJson(),ws_.project.rulesJson,ws_.project.manifest,{{path,current->directory}});
                if(studioDocumentNamesAsset(editor::loadMaterialLibrary(ws_),path,current->directory))throw std::runtime_error("Asset is referenced by materials");
            };
            StudioBatchAssetPlan staged;
            auto plan=studioQuarantineAsset(ws_.project.dir,path,guid,check,&staged);
            const auto backup=plan.files.front().to;
            auto next=assetHistory_;next.recordMove(std::move(plan),"Delete asset: "+path);
            studioApplyHistoryBatch(ws_.project.dir,staged.relocations,staged.documents,staged.movingResources,&assetHistory_,&next);
            assetHistory_=std::move(next);assetHistoryPersistenceWarning_.clear();
            try{catalog.rebuild(false);}catch(const std::exception& e){assetHistoryPersistenceWarning_="Asset index refresh required: "+std::string(e.what());}
            agentHistoryUnsafe_=true;
            pendingAssetTransfer_.reset();
            Json r=Json::object();r.set("path",str(path));r.set("backup",str(backup));r.set("historyRecorded",Json::fromBool(assetHistory_.canUndo()));
            reply.set("result",std::move(r));
        } else if (method == "studio.assetFolderCreate") {
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            editor::ProjectHistory::MovePlan history;
            StudioBatchAssetPlan staged;
            const std::string path=studioCreateContentFolder(ws_.project.dir,
                params.getStr("parent"),params.getStr("name"),&history,&staged);
            auto next=assetHistory_;next.recordMove(std::move(history),"Create folder: "+path);
            studioApplyHistoryBatch(ws_.project.dir,staged.relocations,staged.documents,staged.movingResources,&assetHistory_,&next);
            assetHistory_=std::move(next);assetHistoryPersistenceWarning_.clear();
            try{StudioAssetCatalog catalog(ws_.project.dir);catalog.rebuild(false);}
            catch(const std::exception& e){assetHistoryPersistenceWarning_="Asset index refresh required: "+std::string(e.what());}
            agentHistoryUnsafe_=true;
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
            if(params.has("newName")){
                if(sources.size()!=1||!params["newName"].isString())
                    throw std::runtime_error("Rename requires one asset and a filename");
                pendingAssetTransfer_=StudioTransferPlan::rename(ws_.project.dir,sources.front(),params.getStr("newName"));
            }else pendingAssetTransfer_=StudioTransferPlan::internal(ws_.project.dir,
                params.getStr("destination"),sources);
            Json preview=pendingAssetTransfer_->describe();
            StudioAssetCatalog catalog(ws_.project.dir);
            catalog.rebuild();
            const auto dependencies=studioCollectProjectAssetReferences(
                ws_.project.dir,editor::loadMaterialLibrary(ws_),ws_.project.rulesJson,
                ws_.map.toJson(),ws_.project.manifest,catalog.all());
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
            if(currentPlan.getStr("source")=="internal"){
                std::vector<std::pair<std::string,bool>> paths;
                for(const auto& item:currentPlan["items"].items()){
                    const int id=int(item["id"].integer());
                    std::string action=item.getStr("conflict").empty()?"import":"skip";
                    for(const auto& d:decisions)if(d.id==id)action=d.action;
                    if(action!="skip")paths.emplace_back(item.getStr("from"),item.getBool("directory"));
                }
                studioCheckMapAssetPaths(ws_.project.dir,ws_.map.toJson(),ws_.project.rulesJson,ws_.project.manifest,paths,false);
            }
            const bool internal=currentPlan.getStr("source")=="internal";
            StudioBatchAssetPlan staged;
            std::optional<editor::ProjectHistory::MovePlan> moveHistory;
            auto result=pendingAssetTransfer_->apply(token,decisions,{},&moveHistory,internal,&staged);
            if(internal&&moveHistory){
                std::vector<std::pair<std::string,bool>> paths;
                for(size_t i=0;i<moveHistory->files.size();i+=2){
                    const auto& f=moveHistory->files[i];
                    paths.emplace_back(f.from,f.directory);paths.emplace_back(f.to,f.directory);
                }
                // Include the actual keep-both destinations, not only preview
                // sources. Do not adopt unresolved map/rules references.
                studioCheckMapAssetPaths(ws_.project.dir,ws_.map.toJson(),ws_.project.rulesJson,ws_.project.manifest,paths,false);
            }
            const auto beforeHistory=assetHistory_.version();
            if(moveHistory){
                auto next=assetHistory_;
                const auto label=moveHistory->replacement?"Replace assets":internal?"Move assets":"Import assets";
                next.recordMove(std::move(*moveHistory),label);
                studioApplyHistoryBatch(ws_.project.dir,staged.relocations,staged.documents,staged.movingResources,&assetHistory_,&next);
                assetHistory_=std::move(next);assetHistoryPersistenceWarning_.clear();agentHistoryUnsafe_=true;
                try{StudioAssetCatalog catalog(ws_.project.dir);catalog.rebuild(false);}
                catch(const std::exception& e){assetHistoryPersistenceWarning_="Asset index refresh required: "+std::string(e.what());}
                if(internal&&result.getInt("repairedMaterialReferences")>0)
                    ws_.note({editor::Change::Rules,0,0,-1,-1,-1,"Studio Assets: repair material paths after move"});
            }
            result.set("historyRecorded",Json::fromBool(assetHistory_.version()!=beforeHistory&&assetHistory_.canUndo()&&moveHistory.has_value()));
            pendingAssetTransfer_.reset();reply.set("result",std::move(result));
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
        } else if (method == "studio.select" && params.has("previewRef")) {
            if(!sameDocumentScope(params["scope"],graphDocumentScope()) ||
               params.getStr("historyVersion")!=ws_.project.historyVersion())throw std::runtime_error("Hierarchy preview changed");
            Json refs=Json::array();
            refs.push(params["previewRef"]);
            const auto row=resolvePreviewRefs(refs)[0];
            if(row.getInt("doc",0)>=0)throw std::runtime_error("Select the authored entity in the Hierarchy");
            auto detail=queryEditWorld(row,false);
            Json ids=Json::array();ids.push(row["id"]);
            clearSelection();
            setPreviewSelection(std::move(ids),std::move(detail));
            reply.set("result",snapshot());
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
            selectedPreviewIds_=Json();
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
                editor::MapDoc::CompoundEdit batch(ws_.map);
                try {
                    for(const int index:indices) {
                        Json args=Json::object(); args.set("index",num(index));
                        const Json result=ws_.call("entity_delete",args,"studio");
                        if (result.has("error")) throw std::runtime_error(result.getStr("error"));
                    }
                } catch(...) { throw; }
                batch.commit();
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
            const bool isFx=asset.rfind("spectrafx:",0)==0;
            const std::string fxName=isFx?asset.substr(10):std::string();
            if(isFx)requireStudioSpectraGraph(ws_,fxName);
            for(const char* key:{"x","z"}){const double v=params[key].toDouble(NAN);if(!std::isfinite(v)||std::abs(v)>1000000)throw std::runtime_error("Drop position out of range");}
            std::string type;
            for(const char* candidate:{"prop","props","decor","decoration","scenery","doodad","static"})
                if(ws_.project.rules.findType(candidate)>=0){type=candidate;break;}
            if(!isFx && type.empty() && !selected_.empty() && selected_.compare(0,7,"entity:")==0)
                type=ws_.map.data.entities.at(size_t(indexFor(selected_))).type;
            if(type.empty())throw std::runtime_error(isFx?"VFX placement requires a neutral scenery type (prop)":"Add a scenery type (\"prop\") to rules.json, or select an entity whose type to use");
            Json place=Json::object();place.set("type",str(type));place.set("x",params["x"]);place.set("z",params["z"]);place.set("owner",num(int64_t(0)));
            editor::MapDoc::CompoundEdit batch(ws_.map);
            try {
                const Json placed=ws_.call("entity_place",place,"studio");
                if(placed.has("error"))throw std::runtime_error(placed.getStr("error"));
                const int index=int(ws_.map.data.entities.size())-1;
                auto candidate=promotedAuthored(index);
                auto components=candidate.extra["components"];
                if(isFx){
                    lib::mechanics::SpectraSourceComponent source;source.effect=fxName;
                    components.set(lib::mechanics::kSpectraSourceDocumentKey,lib::mechanics::writeSpectraSourceDocument(source));
                }else components.set("StaticModel",Json::object());
                candidate.extra.set("components",components);
                ws_.map.beginEdit();ws_.map.data.entities[size_t(index)]=std::move(candidate);ws_.map.touched();
                const std::string id=studioDocumentId(ws_.map,editor::DocumentEntityRef::mapEntity(size_t(index)));
                if(!isFx&&!asset.empty())setMeshRenderer(index,"asset",str(asset)); // validates the path
                selectOnly(id);
            } catch(...) { throw; }
            batch.commit();
            reply.set("result",snapshot());
        } else if (method == "studio.project") {
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            Json state=Json::object(),maps=Json::array();
            std::set<std::string> listed;
            for (const auto& item:ws_.project.manifest["maps"].items()) {
                const auto file=item.getStr("file");
                listed.insert(file);
                Json row=Json::object();row.set("file",str(file));
                row.set("title",str(item.getStr("title",file)));maps.push(std::move(row));
            }
            namespace fs=std::filesystem;
            std::error_code ec;
            const fs::path folder=ws_.mapsDir();
            std::set<std::string> additional;
            if (!fs::is_symlink(folder,ec)) {
                for (fs::directory_iterator it(folder,ec),end; !ec && it!=end; it.increment(ec)) {
                    const auto& entry=*it;
                    if(entry.path().extension()!=".json" || entry.is_symlink(ec) || !entry.is_regular_file(ec))continue;
                    const auto file=(fs::path("maps")/entry.path().filename()).generic_string();
                    if(!listed.count(file))additional.insert(file);
                }
            }
            for(const auto& file:additional){
                Json row=Json::object();row.set("file",str(file));row.set("title",str(fs::path(file).stem().string()));maps.push(std::move(row));
            }
            Json groups=Json::object();
            for(const auto* key:{"types","resources","techs","factions"}){
                Json rows=Json::array();
                for(const auto& [id,value]:ws_.project.rulesJson[key].members()){
                    Json row=Json::object();row.set("id",str(id));row.set("name",str(value.getStr("name",id)));rows.push(std::move(row));
                }
                groups.set(key,std::move(rows));
            }
            state.set("groups",std::move(groups));
            state.set("rulesError",str(ws_.project.rulesError));
            state.set("maps",std::move(maps));state.set("currentMap",str(ws_.map.path.empty()?"":ws_.relative(ws_.map.path)));
            state.set("canCreateMap",Json::fromBool(!ws_.map.data.worldSpace));
            state.set("maxMapSize",num(game::kMaxMapSize));reply.set("result",std::move(state));
        } else if (method == "studio.projectRule") {
            if (ws_.project.dir.empty() || ws_.project.dir!=params.getStr("project")) throw std::runtime_error("Project changed");
            const auto key=params.getStr("group"),id=params.getStr("id");
            if(key!="types" && key!="resources" && key!="techs" && key!="factions") throw std::runtime_error("Unknown rule group");
            if(id.empty())throw std::runtime_error("Enter a rule ID");
            const auto& group=ws_.project.rulesJson[key];
            if(params.has("value")){
                const auto& value=params["value"];
                if(!value.isObject() || !params.has("original"))throw std::runtime_error("Expected a rule object and original value");
                if(params["original"].isNull()){
                    if(group.has(id))throw std::runtime_error("Rule ID already exists");
                }else if(!group.has(id) || params.getStr("original")!=group[id].dump())throw std::runtime_error("Rule changed. Reopen it before saving.");
                auto updated=group.isObject()?group:Json::object();updated.set(id,value);
                Json args=Json::object();args.set("path",str(key));args.set("value",std::move(updated));
                const auto changed=ws_.call("rules_set",args,"studio");
                if(changed.has("error"))throw std::runtime_error(changed.getStr("error"));
            }else if(!group.has(id))throw std::runtime_error("Rule no longer exists");
            Json result=Json::object();result.set("text",str(ws_.project.rulesJson[key][id].dump()));reply.set("result",std::move(result));
        } else if (method == "studio.openMap" || method == "studio.newMap") {
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            if (method == "studio.openMap") {
                const auto file=params.getStr("file");bool listed=false;
                for (const auto& item:ws_.project.manifest["maps"].items()) listed|=item.getStr("file")==file;
                namespace fs=std::filesystem;
                const fs::path candidate(file);
                const bool localMap=candidate.parent_path()=="maps" && candidate.extension()==".json" &&
                    !fs::is_symlink(ws_.mapsDir()) && !fs::is_symlink(fs::path(ws_.project.dir)/candidate) &&
                    fs::is_regular_file(fs::path(ws_.project.dir)/candidate);
                if (file.empty() || (!listed && !localMap)) throw std::runtime_error("Choose a map from the current project");
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
            ++graphDocumentEpoch_;
            clearSelection();hidden_.clear();locked_.clear();previewGraphTitle_.clear();previewGraphFactors_=Json();previewMaterialName_.clear();refreshMaterialPreview();
            reply.set("result",snapshot());
        } else if (method == "studio.selectedRoadSet") {
            const auto selection = selectedObjectIds();
            const auto& ids = params["ids"];
            int a, b;
            bool linked;
            if (ws_.project.dir.empty() || !sameDocumentScope(params["scope"], graphDocumentScope()) ||
                !selectedRoad(selection, a, b, linked) || !ids.isArray() || ids.size() != 2 ||
                ids[0].str() != selection[0] || ids[1].str() != selection[1])
                throw std::runtime_error("Select the same two editable objects on the current map.");
            if (params["linked"].boolean(false) != params["linked"].boolean(true))
                throw std::runtime_error("Choose whether to connect the objects.");
            if (linked != params["linked"].boolean()) {
                Json args = Json::object();
                args.set("a", num(a));
                args.set("b", num(b));
                const auto response = ws_.call("road_toggle", args, "studio");
                if (response.has("error")) throw std::runtime_error(response.getStr("error"));
                selection_ = {nodeId(ws_.map, editor::DocumentEntityRef::mapEntity(size_t(a)))};
                selected_ = nodeId(ws_.map, editor::DocumentEntityRef::mapEntity(size_t(b)));
            }
            reply.set("result", snapshot());
        } else if(method=="studio.mapRegions" || method=="studio.mapRegionName"){
            if(ws_.project.dir.empty() || !sameDocumentScope(params["scope"],graphDocumentScope()))
                throw std::runtime_error("Map changed. Reload the region.");
            const bool write=method=="studio.mapRegionName";
            auto result=ws_.call(write?"region_name":"region_list",params,write?"studio":"editor");
            if(result.has("error"))throw std::runtime_error(result.getStr("error"));
            reply.set("result",write?snapshot():std::move(result));
        } else if(method=="studio.mapArea"){
            if(ws_.project.dir.empty() || !sameDocumentScope(params["scope"],graphDocumentScope()))
                throw std::runtime_error("Map changed. Reload the area list.");
            const auto result=ws_.call("map_area",params,"studio");
            if(result.has("error"))throw std::runtime_error(result.getStr("error"));
            reply.set("result",snapshot());
        } else if(method=="studio.mapPlayer"){
            reply.set("result",mapPlayer(params));
        } else if(method=="studio.mapProperties" || method=="studio.mapPropertiesSet"){
            reply.set("result",mapProperties(params,method=="studio.mapPropertiesSet"));
        } else if (method == "studio.hierarchyGroups") {
            reply.set("result",hierarchyGroups(params));
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
            if(stroking_)throw std::runtime_error("Finish the active Scene stroke before saving");
            saveStudioDocuments(ws_,ws_.map.path,false);
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
        } else if (method=="studio.rulesDocument" || method=="studio.rulesDocumentSave") {
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            const auto section=params.getStr("section");
            const bool simulation=section=="actorSimulation";
            const bool movement=section=="actorMovement";
            const bool actorSettings=simulation || movement;
            const bool loadout=section=="actorLoadout";
            if((actorSettings || loadout) && !ws_.project.actorSession())throw std::runtime_error("Open an actor project to edit actor settings.");
            if(!actorSettings && !loadout && section!="global"&&section!="document") throw std::runtime_error("Unknown rules section");
            const auto separate=[](const std::string& key){
                return key=="types"||key=="resources"||key=="techs"||key=="factions"||key=="triggers";
            };
            const bool weaponList=ws_.project.rulesJson["actor"]["weapons"].isArray();
            const auto readSection=[&](){
                if(loadout){
                    Json result=Json::object();
                    const char* field=weaponList?"weapons":"weapon";
                    result.set(field,ws_.project.rulesJson["actor"][field]);
                    return result;
                }
                if(actorSettings){
                    const auto config=actor::readConfig(ws_.project.rulesJson);
                    Json result=Json::object();
                    if(movement){
                        result.set("speed",Json::number(config.motor.speed));result.set("sprintSpeed",Json::number(config.motor.sprintSpeed));
                        return result;
                    }
                    result.set("tickRate",num(config.tickRate));result.set("snapshotRate",num(config.snapshotRate));
                    result.set("maxPlayers",num(config.maxPlayers));return result;
                }
                if(section=="document")return ws_.project.rulesJson;
                Json global=Json::object();
                for(const auto& [key,value]:ws_.project.rulesJson.members())
                    if(!separate(key))global.set(key,value);
                return global;
            };
            Json current=readSection();
            if(method=="studio.rulesDocumentSave"){
                if(params.getStr("projectPath")!=ws_.project.dir || params.getStr("original")!=current.dump())
                    throw std::runtime_error("The project or rules changed. Reload this section before saving.");
                const auto& edited=params["value"];
                if(!edited.isObject())throw std::runtime_error("Expected a rules object");
                Json candidate=edited;
                if(section=="global"){
                    for(const auto& [key,value]:edited.members())
                        if(separate(key))throw std::runtime_error("Edit "+key+" in the complete rules document");
                    for(const auto& [key,value]:ws_.project.rulesJson.members())
                        if(separate(key))candidate.set(key,value);
                }
                if(actorSettings){
                    Json patch=Json::object();
                    for(const auto& [key,value]:edited.members()){
                        if(!current.has(key)||!value.isNumber()||(simulation && value.toDouble()!=value.integer()))
                            throw std::runtime_error("Invalid actor setting: "+key);
                        if(value!=current[key])patch.set(key,value);
                    }
                    if(patch.has("maxPlayers") && patch.getInt("maxPlayers")<int64_t(ws_.map.data.extra["players"].size()))
                        throw std::runtime_error("Player limit is below the number of players in the open scene.");
                    candidate=std::move(patch);
                }
                if(loadout){
                    const char* field=weaponList?"weapons":"weapon";
                    Json updated=current[field];
                    const size_t count=weaponList?updated.size():1;
                    const auto& slots=edited["slots"];
                    if(!slots.isArray() || slots.size()!=count)throw std::runtime_error("Loadout slot count changed.");
                    for(size_t i=0;i<count;++i){
                        if(!slots[i].isObject())throw std::runtime_error("Expected loadout slot edits.");
                        auto& slot=weaponList?updated.at(i):updated;
                        for(const auto& [key,value]:slots[i].members()){
                            if((key!="damage" && key!="magazine") || !value.isNumber() || value.toDouble()!=value.integer())
                                throw std::runtime_error("Invalid loadout setting: "+key);
                            if(slot.isNull())slot=Json::object();
                            slot.set(key,value);
                        }
                    }
                    if(updated!=current[field]){
                        Json args=Json::object();args.set("path",str(weaponList?"actor.weapons":"actor.weapon"));args.set("value",updated);
                        const auto changed=ws_.call("rules_set",args,"studio");
                        if(changed.has("error"))throw std::runtime_error(changed.getStr("error"));
                    }
                }else{
                    Json args=Json::object();args.set("path",str(movement?"actor.motor":simulation?"actor":""));args.set("value",candidate);
                    const auto changed=actorSettings && !candidate.size()?Json::object():ws_.call(actorSettings?"rules_merge":"rules_set",args,"studio");
                    if(changed.has("error"))throw std::runtime_error(changed.getStr("error"));
                }
                current=readSection();
            }
            Json out=Json::object();out.set("text",str(current.dump()));reply.set("result",std::move(out));
        } else if(method=="studio.planetForest"||method=="studio.planetForestSet"||method=="studio.planetForestPlant"||method=="studio.planetForestMove"){
            if(ws_.project.dir.empty())throw std::runtime_error("Open a project first");
            const auto op=method=="studio.planetForestSet"?"planet_forest_set":method=="studio.planetForestPlant"?"planet_forest_plant":method=="studio.planetForestMove"?"planet_forest_move":"planet_forest_get";
            const auto result=ws_.call(op,params,"studio");
            if(result.has("error"))throw std::runtime_error(result.getStr("error"));
            reply.set("result",result);
        } else if (method == "studio.rules" || method == "studio.rulesSet") {
            // Rules form: unit types as written (own fields) and resolved (with "extends").
            if (ws_.project.dir.empty()) throw std::runtime_error("Open a project first");
            if (method == "studio.rulesSet") {
                const std::string path=params.getStr("path");
                static const std::regex allowed(R"(^types\.[A-Za-z0-9_\-]+((\.[A-Za-z0-9_]+)|(\[[0-9]{1,4}\])){0,6}$)");
                const bool triggers=path=="triggers", damage=path=="damageTable";
                if (triggers || damage) {
                    if (triggers ? !params["value"].isArray() : !params["value"].isObject())
                        throw std::runtime_error(triggers?"Expected a trigger array":"Expected a damage table object");
                    if (damage) for (const auto& attack:params["value"].members()) {
                        if (!attack.second.isObject()) throw std::runtime_error("Expected a row for "+attack.first);
                        for (const auto& armor:attack.second.members())
                            if (!armor.second.isNumber()) throw std::runtime_error("Expected a damage multiplier for "+attack.first+" / "+armor.first);
                    }
                } else if (path != "targeting.acquireOrder" && (path.size()>200 || !std::regex_match(path,allowed)))
                    throw std::runtime_error("Unsupported rule field");
                Json args=Json::object();args.set("path",str(path));
                const bool erase=params["value"].isNull();
                if (!erase) args.set("value",params["value"]);
                const Json changed=ws_.call(erase?"rules_delete":"rules_set",args,"studio");
                if (changed.has("error")) throw std::runtime_error(changed.getStr("error"));
            }
            const auto list=ws_.call("rules_list",Json::object(),"studio");
            if(list.has("error"))throw std::runtime_error(list.getStr("error"));
            Json out=Json::object();
            const auto& authoredTypes=ws_.project.rulesJson["types"];
            if(authoredTypes.isNull()){
                out.set("types",Json::object());out.set("resolved",Json::object());
            }else{
                Json args=Json::object();
                args.set("path",str("types"));args.set("resolved",Json::fromBool(true));
                const auto resolved=ws_.call("rules_get",args,"studio");
                if(resolved.has("error"))throw std::runtime_error(resolved.getStr("error"));
                out.set("types",authoredTypes);out.set("resolved",resolved["value"]);
            }
            Json bodies=Json::object();
            for(const auto& type:ws_.project.rules.types){
                Json body=Json::object();
                body.set("radius",Json::number(type.radius));
                body.set("mass",Json::number(type.mass));
                bodies.set(type.id,std::move(body));
            }
            out.set("bodies",std::move(bodies));
            out.set("resources",list["resources"]);out.set("summary",list["types"]);
            const auto& authored=ws_.project.rulesJson;
            out.set("triggers",authored["triggers"].isNull()?Json::array():authored["triggers"]);
            out.set("armorTypes",authored["armorTypes"].isNull()?Json::array():authored["armorTypes"]);
            out.set("damageTable",authored["damageTable"].isNull()?Json::object():authored["damageTable"]);
            out.set("acquireOrder", str(ws_.project.rules.acquisitionOrder == game::AcquisitionOrder::RowMajor ? "rowMajor" : "nearest"));
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
            if(params.has("project")&&params.getStr("project")!=ws_.project.dir)throw std::runtime_error("Attachment project changed");
            const std::string rel=params.getStr("path","attachments.json");
            const fs::path relPath=fs::path(rel).lexically_normal();
            if (rel.empty()||rel.size()>300||relPath.is_absolute()||rel.find("..")!=std::string::npos||relPath.extension()!=".json")
                throw std::runtime_error("Choose a .json file inside the project");
            const auto root=fs::weakly_canonical(ws_.project.dir);
            const fs::path file=root/relPath;
            const auto local=fs::weakly_canonical(file).lexically_relative(root);
            if(local.empty()||*local.begin()==".."||fs::is_symlink(fs::symlink_status(file)))
                throw std::runtime_error("Attachment layout is outside the project or is a symlink");
            view::AttachmentLayout layout;
            if (method=="studio.attachmentsSet") {
                view::AttachmentLayoutCodec::read(params["document"],layout);   // throws on invalid data
                const Json canonical=view::AttachmentLayoutCodec::write(layout);
                editor::graph_asset::Transaction transaction(file,root);
                if(params.has("expected")){
                    if(params["expected"].isNull()){
                        if(fs::exists(file))throw std::runtime_error("Attachment file already exists; reopen it");
                    }else{
                        view::AttachmentLayout previous;
                        view::AttachmentLayoutCodec::read(Json::parseFile(file.string()),previous);
                        if(!editor::graph_asset::sameDocument(params["expected"],view::AttachmentLayoutCodec::write(previous)))
                            throw std::runtime_error("Attachment layout changed on disk; reload before saving");
                    }
                }
                transaction.replace(canonical.dump(2)+"\n",4*1024*1024);
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
            const bool positions=params.getBool("positions",true),amountsEnabled=params.getBool("amounts",true);
            const bool dryRun=params.getBool("dryRun");
            auto staged=ws_.map.data;auto& d=staged;
            std::vector<size_t> dead;
            int moved=0,amounts=0,skipped=0;
            bool begun=false;
            auto begin=[&]{begun=true;};
            for (const auto& st:states.items()) {
                const int64_t i=st["index"].integer(-1);
                if (i<0 || i>=int64_t(d.entities.size())) {++skipped;continue;}
                auto& e=d.entities[size_t(i)];
                if (st.getBool("removed")) { if(removeDead)dead.push_back(size_t(i)); continue; }
                if (st.has("type") && st.getStr("type")!=e.type) {++skipped;continue;}
                if (positions&&st["x"].isNumber() && st["z"].isNumber()) {
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
                if (amountsEnabled&&st["amount"].isNumber()) {
                    const float a=float(st["amount"].toDouble());
                    // A map entity without an amount uses its type's default: only a real change is kept.
                    const float current=e.amount>=0?e.amount:float(st["defaultAmount"].toDouble(double(a)));
                    if (std::isfinite(a)&&a>=0&&std::abs(a-current)>0.01f) {begin();e.amount=a;++amounts;}
                }
            }
            int removed=0;
            if (!dead.empty()) {
                // Map each original ordinal once so every surviving road keeps its endpoints.
                const size_t originalCount=d.entities.size();
                std::vector<int> remap(originalCount);
                for(size_t index:dead)remap[index]=-1;
                size_t kept=0;
                for(size_t index=0;index<originalCount;++index){
                    if(remap[index]<0)continue;
                    remap[index]=static_cast<int>(kept);
                    if(kept!=index)d.entities[kept]=std::move(d.entities[index]);
                    ++kept;
                }
                removed=static_cast<int>(originalCount-kept);
                d.entities.resize(kept);
                auto deleted=[&](int index){
                    return index>=0&&size_t(index)<originalCount&&remap[size_t(index)]<0;
                };
                auto newIndex=[&](int index){
                    if(index<0)return index;
                    return size_t(index)<originalCount?remap[size_t(index)]:index-removed;
                };
                std::erase_if(d.roads,[&](const auto& road){
                    return deleted(road.first)||deleted(road.second);
                });
                for(auto& [a,b]:d.roads){a=newIndex(a);b=newIndex(b);}
                begin();
            }
            if(begun&&!dryRun){
                ws_.map.beginEdit();ws_.map.data=std::move(staged);
                if(removed){clearSelection();dropOrdinalFlags();}
                ws_.map.touched();ws_.log("studio","Keep play changes: "+std::to_string(moved)+" moved, "+std::to_string(amounts)+" amounts, "+std::to_string(removed)+" removed");
            }
            Json out=Json::object();
            out.set("moved",num(int64_t(moved)));out.set("amounts",num(int64_t(amounts)));out.set("removed",num(int64_t(removed)));
            out.set("skipped",num(int64_t(skipped)));if(!dryRun)out.set("snapshot",snapshot());
            reply.set("result",std::move(out));
        } else if (method == "studio.splineEdit") {
            // Roads / Water panels: show node handles in the Scene ("road" | "river" | "" off).
            const std::string k=params.getStr("kind");
            if (k!="road"&&k!="river"&&!k.empty()) throw std::runtime_error("Unknown spline kind");
            splineEdit_=ws_.project.dir.empty()?std::string():k;
            if (!splineEdit_.empty()) {brush_.armed=false;brush_.pickHeight=false;}
            reply.set("result",splineHandles());
        } else if (method == "studio.sceneBrush") {
            // Arm / disarm the Scene brush and set its tool and settings (session only).
            if(stroking_)throw std::runtime_error("Finish the active Scene stroke first");
            if(params.has("scope") && !sameDocumentScope(params["scope"],graphDocumentScope()))
                throw std::runtime_error("Map changed. Reload the region before painting.");
            auto next=brush_;auto& b=next;
            static const std::set<std::string> kinds={"sparse","terrain","paint","tiles","region","field","objects","road","river","caverns"};
            static const std::set<std::string> ops={"set","raise","lower","smooth","flatten","noise","erode","stamp"};
            static const std::set<std::string> modes={"set","add","max","erase"};
            auto number=[&](const char* k,double lo,double hi){const double v=params[k].toDouble();
                if(!params[k].isNumber()||!std::isfinite(v)||v<lo||v>hi)throw std::runtime_error(std::string(k)+" must be "+std::to_string(lo)+".."+std::to_string(hi));return v;};
            auto text=[&](const char* k){const std::string v=params.getStr(k);if(v.size()>128)throw std::runtime_error(std::string(k)+" is too long");return v;};
            if (params.has("kind")) { const std::string k=params.getStr("kind"); if(!kinds.count(k))throw std::runtime_error("Unknown brush tool"); b.kind=k; }
            if(params.has("sparseMode")){
                const auto mode=params.getStr("sparseMode");
                if(mode!="create" && mode!="raise" && mode!="lower")throw std::runtime_error("Unknown sparse terrain operation");
                b.sparseMode=mode;
            }
            if(params.has("chunkMetres") || params.has("depthCm")){
                auto integer=[&](const char* key,int lo,int hi){
                    const double value=number(key,lo,hi);
                    if(value!=std::floor(value))throw std::runtime_error(std::string(key)+" requires a whole number");
                    return int(value);
                };
                if(params.has("chunkMetres"))b.chunkMetres=integer("chunkMetres",16,1024);
                if(params.has("depthCm"))b.depthCm=integer("depthCm",1,20000);
            }
            if (params.has("op")) { const std::string op=params.getStr("op"); if(!ops.count(op))throw std::runtime_error("Unknown brush op"); b.op=op; }
            if(params.has("terrainGrid")){
                const auto grid=params.getStr("terrainGrid");
                if(grid!="auto" && grid!="base" && grid!="detail")throw std::runtime_error("Unknown terrain grid");
                b.terrainGrid=grid;
            }
            if(b.kind=="terrain" && b.terrainGrid=="base" && (b.op=="stamp" || b.op=="erode"))
                throw std::runtime_error("Stamp and erosion require the detail grid");
            if(params.has("targetHeight"))b.targetHeight=number("targetHeight",-2048,2047.9375);
            if(params.has("pickHeight")){
                if(params["pickHeight"].type()!=Json::Type::Bool)throw std::runtime_error("Invalid height picker flag");
                b.pickHeight=params.getBool("pickHeight");
            }
            if (params.has("radius")) b.radius=number("radius",0.25,64);
            if (params.has("strength")) {
                if (b.kind=="terrain" && b.op=="stamp") b.stampHeight=number("strength",-20,20);
                else b.strength=number("strength",0,20);
            }
            if (params.has("layer")) b.layer=int(number("layer",0,15));
            if (params.has("stamp")) {
                const auto name=params.getStr("stamp");
                if(name!="crater" && name!="mesa" && name!="dunes" && name!="rocks")throw std::runtime_error("Unknown terrain stamp");
                b.stamp=name;
            }
            if(params.has("angle")) b.angle=number("angle",0,360);
            if(params.has("hardness")) b.hardness=number("hardness",0,1);
            if(params.has("seed")) {
                const auto seed=number("seed",0,UINT32_MAX);
                if(seed!=std::floor(seed))throw std::runtime_error("Choose a whole stamp seed");
                b.seed=uint32_t(seed);
            }
            if (params.has("fixedTile")) {
                const double tile=number("fixedTile",-1,INT32_MAX);
                if(std::floor(tile)!=tile)throw std::runtime_error("Choose a whole atlas tile number");
                b.fixedTile=int(tile);
            }
            if (params.has("tile")) b.tile=text("tile");
            if (params.has("field")) b.field=text("field");
            if(params.has("regionBorders")){
                if(params["regionBorders"].type()!=Json::Type::Bool)throw std::runtime_error("Expected a border visibility flag");
                b.regionBorders=params.getBool("regionBorders");
            }
            if (params.has("regionLetter")) {
                const auto letter=params.getStr("regionLetter");
                if(letter.size()!=1 || (letter!="." && (letter[0]<'A' || letter[0]>'Z')))
                    throw std::runtime_error("Choose a region A–Z or erase");
                b.regionLetter=letter;
            }
            if (params.has("fieldPlayer")) {
                const double player=number("fieldPlayer",1,ws_.map.data.players.size());
                if(std::floor(player)!=player)throw std::runtime_error("Choose a whole player number");
                b.fieldPlayer=int(player);
            }
            if (params.has("fieldMode")) { const std::string m=params.getStr("fieldMode"); if(!modes.count(m))throw std::runtime_error("Unknown field mode"); b.fieldMode=m; }
            if (params.has("value")) b.value=number("value",INT32_MIN,INT32_MAX);
            if (params.has("type")) b.type=text("type");
            if (params.has("owner")) b.owner=int(number("owner",-1,16));
            if (params.has("count")) b.count=int(number("count",1,50));
            if (params.has("spread")) b.spread=number("spread",0,32);
            if (params.has("roadType")) b.roadType=text("roadType");
            if (params.has("width")) b.width=number("width",1,200);
            if(params.has("caveMode")){const auto mode=params.getStr("caveMode");if(mode!="carve"&&mode!="fill"&&mode!="shaft")throw std::runtime_error("Unknown cave brush mode");b.caveMode=mode;}
            if(params.has("caveY"))b.caveY=number("caveY",-10000000,10000000);
            if(params.has("caveSurface"))b.caveSurface=params.getBool("caveSurface");
            if(params.has("caveDepth"))b.caveDepth=number("caveDepth",.01,10000);
            if (params.has("armed")) b.armed=params.getBool("armed")&&!ws_.project.dir.empty();
            if(!b.armed || b.kind!="terrain" || b.op!="set")b.pickHeight=false;
            if(b.kind=="terrain" && ws_.map.data.planet() && (b.op=="erode" || b.op=="stamp")){
                if(params.has("op"))throw std::runtime_error("Erosion and stamps require a flat detail surface");
                b.op="raise";
            }
            if(b.armed && b.kind=="terrain" && (b.op=="stamp" || b.op=="set") && ws_.map.data.worldSpace && !ws_.map.data.planet())
                throw std::runtime_error("This terrain brush requires a flat heightmap");
            if(b.armed && b.kind=="region" && (ws_.map.data.worldSpace || ws_.map.data.planet()))
                throw std::runtime_error("Region painting requires a flat map");
            if(b.armed&&b.kind=="caverns"){
                if(params.getBool("armed",false)&&(params.getStr("expectedProject")!=ws_.project.dir||
                   !params["expectedMap"].isString()||params.getStr("expectedMap")!=(ws_.map.path.empty()?"":ws_.relative(ws_.map.path))))
                    throw std::runtime_error("Caverns brush project/map changed");
                bool paint=false,pick=false;
                for(const auto& op:ws_.ops()){paint|=op.name=="terraweave_caverns_paint";pick|=op.name=="terraweave_caverns_pick";}
                if(!paint||!pick)throw std::runtime_error("Caverns native backend unavailable");
                if(ws_.map.data.planet()){
                    (void)editor::planet_caves::read(ws_.map.data);
                    b.caveRegionId=editor::planet_caves::activeRegionId(ws_.map.data);
                }
                else if(b.caveMode=="shaft")throw std::runtime_error("Planetary chart required for shaft brush");
            }
            brush_=std::move(next);
            if(brush_.armed)splineEdit_.clear();
            reply.set("result",sceneBrushJson());
        } else if (method == "studio.agentInfo" || method == "studio.agentStart" || method == "studio.agentStop" || method == "studio.agentReset") {
            reply.set("result", agentRequest(method, params));
        } else if (method == "studio.buildGameWindow" || method == "studio.buildNativeProject" || method == "studio.buildInfo" || method == "studio.buildFileTextureSize" || method == "studio.buildFileExclusion" || method == "studio.buildFileCompression" || method == "studio.buildRemoveFileRule" || method == "studio.buildSplitLimit" || method == "studio.buildSplitMode" || method == "studio.buildCompression" || method == "studio.buildAtlasFixSizes" || method == "studio.buildAtlasPage" || method == "studio.buildAtlasEnabled" || method == "studio.installBuildTools" || method == "studio.buildToolchain" || method == "studio.buildStart" ||
                   method == "studio.buildLog" || method == "studio.buildEstimate") {
            reply.set("result", buildRequest(method, params));
        } else if(method=="studio.worldProcessAreaBatchResize"){
            if(ws_.map.data.planet())throw std::runtime_error("Planet regions use local east/south numeric extents; planar group pivot unavailable");
            if (params.has("scope") && !sameDocumentScope(params["scope"],graphDocumentScope()))
                throw std::runtime_error("Map changed. Review the selected regions.");
            // All selected regions are validated and staged before one Undo.
            const auto& ids=params["entityIds"];
            const auto& numerator=params["scaleNumerator"];
            const auto& denominator=params["scaleDenominator"];
            const std::string pivot=params.getStr("pivot");
            const std::string axis=params.getStr("axis");
            if(!ids.isArray()||ids.size()<2||ids.size()>256||
               (pivot!="individual"&&pivot!="shared"&&pivot!="custom")||
               (axis!="x"&&axis!="z"&&axis!="both")||
               !numerator.isNumber()||!denominator.isNumber())
                throw std::runtime_error("Invalid EVOLVE group resize request");
            const int64_t n=numerator.integer(),d=denominator.integer();
            if(n<1||n>1000000||d<1||d>1000000||
               numerator.numberLiteral()!=std::to_string(n)||
               denominator.numberLiteral()!=std::to_string(d))
                throw std::runtime_error("Invalid EVOLVE group scale ratio");
            const auto& nzValue=params["scaleNumeratorZ"];
            const auto& dzValue=params["scaleDenominatorZ"];
            int64_t nz=axis=="both"?nzValue.integer(-1):n;
            int64_t dz=axis=="both"?dzValue.integer(-1):d;
            if(axis=="both"&&(!nzValue.isNumber()||!dzValue.isNumber()||
               nz<1||nz>1000000||dz<1||dz>1000000||
               nzValue.numberLiteral()!=std::to_string(nz)||
               dzValue.numberLiteral()!=std::to_string(dz)))
                throw std::runtime_error("Invalid EVOLVE Z scale ratio");
            const auto& pivotX=params["pivotXcm"];
            const auto& pivotZ=params["pivotZcm"];
            if(pivot=="custom"&&(!pivotX.isNumber()||!pivotZ.isNumber()||
               pivotX.numberLiteral()!=std::to_string(pivotX.integer())||
               pivotZ.numberLiteral()!=std::to_string(pivotZ.integer())||
               std::abs(pivotX.integer())>evolve::kAreaCoordinateCm||
               std::abs(pivotZ.integer())>evolve::kAreaCoordinateCm))
                throw std::runtime_error("Invalid custom EVOLVE pivot");
            std::set<std::string> seen;
            std::vector<std::pair<std::size_t,editor::DocEntity>> pending;

            // Shared pivot is the arithmetic mean of selected authored
            // area centers on the active axis, in integer centimetres.
            std::vector<std::int64_t> centersX,centersZ;
            for(const auto& idValue:ids.items()){
                if(!idValue.isString())throw std::runtime_error("Invalid EVOLVE ID");
                const auto id=idValue.str();
                if(id.rfind("entity:@",0)!=0||!seen.insert(id).second||
                   locked_.contains(id)||hidden_.contains(id))
                    throw std::runtime_error("Duplicate or locked EVOLVE entity");
                const int index=indexFor(id);
                auto candidate=ws_.map.data.entities[std::size_t(index)];
                Json area=candidate.extra["components"]["EvolveArea"];
                if(!scalableEvolveRegion(candidate))
                    throw std::runtime_error("Selected entity lacks valid EVOLVE area");
                if(!area.has("offsetXcm"))area.set("offsetXcm",Json::number(int64_t(0)));
                if(!area.has("offsetZcm"))area.set("offsetZcm",Json::number(int64_t(0)));
                centersX.push_back(std::llround(double(candidate.x)*100.)+
                                   area["offsetXcm"].integer());
                centersZ.push_back(std::llround(double(candidate.z)*100.)+
                                   area["offsetZcm"].integer());
                Json components=candidate.extra["components"];
                components.set("EvolveArea",std::move(area));
                candidate.extra.set("components",std::move(components));
                pending.emplace_back(std::size_t(index),std::move(candidate));
            }
            const auto mean=[](const std::vector<std::int64_t>& values){
                long double sum=0;
                for(auto v:values)sum+=v;
                return std::llround(sum/values.size());
            };
            const std::int64_t sharedX=pivot=="custom"?pivotX.integer():mean(centersX);
            const std::int64_t sharedZ=pivot=="custom"?pivotZ.integer():mean(centersZ);
            for(std::size_t i=0;i<pending.size();++i){
                auto& candidate=pending[i].second;
                Json components=candidate.extra["components"];
                Json area=components["EvolveArea"];
                for(int dim=0;dim<2;++dim){
                    if(axis!="both"&&((axis=="x")!=(dim==0)))continue;
                    const bool x=dim==0;
                    const char* halfKey=x?"halfWidthCm":"halfDepthCm";
                    const char* offsetKey=x?"offsetXcm":"offsetZcm";
                    const auto scaleN=x?n:(axis=="both"?nz:n);
                    const auto scaleD=x?d:(axis=="both"?dz:d);
                    const auto half=area[halfKey].integer();
                    const auto oldCenter=x?centersX[i]:centersZ[i];
                    const auto pivotCenter=x?sharedX:sharedZ;
                    evolve::scale_math::AxisResult transformed;
                    if(!evolve::scale_math::transform(
                           oldCenter,half,pivotCenter,scaleN,scaleD,
                           pivot=="individual",transformed))
                        throw std::runtime_error("Invalid EVOLVE group scale");
                    const auto newHalf=transformed.half;
                    const auto nextCenter=transformed.center;
                    if(newHalf<50||newHalf>evolve::kAreaMaxHalfExtentCm)
                        throw std::runtime_error("EVOLVE group size outside bounds");
                    const std::int64_t origin=std::llround(double(x?candidate.x:candidate.z)*100.);
                    area.set(halfKey,Json::number(newHalf));
                    area.set(offsetKey,Json::number(nextCenter-origin));
                }
                if(!lib::worldprocess::extended::validate("EvolveArea",area))
                    throw std::runtime_error("Invalid resized EVOLVE area");
                const auto cx=std::llround(double(candidate.x)*100.)+area["offsetXcm"].integer();
                const auto cz=std::llround(double(candidate.z)*100.)+area["offsetZcm"].integer();
                if(!evolve::scale_math::withinBounds(
                       cx,area["halfWidthCm"].integer(),
                       evolve::kAreaCoordinateCm,50,evolve::kAreaMaxHalfExtentCm)||
                   !evolve::scale_math::withinBounds(
                       cz,area["halfDepthCm"].integer(),
                       evolve::kAreaCoordinateCm,50,evolve::kAreaMaxHalfExtentCm))
                    throw std::runtime_error("EVOLVE resized area exceeds world bounds");
                components.set("EvolveArea",std::move(area));
                candidate.extra.set("components",std::move(components));
            }
            if(n!=d||(axis=="both"&&nz!=dz)){
                ws_.map.beginEdit();
                for(auto& entry:pending)
                    ws_.map.data.entities[entry.first]=std::move(entry.second);
                ws_.map.touched();
            }
            reply.set("result",Json::number(int64_t(pending.size())));
        } else if(method=="studio.worldProcessAreaBatchMove"){
            if(ws_.map.data.planet())throw std::runtime_error("Planet regions use individual local offsets; planar group move unavailable");
            // One validated, all-or-nothing MapDoc edit for selected regions.
            // The caller supplies stable map IDs, never transient ECS slots.
            const auto& ids=params["entityIds"];
            const std::string axis=params.getStr("axis");
            const auto& amount=params["deltaCm"];
            if(!ids.isArray()||ids.size()<2||ids.size()>256||
               (axis!="x"&&axis!="z")||!amount.isNumber())
                throw std::runtime_error("Invalid EVOLVE batch center request");
            const int64_t delta=amount.integer();
            if(amount.numberLiteral()!=std::to_string(delta)||
               delta < -2*evolve::kAreaCoordinateCm||
               delta > 2*evolve::kAreaCoordinateCm)
                throw std::runtime_error("Invalid EVOLVE batch delta");
            std::vector<std::pair<std::size_t,editor::DocEntity>> pending;
            std::set<std::string> seen;
            const std::string key=axis=="x"?"offsetXcm":"offsetZcm";
            for(const auto& idValue:ids.items()){
                if(!idValue.isString())throw std::runtime_error("Invalid EVOLVE batch ID");
                const auto id=idValue.str();
                if(id.rfind("entity:@",0)!=0||!seen.insert(id).second||
                   locked_.contains(id)||hidden_.contains(id))
                    throw std::runtime_error("Duplicate, hidden or locked EVOLVE batch entity");
                const auto index=indexFor(id);
                auto candidate=ws_.map.data.entities[std::size_t(index)];
                Json area=candidate.extra["components"]["EvolveArea"];
                if(!lib::worldprocess::extended::validate("EvolveArea",area)||
                   !candidate.extra["components"]["EvolveRegion"].isObject())
                    throw std::runtime_error("Batch entity lacks valid EVOLVE region area");
                if(!area.has("offsetXcm"))area.set("offsetXcm",Json::number(int64_t(0)));
                if(!area.has("offsetZcm"))area.set("offsetZcm",Json::number(int64_t(0)));
                const auto old=area[key].integer();
                if((delta>0&&old>evolve::kAreaCoordinateCm-delta)||
                   (delta<0&&old< -evolve::kAreaCoordinateCm-delta))
                    throw std::runtime_error("EVOLVE batch offset outside bounds");
                area.set(key,Json::number(old+delta));
                if(!lib::worldprocess::extended::validate("EvolveArea",area))
                    throw std::runtime_error("Invalid EVOLVE batch area");
                const auto cx=std::llround(double(candidate.x)*100.)+
                              area["offsetXcm"].integer();
                const auto cz=std::llround(double(candidate.z)*100.)+
                              area["offsetZcm"].integer();
                if(!evolve::scale_math::withinBounds(cx,area["halfWidthCm"].integer(),
                       evolve::kAreaCoordinateCm,50,evolve::kAreaMaxHalfExtentCm)||
                   !evolve::scale_math::withinBounds(cz,area["halfDepthCm"].integer(),
                       evolve::kAreaCoordinateCm,50,evolve::kAreaMaxHalfExtentCm))
                    throw std::runtime_error("EVOLVE batch world bounds exceeded");
                Json components=candidate.extra["components"];
                components.set("EvolveArea",std::move(area));
                candidate.extra.set("components",std::move(components));
                pending.emplace_back(std::size_t(index),std::move(candidate));
            }
            if(delta!=0){
                ws_.map.beginEdit();
                for(auto& entry:pending)
                    ws_.map.data.entities[entry.first]=std::move(entry.second);
                ws_.map.touched();
            }
            reply.set("result",Json::number(int64_t(pending.size())));
        } else if(method=="studio.worldProcessAreaSet"){
            // Exactly one MapDoc Undo snapshot for one final gizmo release.
            // Runtime EVOLVE region bounds are not inferred from this visual radius.
            const std::string entityId=params.getStr("entityId");
            const std::string area=params.getStr("componentId");
            const std::string key=params.getStr("key");
            if(entityId.rfind("entity:@",0)!=0)
                throw std::runtime_error("World-process resize requires stable map ID");
            if(locked_.contains(entityId)||hidden_.contains(entityId))
                throw std::runtime_error("World-process resize requires an editable entity");
            const int index=indexFor(entityId);
            const auto& source=ws_.map.data.entities[size_t(index)];
            if(params.has("expectedRevision")&&
               (!params["expectedRevision"].isNumber()||params["expectedRevision"].integer(-1)!=int64_t(ws_.map.revision)))
                throw std::runtime_error("Stale world-process edit revision");
            if(area=="EvolveArea"&&key=="bounds"){
                if(!ws_.map.data.planet()||!source.at.isObject())
                    throw std::runtime_error("Atomic surface bounds require a planet entity");
                const auto& original=source.extra["components"]["EvolveArea"];
                if(!lib::worldprocess::extended::validate("EvolveArea",original))
                    throw std::runtime_error("EVOLVE spatial Area missing or malformed");
                const auto& values=params["value"];
                if(!values.isObject()||values.size()!=4)
                    throw std::runtime_error("Expected four EVOLVE bounds");
                Json next=original;
                for(const char* field:{"offsetXcm","offsetZcm","halfWidthCm","halfDepthCm"}){
                    const auto& v=values[field];
                    if(!v.isNumber()||v.numberLiteral()!=std::to_string(v.integer()))
                        throw std::runtime_error("Expected integer centimetres");
                    next.set(field,v);
                }
                if(!lib::worldprocess::extended::validate("EvolveArea",next))
                    throw std::runtime_error("Invalid EVOLVE surface bounds");
                const evolve::Area bounds{next["offsetXcm"].integer(),next["offsetZcm"].integer(),
                    next["halfWidthCm"].integer(),next["halfDepthCm"].integer(),next.getBool("enabled",true)};
                if(!evolve::valid(bounds))throw std::runtime_error("EVOLVE bounds outside coordinate budget");
                if(original!=next){
                    auto candidate=source;Json components=candidate.extra["components"];
                    components.set("EvolveArea",std::move(next));candidate.extra.set("components",std::move(components));
                    ws_.map.beginEdit();ws_.map.data.entities[size_t(index)]=std::move(candidate);ws_.map.touched();
                }
            }else if(area=="EvolveArea"&&(key=="halfWidthCm"||key=="halfDepthCm"||
                                    key=="offsetXcm"||key=="offsetZcm")){
                const Json& original=source.extra["components"]["EvolveArea"];
                if(!lib::worldprocess::extended::validate("EvolveArea",original))
                    throw std::runtime_error("EVOLVE spatial Area missing or malformed");
                const auto& value=params["value"];
                if(!value.isNumber())throw std::runtime_error("Expected EVOLVE integer centimetres");
                const int64_t extent=value.integer(-1);
                if(value.numberLiteral()!=std::to_string(extent))
                    throw std::runtime_error("Invalid EVOLVE integer value");
                Json next=original;
                if(!next.has("offsetXcm"))next.set("offsetXcm",Json::number(int64_t(0)));
                if(!next.has("offsetZcm"))next.set("offsetZcm",Json::number(int64_t(0)));
                const std::string side=params.getStr("edgeSide");
                if(!side.empty()){
                    if((key!="halfWidthCm"&&key!="halfDepthCm")||
                       (side!="positive"&&side!="negative"))
                        throw std::runtime_error("Invalid EVOLVE edge operation");
                    const bool xAxis=key=="halfWidthCm";
                    const char* centerKey=xAxis?"offsetXcm":"offsetZcm";
                    const int64_t center=next[centerKey].integer();
                    const int64_t half=next[key].integer();
                    const int64_t opposite=side=="positive"?center-half:center+half;
                    // Clamp edge while holding opposite side fixed. Both
                    // center and half-width are exact integral centimetres.
                    // Enforce even span by rounding the requested edge inward.
                    const int64_t minSpan=100;
                    const int64_t maxSpan=2*evolve::kAreaMaxHalfExtentCm;
                    int64_t edge=extent;
                    if(side=="positive"){
                        edge=std::clamp(edge,opposite+minSpan,opposite+maxSpan);
                        if((edge-opposite)%2!=0)--edge;
                    }else{
                        edge=std::clamp(edge,opposite-maxSpan,opposite-minSpan);
                        if((opposite-edge)%2!=0)++edge;
                    }
                    const int64_t newCenter=(edge+opposite)/2;
                    const int64_t newHalf=std::abs(edge-opposite)/2;
                    if(newCenter < -evolve::kAreaCoordinateCm ||
                       newCenter > evolve::kAreaCoordinateCm)
                        throw std::runtime_error("EVOLVE edge exceeds coordinate budget");
                    next.set(centerKey,Json::number(newCenter));
                    next.set(key,Json::number(newHalf));
                }else{
                    const bool offset=key=="offsetXcm"||key=="offsetZcm";
                    if(extent<(offset?-evolve::kAreaCoordinateCm:50)||
                       extent>(offset?evolve::kAreaCoordinateCm:evolve::kAreaMaxHalfExtentCm))
                        throw std::runtime_error("Invalid EVOLVE area value");
                    next.set(key,Json::number(extent));
                }
                if(!lib::worldprocess::extended::validate("EvolveArea",next))
                    throw std::runtime_error("Invalid EVOLVE area update");
                // Check ECS world limits against map position before mutation.
                // Full authoritative quantization is repeated by hydration.
                const auto worldX=ws_.map.data.planet()?0ll:std::llround(double(source.x)*100.);
                const auto worldZ=ws_.map.data.planet()?0ll:std::llround(double(source.z)*100.);
                const auto centerX=worldX+next["offsetXcm"].integer();
                const auto centerZ=worldZ+next["offsetZcm"].integer();
                const auto halfX=next["halfWidthCm"].integer();
                const auto halfZ=next["halfDepthCm"].integer();
                if(!evolve::scale_math::withinBounds(centerX,halfX,
                       evolve::kAreaCoordinateCm,50,evolve::kAreaMaxHalfExtentCm)||
                   !evolve::scale_math::withinBounds(centerZ,halfZ,
                       evolve::kAreaCoordinateCm,50,evolve::kAreaMaxHalfExtentCm))
                    throw std::runtime_error("EVOLVE area exceeds world coordinate budget");
                if(!(original==next)){
                    auto candidate=source;
                    Json components=candidate.extra["components"];
                    components.set("EvolveArea",std::move(next));
                    candidate.extra.set("components",std::move(components));
                    ws_.map.beginEdit();
                    ws_.map.data.entities[std::size_t(index)]=std::move(candidate);
                    ws_.map.touched();
                }
            }else if(area=="EvolveRegion"&&key=="radiusMetres"){
                if(!source.extra["components"]["EvolveRegion"].isObject())
                    throw std::runtime_error("EVOLVE region missing from map entity");
                if(!params["value"].isNumber())throw std::runtime_error("Expected numeric radius");
                const double radius=params["value"].toDouble();
                if(!std::isfinite(radius)||radius<.5||radius>500)
                    throw std::runtime_error("Invalid EVOLVE gizmo radius");
                auto candidate=source;
                Json meta=candidate.extra["worldProcessGizmo"];
                if(!meta.isObject())meta=Json::object();
                const Json next=Json::number(radius,3);
                if(!(meta["radiusMetres"]==next)){
                    meta.set("radiusMetres",next);
                    candidate.extra.set("worldProcessGizmo",std::move(meta));
                    ws_.map.beginEdit();
                    ws_.map.data.entities[std::size_t(index)]=std::move(candidate);
                    ws_.map.touched();
                }
            }else if(area=="GranularField"&&(key=="width"||key=="height")){
                if(!params["value"].isNumber())throw std::runtime_error("Expected grid dimension");
                const int64_t desired=params["value"].integer(-1);
                if(desired<1||desired>granular::kMaxDimension ||
                   params["value"].numberLiteral()!=std::to_string(desired))
                    throw std::runtime_error("Invalid GRANULAR integer grid dimension");
                const Json& original=source.extra["components"]["GranularField"];
                if(!original.isObject() ||
                   !lib::worldprocess::extended::validate("GranularField",original))
                    throw std::runtime_error("Invalid authored GRANULAR grid");
                Json grid=original;
                std::string error;
                if(!grid.has("width")){
                    grid=granular::authoring::create(
                        1,1,original["capacity"].integer());
                    if(grid.isNull())throw std::runtime_error("Invalid GRANULAR capacity");
                    grid.member("mass").at(0)=original["initialMass"];
                    grid.set("enabled",original["enabled"]);
                }
                const int64_t width=key=="width"?desired:grid["width"].integer();
                const int64_t height=key=="height"?desired:grid["height"].integer();
                if(!granular::authoring::resize(grid,
                     std::uint16_t(width),std::uint16_t(height),&error))
                    throw std::runtime_error("Cannot resize GRANULAR grid: "+error);
                if(!(original==grid)){
                    auto candidate=source;
                    Json components=candidate.extra["components"];
                    components.set("GranularField",std::move(grid));
                    candidate.extra.set("components",std::move(components));
                    ws_.map.beginEdit();
                    ws_.map.data.entities[std::size_t(index)]=std::move(candidate);
                    ws_.map.touched();
                }
            }else throw std::runtime_error("Unsupported world-process gizmo edit");
            reply.set("result",snapshot());
        } else if (method == "studio.componentCatalog") {
            reply.set("result", componentCatalog(params.getStr("entityId")));
        } else if (method == "studio.componentAdd" || method == "studio.componentRemove") {
            const std::string entityId=params.getStr("entityId"),componentId=params.getStr("componentId");
            if(entityId.compare(0,7,"entity:")!=0)throw std::runtime_error("Components can be changed on map entities only");
            const int index=indexFor(entityId);
            const auto& source=ws_.map.data.entities[size_t(index)];
            auto candidate=source;
            if(method=="studio.componentAdd") {
                if(componentId=="SpectraSource"){
                    if(source.extra["components"].has("SpectraSource"))throw std::runtime_error("VFX Renderer is already on this entity");
                    candidate=promotedAuthored(index);
                    auto components=candidate.extra["components"];
                    lib::mechanics::SpectraSourceComponent fx;fx.enabled=false;
                    components.set(lib::mechanics::kSpectraSourceDocumentKey,lib::mechanics::writeSpectraSourceDocument(fx));
                    candidate.extra.set("components",components);
                    ws_.map.beginEdit();ws_.map.data.entities[size_t(index)]=std::move(candidate);ws_.map.touched();
                    reply.set("result",snapshot());return reply;
                }
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
                if(componentId=="Transform"||componentId=="SurfacePlacement"||componentId=="Unit"||componentId=="NereidWater")
                    throw std::runtime_error(componentId+" cannot be removed");
                // Deleting a target component must not silently leave a
                // linked World Process referencing the same stable map ID.
                if(!source.id.empty()){
                    for(const auto& other:ws_.map.data.entities)
                        for(const char* kind:{"EchoPortal","EchoEmitter","EchoReceiver",
                                            "FractureBond","AetherDuct","AetherFlux",
                                            "MorphSpring","MorphForce","CrowdPreference",
                                            "ImprintLink","ImprintDeposit","ImprintTracker","CircuitWire","CircuitGate","CircuitSwitch","CircuitDevice",
                                            "GranularSource","EvolvePressure","SocietyContact","SocietyMessage","SocietyNpcTrigger"}){
                            const auto& linkedDoc=other.extra["components"][kind];
                            if(!linkedDoc.isObject())continue;
                            for(const auto& [key,value]:linkedDoc.members()){
                                const auto target=lib::worldprocess::extendedLinks::supports(kind)?
                                    lib::worldprocess::extendedLinks::referenceTarget(kind,key):
                                    lib::worldprocess::linked::isReference(kind,key)?
                                    lib::worldprocess::linked::targetType(kind):std::string_view{};
                                if(target==componentId&&value.isString()&&
                                   value.str()==source.id)
                                    throw std::runtime_error(
                                        "This component is referenced by "+std::string(kind)+
                                        "; clear its link before removal");
                            }
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
            editor::MapDoc::CompoundEdit batch(ws_.map);
            try {
                for(const auto& [id,values]:edits) for(const auto& [key,value]:values.members()) {
                    Json nested=Json::object(),input=Json::object();
                    nested.set("id",request["id"]);nested.set("method",str("studio.setProperty"));
                    input.set("entityId",str(id));input.set("key",str(key));input.set("value",value);
                    nested.set("params",input);
                    const auto updated=dispatchImpl(nested);
                    if(updated.has("error"))throw std::runtime_error(updated.getStr("error"));
                }
            } catch(...) { throw; }
            batch.commit();
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
            editor::MapDoc::CompoundEdit batch(ws_.map);
            if(params.has("expectedRevision")){
                if(!params["expectedRevision"].isNumber()||params["expectedRevision"].integer(-1)!=int64_t(ws_.map.revision))throw std::runtime_error("Stale transform revision");
                for(const auto& id:targets)if(locked_.contains(id)||hidden_.contains(id))throw std::runtime_error("Transform requires editable entity");
            }
            const std::string componentId=params.getStr("componentId");
            if(componentId=="BoxCollider" || componentId=="OrientedBoxCollider"){
                if(!ws_.map.data.worldSpace || !sameDocumentScope(params["scope"],graphDocumentScope()))
                    throw std::runtime_error("Map changed. Reload collider properties.");
                Json staged=ws_.map.toJson();
                auto& colliders=staged.member("colliders");
                std::set<std::string> remaining(targets.begin(),targets.end());
                if(remaining.size()!=targets.size())throw std::runtime_error("Duplicate collider target.");
                for(size_t i=0;i<colliders.size();++i){
                    const auto id=nodeId(ws_.map,editor::DocumentEntityRef::fpsCollider(i));
                    if(!remaining.erase(id))continue;
                    auto& box=colliders.at(i);
                    if(locked_.contains(id) || hidden_.contains(id))throw std::runtime_error("Collider is locked or hidden.");
                    if(componentId=="OrientedBoxCollider"){
                        editOrientedCollider(box,values);
                        continue;
                    }
                    if(box.has("yaw") ||
                       box["min"].size()!=3 || box["max"].size()!=3)
                        throw std::runtime_error("Select an editable axis-aligned box.");
                    for(const auto& [key,value]:values.members()){
                        const auto axis=key.size()==4?std::string_view("XYZ").find(key.back()):std::string_view::npos;
                        const auto bound=key.substr(0,3);
                        if((bound!="min" && bound!="max") || axis==std::string_view::npos || !value.isNumber())
                            throw std::runtime_error("Invalid collider coordinate.");
                        box.member(bound).at(axis)=value;
                    }
                    for(size_t axis=0;axis<3;++axis)
                        if(box["min"][axis].toDouble()>=box["max"][axis].toDouble())
                            throw std::runtime_error("Each minimum must be less than its maximum.");
                }
                if(!remaining.empty())throw std::runtime_error("Collider no longer exists.");
                if(staged!=ws_.map.toJson()){
                    Json args=Json::object();args.set("map",std::move(staged));
                    const auto applied=ws_.call("actor_set",args,"studio");
                    if(applied.has("error"))throw std::runtime_error(applied.getStr("error"));
                }
            }else if(lib::worldprocess::linked::supports(componentId) ||
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
                        if(!(lib::worldprocess::extendedLinks::supports(componentId)?
                          lib::worldprocess::extendedLinks::validValue(componentId,key,value):
                          lib::worldprocess::linked::validValue(componentId,key,value)) ||
                           !validWorldProcessDocumentRef(ws_.map,componentId,key,value))
                            throw std::runtime_error("Invalid linked map reference or value: "+key);
                        edited.set(key,value);
                    }
                    std::string error;
                    if(!(lib::worldprocess::extendedLinks::supports(componentId)?
                          lib::worldprocess::extendedLinks::validate(componentId,edited,&error):
                          lib::worldprocess::linked::validate(componentId,edited,&error)))
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
                } catch(...) { throw; }
            }
            batch.commit();
            reply.set("result",snapshot());
        } else if (method == "studio.setProperty") {
            const std::string entityId = params.getStr("entityId");
            // A pinned Inspector may edit its own explicitly addressed entity while
            // the global selection points elsewhere. Both document branches below
            // validate the target ID against the current project before writing.
            if (entityId.empty()) throw std::runtime_error("Missing document entity ID");
            const std::string key = params.getStr("key");
            const Json& value = params["value"];
            if(params.getStr("componentId")=="SpectraSource"){
                const int index=indexFor(entityId);
                auto candidate=ws_.map.data.entities.at(size_t(index));
                auto components=candidate.extra["components"];
                auto doc=components["SpectraSource"];
                if(!doc.isObject())throw std::runtime_error("VFX Renderer is not on this entity");
                if(key.rfind("parameter:",0)==0){
                    const auto name=key.substr(10);
                    const auto graph=requireStudioSpectraGraph(ws_,doc.getStr("effect"));
                    const auto schema=studioSpectraParameterSchema(graph);
                    const auto def=schema.find(name);
                    if(def==schema.end()||!value.isNumber()||
                       !spectra::checkInstanceParameterValue(def->second,float(value.toDouble())))
                        throw std::runtime_error("Invalid SpectraFX instance parameter");
                    auto overrides=doc["parameters"].isObject()?doc["parameters"]:Json::object();
                    if(overrides[name]==value){reply.set("result",snapshot());return reply;}
                    overrides.set(name,value);doc.set("parameters",overrides);
                }else{
                    if(key!="effect"&&key!="scale"&&key!="enabled"&&key!="killOnDetach")
                        throw std::runtime_error("Unknown VFX Renderer property");
                    if(doc[key]==value){reply.set("result",snapshot());return reply;}
                    doc.set(key,value);
                    if(key=="effect")doc.erase("parameters");
                }
                auto source=lib::mechanics::readSpectraSourceDocument(doc);
                if(key=="effect"&&!source.effect.empty())requireStudioSpectraGraph(ws_,source.effect);
                if(source.enabled&&source.effect.empty())throw std::runtime_error("Choose an effect before enabling");
                if(source.revision==UINT32_MAX)throw std::runtime_error("VFX revision exhausted");
                ++source.revision;
                components.set("SpectraSource",lib::mechanics::writeSpectraSourceDocument(source));
                candidate.extra.set("components",components);
                ws_.map.beginEdit();ws_.map.data.entities[size_t(index)]=std::move(candidate);ws_.map.touched();
                reply.set("result",snapshot());return reply;
            }
            const std::string componentId=params.getStr("componentId");
            if(componentId=="SurfacePlacement"){
                if(params.has("expectedRevision")&&(!params["expectedRevision"].isNumber()||params["expectedRevision"].integer(-1)!=int64_t(ws_.map.revision)))throw std::runtime_error("Stale surface placement revision");
                if(!ws_.map.data.planet()||entityId.compare(0,7,"entity:")!=0||
                   locked_.contains(entityId)||hidden_.contains(entityId))
                    throw std::runtime_error("Planet placement requires an editable planet entity");
                const int index=indexFor(entityId);
                auto candidate=ws_.map.data.entities[size_t(index)];
                Json at=candidate.at;
                if(!at.isObject())throw std::runtime_error("Missing canonical planet placement");
                if(key=="placement"){
                    if(!value.isObject()||!value["body"].isString()||!value["face"].isNumber()||
                       value["face"].fx()!=Fixed::fromInt(value["face"].integer())||
                       value["face"].integer(-1)<0||value["face"].integer(-1)>5)
                        throw std::runtime_error("Invalid complete planet placement");
                    for(const char* axis:{"body","face","x","y"})at.set(axis,value[axis]);
                }else if(key=="body"){
                    if(!value.isString())throw std::runtime_error("Expected planet body ID");
                    at.set(key,value);
                }else if(key=="face"){
                    if(!value.isNumber()||value.integer(-1)<0||value.integer(-1)>5||
                       value.fx()!=Fixed::fromInt(value.integer()))
                        throw std::runtime_error("Expected cube face integer 0..5");
                    at.set(key,value);
                }else if(key=="x"||key=="y"){
                    if(!value.isNumber()||!std::isfinite(value.toDouble()))
                        throw std::runtime_error("Expected finite face coordinate");
                    at.set(key,value);
                }else throw std::runtime_error("Unknown planet placement property");
                const int bi=ws_.map.data.findBody(at.getStr("body"));
                if(bi<0||!ws_.map.data.bodies[size_t(bi)].surface())
                    throw std::runtime_error("Unknown body or body has no surface");
                const double n=ws_.map.data.bodies[size_t(bi)].faceSize;
                for(const char* axis:{"x","y"}){
                    const double v=at[axis].toDouble(NAN);
                    if(!at[axis].isNumber()||!std::isfinite(v))
                        throw std::runtime_error("Missing finite face coordinate");
                    if(key=="body"&&(v<0||v>n))at.set(axis,Json::number(std::clamp(v,0.,n),4));
                    else if(v<0||v>n)throw std::runtime_error("Face coordinate outside body grid");
                }
                if(at.getInt("face",-1)<0||at.getInt("face",-1)>5)
                    throw std::runtime_error("Invalid cube face");
                if(at!=candidate.at){
                    candidate.at=std::move(at);
                    candidate.x=float(candidate.at["x"].toDouble());
                    candidate.z=float(candidate.at["y"].toDouble());
                    ws_.map.beginEdit();
                    ws_.map.data.entities[size_t(index)]=std::move(candidate);
                    ws_.map.touched();
                }
                reply.set("result",snapshot());return reply;
            }
            if(lib::worldprocess::authoring::supports(componentId) ||
               lib::worldprocess::linked::supports(componentId) ||
               lib::worldprocess::extended::supports(componentId) ||
               lib::worldprocess::extendedLinks::supports(componentId)) {
                if(entityId.compare(0,7,"entity:")!=0)
                    throw std::runtime_error("Authoring applies only to map entities");
                const int index=indexFor(entityId);
                const auto& source=ws_.map.data.entities[size_t(index)];
                if(ws_.map.data.planet()&&(componentId=="CrowdAgent"||componentId=="CrowdSlot")&&key!="enabled")
                    throw std::runtime_error("Planet CROWD uses canonical placement and native movement; planar values are inactive");
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
                    if(componentId=="GranularField"&&key=="grid"){
                        // A single validated full-grid replacement is one MapDoc
                        // Undo step; clients may use GranularDocument::resize/
                        // paint to assemble the grid before committing.
                        if(!value.isString()||value.str().size()>1024*1024)
                            throw std::runtime_error("Invalid GRANULAR grid document payload");
                        staged=Json::parse(value.str());
                        if(!lib::worldprocess::extended::validate(componentId,staged,&error))
                            throw std::runtime_error(error);
                    }else if(!lib::worldprocess::extended::edit(componentId,key,value,staged,&error))
                        throw std::runtime_error(error);
                } else if(!lib::worldprocess::authoring::edit(componentId,key,value,staged,&error))
                    throw std::runtime_error(error);
                if(ws_.map.data.planet()&&componentId=="EvolveArea"){
                    evolve::Area local{staged.getInt("offsetXcm",0),staged.getInt("offsetZcm",0),
                        staged.getInt("halfWidthCm"),staged.getInt("halfDepthCm"),staged.getBool("enabled")};
                    if(!evolve::valid(local))throw std::runtime_error("Planet EVOLVE local bounds exceed coordinate budget");
                }
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
