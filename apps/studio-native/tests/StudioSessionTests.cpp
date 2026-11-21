#include "apps/studio-native/StudioSession.h"
#include "apps/editor/AuthoringComponents.h"
#include "tests/TestCheck.h"
#include "apps/studio-native/StudioGameLaunch.h"
#include "apps/studio-native/StudioGameInput.h"
#include "apps/studio-native/StudioGameDevice.h"
#include "apps/studio-native/StudioRuntimeInspect.h"
#include "apps/studio-native/StudioMoveJournal.h"
#include <iostream>
#include <set>
#include <filesystem>
#include <fstream>
#include <chrono>

namespace {
void projectFileRecoveryWorkflow() {
    using namespace aiverse;
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("studio-open-recovery-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto project=root/"templates"/"fixture";
    fs::create_directories(project/"content"/"A");
    fs::create_directories(project/"content"/"B");
    fs::create_directories(project/".aiverse-cache");
    std::ofstream(project/"game.json") << R"({"name":"Recovery fixture","module":"rts","rules":"rules.json"})";
    std::ofstream(project/"rules.json") << R"({"types":{"unit":{"health":{"max":100}}}})";
    std::ofstream(project/"Fixture.aiverse") << R"({"format":1,"name":"Recovery fixture","manifest":"game.json"})";
    const auto original=project/"content"/"A"/"sample.txt";
    const auto moved=project/"content"/"B"/"sample.txt";
    std::ofstream(original) << "persistent bytes";
    auto simulateCrash=[&] {
        studio::StudioMoveJournal journal(project);
        journal.begin({{"A/sample.txt","B/sample.txt"}});
        fs::rename(original,moved);
    };
    const std::vector<std::string> inputs={project.string(),(project/"game.json").string(),
        (project/"Fixture.aiverse").string(),"fixture"};
    for (const auto& input:inputs) {
        simulateCrash();
        studio::StudioSession session(root.string(),input);
        AIVERSE_TEST_CHECK(fs::exists(original) && !fs::exists(moved));
        AIVERSE_TEST_CHECK(!fs::exists(project/".aiverse-cache"/"studio-move-journal"));
        simulateCrash();
        session.openProject(input);
        AIVERSE_TEST_CHECK(fs::exists(original) && !fs::exists(moved));
        AIVERSE_TEST_CHECK(!fs::exists(project/".aiverse-cache"/"studio-move-journal"));
    }
    fs::remove_all(root);
}
void authoredEntityWorkflow() {
   using namespace aiverse;namespace fs=std::filesystem;
   const auto root=fs::temp_directory_path()/("studio-authored-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
   const auto project=root/"fixture";fs::create_directories(project/"maps");
   std::ofstream(project/"game.json")<<R"({"module":"rts","rules":"rules.json","maps":[{"file":"maps/scene.json"}]})";
   std::ofstream(project/"rules.json")<<R"({"types":{"tree":{"health":{"max":100}}}})";
   editor::MapDoc map;map.reset(16,16);editor::DocEntity e;e.id="first";e.type="tree";e.x=2;e.z=3;
   e.extra=Json::parse(R"({"components":{"Transform":{"y":4,"yaw":0,"scale":1},"StaticModel":{"asset":"content/tree.gltf"}}})");map.data.entities.push_back(e);e.id="second";map.data.entities.push_back(e);
   AIVERSE_TEST_CHECK(map.save((project/"maps/scene.json").string(),nullptr));
   studio::StudioSession session(root.string(),project.string());
   auto call=[&](const char* method,const char* params="{}") {auto request=Json::object();request.set("id",Json::number(int64_t(1)));request.set("method",Json::string(method));request.set("params",Json::parse(params));return session.dispatch(request);};
   AIVERSE_TEST_CHECK(!call("studio.select",R"({"id":"entity:@second"})").has("error"));
   AIVERSE_TEST_CHECK(!call("studio.setProperty",R"({"entityId":"entity:@second","key":"y","value":9})").has("error"));
   AIVERSE_TEST_CHECK(session.workspace().map.data.entities[1].extra["components"]["Transform"]["y"].integer()==9);
   AIVERSE_TEST_CHECK(!call("studio.undo").has("error"));AIVERSE_TEST_CHECK(session.workspace().map.data.entities[1].extra["components"]["Transform"]["y"].integer()==4);
   AIVERSE_TEST_CHECK(!call("studio.select",R"({"id":"entity:@first"})").has("error"));
   AIVERSE_TEST_CHECK(!call("studio.entityDelete",R"({"entityId":"entity:@first"})").has("error"));
   AIVERSE_TEST_CHECK(!call("studio.select",R"({"id":"entity:@second"})").has("error"));AIVERSE_TEST_CHECK(session.selectedEntityIndex()==0);
   AIVERSE_TEST_CHECK(!call("studio.entityDuplicate",R"({"entityId":"entity:@second"})").has("error"));
   AIVERSE_TEST_CHECK(session.selectedObjectId().rfind("entity:@",0)==0 && session.selectedObjectId()!="entity:@second");
   AIVERSE_TEST_CHECK(!call("studio.save").has("error"));session.openProject(project.string());
   AIVERSE_TEST_CHECK(session.workspace().map.data.entities[1].extra["components"]["StaticModel"].getStr("asset")=="content/tree.gltf");
   // Inspector exposes entity components; Mesh Renderer edits StaticModel with undo.
   fs::create_directories(project/"content");std::ofstream(project/"content/rock.glb")<<"glb";
   std::ofstream(project/"materials.json")<<R"({"materials":{"stone":{"color":[0.5,0.5,0.5]}}})";
   auto inspect=call("studio.inspect",R"({"entityId":"entity:@second"})");
   AIVERSE_TEST_CHECK(!inspect.has("error"));
   bool mesh=false,transform=false;
   for(const auto& c:inspect["result"]["components"].items()){mesh|=c.getStr("id")=="MeshRenderer";transform|=c.getStr("id")=="Transform";}
   AIVERSE_TEST_CHECK(mesh && transform);
   AIVERSE_TEST_CHECK(!call("studio.setProperty",R"({"entityId":"entity:@second","componentId":"MeshRenderer","key":"asset","value":"content/rock.glb"})").has("error"));
   AIVERSE_TEST_CHECK(call("studio.setProperty",R"({"entityId":"entity:@second","componentId":"MeshRenderer","key":"asset","value":"content/missing.glb"})").has("error"));
   AIVERSE_TEST_CHECK(call("studio.setProperty",R"({"entityId":"entity:@second","componentId":"MeshRenderer","key":"asset","value":"../rules.json"})").has("error"));
   AIVERSE_TEST_CHECK(call("studio.setProperty",R"({"entityId":"entity:@second","componentId":"MeshRenderer","key":"material:*","value":"nope"})").has("error"));
   AIVERSE_TEST_CHECK(!call("studio.setProperty",R"({"entityId":"entity:@second","componentId":"MeshRenderer","key":"material:*","value":"stone"})").has("error"));
   {
       const int second=session.workspace().map.findEntity("second");
       const auto& model=session.workspace().map.data.entities[size_t(second)].extra["components"]["StaticModel"];
       AIVERSE_TEST_CHECK(model.getStr("asset")=="content/rock.glb" && model["materials"].getStr("*")=="stone");
   }
   AIVERSE_TEST_CHECK(!call("studio.undo").has("error"));
   AIVERSE_TEST_CHECK(!session.workspace().map.data.entities[size_t(session.workspace().map.findEntity("second"))].extra["components"]["StaticModel"].has("materials"));
   // Built-in primitives: builtin:sphere is stored, builtin:cube clears to the default cube.
   AIVERSE_TEST_CHECK(!call("studio.setProperty",R"({"entityId":"entity:@second","componentId":"MeshRenderer","key":"asset","value":"builtin:sphere"})").has("error"));
   AIVERSE_TEST_CHECK(session.workspace().map.data.entities[size_t(session.workspace().map.findEntity("second"))].extra["components"]["StaticModel"].getStr("asset")=="builtin:sphere");
   AIVERSE_TEST_CHECK(!call("studio.setProperty",R"({"entityId":"entity:@second","componentId":"MeshRenderer","key":"asset","value":"builtin:cube"})").has("error"));
   {
       auto mesh=call("studio.inspect",R"({"entityId":"entity:@second"})");
       std::string shown;
       for(const auto& c:mesh["result"]["components"].items())if(c.getStr("id")=="MeshRenderer")
           for(const auto& f:c["properties"].items())if(f.getStr("key")=="asset")shown=f.getStr("value");
       AIVERSE_TEST_CHECK(shown=="builtin:cube");
   }
   // Batch edit (Inspector paste / reset): one undo step; a failing value rolls everything back.
   {
       const auto entity=[&]()->const editor::DocEntity&{return session.workspace().map.data.entities[size_t(session.workspace().map.findEntity("second"))];};
       AIVERSE_TEST_CHECK(!call("studio.setProperties",R"({"entityId":"entity:@second","componentId":"Transform","values":{"rotationX":15,"scaleY":2,"y":3}})").has("error"));
       AIVERSE_TEST_CHECK(editor::authoredRotationDegrees(entity())[0]==15 && editor::authoredScale3(entity())[1]==2);
       AIVERSE_TEST_CHECK(!call("studio.undo").has("error"));
       AIVERSE_TEST_CHECK(editor::authoredRotationDegrees(entity())[0]==0 && editor::authoredScale3(entity())[1]==1);
       const auto before=entity().extra.dump();
       AIVERSE_TEST_CHECK(call("studio.setProperties",R"({"entityId":"entity:@second","componentId":"Transform","values":{"rotationX":15,"scaleY":0}})").has("error"));
       AIVERSE_TEST_CHECK(entity().extra.dump()==before);
       // Component catalog, remove and re-add the Mesh Renderer, each undoable.
       const auto catalog=call("studio.componentCatalog",R"({"entityId":"entity:@second"})");
       bool meshListed=false;for(const auto& c:catalog["result"].items())if(c.getStr("id")=="MeshRenderer")meshListed=!c.getBool("available");
       AIVERSE_TEST_CHECK(meshListed);
       AIVERSE_TEST_CHECK(call("studio.componentRemove",R"({"entityId":"entity:@second","componentId":"Transform"})").has("error"));
       AIVERSE_TEST_CHECK(!call("studio.componentRemove",R"({"entityId":"entity:@second","componentId":"MeshRenderer"})").has("error"));
       AIVERSE_TEST_CHECK(!entity().extra["components"].has("StaticModel"));
       AIVERSE_TEST_CHECK(!call("studio.componentAdd",R"({"entityId":"entity:@second","componentId":"MeshRenderer"})").has("error"));
       AIVERSE_TEST_CHECK(entity().extra["components"]["StaticModel"].isObject());
       AIVERSE_TEST_CHECK(call("studio.componentAdd",R"({"entityId":"entity:@second","componentId":"MeshRenderer"})").has("error"));
       AIVERSE_TEST_CHECK(!call("studio.undo").has("error") && !call("studio.undo").has("error"));
       AIVERSE_TEST_CHECK(entity().extra["components"].has("StaticModel"));
   }
   // Multi-selection, group edit / duplicate / delete (one undo each), editor-only flags.
   {
       auto& map=session.workspace().map;
       const size_t before=map.data.entities.size();
       AIVERSE_TEST_CHECK(!call("studio.select",R"({"id":"entity:@second"})").has("error"));
       const std::string other=map.data.entities[0].id=="second"?map.data.entities[1].id:map.data.entities[0].id;
       const std::string otherId="entity:@"+other;
       AIVERSE_TEST_CHECK(!call("studio.select",("{\"id\":\""+otherId+"\",\"mode\":\"add\"}").c_str()).has("error"));
       AIVERSE_TEST_CHECK(session.selectedObjectIds().size()==2 && session.selectedObjectId()==otherId);
       auto snap=call("studio.snapshot");
       AIVERSE_TEST_CHECK(snap["result"]["selectedIds"].size()==2);
       AIVERSE_TEST_CHECK(!call("studio.select",("{\"id\":\""+otherId+"\",\"mode\":\"toggle\"}").c_str()).has("error"));
       AIVERSE_TEST_CHECK(session.selectedObjectIds().size()==1 && session.selectedObjectId()=="entity:@second");
       AIVERSE_TEST_CHECK(call("studio.select",R"({"ids":["entity:@nope"]})").has("error"));
       const std::string both="{\"entityIds\":[\"entity:@second\",\""+otherId+"\"]";
       AIVERSE_TEST_CHECK(!call("studio.setProperties",(both+",\"componentId\":\"Transform\",\"values\":{\"scaleX\":3}}").c_str()).has("error"));
       for(const auto& e:map.data.entities)AIVERSE_TEST_CHECK(editor::authoredScale3(e)[0]==3);
       AIVERSE_TEST_CHECK(!call("studio.undo").has("error"));
       for(const auto& e:map.data.entities)AIVERSE_TEST_CHECK(editor::authoredScale3(e)[0]!=3);
       AIVERSE_TEST_CHECK(!call("studio.entitiesDuplicate",(both+"}").c_str()).has("error"));
       AIVERSE_TEST_CHECK(map.data.entities.size()==before+2 && session.selectedObjectIds().size()==2);
       AIVERSE_TEST_CHECK(!call("studio.undo").has("error") && map.data.entities.size()==before);
       AIVERSE_TEST_CHECK(!call("studio.entitiesDelete",(both+"}").c_str()).has("error"));
       AIVERSE_TEST_CHECK(map.data.entities.size()==before-2 && session.selectedObjectIds().empty());
       AIVERSE_TEST_CHECK(!call("studio.undo").has("error") && map.data.entities.size()==before);
       AIVERSE_TEST_CHECK(!call("studio.entityFlags",R"({"entityIds":["entity:@second"],"hidden":true,"locked":true})").has("error"));
       AIVERSE_TEST_CHECK(session.hiddenIds().count("entity:@second") && session.lockedIds().count("entity:@second"));
       snap=call("studio.snapshot");
       for(const auto& row:snap["result"]["scene"]["entities"].items())
           if(row.getStr("id")=="entity:@second")AIVERSE_TEST_CHECK(!row.getBool("visible",true) && row.getBool("locked"));
       AIVERSE_TEST_CHECK(!call("studio.entityFlags",R"({"entityIds":["entity:@second"],"hidden":false})").has("error") && !session.hiddenIds().count("entity:@second"));
       // Asset drop needs a scenery type or a selected entity type; here the selection supplies "tree".
       AIVERSE_TEST_CHECK(!call("studio.select",R"({"id":"entity:@second"})").has("error"));
       AIVERSE_TEST_CHECK(!call("studio.assetPlace",R"({"asset":"content/rock.glb","x":5,"z":6})").has("error"));
       AIVERSE_TEST_CHECK(map.data.entities.size()==before+1 && map.data.entities.back().extra["components"]["StaticModel"].getStr("asset")=="content/rock.glb");
       AIVERSE_TEST_CHECK(call("studio.assetPlace",R"({"asset":"content/missing.glb","x":5,"z":6})").has("error") && map.data.entities.size()==before+1);
       AIVERSE_TEST_CHECK(!call("studio.undo").has("error") && map.data.entities.size()==before);
   }
   // Rules form: read own + resolved types, edit a type field (validated and saved), reject other paths.
   {
       auto rules=call("studio.rules");
       AIVERSE_TEST_CHECK(!rules.has("error") && rules["result"]["types"].has("tree"));
       AIVERSE_TEST_CHECK(!call("studio.rulesSet",R"({"path":"types.tree.health.max","value":150})").has("error"));
       rules=call("studio.rules");
       AIVERSE_TEST_CHECK(rules["result"]["resolved"]["tree"]["health"]["max"].integer()==150);
       AIVERSE_TEST_CHECK(call("studio.rulesSet",R"({"path":"resources.gold","value":1})").has("error"));
       AIVERSE_TEST_CHECK(!call("studio.rulesSet",R"({"path":"types.tree.health.max","value":null})").has("error"));
       AIVERSE_TEST_CHECK(!call("studio.rules")["result"]["types"]["tree"]["health"].has("max"));
   }
   // Build panel: targets and identity; unknown targets/configs rejected; a project with unsaved
   // changes is never built (no implicit save); no estimate yet is an error, not an empty report.
   {
       const auto info=call("studio.buildInfo");
       AIVERSE_TEST_CHECK(!info.has("error") && info["result"]["targets"].size()==6 && info["result"].has("identity"));
       AIVERSE_TEST_CHECK(call("studio.buildToolchain",R"({"target":"amiga"})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.buildStart",R"({"target":"amiga","config":"Release"})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.buildStart",R"({"target":"web","config":"Fast"})").has("error"));
       auto& buildDoc=const_cast<editor::Workspace&>(session.workspace()).map;
       const bool wasDirty=buildDoc.dirty;buildDoc.dirty=true;
       const auto dirty=call("studio.buildStart",R"({"target":"web","config":"Release"})");
       AIVERSE_TEST_CHECK(dirty.has("error") && dirty.getStr("error").find("Save")!=std::string::npos);
       AIVERSE_TEST_CHECK(!session.workspace().building());
       buildDoc.dirty=wasDirty;
       AIVERSE_TEST_CHECK(call("studio.buildEstimate",R"({"target":"ios"})").has("error"));
       const auto log=call("studio.buildLog",R"({"from":999999})");
       AIVERSE_TEST_CHECK(!log.has("error") && log["result"]["text"].str().empty());
       AIVERSE_TEST_CHECK(log["result"]["next"].integer()==log["result"]["size"].integer());
   }
   // Hierarchy reorder keeps road endpoints attached to the same entities.
   {
       auto& doc=const_cast<editor::Workspace&>(session.workspace()).map;
       doc.data.roads={{0,1}};
       const std::string a=doc.data.entities[0].id,b=doc.data.entities[1].id;
       auto params=Json::object();params.set("entityId",Json::string("entity:@"+a));
       auto request=Json::object();request.set("id",Json::number(int64_t(1)));request.set("method",Json::string("studio.entityMove"));request.set("params",params);
       AIVERSE_TEST_CHECK(!session.dispatch(request).has("error"));
       AIVERSE_TEST_CHECK(doc.data.entities[0].id==b && doc.data.entities[1].id==a);
       AIVERSE_TEST_CHECK(doc.data.roads.size()==1 && doc.data.entities[size_t(doc.data.roads[0].first)].id==a &&
                          doc.data.entities[size_t(doc.data.roads[0].second)].id==b);
   }
   fs::remove_all(root);
}
void entityWorkflow() {
    using namespace aiverse;
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("studio-entities-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto project=root/"templates"/"fixture";
    fs::create_directories(project/"maps");
    std::ofstream(project/"game.json") << R"({"name":"Studio fixture","module":"rts","rules":"rules.json","maps":[{"file":"maps/scene.json"}]})";
    std::ofstream(project/"rules.json") << R"({"types":{"unit":{"health":{"max":100}}}})";
    editor::MapDoc map;
    map.reset(16,16);
    editor::DocEntity entity; entity.type="unit";entity.id="original";entity.x=2;entity.z=3;
    entity.extra=Json::parse(R"({"inventory":{"wood":20}})");
    map.data.extra.set("nereidWater",Json::parse(R"({"surfaces":[{"id":7,"kind":"river","shape":"river","surfaceY":2,"bottomY":0,"points":[[0,0,3],[0,16,3]]}]})"));
    map.data.entities.push_back(entity);
    entity.id="other";entity.x=5;map.data.entities.push_back(entity);
    map.data.roads.emplace_back(0,1);
    AIVERSE_TEST_CHECK(map.save((project/"maps/scene.json").string(),nullptr));
    {
        studio::StudioSession session(root.string(),project.string());
        auto call=[&](const char* method,Json params=Json::object()) {
            Json request=Json::object();request.set("id",Json::number(int64_t(1)));
            request.set("method",Json::string(method));request.set("params",std::move(params));
            return session.dispatch(request);
        };
        {
            // Every map entity exposes a full 3D Transform; the first Y/rotation/scale
            // edit of a legacy unit writes components.Transform and keeps facing in sync.
            auto params=Json::object();params.set("entityId",Json::string("entity:0"));
            auto inspect=call("studio.inspect",params);
            AIVERSE_TEST_CHECK(!inspect.has("error"));
            std::set<std::string> keys;
            for(const auto& c:inspect["result"]["components"].items())
                if(c.getStr("id")=="Transform")for(const auto& f:c["properties"].items())
                    if(f.getBool("editable",true))keys.insert(f.getStr("key"));
            for(const char* key:{"x","y","z","rotationX","rotationY","rotationZ","scaleX","scaleY","scaleZ"})
                AIVERSE_TEST_CHECK(keys.count(key)==1);
            auto set=Json::parse(R"({"entityId":"entity:0","componentId":"Transform","key":"rotationY","value":90})");
            AIVERSE_TEST_CHECK(!call("studio.setProperty",set).has("error"));
            {
                const auto& e=session.workspace().map.data.entities[0];
                AIVERSE_TEST_CHECK(e.extra["components"]["Transform"].isObject());
                AIVERSE_TEST_CHECK(e.extra["facing"].toDouble()==270); // clockwise facing of a CCW 90° yaw
            }
            set=Json::parse(R"({"entityId":"entity:0","key":"rotationX","value":30})");
            AIVERSE_TEST_CHECK(!call("studio.setProperty",set).has("error"));
            set=Json::parse(R"({"entityId":"entity:0","key":"scaleZ","value":2})");
            AIVERSE_TEST_CHECK(!call("studio.setProperty",set).has("error"));
            {
                const auto& t=session.workspace().map.data.entities[0].extra["components"]["Transform"];
                AIVERSE_TEST_CHECK(t["rotation"].size()==3 && t["rotation"][size_t(0)].toDouble()==30 && t["rotation"][size_t(1)].toDouble()==90);
                AIVERSE_TEST_CHECK(t["scale"].size()==3 && t["scale"][size_t(2)].toDouble()==2);
            }
            set=Json::parse(R"({"entityId":"entity:0","key":"scaleX","value":0})");
            AIVERSE_TEST_CHECK(call("studio.setProperty",set).has("error"));
            for(int i=0;i<3;++i)AIVERSE_TEST_CHECK(!call("studio.undo").has("error"));
            AIVERSE_TEST_CHECK(!session.workspace().map.data.entities[0].extra.has("components"));
            AIVERSE_TEST_CHECK(!session.workspace().map.data.entities[0].extra.has("facing"));
            {
            // «Ветер»: session-only preview wind; masks from wind/masks.json.
            fs::create_directories(project/"wind");
            std::ofstream(project/"wind"/"masks.json") << R"({"masks":{"palm_frond":[[0,0],[0.3,0.4],[1,1]]}})";
            const auto before=session.workspace().map.toJson();
            const bool dirtyBefore=session.workspace().map.dirty; // earlier checks may have edited the map
            auto wind=call("studio.wind");
            AIVERSE_TEST_CHECK(!wind.has("error") && wind["result"]["settings"].getBool("enabled"));
            bool frond=false;
            for(const auto& m:wind["result"]["library"]["masks"].items()) frond|=m.getStr("name")=="palm_frond";
            AIVERSE_TEST_CHECK(frond);
            auto set=call("studio.windSet",Json::parse(R"({"speed":12,"direction":90,"debug":"depth"})"));
            AIVERSE_TEST_CHECK(!set.has("error") && set["result"]["settings"].getStr("debug")=="depth");
            AIVERSE_TEST_CHECK(session.windPreview().speed==12.f);
            AIVERSE_TEST_CHECK(call("studio.windSet",Json::parse(R"({"speed":12,"debug":"rainbow"})")).has("error"));
            AIVERSE_TEST_CHECK(session.windPreview().debug==render::wind::WindDebugView::Depth);   // atomic
            AIVERSE_TEST_CHECK(session.workspace().map.toJson()==before && session.workspace().map.dirty==dirtyBefore);
            }
        }
        {
            auto state=call("studio.nereidFlow");AIVERSE_TEST_CHECK(!state.has("error")&&state["result"]["surfaces"].size()==1);
            auto input=Json::parse(R"({"id":7,"mode":"automatic","slopeDegrees":1,"downhillHeadingDegrees":180})");
            AIVERSE_TEST_CHECK(!call("studio.nereidFlowSet",input).has("error"));
            AIVERSE_TEST_CHECK(session.workspace().map.data.extra["nereidWater"]["surfaces"][0]["grade"].has("slopeDegrees"));
            AIVERSE_TEST_CHECK(!call("studio.undo").has("error"));
            AIVERSE_TEST_CHECK(!session.workspace().map.data.extra["nereidWater"]["surfaces"][0].has("grade"));
            AIVERSE_TEST_CHECK(!call("studio.redo").has("error"));
            const auto previous=session.workspace().map.toJson();input.set("slopeDegrees",Json::number(90.0));
            AIVERSE_TEST_CHECK(call("studio.nereidFlowSet",input).has("error"));AIVERSE_TEST_CHECK(session.workspace().map.toJson()==previous);
            AIVERSE_TEST_CHECK(!call("studio.undo").has("error"));
            AIVERSE_TEST_CHECK(!call("studio.save").has("error"));
            session.openProject(project.string());
        }
        {
            Simulation live({1,20,0},std::make_unique<game::RtsModule>());
            live.setup(session.workspace().project.rulesJson,session.workspace().map.toJson());
            studio::StudioRuntimeInspect inspector;
            const auto before=live.checksum();
            auto page=inspector.entities(&live,"run-1",0,1,"");
            AIVERSE_TEST_CHECK(page["active"].boolean() && page["rows"].size()==1 && page["total"].integer()>=2);
            const auto ref=page["rows"][0].getStr("ref");
            const auto detail=inspector.inspect(&live,"run-1","run-1",ref);
            AIVERSE_TEST_CHECK(detail["components"].has("Transform"));
            AIVERSE_TEST_CHECK(live.checksum()==before);
            bool stale=false;try {inspector.inspect(&live,"run-2","run-1",ref);}catch(...){stale=true;}
            AIVERSE_TEST_CHECK(stale);
            AIVERSE_TEST_CHECK(inspector.entities(nullptr,"run-2",0,100,"")["rows"].size()==0);
        }
        const auto plan=studio::studioGameLaunch(session.workspace(),true);
        AIVERSE_TEST_CHECK(plan.map && *plan.map==0);
        AIVERSE_TEST_CHECK(plan.project==session.workspace().project.dir);
        AIVERSE_TEST_CHECK(!studio::studioGameLaunch(session.workspace(),false).map);
        const auto catalog=call("studio.entityCatalog");
        AIVERSE_TEST_CHECK(catalog["result"].getBool("mapEntities"));
        AIVERSE_TEST_CHECK(catalog["result"]["types"].size()==1);
        const auto before=session.workspace().map.toJson();
        AIVERSE_TEST_CHECK(call("studio.entityCreate",Json::parse(R"({"type":"missing","x":1,"z":2,"owner":0})")).has("error"));
        AIVERSE_TEST_CHECK(call("studio.entityCreate",Json::parse(R"({"type":"unit","x":1,"z":2,"owner":0.5})")).has("error"));
        AIVERSE_TEST_CHECK(session.workspace().map.toJson()==before);
        AIVERSE_TEST_CHECK(!session.workspace().map.canUndo());
        const auto created=call("studio.entityCreate",Json::parse(R"({"type":"unit","x":4,"z":6,"owner":0,"count":100})"));
        AIVERSE_TEST_CHECK(!created.has("error"));
        AIVERSE_TEST_CHECK(session.workspace().map.data.entities.size()==3); // unallowlisted count ignored
        AIVERSE_TEST_CHECK(created["result"].getStr("selectedId")=="entity:2");
        AIVERSE_TEST_CHECK(call("studio.entityDelete",Json::parse(R"({"entityId":"entity:0"})")).has("error"));
        AIVERSE_TEST_CHECK(!call("studio.select",Json::parse(R"({"id":"entity:0"})")).has("error"));
        AIVERSE_TEST_CHECK(!call("studio.entityDuplicate",Json::parse(R"({"entityId":"entity:0"})")).has("error"));
        const auto& copy=session.workspace().map.data.entities.back();
        AIVERSE_TEST_CHECK(copy.id!="original" && copy.extra==entity.extra && copy.x==3 && copy.z==4);
        AIVERSE_TEST_CHECK(!call("studio.entityDelete",Json::parse(R"({"entityId":"entity:3"})")).has("error"));
        AIVERSE_TEST_CHECK(session.workspace().map.data.entities.size()==3);
        AIVERSE_TEST_CHECK(!call("studio.undo").has("error"));
        AIVERSE_TEST_CHECK(session.workspace().map.data.entities.size()==4 && session.selectedObjectId().empty());
        AIVERSE_TEST_CHECK(!call("studio.redo").has("error"));
        AIVERSE_TEST_CHECK(session.workspace().map.data.entities.size()==3);
        AIVERSE_TEST_CHECK(!call("studio.save").has("error"));
        AIVERSE_TEST_CHECK(!session.workspace().map.dirty);
        session.openProject(project.string());
        AIVERSE_TEST_CHECK(session.workspace().map.data.entities.size()==3);
        AIVERSE_TEST_CHECK(session.workspace().map.data.entities[2].x==4);
        AIVERSE_TEST_CHECK(!call("studio.select",Json::parse(R"({"id":"entity:0"})")).has("error"));
        AIVERSE_TEST_CHECK(!call("studio.entityDelete",Json::parse(R"({"entityId":"entity:0"})")).has("error"));
        AIVERSE_TEST_CHECK(session.workspace().map.data.roads.empty());
        AIVERSE_TEST_CHECK(!call("studio.undo").has("error"));
        AIVERSE_TEST_CHECK(session.workspace().map.data.roads.size()==1);
        const auto projectState=call("studio.project")["result"];
        AIVERSE_TEST_CHECK(projectState["maps"].size()==1 && projectState.getStr("currentMap")=="maps/scene.json");
        const auto prior=session.workspace().map.toJson();
        AIVERSE_TEST_CHECK(call("studio.newMap",Json::parse(R"({"name":"new","width":0,"height":32})")).has("error"));
        AIVERSE_TEST_CHECK(session.workspace().map.toJson()==prior);
        AIVERSE_TEST_CHECK(call("studio.openMap",Json::parse(R"({"file":"../rules.json"})")).has("error"));
        const auto revision=session.workspace().map.revision;
        AIVERSE_TEST_CHECK(!call("studio.newMap",Json::parse(R"({"name":"New document","width":32,"height":24})")).has("error"));
        AIVERSE_TEST_CHECK(session.workspace().map.data.w==32 && session.workspace().map.dirty);
        AIVERSE_TEST_CHECK(session.workspace().map.path.empty());
        AIVERSE_TEST_CHECK(session.workspace().map.revision>revision);
        bool rejected=false;
        try { session.saveMapAs((project/"game.json").string()); } catch(const std::exception&) { rejected=true; }
        AIVERSE_TEST_CHECK(rejected);
        bool dirtyPlayRejected=false;
        try { (void)studio::studioGameLaunch(session.workspace(),true); }
        catch(const std::exception&) {dirtyPlayRejected=true;}
        AIVERSE_TEST_CHECK(dirtyPlayRejected);
        session.saveMapAs((project/"maps"/"new.json").string());
        AIVERSE_TEST_CHECK(fs::exists(project/"maps"/"new.json") && !session.workspace().map.dirty);
        AIVERSE_TEST_CHECK(call("studio.project")["result"]["maps"].size()==2);
        AIVERSE_TEST_CHECK(!call("studio.openMap",Json::parse(R"({"file":"maps/scene.json"})")).has("error"));
        AIVERSE_TEST_CHECK(session.workspace().map.data.entities.size()==3);
        const auto restored=session.workspace().map.toJson();
        std::ofstream(project/"maps"/"new.json") << "invalid map JSON";
        AIVERSE_TEST_CHECK(call("studio.openMap",Json::parse(R"({"file":"maps/new.json"})")).has("error"));
        AIVERSE_TEST_CHECK(session.workspace().map.toJson()==restored);

    }
    fs::remove_all(root);
}
}

int main() {
    using namespace aiverse;
    aiverse::studio::StudioSession session(".");
    bool playRejected=false;
    try { (void)studio::studioGameLaunch(session.workspace(),false); }
    catch(const std::exception&) {playRejected=true;}
    AIVERSE_TEST_CHECK(playRejected);
    auto phone=studio::StudioGameDevice::parse(Json::parse(R"({"id":"iphone-x","landscape":false,"safeArea":true})"));
    AIVERSE_TEST_CHECK(phone.size(800,600)==std::make_pair(375.f,812.f));
    AIVERSE_TEST_CHECK(phone.insets().top==44 && phone.insets().bottom==34);
    phone.landscape=true;
    AIVERSE_TEST_CHECK(phone.size(800,600)==std::make_pair(812.f,375.f));
    AIVERSE_TEST_CHECK(phone.insets().left==44 && phone.insets().bottom==21);
    phone.safeArea=false;AIVERSE_TEST_CHECK(phone.insets().left==0);
    phone.id="none";AIVERSE_TEST_CHECK(phone.size(800,600)==std::make_pair(800.f,600.f));
    bool badProfile=false;try {studio::StudioGameDevice::parse(Json::parse(R"({"id":"unknown","landscape":true,"safeArea":true})"));}catch(...){badProfile=true;}
    AIVERSE_TEST_CHECK(badProfile);
    AIVERSE_TEST_CHECK(studio::studioGameKey("w")==platform::Key::W);
    AIVERSE_TEST_CHECK(studio::studioGameKey("Escape")==platform::Key::Escape);
    AIVERSE_TEST_CHECK(studio::studioGameKey("ArrowDown")==platform::Key::Down);
    AIVERSE_TEST_CHECK(studio::studioGameKey("not-a-key")==platform::Key::Unknown);
    auto call = [&session](const char* json) { return session.dispatch(Json::parse(json)); };
    const Json snapshot = call(R"({"id":1,"method":"studio.snapshot","params":{}})");
    AIVERSE_TEST_CHECK(snapshot["result"].isObject());
    AIVERSE_TEST_CHECK(snapshot["result"]["scene"]["entities"].isArray());
    AIVERSE_TEST_CHECK(snapshot["result"]["selectedId"].isNull());
    AIVERSE_TEST_CHECK(call(R"({"id":2,"method":"studio.select","params":{"id":"entity:0"}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":3,"method":"workspace.call","params":{}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":4,"method":"studio.setProperty","params":{"entityId":"entity:0","key":"x","value":1}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":5,"method":"studio.select","params":{"id":null}})")["result"]["selectedId"].isNull());
    AIVERSE_TEST_CHECK(call(R"({"id":6,"method":"studio.save","params":{}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":7,"method":"studio.undo","params":{}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":8,"method":"studio.redo","params":{}})").has("error"));
    AIVERSE_TEST_CHECK(session.selectedEntityIndex() == -1);
    AIVERSE_TEST_CHECK(call(R"({"id":21,"method":"studio.materials","params":{}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":22,"method":"studio.materialPatch","params":{"name":"steel","patch":{"metallic":0.5}}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":23,"method":"studio.materialCreate","params":{"name":"new_material","preset":"metal"}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":24,"method":"studio.materialPreview","params":{"name":"steel"}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":25,"method":"studio.shaderGraphs","params":{}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":26,"method":"studio.shaderGraphGet","params":{"name":"../escape"}})").has("error"));
    AIVERSE_TEST_CHECK(session.previewMaterialName().empty());
    AIVERSE_TEST_CHECK(call(R"({"id":29,"method":"studio.shaderGraphCatalog","params":{}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":27,"method":"studio.shaderGraphValidate","params":{"graph":{"domain":"material","nodes":[]}}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":28,"method":"studio.shaderGraphSave","params":{"name":"test","graph":{"domain":"material","nodes":[]}}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":12,"method":"studio.lighting","params":{}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":13,"method":"studio.lightAdd","params":{}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":14,"method":"studio.lightUpdate","params":{"index":0,"key":"intensity","value":-5}})").has("error"));
    AIVERSE_TEST_CHECK(call(R"({"id":15,"method":"studio.lightingSet","params":{"key":"storm","value":2}})").has("error"));
    // Asset listing must never expose a filesystem root without an open project.
    AIVERSE_TEST_CHECK(call(R"({"id":9,"method":"studio.assets","params":{"dir":"../../"}})").has("error"));
    bool openRejected = false;
    try { session.openProject("/nonexistent/aiverse-project"); }
    catch (const std::exception&) { openRejected = true; }
    AIVERSE_TEST_CHECK(openRejected);
    authoredEntityWorkflow();
    entityWorkflow();
    projectFileRecoveryWorkflow();
    std::cout << "studio session contract PASS\n";
}
