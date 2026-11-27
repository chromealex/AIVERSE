#include "apps/editor/core/StudioPlayTools.h"
#include "apps/studio-native/StudioSession.h"
#include "apps/studio-native/StudioWorldProcessesEditing.h"
#include "apps/editor/AuthoringComponents.h"
#include "apps/editor/core/ProjectFileEdits.h"
#include "apps/editor/core/ProjectDocumentJournal.h"
#include "tests/TestCheck.h"
#include "apps/studio-native/StudioGameLaunch.h"
#include "apps/studio-native/StudioNetworkPlay.h"
#include "engine/actor/Project.h"
#include "engine/game/gen/MapGen.h"
#include "apps/studio-native/StudioGameInput.h"
#include "apps/studio-native/StudioGameDevice.h"
#include "apps/studio-native/StudioDeviceMask.h"
#include "apps/studio-native/StudioRuntimeInspect.h"
#include "apps/studio-native/StudioMoveJournal.h"
#include "apps/studio-native/StudioAgentHistoryPolicy.h"
#include "apps/studio-native/StudioAssetCatalog.h"
#include "apps/studio-native/StudioPlanetForestDocument.h"
#include "apps/studio-native/StudioMaterialPublicationJournal.h"
#include "apps/editor/core/ProjectAssetWriteLock.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <set>
#include <filesystem>
#include <fstream>
#include <chrono>

namespace {
void regionOverviewContract(){
    using namespace aiverse;
    editor::Workspace ws;
    ws.map.reset(197,9);
    auto& map=ws.map.data;
    map.regions[map.idx(0,0)]='B';
    map.regions[map.idx(196,8)]='B';
    map.regions[map.idx(195,8)]='A';
    const auto report=ws.call("region_list",Json::parse(R"({"letter":"B"})"));
    AIVERSE_TEST_CHECK(report.getInt("stride")==3 && report["coverage"].size()==3);
    AIVERSE_TEST_CHECK(report["coverage"][0].str().size()==66 && report["coverage"][0].str()[0]=='#');
    AIVERSE_TEST_CHECK(report["coverage"][2].str().back()=='#');
    AIVERSE_TEST_CHECK(report["regions"][0].getInt("cells")==1 && report["regions"][1].getInt("cells")==2);
    AIVERSE_TEST_CHECK(ws.call("region_name",Json::parse(R"({"letter":".","name":"Sea"})")).has("error"));
    AIVERSE_TEST_CHECK(!ws.map.canUndo());
}
void mapCompoundHistoryContract(){
    using namespace aiverse::editor;
    MapDoc map;map.reset(8,8);
    for(int i=0;i<200;++i){map.beginEdit();map.data.name=std::to_string(i);}
    AIVERSE_TEST_CHECK(map.undoDepth()==200);
    const auto oldest=map.undoId(0),mark=map.historyMark();
    map.undo();
    const auto before=map.data.name;const auto redo=map.redoId(0);const bool dirty=map.dirty;
    {
        MapDoc::CompoundEdit batch(map);
        for(int i=0;i<250;++i){map.beginEdit();map.data.name="partial";}
    }
    AIVERSE_TEST_CHECK(map.data.name==before&&map.dirty==dirty&&map.undoDepth()==199&&map.redoId(0)==redo);
    {
        MapDoc::CompoundEdit batch(map);
        for(int i=0;i<250;++i){map.beginEdit();map.data.name="committed";}
        batch.commit();
    }
    AIVERSE_TEST_CHECK(map.undoDepth()==200&&map.redoDepth()==0&&map.historyMark()>mark);
    const auto id=map.undoId(199);
    map.undo();AIVERSE_TEST_CHECK(map.data.name==before&&map.redoId(0)==id);
    map.redo();AIVERSE_TEST_CHECK(map.data.name=="committed"&&map.undoId(199)==id);
    map.beginEdit();map.data.name="trimmed";
    AIVERSE_TEST_CHECK(map.undoDepth()==200&&map.undoId(0)!=oldest&&map.undoId(199)>id);
}
void mapHistoryPersistenceContract(){
    using namespace aiverse;using namespace aiverse::editor;namespace fs=std::filesystem;
    const auto root=fs::canonical(fs::temp_directory_path())/("studio-map-history-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root/"maps");
    const auto file=(root/"maps"/"scene.json").string();
    MapDoc doc;doc.reset(8,8);std::string error;
    DocEntity entity;entity.id="unit";entity.type="unit";entity.x=1.2345678f;entity.z=2.3456789f;
    doc.data.entities.push_back(entity);
    doc.data.regionNames['R']="Ridge";
    doc.data.corners.assign(81,0);doc.data.corners[3]=-321;
    doc.data.detailScale=2;doc.data.detail.assign(17*17,0);doc.data.detail[8]=-1023;
    doc.data.detailSplat.assign(17*17*4,0);doc.data.detailSplat[11]=217;
    doc.data.extra.set("custom",Json::parse(R"({"future":{"id":"test"}})"));
    {
        MapDoc::CompoundEdit stroke(doc);
        doc.beginEdit();doc.data.entities[0].x=5.6789123f;
        doc.beginEdit();doc.data.cells[0]='~';doc.data.corners[3]=999;
        doc.data.detail[8]=2047;doc.data.detailSplat[11]=3;
        stroke.commit();
    }
    doc.setHistoryLabel(doc.undoId(0),"Terrain stroke");
    doc.beginEdit();doc.data.name="Second";
    doc.setHistoryLabel(doc.undoId(1),"Rename map");
    doc.undo();
    const auto epoch=doc.historyEpoch();const auto previousId=doc.undoId(0);
    AIVERSE_TEST_CHECK(doc.save(file,&error)&&doc.historyPersistenceWarning().empty());
    MapDoc reopened;
    AIVERSE_TEST_CHECK(reopened.load(file,&error)&&reopened.historyPersistenceWarning().empty());
    AIVERSE_TEST_CHECK(reopened.data.regionNames.at('R')=="Ridge");
    AIVERSE_TEST_CHECK(reopened.undoDepth()==1&&reopened.redoDepth()==1);
    AIVERSE_TEST_CHECK(reopened.historyEpoch()!=epoch&&reopened.undoId(0)!=previousId);
    AIVERSE_TEST_CHECK(reopened.historyLabel(reopened.undoId(0))=="Terrain stroke");
    AIVERSE_TEST_CHECK(reopened.historyLabel(reopened.redoId(0))=="Rename map");
    reopened.undo();
    AIVERSE_TEST_CHECK(reopened.data.entities[0].x==1.2345678f&&reopened.data.entities[0].z==2.3456789f);
    AIVERSE_TEST_CHECK(reopened.data.cells[0]=='.'&&reopened.data.corners[3]==-321);
    AIVERSE_TEST_CHECK(reopened.data.detail[8]==-1023&&reopened.data.detailSplat[11]==217);
    AIVERSE_TEST_CHECK(reopened.data.extra["custom"]["future"].getStr("id")=="test");
    reopened.redo();reopened.redo();
    AIVERSE_TEST_CHECK(reopened.data.name=="Second");
    AIVERSE_TEST_CHECK(reopened.save(file,&error));
    const auto cache=root/"maps"/".aiverse-cache";
    fs::path checkpoint;
    for(const auto& item:fs::directory_iterator(cache))
        if(item.path().extension()==".json")checkpoint=item.path();
    AIVERSE_TEST_CHECK(!checkpoint.empty());
    const auto saved=readProjectFileBytes(file);
    std::ofstream(file,std::ios::binary|std::ios::app)<<"\n";
    MapDoc changed;
    AIVERSE_TEST_CHECK(changed.load(file,&error)&&!changed.canUndo()&&!changed.historyPersistenceWarning().empty());
    std::ofstream(file,std::ios::binary|std::ios::trunc)<<saved;
    // A dirty marker refuses even a byte-identical saved map.
    const auto marker=cache/(checkpoint.stem().string()+".dirty");
    std::ofstream(marker)<<"interrupted";
    AIVERSE_TEST_CHECK(changed.load(file,&error)&&!changed.canUndo()&&!changed.historyPersistenceWarning().empty());
    fs::remove(marker);
    AIVERSE_TEST_CHECK(changed.load(file,&error)&&changed.canUndo());
    // Save As must not inherit a timeline pointing to the old terrain sidecars.
    const auto copy=(root/"maps"/"copy.json").string();
    AIVERSE_TEST_CHECK(changed.save(copy,&error)&&!changed.canUndo()&&!changed.canRedo());
    AIVERSE_TEST_CHECK(changed.load(copy,&error)&&!changed.canUndo());
    // Planet snapshots carry all six faces, independent detail, extras and file names.
    MapDoc planet;
    AIVERSE_TEST_CHECK(planet.loadJson(Json::parse(R"({"name":"Planet","world":{"bodies":[{"id":"earth","faceSize":4,"kind":"planet"}]}})"),&error));
    auto& body=planet.data.bodies.front();
    body.cells[3]='R';body.corners[5]=-90;
    body.detailScale=1;body.detail.assign(6*5*5,0);body.detail[11]=-123;
    body.faceExtra[4].set("custom",Json::string("retained"));
    planet.beginEdit();planet.data.bodies[0].cells[3]='.';planet.data.bodies[0].detail[11]=222;
    const auto worldFile=(root/"maps"/"planet.json").string();
    AIVERSE_TEST_CHECK(planet.save(worldFile,&error)&&planet.historyPersistenceWarning().empty());
    MapDoc world;AIVERSE_TEST_CHECK(world.load(worldFile,&error)&&world.canUndo());
    world.undo();
    AIVERSE_TEST_CHECK(world.data.bodies[0].cells[3]=='R'&&world.data.bodies[0].corners[5]==-90);
    AIVERSE_TEST_CHECK(world.data.bodies[0].detail[11]==-123&&world.data.bodies[0].faceExtra[4].getStr("custom")=="retained");
    // World-space documents preserve authored extension blocks, too.
    MapDoc authored;
    AIVERSE_TEST_CHECK(authored.loadJson(Json::parse(R"({"name":"Scene","colliders":[],"spawns":[],"custom":{"value":7}})"),&error));
    authored.beginEdit();authored.data.extra.set("custom",Json::parse(R"({"value":8})"));
    const auto authoredFile=(root/"maps"/"authored.json").string();
    AIVERSE_TEST_CHECK(authored.save(authoredFile,&error));
    MapDoc authoredReopen;AIVERSE_TEST_CHECK(authoredReopen.load(authoredFile,&error)&&authoredReopen.canUndo());
    authoredReopen.undo();AIVERSE_TEST_CHECK(authoredReopen.data.worldSpace&&authoredReopen.data.extra["custom"].getInt("value")==7);
    fs::remove_all(root);
}
void projectFileEditContract(){
    using namespace aiverse::editor;namespace fs=std::filesystem;
    const auto root=fs::canonical(fs::temp_directory_path())/("studio-project-files-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    const auto a=root/"a.json",b=root/"b.json";
    std::ofstream(a)<<"before A";std::ofstream(b)<<"before B";
    const std::vector<ProjectFileEdit> edits={{a,"before A","after A"},{b,"before B","after B"}};
    std::ofstream(b)<<"external B";
    bool refused=false;try{applyProjectFileEdits(root,edits);}catch(const std::exception&){refused=true;}
    AIVERSE_TEST_CHECK(refused&&readProjectFileBytes(a)=="before A"&&readProjectFileBytes(b)=="external B");
    std::ofstream(b)<<"before B";
    applyProjectFileEdits(root,edits);
    AIVERSE_TEST_CHECK(readProjectFileBytes(a)=="after A"&&readProjectFileBytes(b)=="after B");
    applyProjectFileEdits(root,reverseProjectFileEdits(edits));
    AIVERSE_TEST_CHECK(readProjectFileBytes(a)=="before A"&&readProjectFileBytes(b)=="before B");
    // Unchanged dependency is still checked; it must not be overwritten.
    std::ofstream(b)<<"external B";
    refused=false;try{applyProjectFileEdits(root,{{a,"before A","after A"},{b,"before B","before B"}});}catch(const std::exception&){refused=true;}
    AIVERSE_TEST_CHECK(refused&&readProjectFileBytes(a)=="before A");
    fs::remove_all(root);
}
void projectDocumentJournalContract(){
    using namespace aiverse::editor;namespace fs=std::filesystem;
    const auto root=fs::canonical(fs::temp_directory_path())/("studio-doc-journal-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    const auto a=root/"game.json",b=root/"rules.json",dep=root/"include.json";
    const std::string beforeA=R"({"name":"Before","rules":"rules.json"})",afterA=R"({"name":"After","rules":"rules.json"})";
    const std::string beforeB=R"({"types":{"unit":{"health":{"max":100}}}})",afterB=R"({"types":{"unit":{"health":{"max":200}}}})";
    std::ofstream(a)<<beforeA;std::ofstream(b)<<beforeB;std::ofstream(dep)<<"dependency";
    const std::vector<ProjectFileEdit> edits={{a,beforeA,afterA},{b,beforeB,afterB},{dep,"dependency","dependency"}};
    const auto folder=root/".aiverse-cache"/"project-document-journal";
    for(size_t count=0;count<=edits.size();++count){
        {
            ProjectDocumentJournal journal(root);AIVERSE_TEST_CHECK(journal.begin(edits));
            for(size_t i=0;i<count;++i)journal.publish(i);
            // Destruction without commit simulates interrupted process publication.
        }
        AIVERSE_TEST_CHECK(recoverProjectFileEdits(root));
        AIVERSE_TEST_CHECK(readProjectFileBytes(a)==beforeA&&readProjectFileBytes(b)==beforeB&&!fs::exists(folder));
        AIVERSE_TEST_CHECK(!recoverProjectFileEdits(root));
    }
    {
        ProjectDocumentJournal journal(root);AIVERSE_TEST_CHECK(journal.begin(edits));journal.publish(0);
    }
    std::ofstream(dep)<<"external";
    bool refused=false;
    try{(void)recoverProjectFileEdits(root);}catch(const std::runtime_error&){refused=true;}
    AIVERSE_TEST_CHECK(refused&&readProjectFileBytes(a)==afterA&&readProjectFileBytes(dep)=="external"&&fs::exists(folder));
    std::ofstream(dep)<<"dependency";
    AIVERSE_TEST_CHECK(recoverProjectFileEdits(root)&&readProjectFileBytes(a)==beforeA);
    {
        ProjectDocumentJournal journal(root);AIVERSE_TEST_CHECK(journal.begin(edits));journal.publish(0);
    }
    std::ofstream(folder/"1.before",std::ios::binary|std::ios::trunc)<<"corrupt backup";
    refused=false;
    try{(void)recoverProjectFileEdits(root);}catch(const std::runtime_error&){refused=true;}
    AIVERSE_TEST_CHECK(refused&&readProjectFileBytes(a)==afterA);
    std::ofstream(folder/"1.before",std::ios::binary|std::ios::trunc)<<beforeB;
    // Project owner recovery happens before parsing manifest/rules.
    Project project;std::string error;
    AIVERSE_TEST_CHECK(project.load(root.string(),&error)&&project.manifest.getStr("name")=="Before");
    AIVERSE_TEST_CHECK(project.rulesJson["types"]["unit"]["health"]["max"].integer()==100);
    {
        ProjectDocumentJournal journal(root);AIVERSE_TEST_CHECK(journal.begin(edits));
        for(size_t i=0;i<edits.size();++i)journal.publish(i);
        // Simulate a durable commit marker left behind before cleanup.
        fs::rename(folder/"pending.json",folder/"committed.json");
    }
    AIVERSE_TEST_CHECK(!recoverProjectFileEdits(root));
    AIVERSE_TEST_CHECK(readProjectFileBytes(a)==afterA&&readProjectFileBytes(b)==afterB&&!fs::exists(folder));
    applyProjectFileEdits(root,reverseProjectFileEdits(edits));
    AIVERSE_TEST_CHECK(readProjectFileBytes(a)==beforeA&&readProjectFileBytes(b)==beforeB);
    ProjectHistory other;
    AIVERSE_TEST_CHECK(project.documentHistory.versionToken()!=other.versionToken());
    fs::remove_all(root);
}
void projectOwnerSaveContract(){
    using namespace aiverse;using namespace aiverse::editor;namespace fs=std::filesystem;
    const auto root=fs::canonical(fs::temp_directory_path())/("studio-project-owner-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    const auto manifest=root/"game.json",rules=root/"rules.json",extra=root/"extra.json";
    const std::string manifestBytes=R"({ "name": "Fixture", "module": "game", "rules": "rules.json" })";
    const std::string rulesBytes=R"({ "include": ["extra.json"], "types": {"unit": {"health":{"max":100}}} })";
    const std::string extraBytes=R"({ "types": {"tree": {"health":{"max":100}}} })";
    std::ofstream(manifest)<<manifestBytes;std::ofstream(rules)<<rulesBytes;std::ofstream(extra)<<extraBytes;
    Workspace ws;std::string error;
    AIVERSE_TEST_CHECK(ws.project.load(root.string(),&error));
    AIVERSE_TEST_CHECK(ws.project.saveRules(&error)&&ws.project.saveManifest(&error));
    AIVERSE_TEST_CHECK(readProjectFileBytes(manifest)==manifestBytes&&readProjectFileBytes(rules)==rulesBytes&&readProjectFileBytes(extra)==extraBytes);
    const auto original=ws.project.rulesJson.dump();
    std::ofstream(extra)<<extraBytes<<"\n";
    const auto edit=Json::parse(R"({"path":"types.unit.health.max","value":200})");
    AIVERSE_TEST_CHECK(ws.call("rules_set",edit).has("error"));
    AIVERSE_TEST_CHECK(ws.project.rulesJson.dump()==original&&!ws.project.rulesDirty&&ws.changes.empty());
    AIVERSE_TEST_CHECK(readProjectFileBytes(rules)==rulesBytes&&readProjectFileBytes(extra)==extraBytes+"\n");
    std::ofstream(extra)<<extraBytes;
    AIVERSE_TEST_CHECK(!ws.call("rules_set",edit).has("error"));
    AIVERSE_TEST_CHECK(ws.project.rulesJson["types"]["unit"]["health"]["max"].integer()==200);
    AIVERSE_TEST_CHECK(readProjectFileBytes(extra)==extraBytes);
    AIVERSE_TEST_CHECK(ws.changes.size()==1);
    std::ofstream(manifest)<<manifestBytes<<"\n";
    AIVERSE_TEST_CHECK(ws.call("set_manifest",Json::parse(R"({"path":"name","value":"New name"})")).has("error"));
    AIVERSE_TEST_CHECK(ws.project.manifest.getStr("name")=="Fixture"&&readProjectFileBytes(manifest)==manifestBytes+"\n");
    auto script=Json::parse(R"({"path":"scripts/main.lua","text":"function on_start() end","register":true,"createOnly":true})");
    const auto existingRules=ws.project.rulesJson;
    AIVERSE_TEST_CHECK(ws.call("script_write",script).has("error"));
    AIVERSE_TEST_CHECK(!fs::exists(root/"scripts/main.lua") && ws.project.rulesJson==existingRules);
    std::ofstream(manifest)<<manifestBytes;
    AIVERSE_TEST_CHECK(!ws.call("script_write",script).has("error"));
    AIVERSE_TEST_CHECK(readProjectFileBytes(root/"scripts/main.lua")==script.getStr("text"));
    AIVERSE_TEST_CHECK(ws.project.rulesJson["scripts"][0].str()=="scripts/main.lua");
    script.set("text",Json::string("-- different"));
    AIVERSE_TEST_CHECK(ws.call("script_write",script).has("error"));
    AIVERSE_TEST_CHECK(readProjectFileBytes(root/"scripts/main.lua")=="function on_start() end");
    script.set("path",Json::string("scripts/view/hud.lua"));
    AIVERSE_TEST_CHECK(!ws.call("script_write",script).has("error"));
    AIVERSE_TEST_CHECK(ws.project.rulesJson["viewScripts"][0].str()=="scripts/view/hud.lua");
    fs::remove_all(root);
}
void projectHistoryContract(){
    using namespace aiverse;using namespace aiverse::editor;namespace fs=std::filesystem;
    const auto root=fs::canonical(fs::temp_directory_path())/("studio-project-history-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    const auto manifest=root/"game.json",rules=root/"rules.json",extra=root/"extra.json";
    const std::string m=R"({ "name": "Fixture", "module": "game", "rules": "rules.json" })";
    const std::string r=R"({ "include": ["extra.json"], "types": {"unit": {"health":{"max":100}}} })";
    const std::string e=R"({ "types": {"tree": {"health":{"max":100}}} })";
    std::ofstream(manifest)<<m;std::ofstream(rules)<<r;std::ofstream(extra)<<e;
    Workspace ws;std::string error;
    AIVERSE_TEST_CHECK(ws.project.load(root.string(),&error));
    ws.map.reset(16,16);ws.map.beginEdit();ws.map.data.name="independent map edit";
    const auto mapMark=ws.map.historyMark(),mapRevision=ws.map.revision;
    auto status=[&]{return ws.call("project_history_status",Json::object());};
    auto step=[&](const char* direction,const std::string& version){
        Json p=Json::object();p.set("direction",Json::string(direction));p.set("expectedVersion",Json::string(version));
        return ws.call("project_history_step",p);
    };
    auto edit=Json::parse(R"({"path":"types.tree.health.max","value":200})");
    AIVERSE_TEST_CHECK(!ws.call("rules_set",edit).has("error"));
    const auto editedExtra=readProjectFileBytes(extra);
    AIVERSE_TEST_CHECK(!ws.call("set_manifest",Json::parse(R"({"path":"name","value":"Edited"})")).has("error"));
    auto s=status();AIVERSE_TEST_CHECK(s["position"].integer()==2&&s["steps"].size()==2);
    AIVERSE_TEST_CHECK(step("undo","stale").has("error"));
    const auto version=s.getStr("version");
    std::ofstream(rules)<<r<<"\n";
    AIVERSE_TEST_CHECK(step("undo",version).has("error"));
    AIVERSE_TEST_CHECK(status().getStr("version")==version&&ws.project.manifest.getStr("name")=="Edited");
    std::ofstream(rules)<<r;
    AIVERSE_TEST_CHECK(!step("undo",version).has("error"));
    AIVERSE_TEST_CHECK(readProjectFileBytes(manifest)==m&&readProjectFileBytes(extra)==editedExtra);
    AIVERSE_TEST_CHECK(!step("undo",status().getStr("version")).has("error"));
    AIVERSE_TEST_CHECK(readProjectFileBytes(extra)==e&&readProjectFileBytes(rules)==r);
    AIVERSE_TEST_CHECK(ws.project.rulesJson["types"]["tree"]["health"]["max"].integer()==100);
    AIVERSE_TEST_CHECK(!step("redo",status().getStr("version")).has("error"));
    AIVERSE_TEST_CHECK(readProjectFileBytes(extra)==editedExtra);
    const auto beforeNoop=status().getStr("version");
    AIVERSE_TEST_CHECK(!ws.call("rules_set",edit).has("error"));
    AIVERSE_TEST_CHECK(status().getStr("version")==beforeNoop&&status().getBool("canRedo"));
    AIVERSE_TEST_CHECK(!step("redo",status().getStr("version")).has("error"));
    AIVERSE_TEST_CHECK(ws.project.manifest.getStr("name")=="Edited");
    AIVERSE_TEST_CHECK(ws.map.historyMark()==mapMark&&ws.map.revision==mapRevision&&ws.map.data.name=="independent map edit");
    AIVERSE_TEST_CHECK(!step("undo",status().getStr("version")).has("error"));
    AIVERSE_TEST_CHECK(!ws.call("set_manifest",Json::parse(R"({"path":"name","value":"Branch"})")).has("error"));
    AIVERSE_TEST_CHECK(!status().getBool("canRedo")&&status()["position"].integer()==2);
    // Untracked owner writes are barriers, rather than misleading selective undo.
    ws.project.manifest.set("name",Json::string("Other owner"));
    AIVERSE_TEST_CHECK(ws.project.saveManifest(&error)&&status()["steps"].size()==0);
    AIVERSE_TEST_CHECK(!ws.call("set_manifest",Json::parse(R"({"path":"name","value":"Before reopen"})")).has("error"));
    const auto stale=status().getStr("version");
    AIVERSE_TEST_CHECK(ws.project.load(root.string(),&error)&&status()["steps"].size()==1);
    AIVERSE_TEST_CHECK(status().getBool("persistent")&&status().getBool("canUndo"));
    AIVERSE_TEST_CHECK(step("undo",stale).has("error"));
    // Native route uses the exact same owner and leaves map history independent.
    studio::StudioSession session(root.string(),root.string());
    auto call=[&](const char* json){return session.dispatch(Json::parse(json));};
    AIVERSE_TEST_CHECK(!call(R"({"id":1,"method":"studio.rulesSet","params":{"path":"types.tree.health.max","value":300}})").has("error"));
    const auto hs=call(R"({"id":2,"method":"studio.projectHistory","params":{}})")["result"];
    AIVERSE_TEST_CHECK(hs.getStr("scope")=="project"&&hs.getBool("canUndo"));
    AIVERSE_TEST_CHECK(call(R"({"id":4,"method":"studio.snapshot","params":{}})")["result"]["project"].getStr("historyVersion")==hs.getStr("version"));
    Json req=Json::parse(R"({"id":3,"method":"studio.projectHistoryStep","params":{"direction":"undo"}})");
    req.find("params")->set("expectedVersion",Json::string(hs.getStr("version")));
    const auto result=session.dispatch(req);
    AIVERSE_TEST_CHECK(!result.has("error")&&result["result"]["snapshot"].isObject());
    AIVERSE_TEST_CHECK(result["result"]["snapshot"]["project"].getStr("historyVersion")==result["result"].getStr("version"));
    AIVERSE_TEST_CHECK(result["result"].getStr("version")!=hs.getStr("version"));
    AIVERSE_TEST_CHECK(session.workspace().project.rulesJson["types"]["tree"]["health"]["max"].integer()==200);
    fs::remove_all(root);
}
void projectHistoryPersistenceContract(){
    using namespace aiverse;using namespace aiverse::editor;namespace fs=std::filesystem;
    const auto root=fs::canonical(fs::temp_directory_path())/("studio-persistent-history-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    const auto manifest=root/"game.json",rules=root/"rules.json";
    const auto checkpoint=root/".aiverse-cache"/"project-history.json";
    const std::string initial=R"({"name":"Fixture","module":"game","rules":"rules.json"})";
    const std::string originalRules=R"({"types":{"unit":{"health":{"max":100}}}})";
    std::ofstream(manifest)<<initial;std::ofstream(rules)<<originalRules;
    std::string error,stale,checkpointBytes;
    {
        Workspace ws;AIVERSE_TEST_CHECK(ws.project.load(root.string(),&error));
        AIVERSE_TEST_CHECK(!ws.call("rules_set",Json::parse(R"({"path":"types.unit.health.max","value":200})")).has("error"));
        AIVERSE_TEST_CHECK(!ws.call("set_manifest",Json::parse(R"({"path":"name","value":"Edited"})")).has("error"));
        AIVERSE_TEST_CHECK(ws.project.historyStep(false,ws.project.historyVersion(),&error));
        stale=ws.project.historyVersion();checkpointBytes=readProjectFileBytes(checkpoint);
    }
    {
        Workspace ws;AIVERSE_TEST_CHECK(ws.project.load(root.string(),&error));
        const auto status=ws.project.historyStatus();
        AIVERSE_TEST_CHECK(status.getInt("position")==1&&status["steps"].size()==2);
        AIVERSE_TEST_CHECK(status.getBool("canUndo")&&status.getBool("canRedo")&&status.getBool("persistent"));
        AIVERSE_TEST_CHECK(ws.project.historyVersion()!=stale&&!ws.project.historyStep(true,stale,&error));
        AIVERSE_TEST_CHECK(ws.project.historyStep(true,ws.project.historyVersion(),&error));
        AIVERSE_TEST_CHECK(ws.project.manifest.getStr("name")=="Edited");
        AIVERSE_TEST_CHECK(ws.project.historyStep(false,ws.project.historyVersion(),&error));
    }
    // External modification must not resurrect a history with a different cursor baseline.
    const auto savedRules=readProjectFileBytes(rules);
    std::ofstream(rules)<<savedRules<<"\n";
    {
        Workspace ws;AIVERSE_TEST_CHECK(ws.project.load(root.string(),&error));
        const auto status=ws.project.historyStatus();
        AIVERSE_TEST_CHECK(!status.getBool("canUndo")&&!status.getBool("canRedo")&&status.has("persistenceWarning"));
    }
    std::ofstream(rules)<<savedRules;
    // Corrupt checkpoint: project remains readable, no unvalidated history is exposed.
    std::ofstream(checkpoint)<<"corrupt";
    {
        Workspace ws;AIVERSE_TEST_CHECK(ws.project.load(root.string(),&error));
        AIVERSE_TEST_CHECK(ws.project.historyStatus()["steps"].size()==0&&ws.project.historyStatus().has("persistenceWarning"));
        AIVERSE_TEST_CHECK(!ws.call("set_manifest",Json::parse(R"({"path":"name","value":"Recovered"})")).has("error"));
        AIVERSE_TEST_CHECK(ws.project.historyStatus().getBool("canUndo"));
    }
    // Interrupted publication must roll document AND history checkpoint back.
    const auto beforeManifest=readProjectFileBytes(manifest),beforeCheckpoint=readProjectFileBytes(checkpoint);
    {
        ProjectDocumentJournal journal(root);
        AIVERSE_TEST_CHECK(journal.begin({{manifest,beforeManifest,initial},{checkpoint,beforeCheckpoint,checkpointBytes}}));
        journal.publish(0);
    }
    {
        Workspace ws;AIVERSE_TEST_CHECK(ws.project.load(root.string(),&error));
        AIVERSE_TEST_CHECK(readProjectFileBytes(manifest)==beforeManifest&&readProjectFileBytes(checkpoint)==beforeCheckpoint);
        AIVERSE_TEST_CHECK(ws.project.historyStatus().getBool("canUndo"));
    }
    fs::remove_all(root);
}
void assetHistorySessionContract(){
    using namespace aiverse;namespace fs=std::filesystem;
    const auto root=fs::canonical(fs::temp_directory_path())/("studio-asset-history-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root/"content");
    std::ofstream(root/"game.json")<<R"({"name":"Fixture","module":"game","rules":"rules.json"})";
    std::ofstream(root/"rules.json")<<R"({"types":{"unit":{"health":{"max":100}}}})";
    std::ofstream(root/"content"/"Texture.png")<<"texture";
    studio::StudioSession session(root.string(),root.string());
    auto call=[&](const char* json){return session.dispatch(Json::parse(json));};
    const auto assets=call(R"({"id":1,"method":"studio.assets","params":{}})");
    AIVERSE_TEST_CHECK(!assets.has("error")&&assets["result"]["files"].size()==1);
    const auto guid=assets["result"]["files"][0].getStr("guid");
    Json patch=Json::parse(R"({"id":2,"method":"studio.assetMetaPatch","params":{"key":"compression","value":"high"}})");
    patch.find("params")->set("guid",Json::string(guid));
    AIVERSE_TEST_CHECK(!session.dispatch(patch).has("error"));
    const auto h=call(R"({"id":3,"method":"studio.assetHistory","params":{}})")["result"];
    AIVERSE_TEST_CHECK(h.getStr("scope")=="assets"&&h.getBool("canUndo")&&h["position"].integer()==1);
    AIVERSE_TEST_CHECK(!call(R"({"id":4,"method":"studio.assetHistoryStep","params":{"direction":"undo","expectedVersion":"stale"}})").has("result"));
    Json req=Json::parse(R"({"id":5,"method":"studio.assetHistoryStep","params":{"direction":"undo"}})");
    req.find("params")->set("expectedVersion",Json::string(h.getStr("version")));
    const auto result=session.dispatch(req);
    AIVERSE_TEST_CHECK(!result.has("error")&&result["result"].getBool("canRedo"));
    AIVERSE_TEST_CHECK(result["result"]["snapshot"]["project"].getStr("assetHistoryVersion")==result["result"].getStr("version"));
    AIVERSE_TEST_CHECK(Json::parseFile((root/"content"/"Texture.png.meta").string())["settings"].getStr("compression")=="auto");
    session.openProject(root.string());
    const auto restored=call(R"({"id":6,"method":"studio.assetHistory","params":{}})")["result"];
    AIVERSE_TEST_CHECK(restored["steps"].size()==1&&restored.getBool("canRedo")&&restored.getBool("persistent"));
    AIVERSE_TEST_CHECK(restored.getStr("version")!=result["result"].getStr("version"));
    req.find("params")->set("direction",Json::string("redo"));
    req.find("params")->set("expectedVersion",Json::string(restored.getStr("version")));
    AIVERSE_TEST_CHECK(!session.dispatch(req).has("error"));
    AIVERSE_TEST_CHECK(Json::parseFile((root/"content"/"Texture.png.meta").string())["settings"].getStr("compression")=="high");
    fs::remove_all(root);
}
void agentScopedHistoryContract(){
    using namespace aiverse;namespace fs=std::filesystem;
    const auto root=fs::canonical(fs::temp_directory_path())/("studio-agent-scopes-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root/"content");
    std::ofstream(root/"game.json")<<R"({"name":"Fixture","module":"game","rules":"rules.json"})";
    std::ofstream(root/"rules.json")<<R"({"types":{"unit":{"health":{"max":100}}}})";
    studio::StudioSession session(root.string(),root.string());
    auto call=[&](const char* text){return session.dispatch(Json::parse(text));};
    auto info=[&]{
        const auto result=call(R"({"id":1,"method":"studio.agentInfo","params":{}})");
        AIVERSE_TEST_CHECK(!result.has("error"));return result["result"];
    };
    const auto initial=info();const int port=int(initial.getInt("port"));
    std::string token;AIVERSE_TEST_CHECK(editor::net::readAgentToken(port,token));
    editor::net::LineClient client;AIVERSE_TEST_CHECK(client.connect(port));
    auto exchange=[&](const Json& request){
        AIVERSE_TEST_CHECK(client.send(request.dump(-1)));
        std::string line;
        for(int i=0;i<100;++i){
            session.pollAgentLink();
            if(client.recv(line,10))return Json::parse(line)["result"];
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        throw std::runtime_error("Agent link fixture timed out");
    };
    Json hello=Json::object();hello.set("id",Json::number(int64_t(1)));hello.set("method",Json::string("hello"));hello.set("token",Json::string(token));
    AIVERSE_TEST_CHECK(exchange(hello).getBool("ok"));
    auto agent=[&](const std::string& name,const Json& arguments){
        Json request=Json::object(),params=Json::object();
        request.set("id",Json::number(int64_t(2)));request.set("method",Json::string("call"));
        params.set("name",Json::string(name));params.set("arguments",arguments);request.set("params",std::move(params));
        return exchange(request);
    };
    // Mixed agent batch on all three owners. No file write occurs until
    // the complete staged plan has passed exact baseline validation.
    const auto texture=root/"content"/"BatchTexture.png";
    std::ofstream(texture)<<"texture";
    {studio::StudioAssetCatalog index(root);index.rebuild();}
    const auto originalName=session.workspace().map.data.name;
    AIVERSE_TEST_CHECK(!agent("map_set",Json::parse(R"({"path":"name","value":"Batch scene"})")).has("error"));
    AIVERSE_TEST_CHECK(!agent("rules_set",Json::parse(R"({"path":"types.unit.health.max","value":200})")).has("error"));
    auto assets=agent("studio_asset_history",Json::object());
    Json patch=Json::object();patch.set("expectedProject",assets["project"]);patch.set("expectedVersion",assets["version"]);
    {studio::StudioAssetCatalog index(root);index.rebuild(false);patch.set("guid",Json::string(index.byPath("BatchTexture.png")->guid));}
    patch.set("key",Json::string("compression"));patch.set("value",Json::string("high"));
    AIVERSE_TEST_CHECK(!agent("studio_asset_meta_patch",patch).has("error"));
    const auto applyBatch=[&](const Json& preview,const std::string& token,bool confirmed){
        Json request=Json::object(),params=Json::object();
        request.set("id",Json::number(int64_t(8)));request.set("method",Json::string("studio.agentBatchApply"));
        params.set("direction",preview["direction"]);params.set("token",Json::string(token));params.set("confirmed",Json::fromBool(confirmed));
        request.set("params",std::move(params));return session.dispatch(request);
    };
    auto reviewed=info()["batchUndo"];
    AIVERSE_TEST_CHECK(reviewed.getBool("available")&&reviewed["steps"].size()==3);
    AIVERSE_TEST_CHECK(applyBatch(reviewed,"stale",true).has("error"));
    AIVERSE_TEST_CHECK(applyBatch(reviewed,reviewed.getStr("token"),false).has("error"));
    const auto rulesBytes=editor::readProjectFileBytes(root/"rules.json");
    std::ofstream(root/"rules.json",std::ios::binary|std::ios::app)<<"\n";
    AIVERSE_TEST_CHECK(applyBatch(reviewed,reviewed.getStr("token"),true).has("error"));
    AIVERSE_TEST_CHECK(session.workspace().map.data.name=="Batch scene");
    AIVERSE_TEST_CHECK(session.workspace().project.rulesJson["types"]["unit"]["health"]["max"].integer()==200);
    AIVERSE_TEST_CHECK(Json::parseFile(texture.string()+".meta")["settings"].getStr("compression")=="high");
    std::ofstream(root/"rules.json",std::ios::binary|std::ios::trunc)<<rulesBytes;
    AIVERSE_TEST_CHECK(!applyBatch(reviewed,reviewed.getStr("token"),true).has("error"));
    AIVERSE_TEST_CHECK(session.workspace().map.data.name==originalName);
    AIVERSE_TEST_CHECK(session.workspace().project.rulesJson["types"]["unit"]["health"]["max"].integer()==100);
    AIVERSE_TEST_CHECK(Json::parseFile(texture.string()+".meta")["settings"].getStr("compression")=="auto");
    reviewed=info()["batchRedo"];
    AIVERSE_TEST_CHECK(reviewed.getBool("available")&&!applyBatch(reviewed,reviewed.getStr("token"),true).has("error"));
    AIVERSE_TEST_CHECK(session.workspace().map.data.name=="Batch scene");
    reviewed=info()["batchUndo"];
    AIVERSE_TEST_CHECK(!applyBatch(reviewed,reviewed.getStr("token"),true).has("error"));
    AIVERSE_TEST_CHECK(!call(R"({"id":9,"method":"studio.agentReset","params":{}})").has("error"));
    AIVERSE_TEST_CHECK(!agent("rules_set",Json::parse(R"({"path":"types.unit.health.max","value":200})")).has("error"));
    assets=agent("studio_asset_history",Json::object());
    Json folder=Json::object();folder.set("expectedProject",assets["project"]);folder.set("expectedVersion",assets["version"]);
    folder.set("parent",Json::string(""));folder.set("name",Json::string("AgentFolder"));
    AIVERSE_TEST_CHECK(!agent("studio_asset_folder_create",folder).has("error"));
    reviewed=info()["batchUndo"];
    AIVERSE_TEST_CHECK(reviewed.getBool("available")&&reviewed["steps"].size()==2);
    AIVERSE_TEST_CHECK(!applyBatch(reviewed,reviewed.getStr("token"),true).has("error"));
    AIVERSE_TEST_CHECK(!fs::exists(root/"content"/"AgentFolder"));
    AIVERSE_TEST_CHECK(session.workspace().project.rulesJson["types"]["unit"]["health"]["max"].integer()==100);
    reviewed=info()["batchRedo"];
    AIVERSE_TEST_CHECK(reviewed.getBool("available")&&!applyBatch(reviewed,reviewed.getStr("token"),true).has("error"));
    AIVERSE_TEST_CHECK(fs::exists(root/"content"/"AgentFolder"));
    AIVERSE_TEST_CHECK(agent("studio_asset_folder_create",folder).has("error")); // stale owner version
    auto changes=info()["changes"];
    Json projectEdit,assetEdit;
    for(const auto& change:changes.items()){
        if(change.getStr("scope")=="project")projectEdit=change;
        if(change.getStr("scope")=="assets")assetEdit=change;
    }
    AIVERSE_TEST_CHECK(projectEdit.getBool("canUndo")&&assetEdit.getBool("canUndo"));
    AIVERSE_TEST_CHECK(info().getInt("startPosition",-1)==-1);
    auto step=[&](const Json& edit,const std::string& direction,const std::string& version){
        Json request=Json::object(),params=Json::object();
        request.set("id",Json::number(int64_t(3)));request.set("method",Json::string("studio.agentHistoryStep"));
        params.set("scope",edit["scope"]);params.set("historyId",edit["historyId"]);
        params.set("direction",Json::string(direction));params.set("expectedVersion",Json::string(version));
        request.set("params",std::move(params));return session.dispatch(request);
    };
    AIVERSE_TEST_CHECK(step(projectEdit,"undo","stale").has("error"));
    AIVERSE_TEST_CHECK(!step(projectEdit,"undo",projectEdit.getStr("historyVersion")).has("error"));
    AIVERSE_TEST_CHECK(session.workspace().project.rulesJson["types"]["unit"]["health"]["max"].integer()==100);
    AIVERSE_TEST_CHECK(!step(assetEdit,"undo",assetEdit.getStr("historyVersion")).has("error"));
    AIVERSE_TEST_CHECK(!fs::exists(root/"content"/"AgentFolder"));
    changes=info()["changes"];
    for(const auto& change:changes.items())if(change.getStr("scope")=="assets")assetEdit=change;
    AIVERSE_TEST_CHECK(assetEdit.getBool("canRedo"));
    AIVERSE_TEST_CHECK(!step(assetEdit,"redo",assetEdit.getStr("historyVersion")).has("error"));
    AIVERSE_TEST_CHECK(fs::exists(root/"content"/"AgentFolder"));
    // Registry inventory uses explicit classifications, never name suffix guesses.
    AIVERSE_TEST_CHECK(std::string(studio::studioAgentHistoryPolicy("fake_get"))=="unverified");
    AIVERSE_TEST_CHECK(std::string(studio::studioAgentHistoryPolicy("heightmap_export"))=="unverified");
    AIVERSE_TEST_CHECK(std::string(studio::studioAgentHistoryPolicy("rules_get"))=="read");
    const auto capabilities=agent("studio_history_capabilities",Json::object());
    bool foundMove=false;
    for(const auto& item:capabilities["operations"].items())
        if(item.getStr("name")=="studio_asset_move_commit")foundMove=item.getStr("policy")=="assets";
    AIVERSE_TEST_CHECK(foundMove&&capabilities.getStr("bulkUndo")=="map-only");
    assets=agent("studio_asset_history",Json::object());
    Json destinationFolder=Json::object();
    destinationFolder.set("expectedProject",assets["project"]);
    destinationFolder.set("expectedVersion",assets["version"]);
    destinationFolder.set("parent",Json::string(""));
    destinationFolder.set("name",Json::string("Destination"));
    AIVERSE_TEST_CHECK(!agent("studio_asset_folder_create",destinationFolder).has("error"));
    studio::StudioAssetCatalog catalog(root);catalog.rebuild();
    const auto guid=catalog.byPath("AgentFolder")->guid;
    assets=agent("studio_asset_history",Json::object());
    Json move=Json::object(),guids=Json::array();guids.push(Json::string(guid));
    move.set("guids",std::move(guids));move.set("destination",Json::string("Destination"));
    move.set("expectedProject",assets["project"]);move.set("expectedVersion",assets["version"]);
    const auto browser=call(R"({"id":10,"method":"studio.assetMovePreview","params":{"paths":["AgentFolder"],"destination":"Destination"}})");
    AIVERSE_TEST_CHECK(!browser.has("error"));
    const auto preview=agent("studio_asset_move_preview",move);
    AIVERSE_TEST_CHECK(!preview.has("error")&&preview.getStr("token")!=browser["result"].getStr("token"));
    Json commit=Json::object();commit.set("expectedProject",assets["project"]);commit.set("expectedVersion",assets["version"]);
    commit.set("token",browser["result"]["token"]);
    commit.set("decisions",Json::parse(R"([{"id":0,"action":"import"}])"));
    AIVERSE_TEST_CHECK(agent("studio_asset_move_commit",commit).has("error"));
    commit.set("token",preview["token"]);
    commit.set("decisions",Json::array());
    AIVERSE_TEST_CHECK(agent("studio_asset_move_commit",commit).has("error"));
    commit.set("decisions",Json::parse(R"([{"id":0,"action":"import"}])"));
    const auto moved=agent("studio_asset_move_commit",commit);
    AIVERSE_TEST_CHECK(!moved.has("error")&&moved.getBool("historyRecorded"));
    AIVERSE_TEST_CHECK(fs::exists(root/"content"/"Destination"/"AgentFolder"));
    changes=info()["changes"];Json moveEdit;
    for(const auto& change:changes.items())if(change.getStr("op")=="studio_asset_move_commit"&&change.getStr("scope")=="assets")moveEdit=change;
    AIVERSE_TEST_CHECK(moveEdit.getBool("canUndo"));
    AIVERSE_TEST_CHECK(!step(moveEdit,"undo",moveEdit.getStr("historyVersion")).has("error"));
    AIVERSE_TEST_CHECK(fs::exists(root/"content"/"AgentFolder"));
    // S6i.2: linked exact history navigation does not append a new edit.
    assets=agent("studio_asset_history",Json::object());
    Json linked=Json::object();linked.set("expectedProject",assets["project"]);
    auto linkedHistory=agent("studio_agent_history_list",linked);
    AIVERSE_TEST_CHECK(linkedHistory["items"].isArray());
    Json currentMove;
    for(const auto& row:linkedHistory["items"].items())
        if(row.getStr("historyId")==moveEdit.getStr("historyId"))currentMove=row;
    AIVERSE_TEST_CHECK(currentMove.getBool("canRedo"));
    Json navigate=linked;navigate.set("scope",Json::string("assets"));
    navigate.set("historyId",currentMove["historyId"]);navigate.set("expectedVersion",currentMove["historyVersion"]);
    navigate.set("direction",Json::string("redo"));
    AIVERSE_TEST_CHECK(agent("studio_agent_history_step",navigate).has("error"));
    navigate.set("confirmed",Json::fromBool(true));
    AIVERSE_TEST_CHECK(agent("studio_agent_history_step",navigate).getBool("ok"));
    AIVERSE_TEST_CHECK(fs::exists(root/"content"/"Destination"/"AgentFolder"));
    AIVERSE_TEST_CHECK(agent("studio_agent_history_step",navigate).has("error"));
    const auto linkedAfter=agent("studio_agent_history_list",linked);
    AIVERSE_TEST_CHECK(linkedAfter.getInt("total")==linkedHistory.getInt("total"));
    for(const auto& row:linkedAfter["items"].items())
        if(row.getStr("historyId")==moveEdit.getStr("historyId"))currentMove=row;
    navigate.set("direction",Json::string("undo"));navigate.set("expectedVersion",currentMove["historyVersion"]);
    AIVERSE_TEST_CHECK(agent("studio_agent_history_step",navigate).getBool("ok"));
    AIVERSE_TEST_CHECK(fs::exists(root/"content"/"AgentFolder"));
    // S6i: authenticated linked readback, confirmed reversible delete, restore
    // and quota CAS. No arbitrary path or permanent purge adapter.
    assets=agent("studio_asset_history",Json::object());
    Json recovery=Json::object();recovery.set("expectedProject",assets["project"]);
    AIVERSE_TEST_CHECK(!agent("studio_asset_recovery_storage",recovery).has("error"));
    const auto trash=agent("studio_asset_trash_list",recovery);
    AIVERSE_TEST_CHECK(trash["items"].isArray());
    Json deps=recovery;deps.set("guid",Json::string(guid));deps.set("depth",Json::number(int64_t(16)));
    AIVERSE_TEST_CHECK(agent("studio_asset_dependencies",deps)["items"].isArray());
    deps.set("guid",Json::string("../outside"));
    AIVERSE_TEST_CHECK(agent("studio_asset_dependencies",deps).has("error"));
    Json deletion=recovery;deletion.set("guid",Json::string(guid));deletion.set("expectedVersion",assets["version"]);
    AIVERSE_TEST_CHECK(agent("studio_asset_delete",deletion).has("error")&&fs::exists(root/"content"/"AgentFolder"));
    deletion.set("confirmed",Json::fromBool(true));
    const auto deleted=agent("studio_asset_delete",deletion);
    AIVERSE_TEST_CHECK(!deleted.has("error")&&deleted.getBool("historyRecorded")&&!fs::exists(root/"content"/"AgentFolder"));
    Json restore=recovery;restore.set("id",Json::string(fs::path(deleted.getStr("backup")).parent_path().filename().string()));
    const auto restoreReview=agent("studio_asset_trash_restore_review",restore);
    AIVERSE_TEST_CHECK(!restoreReview.has("error")&&!restoreReview.getStr("token").empty());
    restore.set("token",restoreReview["token"]);restore.set("expectedVersion",restoreReview["version"]);
    AIVERSE_TEST_CHECK(agent("studio_asset_trash_restore",restore).has("error"));
    restore.set("confirmed",Json::fromBool(true));
    AIVERSE_TEST_CHECK(!agent("studio_asset_trash_restore",restore).has("error")&&fs::exists(root/"content"/"AgentFolder"));
    Json quota=recovery;quota.set("expectedMiB",Json::number(int64_t(0)));quota.set("quotaMiB",Json::number(int64_t(8)));
    AIVERSE_TEST_CHECK(agent("studio_asset_recovery_quota_set",quota).getInt("quotaMiB")==8);
    AIVERSE_TEST_CHECK(agent("studio_asset_recovery_quota_set",quota).has("error"));
    AIVERSE_TEST_CHECK(agent("studio_asset_trash_purge",restore).has("error"));
    const auto owners=agent("studio_owner_history",recovery);
    AIVERSE_TEST_CHECK(!owners.has("error")&&owners["map"].isObject()&&owners["assets"].isObject());
    AIVERSE_TEST_CHECK(!owners.getStr("ownerVersion").empty());
    Json navigation=recovery;navigation.set("scope",Json::string("map"));
    navigation.set("direction",Json::string("undo"));navigation.set("expectedVersion",Json::string("stale"));
    navigation.set("confirmed",Json::fromBool(true));
    AIVERSE_TEST_CHECK(agent("studio_owner_history_step",navigation).has("error"));
    Json save=recovery;save.set("expectedMap",owners["mapPath"]);
    save.set("expectedOwnerVersion",Json::string("stale"));save.set("confirmed",Json::fromBool(true));
    AIVERSE_TEST_CHECK(agent("studio_save_documents",save).has("error"));
    save.set("expectedOwnerVersion",owners["ownerVersion"]);save.set("confirmed",Json::fromBool(false));
    AIVERSE_TEST_CHECK(agent("studio_save_documents",save).has("error"));
    AIVERSE_TEST_CHECK(agent("studio_asset_history_reset_review",recovery).has("error")); // healthy history
    AIVERSE_TEST_CHECK(agent("studio_asset_history_reset_apply",recovery).has("error")); // native-only

    client.close();session.openProject(root.string());
    AIVERSE_TEST_CHECK(step(assetEdit,"undo",assetEdit.getStr("historyVersion")).has("error"));
    fs::remove_all(root);
}
void projectHistoryBudgetContract(){
    using namespace aiverse::editor;
    ProjectHistory history;
    for(size_t i=0;i<ProjectHistory::MaxSteps+10;++i)
        history.record({{"fixture.json",std::to_string(i),std::to_string(i+1)}},"Edit");
    AIVERSE_TEST_CHECK(history.steps().size()==ProjectHistory::MaxSteps&&history.position()==ProjectHistory::MaxSteps);
    history.advance(false);const auto version=history.version();
    history.record({{"fixture.json","same","same"}},"No-op");
    AIVERSE_TEST_CHECK(history.version()==version&&history.canRedo());
    history.record({{"fixture.json","before","branch"}},"Branch");
    AIVERSE_TEST_CHECK(!history.canRedo()&&history.steps().size()==ProjectHistory::MaxSteps);
    history.record({{"fixture.json",std::string(ProjectHistory::MaxBytes,'x'),"large"}},"Oversize");
    AIVERSE_TEST_CHECK(history.steps().empty()&&history.bytes()==0);
}
void projectFileRecoveryWorkflow() {
    using namespace aiverse;
    namespace fs=std::filesystem;
    const auto root=fs::canonical(fs::temp_directory_path())/("studio-open-recovery-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto project=root/"templates"/"fixture";
    fs::create_directories(project/"content"/"A");
    fs::create_directories(project/"content"/"B");
    fs::create_directories(project/".aiverse-cache");
    {
        using aiverse::editor::ProjectAssetWriteLock;
        bool rejected=false;
        {
            ProjectAssetWriteLock outer(project),nested(project);
            std::thread contender([&]{try{ProjectAssetWriteLock other(project);}catch(const std::exception&){rejected=true;}});
            contender.join();
            AIVERSE_TEST_CHECK(rejected);
        }
        ProjectAssetWriteLock reacquired(project);
        AIVERSE_TEST_CHECK(fs::is_regular_file(project/".aiverse-cache"/"asset-writer.lock"));
    }
    {
        const auto fixture=project/"file-material-recovery";
        fs::create_directories(fixture/"content/materials");
        studio::StudioMaterialPublicationJournal publication(fixture,"stone");
        publication.candidate(Json::parse(R"({"roughness":0.75})"));
        std::ofstream(fixture/"materials.json")<<R"({"materials":{"stone":"content/materials/stone.material.json"}})";
        const auto file=fixture/"content/materials/stone.material.json";
        std::ofstream(file)<<R"({"roughness":0.75})";
        AIVERSE_TEST_CHECK(studio::StudioMaterialPublicationJournal::inspect(fixture)[size_t(0)].getStr("status")=="material_committed");
        std::ofstream(file)<<R"({"roughness":0.25})";
        AIVERSE_TEST_CHECK(studio::StudioMaterialPublicationJournal::inspect(fixture)[size_t(0)].getStr("status")=="material_conflict");
        publication.finish();
    }
    {
        studio::StudioMaterialPublicationJournal publication(project,"fixture_pending");
        const auto pending=studio::StudioMaterialPublicationJournal::inspect(project);
        AIVERSE_TEST_CHECK(pending.size()==1 && pending[size_t(0)].getStr("status")=="incomplete_or_active");
        publication.assets(Json::parse(R"([{"path":"GeneratedBuildingMaterials/fixture_pending/base-color.png","guid":"fixture"}])"));
        const auto checked=studio::StudioMaterialPublicationJournal::inspect(project);
        AIVERSE_TEST_CHECK(checked[size_t(0)]["assets"].size()==1);
        AIVERSE_TEST_CHECK(!checked[size_t(0)]["assets"][size_t(0)].getBool("present"));
        publication.finish();
        AIVERSE_TEST_CHECK(studio::StudioMaterialPublicationJournal::inspect(project).size()==0);
    }
    {
        studio::StudioMaterialPublicationJournal publication(project,"isolated",true);
        fs::create_directory(publication.stage());
        std::ofstream(publication.stage()/"base-color.png")<<"staged";
        const auto slot=studio::StudioMaterialPublicationJournal::quarantineIsolated(project,"isolated");
        AIVERSE_TEST_CHECK(fs::is_regular_file(project/".aiverse-cache"/"material-publications-quarantine"/slot/"journal"/"stage"/"base-color.png"));
        AIVERSE_TEST_CHECK(!fs::exists(publication.stage()));
        AIVERSE_TEST_CHECK(studio::StudioMaterialPublicationJournal::inspectQuarantine(project).size()==1);
        studio::StudioMaterialPublicationJournal::restoreQuarantine(project,slot);
        AIVERSE_TEST_CHECK(fs::is_regular_file(publication.stage()/"base-color.png"));
        publication.finish();
        studio::StudioMaterialPublicationJournal exposed(project,"exposed",true);
        exposed.beginExposure();
        bool rejected=false;try{(void)studio::StudioMaterialPublicationJournal::quarantineIsolated(project,"exposed");}catch(...){rejected=true;}
        AIVERSE_TEST_CHECK(rejected);exposed.finish();
    }
    std::ofstream(project/"game.json") << R"({"name":"Recovery fixture","module":"game","rules":"rules.json"})";
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
void planetWorldProcessAuthoringWorkflow(){
    using namespace aiverse;namespace fs=std::filesystem;
    const auto root=fs::canonical(fs::temp_directory_path())/("studio-planet-wp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto project=root/"fixture";fs::create_directories(project/"maps");
    std::ofstream(project/"game.json")<<R"({"module":"game","rules":"rules.json","maps":[{"file":"maps/scene.json"}]})";
    std::ofstream(project/"rules.json")<<R"({"types":{"tree":{"health":{"max":100}}}})";
    std::ofstream(project/"maps/scene.json")<<R"({"players":[{"name":"Blue","team":1},{"name":"Red","team":2}],
        "world":{"start":"terra","bodies":[{"id":"terra","kind":"planet","radius":64,"faceSize":8},
        {"id":"moon","kind":"moon","radius":32,"faceSize":4},{"id":"sun","kind":"star","radius":100}]},
        "entities":[{"id":"region","type":"tree","owner":0,"at":{"body":"terra","face":0,"x":7,"y":3},
        "components":{"Transform":{"y":0,"yaw":0,"scale":1},"EvolveRegion":{"durability":1000000000,"fertility":500000000,"succession":0,"pollution":0,"enabled":true},
        "EvolveArea":{"halfWidthCm":1000,"halfDepthCm":1000,"enabled":true},
        "MorphNode":{"restX":0,"restY":0,"restZ":0,"drag":100,"anchored":false,"enabled":true}}}]})";
    studio::StudioSession session(root.string(),project.string());
    auto call=[&](const char* method,const char* params="{}"){
        Json request=Json::object();request.set("id",Json::number(int64_t(1)));
        request.set("method",Json::string(method));request.set("params",Json::parse(params));return session.dispatch(request);
    };
    const auto forest=call("studio.planetForest");
    Json badPlant=Json::object();badPlant.set("mapPath",forest["result"]["mapPath"]);
    badPlant.set("expectedRevision",forest["result"]["revision"]);
    badPlant.set("species",Json::number(int64_t(1)));badPlant.set("habit",Json::string("tree"));
    badPlant.set("at",Json::parse(R"({"body":"terra","face":0,"x":2,"y":3})"));
    const auto beforePlant=session.workspace().map.undoDepth();
    AIVERSE_TEST_CHECK(call("studio.planetForestPlant",badPlant.dump(-1).c_str()).has("error"));
    AIVERSE_TEST_CHECK(session.workspace().map.undoDepth()==beforePlant);
    AIVERSE_TEST_CHECK(!forest.has("error")&&forest["result"].getBool("available"));
    Json forestPatch=Json::object();forestPatch.set("mapPath",forest["result"]["mapPath"]);
    forestPatch.set("expectedRevision",forest["result"]["revision"]);
    forestPatch.set("forest",Json{});
    forestPatch.set("catalog",Json::parse(R"({"species":[{"id":1,"preset":"oak"}]})"));
    const auto depth=session.workspace().map.undoDepth();
    AIVERSE_TEST_CHECK(!call("studio.planetForestSet",forestPatch.dump(-1).c_str()).has("error"));
    AIVERSE_TEST_CHECK(session.workspace().map.undoDepth()==depth+1);
    AIVERSE_TEST_CHECK(session.workspace().map.data.worldExtra["terraweaveForest"]["species"].size()==1);
    AIVERSE_TEST_CHECK(call("studio.planetForestSet",forestPatch.dump(-1).c_str()).has("error"));
    AIVERSE_TEST_CHECK(!call("studio.undo").has("error")&&!session.workspace().map.data.worldExtra.has("terraweaveForest"));
    AIVERSE_TEST_CHECK(!call("studio.redo").has("error")&&session.workspace().map.data.worldExtra.has("terraweaveForest"));
    AIVERSE_TEST_CHECK(!call("studio.undo").has("error"));
    // Exercise relocation against a detached native document with the real forest validator.
    {
        editor::MapDoc copy;std::string loadError;
        AIVERSE_TEST_CHECK(copy.loadJson(session.workspace().map.toJson(),&loadError));
        copy.path="maps/forest-fixture.json";
        const auto rules=Json::parse(R"({"nativeModules":[{"id":"world_processes","config":{"planetForest":{"species":[{"id":1}],"plants":[{"species":1,"key":7,"at":{"body":"terra","face":0,"x":2,"y":3}},{"species":1,"key":8,"entity":"region"}]}}}]})");
        auto move=Json::parse(R"({"species":1,"key":7,"at":{"body":"moon","face":1,"x":2,"y":1}})");
        move.set("mapPath",Json::string(copy.path));move.set("expectedRevision",Json::string(std::to_string(copy.revision)));
        const auto startDepth=copy.undoDepth();
        AIVERSE_TEST_CHECK(studio::moveStudioPlanetForest(copy,rules,move));
        AIVERSE_TEST_CHECK(copy.undoDepth()==startDepth+1);
        const auto changed=studio::studioPlanetForestDocument(copy,rules);
        AIVERSE_TEST_CHECK(changed["forest"]["plants"].items()[0]["at"].getStr("body")=="moon");
        bool rejected=false;try{studio::moveStudioPlanetForest(copy,rules,move);}catch(const std::exception&){rejected=true;}
        AIVERSE_TEST_CHECK(rejected&&copy.undoDepth()==startDepth+1);
        move.set("expectedRevision",Json::string(std::to_string(copy.revision)));
        AIVERSE_TEST_CHECK(!studio::moveStudioPlanetForest(copy,rules,move)&&copy.undoDepth()==startDepth+1);
        move.set("key",Json::number(int64_t(8)));rejected=false;
        try{studio::moveStudioPlanetForest(copy,rules,move);}catch(const std::exception&){rejected=true;}
        AIVERSE_TEST_CHECK(rejected&&copy.undoDepth()==startDepth+1);
        copy.undo();AIVERSE_TEST_CHECK(!copy.data.worldExtra.has("planetForest"));
        copy.redo();AIVERSE_TEST_CHECK(copy.data.worldExtra["planetForest"]==changed["forest"]);
    }
    const auto inspected=call("studio.inspect",R"({"entityId":"entity:@region"})");
    AIVERSE_TEST_CHECK(!inspected.has("error"));
    bool placement=false,gravity=false;
    for(const auto& c:inspected["result"]["components"].items()){
        placement|=c.getStr("id")=="SurfacePlacement";
        if(c.getStr("id")=="MorphNode")for(const auto& p:c["properties"].items())
            gravity|=p.getStr("key")=="gravityImpulseMilli";
    }
    AIVERSE_TEST_CHECK(placement&&gravity);
    AIVERSE_TEST_CHECK(call("studio.setProperty",R"({"entityId":"entity:@region","componentId":"SurfacePlacement","key":"body","value":"sun"})").has("error"));
    AIVERSE_TEST_CHECK(!call("studio.setProperty",R"({"entityId":"entity:@region","componentId":"SurfacePlacement","key":"body","value":"moon"})").has("error"));
    AIVERSE_TEST_CHECK(session.workspace().map.data.entities[0].at.getStr("body")=="moon");
    AIVERSE_TEST_CHECK(session.workspace().map.data.entities[0].x==4);
    AIVERSE_TEST_CHECK(call("studio.setProperty",R"({"entityId":"entity:@region","componentId":"SurfacePlacement","key":"face","value":6})").has("error"));
    AIVERSE_TEST_CHECK(!call("studio.undo").has("error"));
    AIVERSE_TEST_CHECK(session.workspace().map.data.entities[0].at.getStr("body")=="terra");
    const auto oldPlacement=session.workspace().map.data.entities[0].at;
    AIVERSE_TEST_CHECK(!call("studio.setProperty",R"({"entityId":"entity:@region","componentId":"SurfacePlacement","key":"placement","value":{"body":"terra","face":3,"x":2.5,"y":1.5}})").has("error"));
    AIVERSE_TEST_CHECK(session.workspace().map.data.entities[0].at.getInt("face")==3);
    AIVERSE_TEST_CHECK(session.workspace().map.data.entities[0].x==2.5f);
    AIVERSE_TEST_CHECK(call("studio.setProperty",R"({"entityId":"entity:@region","componentId":"SurfacePlacement","key":"placement","value":{"body":"terra","face":2,"x":999,"y":1}})").has("error"));
    AIVERSE_TEST_CHECK(!call("studio.undo").has("error"));
    AIVERSE_TEST_CHECK(session.workspace().map.data.entities[0].at==oldPlacement);
    const auto oldArea=session.workspace().map.data.entities[0].extra["components"]["EvolveArea"];
    const auto rev=session.workspace().map.revision;
    const auto bounds=std::string(R"({"entityId":"entity:@region","componentId":"EvolveArea","key":"bounds","expectedRevision":)")+std::to_string(rev)+
        R"(,"value":{"offsetXcm":100,"offsetZcm":-200,"halfWidthCm":300,"halfDepthCm":400}})";
    AIVERSE_TEST_CHECK(!call("studio.worldProcessAreaSet",bounds.c_str()).has("error"));
    const auto& updated=session.workspace().map.data.entities[0].extra["components"]["EvolveArea"];
    AIVERSE_TEST_CHECK(updated.getInt("offsetXcm")==100&&updated.getInt("offsetZcm")==-200);
    AIVERSE_TEST_CHECK(updated.getInt("halfWidthCm")==300&&updated.getInt("halfDepthCm")==400);
    AIVERSE_TEST_CHECK(call("studio.worldProcessAreaSet",bounds.c_str()).has("error"));
    AIVERSE_TEST_CHECK(!call("studio.undo").has("error"));
    AIVERSE_TEST_CHECK(session.workspace().map.data.entities[0].extra["components"]["EvolveArea"]==oldArea);
    AIVERSE_TEST_CHECK(!call("studio.setProperty",R"({"entityId":"entity:@region","componentId":"EvolveArea","key":"offsetXcm","value":-250})").has("error"));
    AIVERSE_TEST_CHECK(session.workspace().map.data.entities[0].extra["components"]["EvolveArea"].getInt("offsetXcm")==-250);
    AIVERSE_TEST_CHECK(!call("studio.setProperty",R"({"entityId":"entity:@region","componentId":"MorphNode","key":"gravityRelative","value":false})").has("error"));
    AIVERSE_TEST_CHECK(session.workspace().map.data.entities[0].extra["components"]["MorphNode"].has("gravityImpulseMilli"));
    AIVERSE_TEST_CHECK(call("studio.worldProcessAreaBatchMove",R"({"entityIds":["entity:@region"],"axis":"x","deltaCm":100})").has("error"));
    fs::remove_all(root);
}

void caveGraphCaptureWorkflow() {
    using namespace aiverse;namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("studio-cave-capture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto project=root/"fixture";fs::create_directories(project/"maps");
    std::ofstream(project/"game.json")<<R"({"module":"game","rules":"rules.json","maps":[{"file":"maps/a.json"},{"file":"maps/b.json"}]})";
    std::ofstream(project/"rules.json")<<R"({"types":{"unit":{"health":{"max":100}}}})";
    editor::MapDoc map;map.reset(16,16);map.data.name="Same display name";
    map.data.extra.set("terraweaveCaverns",Json::parse(R"({"version":1,"generatorVersion":1,"sourceRevision":7,"settings":{"seed":2,"cellCm":4800,"tunnelRadiusCm":450,"chamberRadiusCm":1200,"tunnelChance":550,"chamberChance":180,"minYcm":-200000,"maxYcm":-300},"stamps":[{"id":7,"mode":"carve","xCm":10,"yCm":-1000,"zCm":20,"radiusCm":400}],"entrances":[]})"));
    AIVERSE_TEST_CHECK(map.save((project/"maps/a.json").string(),nullptr));
    AIVERSE_TEST_CHECK(map.save((project/"maps/b.json").string(),nullptr));
    {
        studio::StudioSession session(root.string(),project.string());
        auto call=[&](const char* method,const Json& params=Json::object()){
            Json request=Json::object();request.set("id",Json::number(int64_t(1)));request.set("method",Json::string(method));request.set("params",params);return session.dispatch(request);
        };
        auto args=[&]{
            Json p=Json::object();p.set("scope",call("studio.snapshot")["result"]["graphDocument"]);return p;
        };
        // Exercise guarded native Shader Graph saves through the actual Studio bridge.
        fs::create_directories(project/"shaders");
        const Json shader=Json::parse(R"({"version":1,"domain":"material","nodes":[{"id":"input","op":"input"}],"outputs":{"color":"input"}})");
        std::ofstream(project/"shaders"/"guarded.json")<<shader.dump(2);
        Json edited=shader;
        edited.set("nodes",Json::parse(R"([{"id":"input","op":"input","position":[10,20]}])"));
        Json save=Json::object();save.set("name",Json::string("guarded"));save.set("graph",edited);
        const auto projectPath=session.workspace().project.dir;
        Json debug=Json::parse(R"({"op":"graph_debug_session","params":{"action":"status"}})");
        AIVERSE_TEST_CHECK(call("studio.op",debug).has("error"));
        for(const char* op:{"graph_runtime_probe","graph_live_snapshot","graph_live_watch","graph_live_profile","graph_gpu_profile"}) {
            Json stale=Json::object();stale.set("op",Json::string(op));stale.set("params",Json::object());
            AIVERSE_TEST_CHECK(call("studio.op",stale).has("error"));
            stale.set("expectedProject",Json::string("other-project"));
            AIVERSE_TEST_CHECK(call("studio.op",stale).has("error"));
        }
        debug.set("expectedProject",Json::string("other-project"));
        AIVERSE_TEST_CHECK(call("studio.op",debug).has("error"));
        debug.set("expectedProject",Json::string(projectPath));
        debug.set("expectedDocumentEpoch",Json::string("0"));
        AIVERSE_TEST_CHECK(call("studio.op",debug).has("error"));
        debug.set("expectedDocumentEpoch",call("studio.snapshot")["result"]["graphDocument"]["epoch"]);
        const auto debugReply=call("studio.op",debug);
        AIVERSE_TEST_CHECK(!debugReply.has("error") && !debugReply["result"].getBool("changed"));
        save.set("expectedProject",Json::string(projectPath));
        AIVERSE_TEST_CHECK(call("studio.shaderGraphSave",save).has("error"));
        save.set("expected",shader);save.set("expectedProject",Json::string("other-project"));
        AIVERSE_TEST_CHECK(call("studio.shaderGraphSave",save).has("error"));
        AIVERSE_TEST_CHECK(Json::parseFile((project/"shaders"/"guarded.json").string())==shader);
        save.set("expectedProject",Json::string(projectPath));
        AIVERSE_TEST_CHECK(!call("studio.shaderGraphSave",save).has("error"));
        AIVERSE_TEST_CHECK(call("studio.shaderGraphSave",save).has("error"));
        AIVERSE_TEST_CHECK(Json::parseFile((project/"shaders"/"guarded.json").string())==edited);
        const auto original=args();const auto bytes=session.workspace().map.toJson();
        const auto undo=session.workspace().map.undoDepth();const auto dirty=session.workspace().map.dirty;
        bool registered=false;
        for(const auto& op:session.workspace().ops())registered|=op.name=="terraweave_caverns_paint"&&!op.guiOnly;
        Json reordered=Json::object();
        for(const auto* key:{"revision","epoch","mapPath","projectPath"})
            reordered.set(key,original["scope"][key]);
        Json wireParams=Json::object();wireParams.set("scope",reordered);
        const auto r=call("studio.cavernsGraphInspect",wireParams);
        AIVERSE_TEST_CHECK(!r.has("error"));
        AIVERSE_TEST_CHECK(r["result"].getStr("status")==(registered?"ready":"unavailable"));
        if(registered){
            AIVERSE_TEST_CHECK(r["result"]["report"].getInt("stampCount")==1);
            AIVERSE_TEST_CHECK(r["result"]["report"].getInt("revision")==7);
            AIVERSE_TEST_CHECK(r["result"]["report"]["snapshot"]["stamps"][size_t(0)].getInt("id")==7);
        }else AIVERSE_TEST_CHECK(!r["result"].has("report"));
        AIVERSE_TEST_CHECK(session.workspace().map.toJson()==bytes && session.workspace().map.undoDepth()==undo && session.workspace().map.dirty==dirty);
        AIVERSE_TEST_CHECK(!call("studio.openMap",Json::parse(R"({"file":"maps/b.json"})")).has("error"));
        AIVERSE_TEST_CHECK(call("studio.cavernsGraphInspect",original).has("error"));
        AIVERSE_TEST_CHECK(!call("studio.cavernsGraphInspect",args()).has("error"));
        const auto beforeReopen=args();
        session.openProject(project.string());
        AIVERSE_TEST_CHECK(call("studio.op",debug).has("error"));
        AIVERSE_TEST_CHECK(call("studio.cavernsGraphInspect",beforeReopen).has("error"));
    }
    fs::remove_all(root);
}
void authoredEntityWorkflow() {
   using namespace aiverse;namespace fs=std::filesystem;
   const auto root=fs::canonical(fs::temp_directory_path())/("studio-authored-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
   const auto project=root/"fixture";fs::create_directories(project/"maps");
   std::ofstream(project/"game.json")<<R"({"module":"game","rules":"rules.json","maps":[{"file":"maps/scene.json"}]})";
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
   // The same Material Library preset and Mesh Renderer edit that Studio dispatches.
   {
       std::ofstream(project/"content/stone-color.png")<<"catalog fixture";
       std::ofstream(project/"content/stone-normal.png")<<"catalog fixture";
       studio::StudioAssetCatalog assets(project);assets.rebuild();
       const auto color=assets.byPath("stone-color.png"), normal=assets.byPath("stone-normal.png");
       AIVERSE_TEST_CHECK(color && normal);
       Json create=Json::parse(R"({"name":"cliff","preset":"triplanar","repeatMetres":0.75})");
       create.set("colorGuid",Json::string(color->guid));
       create.set("normalGuid",Json::string(normal->guid));
       AIVERSE_TEST_CHECK(!call("studio.materialCreate",create.dump().c_str()).has("error"));
       AIVERSE_TEST_CHECK(call("studio.materialCreate",create.dump().c_str()).has("error"));
       AIVERSE_TEST_CHECK(!call("studio.setProperty",R"({"entityId":"entity:@second","componentId":"MeshRenderer","key":"material:*","value":"cliff"})").has("error"));
       AIVERSE_TEST_CHECK(!call("studio.save").has("error"));
       const auto materialPath=Json::parseFile((project/"materials.json").string())["materials"].getStr("cliff");
       AIVERSE_TEST_CHECK(materialPath=="content/materials/cliff.material.json");
       const auto catalog=call("studio.assets");
       bool listed=false;
       for(const auto& asset:catalog["result"]["files"].items())
           if("content/"+asset.getStr("path")==materialPath && !asset.getStr("guid").empty()) listed=true;
       AIVERSE_TEST_CHECK(listed);
       const auto graph=Json::parseFile((project/materialPath).string())["shaderGraph"];
       AIVERSE_TEST_CHECK(graph["outputs"].has("normal"));
       bool colorBound=false,normalBound=false;
       for (const auto& node:graph["nodes"].items()) {
           colorBound|=node.getStr("texture")=="content/stone-color.png";
           normalBound|=node.getStr("texture")=="content/stone-normal.png";
       }
       AIVERSE_TEST_CHECK(colorBound && normalBound);
       editor::MapDoc saved;std::string loadError;
       AIVERSE_TEST_CHECK(saved.load((project/"maps/scene.json").string(),&loadError));
       const auto& entity=saved.data.entities[size_t(saved.findEntity("second"))];
       AIVERSE_TEST_CHECK(entity.extra["components"]["StaticModel"]["materials"].getStr("*")=="cliff");
       AIVERSE_TEST_CHECK(!call("studio.undo").has("error"));
   }
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
   // World-process map authoring: defaults, strict integer validation,
   // component ownership, one-step Undo/Redo, and real project disk persistence.
   // Runtime remains read-only; the game's native setup must explicitly load
   // these validated component documents into actual ECS entities.
   {
       auto& map=session.workspace().map;
       const auto entity=[&]()->const editor::DocEntity&{
           return map.data.entities[size_t(map.findEntity("second"))];
       };
       const char* ids[]={"EchoZone","FractureMember","AetherGasCell"};
       for(const char* id:ids) {
           const std::string input="{\"entityId\":\"entity:@second\",\"componentId\":\""+std::string(id)+"\"}";
           AIVERSE_TEST_CHECK(!call("studio.componentAdd",input.c_str()).has("error"));
           AIVERSE_TEST_CHECK(entity().extra["components"].has(id));
           AIVERSE_TEST_CHECK(call("studio.componentAdd",input.c_str()).has("error"));
       }
       AIVERSE_TEST_CHECK(!call("studio.setProperty",R"({"entityId":"entity:@second","componentId":"FractureMember","key":"anchored","value":true})").has("error"));
       AIVERSE_TEST_CHECK(entity().extra["components"]["FractureMember"]["anchored"].boolean());
       AIVERSE_TEST_CHECK(!call("studio.setProperty",R"({"entityId":"entity:@second","componentId":"EchoZone","key":"enabled","value":false})").has("error"));
       AIVERSE_TEST_CHECK(!entity().extra["components"]["EchoZone"]["enabled"].boolean());
       const size_t depth=map.undoDepth();
       AIVERSE_TEST_CHECK(!call("studio.setProperties",R"({"entityId":"entity:@second","componentId":"AetherGasCell","values":{"smoke":250,"oxygen":500}})").has("error"));
       AIVERSE_TEST_CHECK(map.undoDepth()==depth+1);
       AIVERSE_TEST_CHECK(entity().extra["components"]["AetherGasCell"]["smoke"].integer()==250);
       AIVERSE_TEST_CHECK(entity().extra["components"]["AetherGasCell"]["oxygen"].integer()==500);
       const auto before=entity().extra.dump();
       AIVERSE_TEST_CHECK(call("studio.setProperties",R"({"entityId":"entity:@second","componentId":"AetherGasCell","values":{"smoke":300,"oxygen":1000000001}})").has("error"));
       AIVERSE_TEST_CHECK(entity().extra.dump()==before && map.undoDepth()==depth+1);
       AIVERSE_TEST_CHECK(call("studio.setProperty",R"({"entityId":"entity:@second","componentId":"FractureMember","key":"broken","value":true})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.setProperty",R"({"entityId":"entity:@second","componentId":"AetherGasCell","key":"smoke","value":2.5})").has("error"));
       AIVERSE_TEST_CHECK(entity().extra.dump()==before);
       AIVERSE_TEST_CHECK(!call("studio.undo").has("error"));
       AIVERSE_TEST_CHECK(entity().extra["components"]["AetherGasCell"]["smoke"].integer()==0);
       AIVERSE_TEST_CHECK(!call("studio.redo").has("error"));
       AIVERSE_TEST_CHECK(entity().extra["components"]["AetherGasCell"]["smoke"].integer()==250);
       AIVERSE_TEST_CHECK(!call("studio.select",R"({"id":"entity:@second"})").has("error"));
       const auto snap=call("studio.snapshot");
       bool foundEdited=false;
       for(const auto& comp:snap["result"]["inspector"]["components"].items()) {
           if(comp.getStr("id")!="AetherGasCell")continue;
           for(const auto& field:comp["properties"].items())
               if(field.getStr("key")=="smoke")
                   foundEdited=field.getBool("editable")&&field["value"].integer()==250;
       }
       AIVERSE_TEST_CHECK(foundEdited);
       AIVERSE_TEST_CHECK(!call("studio.save").has("error"));
       editor::MapDoc disk;
       std::string why;
       AIVERSE_TEST_CHECK(disk.load((project/"maps/scene.json").string(),&why));
       const auto& saved=disk.data.entities[size_t(disk.findEntity("second"))].extra["components"];
       AIVERSE_TEST_CHECK(saved["AetherGasCell"]["oxygen"].integer()==500);
       AIVERSE_TEST_CHECK(saved["FractureMember"]["anchored"].boolean());
       for(const char* id:ids) {
           const std::string input="{\"entityId\":\"entity:@second\",\"componentId\":\""+std::string(id)+"\"}";
           AIVERSE_TEST_CHECK(!call("studio.componentRemove",input.c_str()).has("error"));
           AIVERSE_TEST_CHECK(!entity().extra["components"].has(id));
       }
   }
   // Exercise each flat-map Scene edge through the same MapDoc RPC used on release.
   {
       namespace editing = studio::worldprocess_edit;
       using Mode = editing::Handle::Mode;
       auto& doc = session.workspace().map;
       const auto area = [&]() { return doc.data.entities.at(doc.findEntity("second")).extra["components"]["EvolveArea"]; };
       AIVERSE_TEST_CHECK(!call("studio.componentAdd",R"({"entityId":"entity:@second","componentId":"EvolveArea"})").has("error"));
       for (const auto mode : {Mode::AreaWidth, Mode::AreaWidthNegative, Mode::AreaDepth, Mode::AreaDepthNegative}) {
           const auto initial = area();
           const auto handles = editing::handles(doc,"entity:@second");
           const auto edge = std::find_if(handles.begin(),handles.end(),[&](const auto& h){return h.mode==mode;});
           AIVERSE_TEST_CHECK(edge!=handles.end());
           const bool x = mode==Mode::AreaWidth || mode==Mode::AreaWidthNegative;
           const bool negative = mode==Mode::AreaWidthNegative || mode==Mode::AreaDepthNegative;
           const auto& owner = doc.data.entities.at(doc.findEntity("second"));
           const auto centerKey = x?"offsetXcm":"offsetZcm";
           const auto half = initial.getInt(edge->key,0);
           const auto center = initial.getInt(centerKey,0);
           auto mouse = edge->position;
           (x?mouse.x:mouse.z) += negative?-2.f:2.f;
           float coordinate=0;
           AIVERSE_TEST_CHECK(editing::extent(owner,*edge,mouse,coordinate));
           AIVERSE_TEST_CHECK(coordinate==center+(negative?-1:1)*(half+200));
           editing::AreaPreview symmetric;
           AIVERSE_TEST_CHECK(editing::previewArea(owner,mode,coordinate,true,symmetric));
           const auto symmetricHalf=std::llround((x?symmetric.halfWidth:symmetric.halfDepth)*100.);
           AIVERSE_TEST_CHECK(symmetricHalf==half+200);
           const auto depth=doc.undoDepth();
           Json params=Json::object();
           params.set("entityId",Json::string("entity:@second"));
           params.set("componentId",Json::string("EvolveArea"));
           params.set("key",Json::string(edge->key));
           params.set("value",Json::number(int64_t(symmetricHalf)));
           AIVERSE_TEST_CHECK(!call("studio.worldProcessAreaSet",params.dump().c_str()).has("error"));
           AIVERSE_TEST_CHECK(area().getInt(centerKey)==center && area().getInt(edge->key)==symmetricHalf);
           AIVERSE_TEST_CHECK(doc.undoDepth()==depth+1);
           AIVERSE_TEST_CHECK(!call("studio.undo").has("error") && area()==initial);
           params.set("edgeSide",Json::string(negative?"negative":"positive"));
           params.set("expectedRevision",Json::number(int64_t(doc.revision)));
           params.set("value",Json::number(int64_t(coordinate)));
           AIVERSE_TEST_CHECK(!call("studio.worldProcessAreaSet",params.dump().c_str()).has("error"));
           const auto edited=area();
           AIVERSE_TEST_CHECK(doc.undoDepth()==depth+1 && edited.getInt(edge->key)==half+100);
           AIVERSE_TEST_CHECK(edited.getInt(centerKey)==center+(negative?-100:100));
           AIVERSE_TEST_CHECK(!call("studio.undo").has("error") && area()==initial);
           AIVERSE_TEST_CHECK(!call("studio.redo").has("error") && area()==edited);
           AIVERSE_TEST_CHECK(!call("studio.save").has("error"));
           editor::MapDoc disk;
           std::string error;
           AIVERSE_TEST_CHECK(disk.load((project/"maps/scene.json").string(),&error));
           AIVERSE_TEST_CHECK(disk.data.entities.at(disk.findEntity("second")).extra["components"]["EvolveArea"]==edited);
       }
       AIVERSE_TEST_CHECK(!call("studio.componentRemove",R"({"entityId":"entity:@second","componentId":"EvolveArea"})").has("error"));
   }
   // Stable linked object selection: maps document IDs to typed targets,
   // validates a multi-field reference edit as a single atomic Undo step.
   {
       // The earlier delete test removed "first"; restore a distinct authored
       // endpoint for the linked-reference workflow.
       editor::MapDoc endpointMap;std::string endpointError;
       AIVERSE_TEST_CHECK(endpointMap.load((project/"maps/scene.json").string(),&endpointError));
       auto& clean=endpointMap.data.entities[size_t(endpointMap.findEntity("second"))].extra.member("components");
       for(const char* id:{"EchoZone","EchoPortal","FractureMember","AetherGasCell"})clean.erase(id);
       if(endpointMap.findEntity("first")<0){
           for(auto& endpoint:endpointMap.data.entities)if(endpoint.id!="second"){
               endpoint.id="first";
               auto& components=endpoint.extra.member("components");
               for(const char* id:{"EchoZone","EchoPortal","FractureMember","AetherGasCell"})components.erase(id);
               break;
           }
           AIVERSE_TEST_CHECK(endpointMap.findEntity("first")>=0);
       }
       AIVERSE_TEST_CHECK(endpointMap.save((project/"maps/scene.json").string(),&endpointError));
       session.openProject(project.string());
       const auto& doc=session.workspace().map;
       auto entity=[&]()->const editor::DocEntity&{
           return doc.data.entities[size_t(doc.findEntity("second"))];
       };
       AIVERSE_TEST_CHECK(!call("studio.componentAdd",R"({"entityId":"entity:@first","componentId":"EchoZone"})").has("error"));
       AIVERSE_TEST_CHECK(!call("studio.componentAdd",R"({"entityId":"entity:@second","componentId":"EchoZone"})").has("error"));
       AIVERSE_TEST_CHECK(!call("studio.componentAdd",R"({"entityId":"entity:@second","componentId":"EchoPortal"})").has("error"));
       const auto list=call("studio.componentCatalog",R"({"entityId":"entity:@second"})");
       bool portalFound=false;
       for(const auto& item:list["result"].items())
           if(item.getStr("id")=="EchoPortal")portalFound=!item.getBool("available");
       AIVERSE_TEST_CHECK(portalFound);
       const auto depth=doc.undoDepth();
       AIVERSE_TEST_CHECK(!call("studio.setProperties",
           R"({"entityId":"entity:@second","componentId":"EchoPortal","values":{"from":"first","to":"second","loss":25,"enabled":true}})").has("error"));
       AIVERSE_TEST_CHECK(doc.undoDepth()==depth+1);
       AIVERSE_TEST_CHECK(entity().extra["components"]["EchoPortal"]["from"].str()=="first");
       AIVERSE_TEST_CHECK(entity().extra["components"]["EchoPortal"]["enabled"].boolean());
       const auto before=entity().extra.dump();
       AIVERSE_TEST_CHECK(call("studio.setProperty",
           R"({"entityId":"entity:@second","componentId":"EchoPortal","key":"to","value":"not-real"})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.setProperty",
           R"({"entityId":"entity:@second","componentId":"EchoPortal","key":"to","value":"entity:0"})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.setProperties",
           R"({"entityId":"entity:@second","componentId":"EchoPortal","values":{"from":"second","to":"second"}})").has("error"));
       AIVERSE_TEST_CHECK(doc.undoDepth()==depth+1 && entity().extra.dump()==before);
       AIVERSE_TEST_CHECK(call("studio.componentRemove",
           R"({"entityId":"entity:@first","componentId":"EchoZone"})").has("error"));
       AIVERSE_TEST_CHECK(!call("studio.select",R"({"id":"entity:@first"})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.entityDelete",
           R"({"entityId":"entity:@first"})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.entitiesDelete",
           R"({"entityIds":["entity:@first"]})").has("error"));
       AIVERSE_TEST_CHECK(doc.findEntity("first")>=0);
       const auto insp=call("studio.select",R"({"id":"entity:@second"})");
       AIVERSE_TEST_CHECK(!insp.has("error"));
       const auto snap=call("studio.snapshot");
       bool hasRefPicker=false;
       for(const auto& comp:snap["result"]["inspector"]["components"].items()){
           if(comp.getStr("id")!="EchoPortal")continue;
           for(const auto& field:comp["properties"].items()){
               if(field.getStr("key")!="from")continue;
               hasRefPicker=field.getBool("entityReference")&&field.getBool("editable");
               bool firstOption=false;
               for(const auto& option:field["options"].items())
                   if(option.getStr("value")=="first")firstOption=true;
               AIVERSE_TEST_CHECK(firstOption);
           }
       }
       AIVERSE_TEST_CHECK(hasRefPicker);
       AIVERSE_TEST_CHECK(!call("studio.undo").has("error"));
       AIVERSE_TEST_CHECK(!entity().extra["components"]["EchoPortal"]["enabled"].boolean());
       AIVERSE_TEST_CHECK(!call("studio.redo").has("error"));
       AIVERSE_TEST_CHECK(entity().extra["components"]["EchoPortal"]["from"].str()=="first");
       AIVERSE_TEST_CHECK(!call("studio.save").has("error"));
       editor::MapDoc disk;std::string why;
       AIVERSE_TEST_CHECK(disk.load((project/"maps/scene.json").string(),&why));
       AIVERSE_TEST_CHECK(disk.data.entities[size_t(disk.findEntity("second"))]
           .extra["components"]["EchoPortal"]["from"].str()=="first");
       AIVERSE_TEST_CHECK(!call("studio.componentRemove",
           R"({"entityId":"entity:@second","componentId":"EchoPortal"})").has("error"));
       AIVERSE_TEST_CHECK(!call("studio.componentRemove",
           R"({"entityId":"entity:@first","componentId":"EchoZone"})").has("error"));
       AIVERSE_TEST_CHECK(!call("studio.componentRemove",
           R"({"entityId":"entity:@second","componentId":"EchoZone"})").has("error"));
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
       for(const auto& id:{std::string("second"),other})
           AIVERSE_TEST_CHECK(editor::authoredScale3(map.data.entities[size_t(map.findEntity(id))])[0]==3);
       AIVERSE_TEST_CHECK(!call("studio.undo").has("error"));
       for(const auto& id:{std::string("second"),other})
           AIVERSE_TEST_CHECK(editor::authoredScale3(map.data.entities[size_t(map.findEntity(id))])[0]!=3);
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
       // The genre-neutral VFX source authoring contract uses the existing
       // component fields and participates in document undo.
       AIVERSE_TEST_CHECK(!call("studio.componentAdd",R"({"entityId":"entity:@second","componentId":"SpectraSource"})").has("error"));
       const auto fxComponent=[&](){return map.data.entities[size_t(map.findEntity("second"))].extra["components"]["SpectraSource"];};
       AIVERSE_TEST_CHECK(fxComponent().isObject()&&!fxComponent().getBool("enabled"));
       AIVERSE_TEST_CHECK(!call("studio.setProperty",R"({"entityId":"entity:@second","componentId":"SpectraSource","key":"scale","value":2})").has("error"));
       AIVERSE_TEST_CHECK(fxComponent().getInt("revision")==1&&fxComponent()["scale"].toDouble()==2);
       AIVERSE_TEST_CHECK(call("studio.setProperty",R"({"entityId":"entity:@second","componentId":"SpectraSource","key":"effect","value":"../bad"})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.setProperty",R"({"entityId":"entity:@second","componentId":"SpectraSource","key":"enabled","value":true})").has("error"));
       AIVERSE_TEST_CHECK(!call("studio.undo").has("error")&&fxComponent()["scale"].toDouble()==1);
       AIVERSE_TEST_CHECK(!call("studio.componentRemove",R"({"entityId":"entity:@second","componentId":"SpectraSource"})").has("error"));
       AIVERSE_TEST_CHECK(!fxComponent().isObject());
       AIVERSE_TEST_CHECK(call("studio.assetPlace",R"({"asset":"spectrafx:missing","x":5,"z":6})").has("error")&&map.data.entities.size()==before);
       // Asset drop needs a scenery type or a selected entity type; here the selection supplies "tree".
       AIVERSE_TEST_CHECK(!call("studio.select",R"({"id":"entity:@second"})").has("error"));
       AIVERSE_TEST_CHECK(!call("studio.assetPlace",R"({"asset":"content/rock.glb","x":5,"z":6})").has("error"));
       AIVERSE_TEST_CHECK(map.data.entities.size()==before+1 && map.data.entities.back().extra["components"]["StaticModel"].getStr("asset")=="content/rock.glb");
       AIVERSE_TEST_CHECK(call("studio.assetPlace",R"({"asset":"content/missing.glb","x":5,"z":6})").has("error") && map.data.entities.size()==before+1);
       AIVERSE_TEST_CHECK(!call("studio.undo").has("error") && map.data.entities.size()==before);
   }
   {
       const auto invalidTrack=call("studio.op",R"({"op":"animation_root_motion_generate","params":{"model":"missing.gltf","id":4294967297}})");
       AIVERSE_TEST_CHECK(invalidTrack.getStr("error").find("Invalid root motion track ID")!=std::string::npos);
       const auto avatars=project/"animations"/"avatar";
       fs::create_directories(avatars);
       std::ofstream(avatars/"saved_profile.json")<<R"({"format":"aiverse-humanoid-v1","model":"content/avatar.gltf","bones":{}})";
       const auto files=call("studio.op",R"({"op":"animation_asset_list","params":{"kind":"avatar"}})");
       const auto& catalog=files["result"]["result"];
       AIVERSE_TEST_CHECK(!files.has("error") && !files["result"].getBool("changed") &&
           catalog["names"][0].str()=="saved_profile" && catalog["roles"].size()==23);
       const auto loaded=call("studio.op",R"({"op":"animation_asset_get","params":{"kind":"avatar","name":"saved_profile"}})");
       AIVERSE_TEST_CHECK(loaded["result"]["result"].getStr("revision").size()==64);
       AIVERSE_TEST_CHECK(call("studio.op",R"({"op":"animation_asset_list","params":{"kind":"../"}})").has("error"));
       fs::remove(avatars/"saved_profile.json");
       const auto branding=call("studio.op",R"({"op":"branding_get","params":{}})");
       AIVERSE_TEST_CHECK(branding["result"]["result"]["iconsVersion"].isString());
       const auto conflict=call("studio.op",R"({"op":"branding_set","params":{"expectedIconsVersion":"outdated","icons":{}}})");
       AIVERSE_TEST_CHECK(conflict.getStr("error").find("Icon settings changed")!=std::string::npos);
       AIVERSE_TEST_CHECK(branding["result"]["result"]["splashVersion"].isString());
       const auto staleSplash=call("studio.op",R"({"op":"branding_set","params":{"expectedSplashVersion":"stale","splash":false}})");
       AIVERSE_TEST_CHECK(staleSplash.getStr("error").find("Splash settings changed")!=std::string::npos);
       const auto splashFrames=call("studio.op",R"({"op":"splash_preview","params":{"frames":true,"times":[0,1],"width":64,"height":48}})");
       AIVERSE_TEST_CHECK(!splashFrames.has("error") && !splashFrames["result"].getBool("changed"));
       const auto& frames=splashFrames["result"]["result"]["frames"].items();
       AIVERSE_TEST_CHECK(frames.size()==2 && frames[0].str().starts_with("iVBOR") && frames[0].str()!=frames[1].str());
       const auto android=call("studio.op",R"({"op":"icons_preview","params":{"platform":"Android"}})");
       AIVERSE_TEST_CHECK(!android.has("error") && !android["result"].getBool("changed"));
       const auto& variants=android["result"]["result"]["variants"].items();
       AIVERSE_TEST_CHECK(std::any_of(variants.begin(),variants.end(),[](const Json& icon){
           return icon.getStr("label")=="ADAPT CIRCLE" && icon.getInt("size")>0 && icon.getStr("png").starts_with("iVBOR");
       }));
       const auto animation=call("studio.op",R"({"op":"animation_graph_edit","params":{"action":"new"}})");
       AIVERSE_TEST_CHECK(!animation.has("error")&&!animation["result"].getBool("changed"));
       AIVERSE_TEST_CHECK(animation["result"]["result"]["graph"].getStr("output")=="bind");
       const auto graphCatalog=call("studio.op",R"({"op":"animation_graph_list","params":{}})");
       const auto& rigItems=graphCatalog["result"]["result"]["rigItemPresets"];
       AIVERSE_TEST_CHECK(rigItems.size()==7&&rigItems["effectors"].getStr("target")=="target");
       AIVERSE_TEST_CHECK(rigItems["joints"]["min"].size()==3&&rigItems["legs"].has("hip")&&rigItems["obstacles"].getStr("kind")=="box");
       const auto contacts=call("studio.op",R"({"op":"animation_graph_edit","params":{"action":"addContacts","id":"body","index":0,"graph":{"nodes":[{"id":"body","kind":"rig","units":[{"op":"fullBodyIK","bone":"hip","effectors":[{"bone":"toe"}]}]}]}}})");
       const auto& settings=contacts["result"]["result"]["graph"]["nodes"][0]["units"][0]["contacts"];
       AIVERSE_TEST_CHECK(settings["feet"][0].getStr("bone")=="toe"&&settings["capsules"][0].getStr("bone")=="hip");
       const auto surface=call("studio.op",R"({"op":"animation_graph_edit","params":{"action":"addUnit","kind":"surfaceLocomotion","id":"body","graph":{"nodes":[{"id":"body","kind":"rig","units":[]}]}}})");
       const auto& surfaceGraph=surface["result"]["result"]["graph"];
       AIVERSE_TEST_CHECK(surfaceGraph["parameters"]["velocity"].size()==3&&surfaceGraph["nodes"][0]["units"][0].getStr("op")=="surfaceLocomotion");
       Json save=Json::object(),args=Json::object();
       args.set("name",Json::string("pose_revision"));
       args.set("graph",animation["result"]["result"]["graph"]);
       save.set("op",Json::string("animation_graph_set"));save.set("params",args);
       const auto initial=call("studio.op",save.dump().c_str());
       const auto version=initial["result"]["result"].getStr("revision");
       AIVERSE_TEST_CHECK(version.size()==64);
       args.set("expectedRevision",Json::string(version));
       auto changed=args["graph"];changed.set("model",Json::string("content/actor.gltf"));args.set("graph",changed);
       save.set("params",args);
       AIVERSE_TEST_CHECK(!call("studio.op",save.dump().c_str()).has("error"));
       AIVERSE_TEST_CHECK(call("studio.op",save.dump().c_str()).has("error"));
       AIVERSE_TEST_CHECK(call("studio.op",R"({"op":"animation_graph_list","expectedProject":"/wrong","params":{}})").has("error"));
       const auto materials=call("studio.materialCreate",R"({"name":"docked_steel","preset":"metal"})");
       const auto assetPath=materials["result"]["files"].getStr("docked_steel");
       AIVERSE_TEST_CHECK(assetPath=="content/materials/docked_steel.material.json");
       AIVERSE_TEST_CHECK(Json::parseFile((project/assetPath).string()).isObject());
       AIVERSE_TEST_CHECK(Json::parseFile((project/"materials.json").string())["materials"].getStr("docked_steel")==assetPath);
       AIVERSE_TEST_CHECK(call("studio.assets")["result"]["files"].dump().find("materials/docked_steel.material.json")!=std::string::npos);
       const auto legacyFile=project/"content/materials/stone.material.json";
       auto stone=Json::parseFile(legacyFile.string());stone.set("roughness",Json::number(0.125));
       std::ofstream(legacyFile)<<stone.dump();
       AIVERSE_TEST_CHECK(call("studio.materials")["result"]["library"]["stone"]["roughness"].toDouble()==0.125);
       const char* create=R"({"op":"shader_graph_create","params":{"name":"studio_tint","domain":"material"}})";
       AIVERSE_TEST_CHECK(!call("studio.op",create).has("error"));
       AIVERSE_TEST_CHECK(call("studio.op",create).has("error"));
       auto graph=call("studio.shaderGraphGet",R"({"name":"studio_tint"})");
       AIVERSE_TEST_CHECK(graph["result"]["graph"].getStr("domain")=="material");
       auto preview=Json::object();preview.set("projectPath",Json::string(session.workspace().project.dir));
       preview.set("name",Json::string("studio_tint"));preview.set("graph",graph["result"]["graph"]);
       AIVERSE_TEST_CHECK(!call("studio.shaderGraphPreview",preview.dump().c_str()).has("error"));
       AIVERSE_TEST_CHECK(session.previewGraphFactors()["studioGraphDraft"]==graph["result"]["graph"]);
       AIVERSE_TEST_CHECK(!call("studio.materialPreview",R"({"name":"stone"})").has("error"));
       AIVERSE_TEST_CHECK(session.previewMaterialName()=="stone");
       const auto materialRevision=session.previewMaterialRevision();
       AIVERSE_TEST_CHECK(!call("studio.shaderGraphPreview",preview.dump().c_str()).has("error"));
       AIVERSE_TEST_CHECK(session.previewMaterialName()=="stone" && session.previewMaterialRevision()==materialRevision);
       AIVERSE_TEST_CHECK(session.previewGraphTitle()=="Shader Graph / studio_tint");
       Json rigRequest=Json::parse(R"({"rig":{"version":1,"morphs":[{"parameter":"jawOpen","node":"Face","target":"jawOpen"}],"rig":[]}})");
       const auto rigAssets=call("studio.assets");
       for(const auto& asset:rigAssets["result"]["files"].items())
           if(asset.getStr("path")=="rock.glb")rigRequest.set("guid",asset["guid"]);
       AIVERSE_TEST_CHECK(!call("studio.facialPreviewBind",rigRequest.dump().c_str()).has("error"));
       AIVERSE_TEST_CHECK(session.rigPreviewBinding().bound()&&!session.facialPreviewBinding().bound());
       AIVERSE_TEST_CHECK(session.previewGraphTitle()=="Shader Graph / studio_tint"&&session.previewMaterialName()=="stone");
       AIVERSE_TEST_CHECK(!call("studio.humanCreatorPreview",R"({"enabled":true})").has("error"));
       AIVERSE_TEST_CHECK(session.facialPreviewBinding().creatorMode()&&session.rigPreviewBinding().bound());
       AIVERSE_TEST_CHECK(session.previewMaterialName()=="stone"&&session.previewGraphTitle()=="Shader Graph / studio_tint");
       AIVERSE_TEST_CHECK(!call("studio.humanCreatorPreview",R"({"enabled":false})").has("error"));
       AIVERSE_TEST_CHECK(!call("studio.facialPreviewBind",R"({"guid":""})").has("error"));
       AIVERSE_TEST_CHECK(!session.rigPreviewBinding().bound());
       auto particle=Json::parse(R"({"version":1,"domain":"particle","nodes":[{"id":"age","op":"particleAge01"}],"outputs":{"color":"age"}})");
       preview.set("graph",particle);
       AIVERSE_TEST_CHECK(!call("studio.shaderGraphPreview",preview.dump().c_str()).has("error"));
       AIVERSE_TEST_CHECK(session.previewGraphFactors()["studioGraphDraft"]==particle);
       AIVERSE_TEST_CHECK(session.previewMaterialName()=="stone");
       preview.set("graph",Json::object());
       AIVERSE_TEST_CHECK(call("studio.shaderGraphPreview",preview.dump().c_str()).has("error"));
       AIVERSE_TEST_CHECK(!call("studio.materialPreview",R"({"name":""})").has("error"));
       AIVERSE_TEST_CHECK(session.previewMaterialName().empty());
       AIVERSE_TEST_CHECK(!call("studio.op",R"({"op":"shader_graph_apply","params":{"name":"studio_tint","target":"studio_brass"}})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.op",R"({"op":"shader_graph_create","params":{"name":"bad_domain","domain":"invalid"}})").has("error"));
   }
   // Rules form: read own + resolved types, edit a type field (validated and saved), reject other paths.
   {
       auto rules=call("studio.rules");
       AIVERSE_TEST_CHECK(!rules.has("error") && rules["result"]["types"].has("tree"));
       AIVERSE_TEST_CHECK(rules["result"].getStr("acquireOrder")=="nearest");
       AIVERSE_TEST_CHECK(call("studio.rulesSet",R"({"path":"targeting.acquireOrder","value":"rowMajor"})")["result"].getStr("acquireOrder")=="rowMajor");
       AIVERSE_TEST_CHECK(call("studio.rulesSet",R"({"path":"targeting.acquireOrder","value":"invalid"})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.rules")["result"].getStr("acquireOrder")=="rowMajor");
       AIVERSE_TEST_CHECK(call("studio.rulesSet",R"({"path":"targeting.acquireOrder","value":null})")["result"].getStr("acquireOrder")=="nearest");
       AIVERSE_TEST_CHECK(!call("studio.rulesSet",R"({"path":"types.tree.selectable","value":false})").has("error"));
       AIVERSE_TEST_CHECK(!call("studio.rules")["result"]["resolved"]["tree"].getBool("selectable",true));
       AIVERSE_TEST_CHECK(!call("studio.rulesSet",R"({"path":"types.tree.health.max","value":150})").has("error"));
       rules=call("studio.rules");
       AIVERSE_TEST_CHECK(rules["result"]["resolved"]["tree"]["health"]["max"].integer()==150);
       AIVERSE_TEST_CHECK(call("studio.rulesSet",R"({"path":"resources.gold","value":1})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.rulesSet",R"({"path":"triggers","value":{}})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.rulesSet",R"({"path":"damageTable","value":[]})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.rulesSet",R"({"path":"damageTable","value":{"strike":{"plate":"bad"}}})").has("error"));
       AIVERSE_TEST_CHECK(!call("studio.rulesSet",R"({"path":"damageTable","value":{"strike":{"plate":0.75}}})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.rules")["result"]["damageTable"]["strike"]["plate"].toDouble()==0.75);
       AIVERSE_TEST_CHECK(!call("studio.rulesSet",R"({"path":"triggers","value":[]})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.rules")["result"]["triggers"].isArray());
       AIVERSE_TEST_CHECK(!call("studio.rulesSet",R"({"path":"types.tree.health.max","value":null})").has("error"));
       AIVERSE_TEST_CHECK(!call("studio.rules")["result"]["types"]["tree"]["health"].has("max"));
       auto query=Json::object();query.set("section",Json::string("global"));
       const std::string before=call("studio.rulesDocument",query.dump().c_str())["result"].getStr("text");
       auto globals=Json::parse(before);
       AIVERSE_TEST_CHECK(!globals.has("types")&&!globals.has("triggers"));
       globals.set("supplyCap",Json::number(int64_t(123)));
       query.set("projectPath",Json::string(session.workspace().project.dir));
       query.set("original",Json::string(before));query.set("value",globals);
       AIVERSE_TEST_CHECK(!call("studio.rulesDocumentSave",query.dump().c_str()).has("error"));
       AIVERSE_TEST_CHECK(call("studio.rulesDocumentSave",query.dump().c_str()).has("error"));
       query.set("section",Json::string("document"));
       auto full=Json::parse(call("studio.rulesDocument",query.dump().c_str())["result"].getStr("text"));
       AIVERSE_TEST_CHECK(full["types"].has("tree")&&full.getInt("supplyCap")==123);
       query.set("original",Json::string(full.dump()));query.set("value",full);
       AIVERSE_TEST_CHECK(!call("studio.rulesDocumentSave",query.dump().c_str()).has("error"));
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
       auto& workspace=const_cast<editor::Workspace&>(session.workspace());
       workspace.appendBuildLog("новая строка");
       const auto replay=call("studio.buildLog",R"({"from":999999,"run":999999})");
       AIVERSE_TEST_CHECK(replay["result"]["from"].integer()==0);
       AIVERSE_TEST_CHECK(replay["result"]["text"].str().find("новая строка")!=std::string::npos);
       AIVERSE_TEST_CHECK(replay["result"]["run"].integer()==log["result"]["run"].integer());
   }
   // Align: one undo step, labelled in History; History jump undoes/redoes to a position.
   {
       auto& doc=const_cast<editor::Workspace&>(session.workspace()).map;
       if(doc.data.entities.size()>=2) {
           const std::string a="entity:"+std::to_string(0),b="entity:"+std::to_string(1);
           const float bz=doc.data.entities[1].z;
           auto params=Json::object(),ids=Json::array();ids.push(Json::string(a));ids.push(Json::string(b));
           params.set("entityIds",ids);params.set("mode",Json::string("minX"));
           auto request=Json::object();request.set("id",Json::number(int64_t(1)));request.set("method",Json::string("studio.entitiesAlign"));request.set("params",params);
           const size_t depth=doc.undoDepth();
           AIVERSE_TEST_CHECK(!session.dispatch(request).has("error"));
           AIVERSE_TEST_CHECK(doc.undoDepth()==depth+1);
           AIVERSE_TEST_CHECK(std::abs(doc.data.entities[0].x-doc.data.entities[1].x)<1e-3 && doc.data.entities[1].z==bz);
           auto history=call("studio.history");
           AIVERSE_TEST_CHECK(!history.has("error") && history["result"]["steps"].items().back().getStr("label")=="Align");
           const int64_t pos=history["result"]["position"].integer();
           auto jump=Json::parse(R"({"position":0})");jump.set("position",Json::number(pos-1));
           AIVERSE_TEST_CHECK(!call("studio.historyJump",jump.dump(-1).c_str()).has("error") && doc.undoDepth()==depth);
           jump.set("position",Json::number(pos));
           AIVERSE_TEST_CHECK(!call("studio.historyJump",jump.dump(-1).c_str()).has("error") && doc.undoDepth()==depth+1);
           params.set("mode",Json::string("sideways"));request.set("params",params);
           AIVERSE_TEST_CHECK(session.dispatch(request).has("error"));
       }
   }
   // Asset usage: named content files are used; a file no data names is listed as unused.
   {
       std::ofstream(project/"content"/"orphan.png")<<"png";
       const auto all=call("studio.assetUsage");
       AIVERSE_TEST_CHECK(!all.has("error"));
       bool orphan=false,tree=false;
       for(const auto& p:all["result"]["unused"].items()){orphan|=p.str()=="orphan.png";tree|=p.str()=="tree.gltf";}
       AIVERSE_TEST_CHECK(orphan && !tree);
       const auto refs=call("studio.assetUsage",R"({"path":"tree.gltf"})");
       AIVERSE_TEST_CHECK(!refs.has("error") && refs["result"]["mentions"].size()>=1);
       AIVERSE_TEST_CHECK(call("studio.assetUsage",R"({"path":"../x"})").has("error"));
   }
   // General manifest editing: native project fence, original value and Project history.
   {
       const auto before=call("studio.op",R"({"op":"game_info","params":{}})");
       AIVERSE_TEST_CHECK(!before.has("error") && !before["result"].getBool("changed"));
       const auto original=before["result"]["result"].getStr("originalManifest");
       const auto bytes=editor::readProjectFileBytes(project/"game.json");
       auto value=Json::parse(original);value.set("name",Json::string("Settings acceptance"));
       Json input=Json::object();input.set("path",Json::string(""));input.set("value",value);
       input.set("expectedManifest",Json::string(original));
       Json request=Json::object();request.set("op",Json::string("set_manifest"));request.set("params",input);
       AIVERSE_TEST_CHECK(call("studio.op",request.dump().c_str()).has("error"));
       request.set("expectedProject",Json::string("wrong project"));
       AIVERSE_TEST_CHECK(call("studio.op",request.dump().c_str()).has("error"));
       request.set("expectedProject",Json::string(project.string()));
       AIVERSE_TEST_CHECK(!call("studio.op",request.dump().c_str()).has("error"));
       AIVERSE_TEST_CHECK(Json::parseFile((project/"game.json").string()).getStr("name")=="Settings acceptance");
       AIVERSE_TEST_CHECK(call("studio.op",request.dump().c_str()).has("error")); // stale original
       auto& owner=const_cast<editor::Workspace&>(session.workspace());
       auto history=owner.call("project_history_status",Json::object());
       Json step=Json::object();step.set("direction",Json::string("undo"));step.set("expectedVersion",history["version"]);
       AIVERSE_TEST_CHECK(!owner.call("project_history_step",step).has("error"));
       AIVERSE_TEST_CHECK(editor::readProjectFileBytes(project/"game.json")==bytes);
       step.set("direction",Json::string("redo"));step.set("expectedVersion",owner.call("project_history_status",Json::object())["version"]);
       AIVERSE_TEST_CHECK(!owner.call("project_history_step",step).has("error"));
       step.set("direction",Json::string("undo"));step.set("expectedVersion",owner.call("project_history_status",Json::object())["version"]);
       AIVERSE_TEST_CHECK(!owner.call("project_history_step",step).has("error"));
       input.set("value",Json::array());request.set("params",input);
       AIVERSE_TEST_CHECK(call("studio.op",request.dump().c_str()).has("error"));
       AIVERSE_TEST_CHECK(owner.project.manifest.dump()==original);
       input.set("value",value);request.set("params",input);
       std::ofstream(project/"game.json")<<bytes<<"\n";
       AIVERSE_TEST_CHECK(call("studio.op",request.dump().c_str()).has("error")); // external file edit
       AIVERSE_TEST_CHECK(owner.project.manifest.dump()==original);
       AIVERSE_TEST_CHECK(editor::readProjectFileBytes(project/"game.json")==bytes+"\n");
       std::ofstream(project/"game.json")<<bytes;
       input.erase("value");input.set("valueJson",Json::string("{"));request.set("params",input);
       AIVERSE_TEST_CHECK(call("studio.op",request.dump().c_str()).has("error"));
       value.set("seed",Json::number(int64_t(9007199254740993LL)));
       input.set("valueJson",Json::string(value.dump()));request.set("params",input);
       AIVERSE_TEST_CHECK(!call("studio.op",request.dump().c_str()).has("error"));
       AIVERSE_TEST_CHECK(Json::parseFile((project/"game.json").string())["seed"].numberLiteral()=="9007199254740993");
       step.set("expectedVersion",owner.call("project_history_status",Json::object())["version"]);
       AIVERSE_TEST_CHECK(!owner.call("project_history_step",step).has("error"));
       AIVERSE_TEST_CHECK(editor::readProjectFileBytes(project/"game.json")==bytes);
   }
   // Shared project data editing uses the Workspace owner and rejects stale drafts.
   {
       std::ofstream(project/"audio.json") << "{}\n";
       AIVERSE_TEST_CHECK(call("studio.ops",R"({"panel":"project_data"})")["result"]["ops"].size()==2);
       Json input=Json::object();input.set("path",Json::string("audio.json"));
       input.set("text",Json::string("{\"types\":{}}\n"));input.set("expectedText",Json::string("{}\n"));
       Json request=Json::object();request.set("op",Json::string("game_file_write"));request.set("params",input);
       AIVERSE_TEST_CHECK(call("studio.op",request.dump().c_str()).has("error"));
       request.set("expectedProject",Json::string(project.string()));
       AIVERSE_TEST_CHECK(!call("studio.op",request.dump().c_str()).has("error"));
       AIVERSE_TEST_CHECK(call("studio.op",R"({"op":"game_file_read","params":{"path":"audio.json"}})")["result"]["result"].getStr("text")=="{\"types\":{}}\n");
       AIVERSE_TEST_CHECK(call("studio.op",request.dump().c_str()).has("error")); // old before-image
       input.set("expectedText",Json::string("{\"types\":{}}\n"));input.set("text",Json::string("{"));
       request.set("params",input);
       AIVERSE_TEST_CHECK(call("studio.op",request.dump().c_str()).has("error")); // invalid JSON preserves bytes
       AIVERSE_TEST_CHECK(Json::parseFile((project/"audio.json").string()).has("types"));
       input.set("path",Json::string("../outside.json"));request.set("params",input);
       AIVERSE_TEST_CHECK(call("studio.op",request.dump().c_str()).has("error"));
       input.set("path",Json::string("rules.json"));request.set("params",input);
       AIVERSE_TEST_CHECK(call("studio.op",request.dump().c_str()).has("error"));
       std::error_code linkError;fs::create_symlink(project/"audio.json",project/"audio-link.json",linkError);
       if(!linkError) AIVERSE_TEST_CHECK(call("studio.op",R"({"op":"game_file_read","params":{"path":"audio-link.json"}})").has("error"));
   }
   // Tool panels: op lists carry schemas, only allowlisted ops run, edits count as History steps.
   {
       const auto ops=call("studio.ops",R"({"panel":"terrain"})");
       AIVERSE_TEST_CHECK(!ops.has("error") && ops["result"]["ops"].size()>=5);
       AIVERSE_TEST_CHECK(call("studio.ops",R"({"panel":"nope"})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.op",R"({"op":"map_new","params":{}})").has("error"));
       const auto info=call("studio.op",R"({"op":"map_info","params":{}})");
       AIVERSE_TEST_CHECK(!info.has("error") && !info["result"].getBool("changed"));
       auto stepProject = [&](const char* direction) {
           Json step = Json::object();
           step.set("direction", Json::string(direction));
           step.set("expectedVersion", call("studio.projectHistory")["result"]["version"]);
           return call("studio.projectHistoryStep", step.dump().c_str());
       };
       const auto ring = call("studio.op",R"({"op":"overlay_set","params":{"template":"local_order_ring","id":"command_ring"}})");
       AIVERSE_TEST_CHECK(!ring.has("error"));
       AIVERSE_TEST_CHECK(call("studio.op",R"({"op":"overlay_get","params":{"id":"command_ring"}})")["result"]["result"]["rules"]["event"].getStr("on")=="order");
       AIVERSE_TEST_CHECK(!call("studio.rulesSet",R"({"path":"types.tree.view.showUpgradeTarget","value":true})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.rulesSet",R"({"path":"types.tree.view.showUpgradeTarget","value":"invalid"})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.rules")["result"]["types"]["tree"]["view"].getBool("showUpgradeTarget"));
       AIVERSE_TEST_CHECK(!stepProject("undo").has("error"));
       AIVERSE_TEST_CHECK(!call("studio.rules")["result"]["types"]["tree"]["view"].has("showUpgradeTarget"));
       AIVERSE_TEST_CHECK(!stepProject("undo").has("error"));
       AIVERSE_TEST_CHECK(call("studio.op",R"({"op":"overlay_get","params":{"id":"command_ring"}})").has("error"));
       AIVERSE_TEST_CHECK(!stepProject("redo").has("error"));
       AIVERSE_TEST_CHECK(!stepProject("redo").has("error"));
       const auto savedRules = Json::parseFile((project/"rules.json").string());
       AIVERSE_TEST_CHECK(savedRules["types"]["tree"]["view"].getBool("showUpgradeTarget"));
       AIVERSE_TEST_CHECK(!call("studio.rulesSet",R"({"path":"types.tree.view.showUpgradeTarget","value":null})").has("error"));
       AIVERSE_TEST_CHECK(!call("studio.op",R"({"op":"overlay_delete","params":{"id":"command_ring"}})").has("error"));
       const auto readMeadow = [&] { return call("studio.op",R"({"op":"terraweave_meadow_get","params":{}})"); };
       AIVERSE_TEST_CHECK(readMeadow()["result"]["result"]["meadow"].isNull());
       AIVERSE_TEST_CHECK(!call("studio.op",R"({"op":"terraweave_meadow_set","params":{"preset":"wetland"}})").has("error"));
       AIVERSE_TEST_CHECK(readMeadow()["result"]["result"]["meadow"].str()=="wetland");
       editor::MapDoc reloadedMeadow;
       AIVERSE_TEST_CHECK(reloadedMeadow.loadJson(session.workspace().map.toJson(), nullptr));
       AIVERSE_TEST_CHECK(reloadedMeadow.data.extra["grass"]["meadow"].str()=="wetland");
       const auto depthWithMeadow = session.workspace().map.undoDepth();
       AIVERSE_TEST_CHECK(call("studio.op",R"({"op":"terraweave_meadow_set","params":{"config":{"species":[{"preset":"unknown"}]}}})").has("error"));
       AIVERSE_TEST_CHECK(session.workspace().map.undoDepth()==depthWithMeadow);
       AIVERSE_TEST_CHECK(!call("studio.undo").has("error"));
       AIVERSE_TEST_CHECK(readMeadow()["result"]["result"]["meadow"].isNull());
       const auto merged=call("studio.op",R"({"op":"rules_merge","params":{"path":"fogOfWar","value":{"mode":"explored"}}})");
       AIVERSE_TEST_CHECK(!merged.has("error") && merged["result"].getBool("changed"));
       const auto fog=call("studio.op",R"({"op":"rules_get","params":{"path":"fogOfWar"}})");
       AIVERSE_TEST_CHECK(fog["result"]["result"]["value"].getStr("mode")=="explored");
       // TerraWeave Generator panel: its five ops, forest recipe write is a History step.
       const auto terra=call("studio.ops",R"({"panel":"terraweave_generator"})");
       AIVERSE_TEST_CHECK(!terra.has("error") && terra["result"]["ops"].size()==5);
       const auto forestGet=call("studio.op",R"({"op":"terraweave_forest_get","params":{}})");
       AIVERSE_TEST_CHECK(!forestGet.has("error") && !forestGet["result"].getBool("changed"));
       const auto forestSet=call("studio.op",R"({"op":"terraweave_forest_set","params":{"densityPermille":500}})");
       AIVERSE_TEST_CHECK(!forestSet.has("error") && forestSet["result"].getBool("changed"));
       AIVERSE_TEST_CHECK(call("studio.op",R"({"op":"terraweave_forest_set","params":{"spacingCm":5}})").has("error"));
       const auto input=call("studio.inputGet");
       AIVERSE_TEST_CHECK(!input.has("error") && input["result"]["keys"].size()>20 && !input["result"].getBool("exists"));
       const auto bad=call("studio.inputSet",R"({"asset":{"version":1,"actions":[{"name":"Jump","type":"nope"}],"contexts":[]}})");
       AIVERSE_TEST_CHECK(bad.has("error"));
       const auto saved=call("studio.inputSet",R"({"asset":{"version":1,"actions":[{"name":"Jump","type":"bool"}],
           "contexts":[{"name":"Play","priority":0,"active":true,"mappings":[{"id":1,"action":"Jump","control":{"device":"keyboard","code":"Space"},"consume":false}]}]}})");
       AIVERSE_TEST_CHECK(!saved.has("error") && saved["result"].getBool("exists") && saved["result"]["asset"]["contexts"].size()==1);
       std::ofstream(project/"ui.json")<<R"({"icon":"content/missing_icon.png"})";
       const auto audit=call("studio.assetAudit");
       bool found=false;
       for(const auto& r:audit["result"]["missing"].items())found|=r.getStr("reference")=="content/missing_icon.png";
       AIVERSE_TEST_CHECK(!audit.has("error") && found);
   }
#if !defined(_WIN32)
   // Exercise the actual Agent session bridge without launching a remote client.
   {
       const auto bin=project/"fixture-client";
       fs::create_directories(bin);
       const auto cli=bin/"codex";
       std::ofstream(cli)<<R"(#!/bin/sh
printf '%s\n' "$@" > fixture-args
cat > fixture-input
echo '{"type":"item.started","item":{"type":"mcp_tool_call","tool":"rules_list"}}'
if test -f fixture-wait; then
  trap '' TERM
  touch fixture-ready
  sleep 30
fi
echo '{"type":"item.completed","item":{"type":"agent_message","text":"Fixture completed"}}'
)";
       fs::permissions(cli,fs::perms::owner_all);
       const std::string oldPath=std::getenv("PATH");
       setenv("PATH",(bin.string()+":"+oldPath).c_str(),1);
       AIVERSE_TEST_CHECK(editor::agentCliPath("codex")==cli.string());
       session.setMcpTool(cli.string());
       Json request=Json::object();
       request.set("client",Json::string("codex"));request.set("prompt",Json::string("Review fixture economy"));
       request.set("expectedProject",Json::string("wrong-project"));
       AIVERSE_TEST_CHECK(call("studio.agentStart",request.dump().c_str()).has("error"));
       request.set("expectedProject",Json::string(session.workspace().project.dir));
       auto start=[&]{return call("studio.agentStart",request.dump().c_str());};
       auto finish=[&]{
           for(int i=0;i<150;++i){
               const auto result=call("studio.agentInfo")["result"];
               if(!result.getBool("running"))return result;
               std::this_thread::sleep_for(std::chrono::milliseconds(30));
           }
           throw std::runtime_error("Local agent did not finish");
       };
       AIVERSE_TEST_CHECK(!start().has("error"));
       const auto completed=finish();
       AIVERSE_TEST_CHECK(completed.getStr("result")=="Fixture completed" && completed.getStr("runClient")=="codex");
       std::ifstream argvFile(project/"fixture-args"),inputFile(project/"fixture-input");
       const std::string args{std::istreambuf_iterator<char>(argvFile),{}},input{std::istreambuf_iterator<char>(inputFile),{}};
       AIVERSE_TEST_CHECK(args.find("mcp_servers.aiverse.args=")!=std::string::npos && args.find("--no-editor")!=std::string::npos &&
           args.find(std::to_string(completed["port"].integer()))!=std::string::npos && input.find("Review fixture economy")!=std::string::npos);
       request.set("useWizardBrief",Json::fromBool(true));
       AIVERSE_TEST_CHECK(start().has("error"));
       const std::string wizardTask="Write the final report to WIZARD.md. "+std::string(20500,'w');
       std::ofstream(project/"WIZARD_TASK.md")<<wizardTask;
       AIVERSE_TEST_CHECK(!start().has("error"));
       finish();
       std::ifstream taskFile(project/"fixture-input");
       const std::string sent{std::istreambuf_iterator<char>(taskFile),{}};
       AIVERSE_TEST_CHECK(sent.find(wizardTask)!=std::string::npos);
       fs::remove(project/"WIZARD_TASK.md");
       const std::string report="# Fixture\n\n## Agent\nFinished.";
       std::ofstream(project/"WIZARD.md")<<report;
       Json read=Json::object();read.set("reportForProject",Json::string(session.workspace().project.dir));
       AIVERSE_TEST_CHECK(call("studio.agentInfo",read.dump().c_str())["result"].getStr("wizardReport")==report);
       read.set("reportForProject",Json::string("/other"));
       AIVERSE_TEST_CHECK(call("studio.agentInfo",read.dump().c_str()).has("error"));
       request.set("useWizardBrief",Json::fromBool(false));
       std::ofstream(project/"fixture-wait")<<"wait";
       AIVERSE_TEST_CHECK(!start().has("error"));
       for(int i=0;i<150 && !fs::exists(project/"fixture-ready");++i)std::this_thread::sleep_for(std::chrono::milliseconds(30));
       AIVERSE_TEST_CHECK(fs::exists(project/"fixture-ready"));
       AIVERSE_TEST_CHECK(call("studio.agentInfo")["result"]["lines"].dump().find("rules_list")!=std::string::npos);
       AIVERSE_TEST_CHECK(!call("studio.agentStop").has("error"));
       const auto stopped=finish();
       AIVERSE_TEST_CHECK(!stopped.getBool("running") && stopped["lines"].dump().find("Stopped by user")!=std::string::npos);
       AIVERSE_TEST_CHECK(!start().has("error"));
       call("studio.agentStop");
       AIVERSE_TEST_CHECK(!finish().getBool("running"));
       call("studio.agentReset");
       session.setMcpTool("");
       setenv("PATH",oldPath.c_str(),1);
   }
#endif
   // Agent link: aiversemcp's protocol (hello + token, then calls); an edit is a History step "Agent · …" and a change.
   {
       const auto info=call("studio.agentInfo");
       AIVERSE_TEST_CHECK(!info.has("error") && info["result"]["port"].integer()>0);
       AIVERSE_TEST_CHECK(!info["result"].getBool("hasWizardBrief"));
       std::ofstream(project/"WIZARD_TASK.md")<<"Review the harbor markets.";
       Json briefRequest=Json::object();
       briefRequest.set("briefForProject",Json::string(session.workspace().project.dir));
       const auto brief=call("studio.agentInfo",briefRequest.dump().c_str());
       AIVERSE_TEST_CHECK(brief["result"].getBool("hasWizardBrief") &&
           brief["result"].getStr("wizardBrief")=="Review the harbor markets." &&
           !brief["result"].getBool("running"));
       AIVERSE_TEST_CHECK(call("studio.agentInfo",R"({"briefForProject":"different-project"})").has("error"));
       std::filesystem::remove(project/"WIZARD_TASK.md");
       const int port=int(info["result"]["port"].integer());
       std::string token;
       AIVERSE_TEST_CHECK(editor::net::readAgentToken(port,token));
       editor::net::LineClient client;
       AIVERSE_TEST_CHECK(client.connect(port,1000));
       auto roundTrip=[&](const std::string& line){
           client.send(line);std::string answer;
           for(int i=0;i<200 && answer.empty();++i){session.pollAgentLink();client.recv(answer,10);}
           return answer.empty()?Json():Json::parse(answer);
       };
       AIVERSE_TEST_CHECK(roundTrip(R"({"id":1,"method":"hello","token":")"+token+R"("})")["result"].getBool("ok"));
       const size_t before=session.workspace().map.data.entities.size();
       const auto placed=roundTrip(R"({"id":2,"method":"call","params":{"name":"entity_place","arguments":{"type":"tree","x":3,"y":3}}})");
       AIVERSE_TEST_CHECK(placed.has("result"));
       const auto after=call("studio.agentInfo");
       const auto history=call("studio.history");
       if(!placed["result"].has("error")) {
           AIVERSE_TEST_CHECK(session.workspace().map.data.entities.size()==before+1);
           AIVERSE_TEST_CHECK(after["result"]["changes"].size()==1 && after["result"]["changes"][0].getBool("undoable"));
           const auto& steps=history["result"]["steps"];
           AIVERSE_TEST_CHECK(steps.size()>0 && steps[steps.size()-1].getStr("label")=="Agent · Entity place");
           AIVERSE_TEST_CHECK(!call("studio.undo").has("error") && session.workspace().map.data.entities.size()==before);
       } else AIVERSE_TEST_CHECK(after["result"]["changes"].size()==1 && !after["result"]["changes"][0].getBool("ok"));
       // view_screenshot waits for the host's capture; the host answers through finishAgentScreenshot.
       client.send(R"({"id":3,"method":"call","params":{"name":"view_screenshot","arguments":{}}})");
       for(int i=0;i<100 && !session.agentScreenshotPending();++i){session.pollAgentLink();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
       AIVERSE_TEST_CHECK(session.agentWindowScreenshotPending() && !session.agentGameScreenshotPending());
       session.finishAgentScreenshot("iVBORw0KGgo=",2,2,"");
       std::string shot;client.recv(shot,1000);
       AIVERSE_TEST_CHECK(!shot.empty() && Json::parse(shot)["result"].getStr("png_base64")=="iVBORw0KGgo=" && !session.agentScreenshotPending());
       AIVERSE_TEST_CHECK(Json::parse(shot)["result"].getStr("view")=="HTML Studio window");
       auto sceneRequest=[](const char* options){
           return std::string(R"({"id":4,"method":"call","params":{"name":"view_screenshot","arguments":{"window":"scene",)")+options+"}}}";
       };
       for(const char* invalid:{R"("width":512)",R"("width":4096,"height":4096)",R"("width":256.5,"height":256)",
               R"("x":1)",R"("yaw":"90")",R"("y":1000001)",R"("clean":false,"width":256,"height":256)",R"("device":"none")"}){
           AIVERSE_TEST_CHECK(roundTrip(sceneRequest(invalid))["result"].has("error") && !session.agentScreenshotPending());
       }
       client.send(sceneRequest(R"("x":1,"z":3,"distance":100000,"pitch":100,"yaw":90,"width":2048,"height":4096)"));
       for(int i=0;i<100 && !session.agentScreenshotPending();++i){session.pollAgentLink();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
       AIVERSE_TEST_CHECK(session.agentScreenshotPending());
       const auto& options=session.agentSceneScreenshotOptions();
       render::Camera base;base.target.y=42;
       const auto camera=options.camera(base);
       AIVERSE_TEST_CHECK(options.clean && options.width==2048 && options.height==4096 && camera.target.x==1 &&
           camera.target.y==0 && camera.target.z==3 && base.target.y==42 && camera.distance==base.maxDistance &&
           std::abs(camera.pitch-1.55334f)<.0001f && std::abs(camera.yaw-1.5708f)<.0001f);
       session.finishAgentScreenshot("",0,0,"fixture finished");
       client.recv(shot,1000);
       client.close();
       AIVERSE_TEST_CHECK(call("studio.agentStart",R"({"prompt":""})").has("error"));
   }
   // Terrain brush: a stroke of several dabs is one History step; Stats counts the map.
   {
       AIVERSE_TEST_CHECK(call("studio.sceneBrush",R"({"op":"nope"})").has("error"));
       const auto armed=call("studio.sceneBrush",R"({"armed":true,"kind":"terrain","op":"raise","radius":3,"strength":1})");
       AIVERSE_TEST_CHECK(armed["result"].getBool("armed"));
       const size_t depth=session.workspace().map.undoDepth();
       std::string err;
       AIVERSE_TEST_CHECK(session.strokeBegin(4,4,&err));
       AIVERSE_TEST_CHECK(session.strokeActive()&&session.workspace().map.undoDepth()==depth);
       AIVERSE_TEST_CHECK(call("studio.undo").has("error"));
       AIVERSE_TEST_CHECK(call("studio.history")["result"].getBool("strokeActive"));
       AIVERSE_TEST_CHECK(session.strokeDab(8,4,&err) && session.strokeDab(12,4,&err));
       AIVERSE_TEST_CHECK(session.strokeEnd(&err));
       AIVERSE_TEST_CHECK(session.workspace().map.undoDepth()==depth+1);
       const auto steps=call("studio.history")["result"]["steps"];
       AIVERSE_TEST_CHECK(steps.size()>0 && steps[steps.size()-1].getStr("label")=="Terrain brush");
       AIVERSE_TEST_CHECK(!call("studio.undo").has("error") && session.workspace().map.undoDepth()==depth);
       const auto originalDetail=session.workspace().map.data.detail;
       AIVERSE_TEST_CHECK(!call("studio.sceneBrush",R"({"op":"stamp","stamp":"dunes","angle":90,"seed":17,"hardness":0.75})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.sceneBrush",R"({"seed":1.5})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.sceneBrush",R"({"stamp":"other"})").has("error"));
       AIVERSE_TEST_CHECK(session.strokeBegin(4,4,&err));
       const auto stamped=session.workspace().map.data.detail;
       AIVERSE_TEST_CHECK(stamped!=originalDetail);
       AIVERSE_TEST_CHECK(session.strokeDab(12,12,&err) && session.strokeEnd(&err));
       AIVERSE_TEST_CHECK(session.workspace().map.data.detail==stamped && session.workspace().map.undoDepth()==depth+1);
       AIVERSE_TEST_CHECK(!call("studio.undo").has("error") && session.workspace().map.data.detail==originalDetail);
       AIVERSE_TEST_CHECK(!call("studio.redo").has("error") && session.workspace().map.data.detail==stamped);
       AIVERSE_TEST_CHECK(!call("studio.undo").has("error"));
       AIVERSE_TEST_CHECK(call("studio.sceneBrush",R"({"strength":-2})")["result"]["strength"].toDouble()==-2);
       AIVERSE_TEST_CHECK(session.strokeBegin(4,4,&err) && session.strokeEnd(&err));
       AIVERSE_TEST_CHECK(session.workspace().map.data.detail!=stamped && session.workspace().map.data.detail!=originalDetail);
       AIVERSE_TEST_CHECK(!call("studio.undo").has("error"));
       AIVERSE_TEST_CHECK(call("studio.sceneBrush",R"({"op":"raise"})")["result"]["strength"].toDouble()==1);
       const auto heightDepth=session.workspace().map.undoDepth();
       AIVERSE_TEST_CHECK(!call("studio.sceneBrush",R"({"op":"set","targetHeight":-3.25,"pickHeight":true})").has("error"));
       AIVERSE_TEST_CHECK(!session.strokeBegin(4,4,&err) && session.workspace().map.undoDepth()==heightDepth);
       AIVERSE_TEST_CHECK(!call("studio.sceneBrush",R"({"pickHeight":false})").has("error"));
       AIVERSE_TEST_CHECK(session.strokeBegin(4,4,&err) && session.strokeEnd(&err));
       const auto& heightMap=session.workspace().map.data;
       AIVERSE_TEST_CHECK(heightMap.corners[size_t(4*(heightMap.w+1)+4)]==-52);
       AIVERSE_TEST_CHECK(!call("studio.undo").has("error") && session.workspace().map.undoDepth()==heightDepth);
       AIVERSE_TEST_CHECK(!call("studio.sceneBrush",R"({"terrainGrid":"detail"})").has("error"));
       AIVERSE_TEST_CHECK(session.strokeBegin(4,4,&err) && session.strokeEnd(&err));
       const auto detailHeights=heightMap.detail;
       AIVERSE_TEST_CHECK(heightMap.detailScale>0 && !detailHeights.empty());
       AIVERSE_TEST_CHECK(!call("studio.sceneBrush",R"({"terrainGrid":"base","targetHeight":5})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.sceneBrush",R"({"op":"erode"})").has("error"));
       AIVERSE_TEST_CHECK(session.strokeBegin(4,4,&err) && session.strokeEnd(&err));
       AIVERSE_TEST_CHECK(heightMap.detail==detailHeights && heightMap.corners[size_t(4*(heightMap.w+1)+4)]==80);
       const auto cellMaterials=heightMap.extra["terrainMaterial"];
       const auto splat=heightMap.detailSplat;
       AIVERSE_TEST_CHECK(!call("studio.sceneBrush",R"({"kind":"paint","layer":3})").has("error"));
       AIVERSE_TEST_CHECK(session.strokeBegin(4,4,&err) && session.strokeEnd(&err));
       AIVERSE_TEST_CHECK(heightMap.extra["terrainMaterial"][size_t(4)].str().at(4)=='3' && heightMap.detailSplat==splat);
       AIVERSE_TEST_CHECK(session.strokeBegin(4,4,&err,0,true) && session.strokeEnd(&err));
       AIVERSE_TEST_CHECK(heightMap.extra["terrainMaterial"]==cellMaterials);
       for(int i=0;i<4;++i)AIVERSE_TEST_CHECK(!call("studio.undo").has("error"));
       AIVERSE_TEST_CHECK(session.workspace().map.undoDepth()==heightDepth);
       AIVERSE_TEST_CHECK(!call("studio.sceneBrush",R"({"terrainGrid":"auto"})").has("error"));
       {
           aiverse::editor::MapDoc globe;
           AIVERSE_TEST_CHECK(globe.loadJson(Json::parse(R"({"world":{"bodies":[{"id":"a","faceSize":8},{"id":"b","faceSize":8}]}})"),&err));
           auto& map=session.workspace().map;
           map.data.bodies.swap(globe.data.bodies);
           const auto start=map.toJson();
           AIVERSE_TEST_CHECK(!call("studio.sceneBrush",R"({"kind":"terrain","op":"set","targetHeight":4,"terrainGrid":"base"})").has("error"));
           AIVERSE_TEST_CHECK(!session.strokeBegin(1,0,&err,0,false));
           AIVERSE_TEST_CHECK(session.strokeBegin(1,0,&err,0,false,"b"));
           AIVERSE_TEST_CHECK(!session.strokeDab(0,1,&err,0,false,"a"));
           AIVERSE_TEST_CHECK(map.toJson()==start);
           AIVERSE_TEST_CHECK(session.strokeBegin(1,0,&err,.98,false,"b") && session.strokeDab(.98,0,&err,1,false,"b") && session.strokeEnd(&err));
           const auto& body=map.data.bodies[1];
           AIVERSE_TEST_CHECK(std::count_if(body.corners.begin(),body.corners.end(),[](auto h){return h>0;})>0);
           AIVERSE_TEST_CHECK(map.data.bodies[0].corners!=body.corners);
           AIVERSE_TEST_CHECK(!call("studio.undo").has("error") && map.toJson()==start);
           map.data.bodies.swap(globe.data.bodies);
       }
       const auto sparseDepth=session.workspace().map.undoDepth();
       AIVERSE_TEST_CHECK(!call("studio.sceneBrush",R"({"kind":"sparse","sparseMode":"raise","chunkMetres":16,"depthCm":80,"radius":2})").has("error"));
       AIVERSE_TEST_CHECK(session.strokeBegin(-32,-40,&err) && session.strokeDab(-48,-40,&err) && session.strokeEnd(&err));
       const auto sparse=session.workspace().map.data.extra["terraweaveSparse"];
       AIVERSE_TEST_CHECK(sparse.getInt("chunkMetres")==16 && sparse["chunks"].size()>=2 && sparse["heights"].size()>0);
       AIVERSE_TEST_CHECK(session.workspace().map.undoDepth()==sparseDepth+1);
       AIVERSE_TEST_CHECK(session.strokeBegin(-32,-40,&err,0,true) && session.strokeEnd(&err));
       AIVERSE_TEST_CHECK(session.workspace().map.data.extra["terraweaveSparse"]["heights"]!=sparse["heights"]);
       AIVERSE_TEST_CHECK(!call("studio.undo").has("error") && session.workspace().map.data.extra["terraweaveSparse"]==sparse);
       AIVERSE_TEST_CHECK(!call("studio.undo").has("error") && session.workspace().map.data.extra["terraweaveSparse"].isNull());
       const auto originalRegions=session.workspace().map.data.regions;
       const auto borderDepth=session.workspace().map.undoDepth();
       AIVERSE_TEST_CHECK(call("studio.sceneBrush",R"({"regionBorders":true})")["result"].getBool("regionBorders"));
       AIVERSE_TEST_CHECK(call("studio.sceneBrush",R"({"regionBorders":1})").has("error"));
       AIVERSE_TEST_CHECK(!call("studio.sceneBrush",R"({"regionBorders":false})")["result"].getBool("regionBorders"));
       AIVERSE_TEST_CHECK(session.workspace().map.undoDepth()==borderDepth);
       AIVERSE_TEST_CHECK(!call("studio.sceneBrush",R"({"kind":"region","regionLetter":"B","radius":0.5})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.sceneBrush",R"({"regionLetter":"BC"})").has("error"));
       AIVERSE_TEST_CHECK(session.strokeBegin(4,4,&err) && session.strokeDab(5,4,&err) && session.strokeEnd(&err));
       const auto& grid=session.workspace().map.data;
       AIVERSE_TEST_CHECK(grid.regions[grid.idx(4,4)]=='B' && grid.regions[grid.idx(5,4)]=='B');
       AIVERSE_TEST_CHECK(session.workspace().map.undoDepth()==depth+1);
       AIVERSE_TEST_CHECK(!call("studio.sceneBrush",R"({"regionLetter":"."})").has("error"));
       AIVERSE_TEST_CHECK(session.strokeBegin(4,4,&err) && session.strokeEnd(&err));
       AIVERSE_TEST_CHECK(grid.regions[grid.idx(4,4)]=='.' && grid.regions[grid.idx(5,4)]=='B');
       AIVERSE_TEST_CHECK(!call("studio.undo").has("error") && grid.regions[grid.idx(4,4)]=='B');
       AIVERSE_TEST_CHECK(!call("studio.undo").has("error") && grid.regions==originalRegions);
       {
           auto& map=session.workspace().map.data;
           const auto players=map.players;
           const auto extra=map.extra;
           map.players.resize(2);
           map.extra.member("gridFields").set("influence",Json::parse(R"({"type":"int","perPlayer":true})"));
           auto value=[&](int player){
               Json query=Json::parse(R"({"id":"influence","rect":[4,4,1,1]})");
               query.set("player",Json::number(int64_t(player)));
               return session.workspace().call("field_get",query)["rows"][0][0].integer();
           };
           AIVERSE_TEST_CHECK(!call("studio.sceneBrush",R"({"kind":"field","field":"influence","fieldMode":"set","fieldPlayer":1,"value":7})").has("error"));
           AIVERSE_TEST_CHECK(session.strokeBegin(4,4,&err) && session.strokeEnd(&err));
           AIVERSE_TEST_CHECK(value(1)==7 && value(2)==0);
           AIVERSE_TEST_CHECK(call("studio.sceneBrush",R"({"fieldPlayer":2,"value":12})")["result"].getInt("fieldPlayer")==2);
           AIVERSE_TEST_CHECK(call("studio.sceneBrush",R"({"fieldPlayer":3})").has("error"));
           AIVERSE_TEST_CHECK(call("studio.sceneBrush",R"({"fieldPlayer":1.5})").has("error"));
           AIVERSE_TEST_CHECK(session.strokeBegin(4,4,&err) && session.strokeEnd(&err));
           AIVERSE_TEST_CHECK(value(1)==7 && value(2)==12);
           AIVERSE_TEST_CHECK(session.strokeBegin(4,4,&err,0,true) && session.strokeEnd(&err));
           AIVERSE_TEST_CHECK(value(1)==7 && value(2)==0 && session.sceneBrush().fieldMode=="set");
           AIVERSE_TEST_CHECK(!call("studio.undo").has("error") && value(2)==12);
           AIVERSE_TEST_CHECK(!call("studio.undo").has("error") && !call("studio.undo").has("error"));
           map.extra=extra;
           map.players=players;
       }
       // Object scatter: three dabs far apart place three entities, one History step "Scatter objects".
       const size_t entities=session.workspace().map.data.entities.size(),depth2=session.workspace().map.undoDepth();
       AIVERSE_TEST_CHECK(call("studio.sceneBrush",R"({"kind":"objects","type":"tree","radius":2})")["result"].getStr("kind")=="objects");
       AIVERSE_TEST_CHECK(session.strokeBegin(5,5,&err) && session.strokeDab(10,5,&err) && session.strokeDab(15,5,&err) && session.strokeEnd(&err));
       AIVERSE_TEST_CHECK(session.workspace().map.data.entities.size()==entities+3 && session.workspace().map.undoDepth()==depth2+1);
       const auto steps2=call("studio.history")["result"]["steps"];
       AIVERSE_TEST_CHECK(steps2[steps2.size()-1].getStr("label")=="Scatter objects");
       AIVERSE_TEST_CHECK(!call("studio.undo").has("error") && session.workspace().map.data.entities.size()==entities);
       const auto beforeCancel=session.workspace().map.toJson();
       const auto redoBefore=session.workspace().map.redoDepth();
       AIVERSE_TEST_CHECK(session.strokeBegin(5,5,&err));
       AIVERSE_TEST_CHECK(session.strokeCancel()&&!session.strokeActive());
       AIVERSE_TEST_CHECK(session.workspace().map.toJson()==beforeCancel&&session.workspace().map.redoDepth()==redoBefore);
       AIVERSE_TEST_CHECK(session.strokeBegin(5,5,&err));
       AIVERSE_TEST_CHECK(!session.strokeDab(1e20,0,&err)&&!session.strokeActive());
       AIVERSE_TEST_CHECK(session.workspace().map.toJson()==beforeCancel&&session.workspace().map.redoDepth()==redoBefore);
       // Path tool without a road type refuses to start.
       call("studio.sceneBrush",R"({"kind":"road","roadType":""})");
       AIVERSE_TEST_CHECK(!session.strokeBegin(5,5,&err) && !err.empty());
       call("studio.sceneBrush",R"({"armed":false,"kind":"terrain"})");
       const auto stats=call("studio.stats");
       AIVERSE_TEST_CHECK(!stats.has("error") && stats["result"]["entities"].integer()==int64_t(session.workspace().map.data.entities.size()));
   }
   // Keep play changes: positions by index + type, wrong type skipped, dead removed only on request; one undo step.
   {
       const auto& doc=session.workspace().map;
       const std::string type0=doc.data.entities[0].type;
       const size_t count=doc.data.entities.size(),depth=doc.undoDepth();
       const std::string body=std::string(R"({"entities":[{"index":0,"type":")")+type0+R"(","x":7.5,"z":9.25},{"index":999,"type":"__nope__","x":1,"z":1},{"index":0,"removed":false}]})";
       auto dry=Json::parse(body);dry.set("dryRun",Json::fromBool(true));
       const auto preview=call("studio.keepPlayChanges",dry.dump().c_str());
       AIVERSE_TEST_CHECK(!preview.has("error") && preview["result"]["moved"].integer()==1 && doc.undoDepth()==depth);
       const auto kept=call("studio.keepPlayChanges",body.c_str());
       AIVERSE_TEST_CHECK(!kept.has("error") && kept["result"]["moved"].integer()==1 && kept["result"]["skipped"].integer()==1 && kept["result"]["removed"].integer()==0);
       AIVERSE_TEST_CHECK(doc.data.entities[0].x==7.5f && doc.data.entities[0].z==9.25f && doc.data.entities.size()==count && doc.undoDepth()==depth+1);
       const auto steps=call("studio.history")["result"]["steps"];
       AIVERSE_TEST_CHECK(steps[steps.size()-1].getStr("label")=="Keep play changes");
       if(count>=2){
           const std::string last=std::string(R"({"entities":[{"index":)")+std::to_string(count-1)+R"(,"removed":true}],"removeDead":true})";
           const auto removed=call("studio.keepPlayChanges",last.c_str());
           AIVERSE_TEST_CHECK(removed["result"]["removed"].integer()==1 && doc.data.entities.size()==count-1);
           AIVERSE_TEST_CHECK(!call("studio.undo").has("error") && doc.data.entities.size()==count);
       }
       AIVERSE_TEST_CHECK(!call("studio.undo").has("error") && doc.undoDepth()==depth);
   }
   // A review can report the same dead entity twice. Compact once and preserve road endpoints.
   {
       auto& map=session.workspace().map;
       auto original=map.data;
       const auto sample=map.data.entities.front();
       map.data.entities.assign(5,sample);
       for(size_t i=0;i<5;++i)map.data.entities[i].x=float(i);
       map.data.roads={{0,2},{1,4},{2,4},{4,3}};
       const auto depth=map.undoDepth();
       const auto kept=call("studio.keepPlayChanges",R"({"entities":[{"index":3,"removed":true},{"index":1,"removed":true},{"index":3,"removed":true}],"removeDead":true})");
       AIVERSE_TEST_CHECK(!kept.has("error")&&kept["result"]["removed"].integer()==2);
       AIVERSE_TEST_CHECK(map.data.entities.size()==3&&map.data.entities[0].x==0&&map.data.entities[1].x==2&&map.data.entities[2].x==4);
       const std::vector<std::pair<int,int>> expectedRoads={{0,1},{1,2}};
       AIVERSE_TEST_CHECK(map.data.roads==expectedRoads&&map.undoDepth()==depth+1);
       AIVERSE_TEST_CHECK(!call("studio.undo").has("error")&&map.data.entities.size()==5&&map.data.roads.size()==4);
       map.data=std::move(original);
   }
   // Extensions: the example script gives widgets, an action command, a change command and a gizmo.
   {
       AIVERSE_TEST_CHECK(!call("studio.extensions")["result"].getBool("exists"));
       const auto made=call("studio.extensionCreate");
       AIVERSE_TEST_CHECK(!made.has("error") && made["result"]["widgets"].size()==4 && made["result"]["gizmos"].size()==2);
       AIVERSE_TEST_CHECK(call("studio.extensionCreate").has("error"));
       AIVERSE_TEST_CHECK(call("studio.extensionAction",R"({"id":"inspector"})")["result"]["command"].getStr("id")=="inspector");
       AIVERSE_TEST_CHECK(call("studio.extensionAction",R"({"id":"radius","value":25})")["result"]["command"].getStr("command")=="status");
       AIVERSE_TEST_CHECK(session.extensionGizmos()[0]["radius"].toDouble()==25);
   }
   // Popup scripts have their own Lua state and receive the same target for list and action.
   {
       const auto project = std::filesystem::path(session.workspace().project.dir);
       const auto script = project / "editor" / "context_menus.lua";
       AIVERSE_TEST_CHECK(call("studio.contextMenu", R"({"window":"lua.widget"})")["result"]["items"].size()==0);
       std::ofstream(script) << "local opened=0\n"
           "function context_menu(ctx) opened=opened+1 return {{label=ctx.window,children={{id='inspect',label=ctx.element}}}} end\n"
           "function on_context_action(ctx,id) return {command='status',text=ctx.element..id..opened} end";
       const auto menu = call("studio.contextMenu", R"({"window":"lua.widget","element":"radius"})");
       AIVERSE_TEST_CHECK(menu["result"]["items"][0]["children"][0].getStr("label")=="radius");
       const auto action = call("studio.contextAction", R"({"window":"lua.widget","element":"radius","id":"inspect"})");
       AIVERSE_TEST_CHECK(action["result"]["command"].getStr("text")=="radiusinspect1");
       AIVERSE_TEST_CHECK(session.extensionGizmos()[0]["radius"].toDouble()==25);
   }
   // Attachments layout: invalid entries are refused, a valid one round-trips through the codec.
   {
       AIVERSE_TEST_CHECK(!call("studio.attachmentsGet")["result"].getBool("exists"));
       AIVERSE_TEST_CHECK(call("studio.attachmentsSet",R"({"document":{"parent":"content/a.glb","attachments":[{"model":"content/b.glb"}]}})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.attachmentsGet",R"({"path":"../x.json"})").has("error"));
       const auto saved=call("studio.attachmentsSet",R"({"document":{"parent":"content/a.glb","attachments":[{"model":"content/b.glb","socket":"hand_r","id":"sword"}]}})");
       AIVERSE_TEST_CHECK(call("studio.attachmentsSet",R"({"expected":null,"document":{"parent":"changed","attachments":[]}})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.attachmentsSet",R"({"expected":{"parent":"wrong","attachments":[]},"document":{"parent":"changed","attachments":[]}})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.attachmentsGet",R"({"project":"wrong-project"})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.attachmentsGet")["result"]["document"]==saved["result"]["document"]);

       AIVERSE_TEST_CHECK(!saved.has("error") && saved["result"].getBool("exists") && saved["result"]["document"]["attachments"][0].getStr("socket")=="hand_r");
   }
   // Spline editing: a river's point moves by handle id as one History step.
   {
       const auto river=call("studio.op",R"({"op":"water_set","params":{"body":{"id":7,"kind":"river","points":[[2,2,6],[10,8,6],[20,10,6]]}}})");
       if(!river.has("error")){
           const auto handles=call("studio.splineEdit",R"({"kind":"river"})");
           AIVERSE_TEST_CHECK(handles["result"]["handles"].size()==3 && handles["result"]["segments"].size()==2);
           std::string err;
           const size_t depth=session.workspace().map.undoDepth();
           const bool movedRiver=session.splineMove("water:7:1",12,9,&err);
           if(!movedRiver)std::cerr<<"splineMove: "<<err<<"\n";
           AIVERSE_TEST_CHECK(movedRiver);
           AIVERSE_TEST_CHECK(session.workspace().map.undoDepth()==depth+1);
           const auto after=call("studio.splineEdit",R"({"kind":"river"})");
           AIVERSE_TEST_CHECK(after["result"]["handles"][1]["x"].toDouble()==12);
           const auto steps=call("studio.history")["result"]["steps"];
           AIVERSE_TEST_CHECK(steps[steps.size()-1].getStr("label")=="Move river point");
           AIVERSE_TEST_CHECK(!session.splineMove("water:nope:0",1,1,&err));
           call("studio.undo");call("studio.undo");
       } else std::cerr<<"water_set unavailable in the test project: "<<river.getStr("error")<<"\n";
       call("studio.splineEdit",R"({"kind":""})");
   }
   // Project hub: templates start with "empty"; recent projects come from the shared registry.
   {
       AIVERSE_TEST_CHECK(call("studio.hubForget", R"({"dir":""})").has("error"));
       AIVERSE_TEST_CHECK(!call("studio.hubForget", R"({"dir":"unlisted-hub-test-entry"})").has("error"));
       const auto wizard = call("studio.wizardOptions")["result"];
       AIVERSE_TEST_CHECK(call("studio.wizardPlan", R"({"languages":["../bad"]})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.wizardPlan", R"({"stores":["unknown-store"]})").has("error"));
       AIVERSE_TEST_CHECK(wizard["features"].size() > 0 && wizard["defaults"].has("levels"));
       AIVERSE_TEST_CHECK(call("studio.wizardPlan", R"({"genre":"../bad"})").has("error"));
       AIVERSE_TEST_CHECK(call("studio.wizardPlan", R"({"artDir":"unselected-folder"})").has("error"));
       if (wizard["templates"].size()) {
           auto plan = call("studio.wizardPlan", R"({"idea":"economy","levels":3,"tutorial":"none","mapPreset":"islands"})");
           AIVERSE_TEST_CHECK(!plan.has("error") && plan["result"].getInt("levels")==3);
           AIVERSE_TEST_CHECK(plan["result"].getStr("levelPreset")=="islands" && plan["result"].getStr("tutorialArchetype")=="none");
       }
       const auto hub=call("studio.hub");
       AIVERSE_TEST_CHECK(!hub.has("error") && hub["result"]["templates"].size()>=1 && hub["result"]["templates"][0].getStr("id")=="empty");
       AIVERSE_TEST_CHECK(hub["result"]["recent"].isArray() && !hub["result"].getStr("defaultLocation").empty());
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
    const auto root=fs::canonical(fs::temp_directory_path())/("studio-entities-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto project=root/"templates"/"fixture";
    fs::create_directories(project/"maps");
    std::ofstream(project/"game.json") << R"({"name":"Studio fixture","module":"game","editor":{"hierarchy":{"groupings":[{"title":"By type and origin","by":["type","origin"]}]}},"rules":"rules.json","maps":[{"file":"maps/scene.json"}]})";
    fs::create_directories(project/"scripts");
    std::ofstream(project/"scripts"/"preview.lua") << "function on_start() game.spawn('unit',0,8,8) end";
    std::ofstream(project/"rules.json") << R"({"scripts":["scripts/preview.lua"],"types":{"unit":{"health":{"max":100}}}})";
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
            auto& playerDoc=session.workspace().map;
            const auto originalOwner=playerDoc.data.entities[0].owner, otherOwner=playerDoc.data.entities[1].owner;
            playerDoc.data.entities[0].owner=1;playerDoc.data.entities[1].owner=2;
            {
                auto& doc=session.workspace().map;
                const auto savedExtra=doc.data.extra;
                doc.data.extra.set("triggers",Json::parse(R"([{"event":{"type":"enterArea","area":"Harbor"},"actions":[{"area":"Harbor","text":"Harbor"}]}])"));
                doc.data.extra.set("waves",Json::parse(R"([{"route":["Harbor",[2,3]]}])"));
                auto area=[&](const char* json){
                    auto args=Json::parse(json);
                    args.set("scope",call("studio.snapshot")["result"]["graphDocument"]);
                    return call("studio.mapArea",std::move(args));
                };
                AIVERSE_TEST_CHECK(!area(R"({"action":"create","name":"Harbor","rect":[2,3,4,6]})").has("error"));
                const auto before=doc.toJson();
                AIVERSE_TEST_CHECK(area(R"({"action":"remove","name":"Harbor"})").has("error") && doc.toJson()==before);
                AIVERSE_TEST_CHECK(area(R"({"action":"edit","name":"Harbor","newName":"Port","rect":[1,2,-1,4]})").has("error"));
                AIVERSE_TEST_CHECK(!area(R"({"action":"edit","name":"Harbor","newName":"Port","rect":[3,4,5,7]})").has("error"));
                AIVERSE_TEST_CHECK(doc.data.extra["triggers"][0]["event"].getStr("area")=="Port");
                AIVERSE_TEST_CHECK(doc.data.extra["triggers"][0]["actions"][0].getStr("text")=="Harbor");
                AIVERSE_TEST_CHECK(doc.data.extra["waves"][0]["route"][0].str()=="Port");
                call("studio.undo");AIVERSE_TEST_CHECK(doc.toJson()==before);
                call("studio.undo");doc.data.extra=savedExtra;
            }
            {
                auto& doc=session.workspace().map;
                auto args=Json::parse(R"({"letter":"K","name":"Harbor ridge"})");
                args.set("scope",call("studio.snapshot")["result"]["graphDocument"]);
                const auto original=doc.data.regions;
                AIVERSE_TEST_CHECK(!call("studio.mapRegionName",args).has("error"));
                AIVERSE_TEST_CHECK(doc.data.regions==original && doc.data.regionNames.at('K')=="Harbor ridge");
                AIVERSE_TEST_CHECK(call("studio.mapRegionName",args).has("error"));
                args.set("scope",call("studio.snapshot")["result"]["graphDocument"]);
                const auto depth=doc.undoDepth();
                AIVERSE_TEST_CHECK(!call("studio.mapRegionName",args).has("error") && doc.undoDepth()==depth);
                const auto result=call("studio.mapRegions",args)["result"];
                AIVERSE_TEST_CHECK(result["regions"].size()==26 && result["regions"][10].getStr("name")=="Harbor ridge");
                AIVERSE_TEST_CHECK(result["coverage"].size()<=96 && result["coverage"][0].str().size()<=96);
                AIVERSE_TEST_CHECK(!call("studio.undo").has("error") && !doc.data.regionNames.count('K'));
            }
            const auto originalDoc=session.workspace().map.toJson();
            auto playerCall=[&](const Json& request){
                Json scoped=request;
                scoped.set("scope",call("studio.snapshot")["result"]["graphDocument"]);
                return call("studio.mapPlayer",scoped);
            };
            const int previousCount=int(session.workspace().map.data.players.size());
            Json add=Json::parse(R"({"action":"add"})");
            add.set("slot",Json::number(int64_t(previousCount+1)));
            AIVERSE_TEST_CHECK(!playerCall(add).has("error") && !session.workspace().map.data.players.empty());
            Json edit=Json::parse(R"({"slot":1,"action":"edit","changes":{"name":"North","team":4,"waves":true,"start":[3.5,7]}})");
            const auto beforeEdit=call("studio.snapshot")["result"]["graphDocument"];
            AIVERSE_TEST_CHECK(!playerCall(edit).has("error"));
            edit.set("scope",beforeEdit);
            AIVERSE_TEST_CHECK(call("studio.mapPlayer",edit).has("error"));
            const auto& north=session.workspace().map.data.players[0];
            AIVERSE_TEST_CHECK(north.name=="North" && north.team==4 && north.waves && north.hasStart && north.startX==3.5f);
            edit.set("changes",Json::parse(R"({"name":"Partial","start":["x",1]})"));
            AIVERSE_TEST_CHECK(playerCall(edit).has("error") && north.name=="North");
            edit.set("changes",Json::parse(R"({"start":null})"));
            AIVERSE_TEST_CHECK(!playerCall(edit).has("error") && !north.hasStart);
            Json remove=Json::parse(R"({"slot":1,"action":"remove"})");
            AIVERSE_TEST_CHECK(!playerCall(remove).has("error") && int(session.workspace().map.data.players.size())==previousCount);
            AIVERSE_TEST_CHECK(playerDoc.data.entities[0].owner==0 && playerDoc.data.entities[1].owner==1);
            for(int i=0;i<4;++i)call("studio.undo");
            AIVERSE_TEST_CHECK(session.workspace().map.toJson()==originalDoc && !session.workspace().map.canUndo());
            playerDoc.data.entities[0].owner=originalOwner;playerDoc.data.entities[1].owner=otherOwner;
            editor::DocBody body;body.id="home";body.faceSize=8;body.cells.assign(6*8*8,'.');body.levels.resize(6*8*8);body.corners.resize(6*9*9);body.faceExtra.resize(6,Json::object());playerDoc.data.bodies.push_back(body);
            edit.set("changes",Json::parse(R"({"start":{"body":"home","face":2,"x":3,"y":8}})"));
            AIVERSE_TEST_CHECK(!playerCall(edit).has("error") && playerDoc.toJson()["players"][0]["start"]==edit["changes"]["start"]);
            edit.set("changes",Json::parse(R"({"start":{"body":"home","face":2,"x":3,"y":9}})"));
            AIVERSE_TEST_CHECK(playerCall(edit).has("error"));
            call("studio.undo");playerDoc.data.bodies.clear();
            const auto snapshot=call("studio.snapshot")["result"];
            Json mapRequest=Json::object();mapRequest.set("scope",snapshot["graphDocument"]);
            mapRequest.set("validate",Json::fromBool(true));
            const auto old=call("studio.mapProperties",mapRequest)["result"];
            AIVERSE_TEST_CHECK(old["issues"].isArray() && old.getInt("width")==16);
            mapRequest.set("changes",Json::parse(R"({"name":"Harbor","size":[20,18],"roadsOnly":true,"victory":{"mode":"score"}})"));
            const auto updated=call("studio.mapPropertiesSet",mapRequest);
            AIVERSE_TEST_CHECK(updated["result"]["scene"].getStr("name")=="Harbor" && session.workspace().map.data.w==20);
            AIVERSE_TEST_CHECK(call("studio.mapPropertiesSet",mapRequest).has("error"));
            mapRequest.set("scope",updated["result"]["graphDocument"]);
            mapRequest.set("changes",Json::parse(R"({"name":"Partial","size":[10,3.5]})"));
            AIVERSE_TEST_CHECK(call("studio.mapPropertiesSet",mapRequest).has("error"));
            AIVERSE_TEST_CHECK(session.workspace().map.data.name=="Harbor");
            const auto restored=call("studio.undo");
            AIVERSE_TEST_CHECK(session.workspace().map.data.name==old.getStr("name") && session.workspace().map.data.w==16 && !session.workspace().map.canUndo());
            AIVERSE_TEST_CHECK(snapshot["scene"]["groupings"][0].str()=="By type and origin");
            const auto unchanged=session.workspace().map.toJson();
            Json params=Json::object();
            params.set("expectedProject",snapshot["project"]["path"]);
            Json reordered=Json::object();
            for(const char* field:{"revision","epoch","mapPath","projectPath"}) reordered.set(field,restored["result"]["graphDocument"][field]);
            params.set("scope",reordered);
            params.set("group",Json::number(int64_t(0)));
            Json reply;
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
            do {
                reply=call("studio.hierarchyGroups",params);
                if(!reply["result"].getBool("pending"))break;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }while(std::chrono::steady_clock::now()<deadline);
            const auto& path=reply["result"]["paths"][snapshot["scene"]["entities"][0].getStr("id")];
            AIVERSE_TEST_CHECK(path.size()==2 && !path[0].str().empty() && path[1].str()==editor::worldOriginTitle(editor::WorldOrigin::Map));
            AIVERSE_TEST_CHECK(snapshot["scene"]["groupings"][2].str()=="Component");
            params.set("group",Json::number(int64_t(2)));
            const auto byComponent=call("studio.hierarchyGroups",params);
            const auto& components=byComponent["result"]["paths"][snapshot["scene"]["entities"][0].getStr("id")][0];
            AIVERSE_TEST_CHECK(components.isArray() && components.size()>1);
            AIVERSE_TEST_CHECK(session.workspace().map.toJson()==unchanged);
            const auto previews=byComponent["result"]["preview"];
            AIVERSE_TEST_CHECK(previews.size()==1 && previews[0].getStr("id").starts_with("preview:"));
            AIVERSE_TEST_CHECK(previews[0]["preview"]["components"].size()>1);
            AIVERSE_TEST_CHECK(byComponent["result"]["paths"].has(previews[0].getStr("id")));
            Json flat=Json::object();flat.set("scope",params["scope"]);flat.set("expectedProject",params["expectedProject"]);
            AIVERSE_TEST_CHECK(call("studio.hierarchyGroups",flat)["result"]["preview"]==previews);
            AIVERSE_TEST_CHECK(session.workspace().map.toJson()==unchanged);
            params.member("scope").set("revision",Json::string("stale"));
            AIVERSE_TEST_CHECK(call("studio.hierarchyGroups",params).has("error"));
        }
        {
            fs::create_directories(project/"shaders");
            const auto path=project/"shaders"/"precise.json";
            std::ofstream(path) << R"({"version":1,"domain":"material","nodes":[{"id":"c","op":"constant","value":[0.2000,0.7,1,1]}],"outputs":{"color":"c"}})";
            auto params=Json::parse(R"({"name":"precise"})");
            const auto reply=call("studio.shaderGraphGet",params);
            AIVERSE_TEST_CHECK(!reply.has("error"));
            const auto& loaded=reply["result"];
            const auto baseline=loaded.getStr("originalJson");
            AIVERSE_TEST_CHECK(baseline.find("0.2000")!=std::string::npos);
            params.set("expectedProject",Json::string(session.workspace().project.dir));
            params.set("expected",Json::string(baseline));
            auto draft=loaded["graph"];
            draft.member("nodes").at(0).set("value",Json::parse("[0.375,0.7,1,1]"));
            params.set("graph",draft);
            const auto saved=call("studio.shaderGraphSave",params);
            AIVERSE_TEST_CHECK(!saved.has("error") && saved["result"]["originalJson"].isString());
            AIVERSE_TEST_CHECK(call("studio.shaderGraphSave",params).has("error"));
            AIVERSE_TEST_CHECK(Json::parseFile(path.string())==draft);
        }
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
            Simulation live({1,20,0},std::make_unique<game::GameCore>());
            live.setup(session.workspace().project.rulesJson,session.workspace().map.toJson());
            studio::StudioRuntimeInspect inspector;
            const auto before=live.checksum();
            auto page=inspector.entities(&live,"run-1",0,1,"");
            AIVERSE_TEST_CHECK(page["active"].boolean() && page["rows"].size()==1 && page["total"].integer()>=2);
            const auto ref=page["rows"][0].getStr("ref");
            const auto& placed=static_cast<game::GameCore&>(live.module()).mapPlaced();
            AIVERSE_TEST_CHECK(!placed.empty() && placed.front().valid());
            const auto mapRef=std::to_string(editor::worldRef(placed.front()));
            const auto placedPage=inspector.entities(&live,"run-1",0,1,"ref:"+mapRef);
            AIVERSE_TEST_CHECK(placedPage.getInt("total")==1 && placedPage["rows"][0].getInt("doc",-1)==0);
            AIVERSE_TEST_CHECK(placedPage["rows"][0].getStr("origin")=="map");
            const auto second=inspector.entities(&live,"run-1",1,1,"");
            AIVERSE_TEST_CHECK(second["total"].integer()==page["total"].integer() && second["rows"].size()==1);
            AIVERSE_TEST_CHECK(second["rows"][0].getStr("ref")!=ref);
            const auto filtered=inspector.entities(&live,"run-1",0,100,"ref:"+second["rows"][0].getStr("ref"));
            AIVERSE_TEST_CHECK(filtered["total"].integer()==1 && filtered["rows"][0]==second["rows"][0]);
            studio::StudioRuntimeInspect::GroupQuery grouped;
            grouped.keys={"type","origin"};
            auto level=inspector.entities(&live,"run-1",0,1,"",grouped);
            AIVERSE_TEST_CHECK(level.getInt("total")==page.getInt("total") && level["groups"].size()==1);
            const auto type=level["groups"][0];
            grouped.path.push_back(type.getStr("name"));
            level=inspector.entities(&live,"run-1",0,1,"",grouped);
            AIVERSE_TEST_CHECK(level.getInt("total")==type.getInt("count"));
            grouped.path.push_back(level["groups"][0].getStr("name"));
            level=inspector.entities(&live,"run-1",0,1,"",grouped);
            AIVERSE_TEST_CHECK(level["rows"].size()==1 && !level.has("groups"));
            grouped.keys={"component"}; grouped.path.clear();
            level=inspector.entities(&live,"run-1",0,100,"ref:"+ref,grouped);
            AIVERSE_TEST_CHECK(level.getInt("total")==1 && level["groups"].size()>1);
            for(const auto& component:level["groups"].items()){
                grouped.path={component.getStr("name")};
                const auto members=inspector.entities(&live,"run-1",0,1,"ref:"+ref,grouped);
                AIVERSE_TEST_CHECK(members.getInt("total")==1 && members["rows"][0].getStr("ref")==ref);
            }
            inspector.select(&live,"run-1","run-1",ref);
            AIVERSE_TEST_CHECK(inspector.selectedRef(&live,"run-1")==ref);
            AIVERSE_TEST_CHECK(inspector.selectedRef(&live,"run-2").empty());
            bool invalidPick=false;
            try { inspector.select(&live,"run-1","run-2",second["rows"][0].getStr("ref")); }
            catch(const std::runtime_error&){invalidPick=true;}
            AIVERSE_TEST_CHECK(invalidPick && inspector.selectedRef(&live,"run-1")==ref);
            const auto detail=inspector.inspect(&live,"run-1","run-1",ref);
            AIVERSE_TEST_CHECK(detail["components"].has("Transform"));
            AIVERSE_TEST_CHECK(live.checksum()==before);
            bool stale=false;try {inspector.inspect(&live,"run-2","run-1",ref);}catch(...){stale=true;}
            AIVERSE_TEST_CHECK(stale);
            const auto childRef=second["rows"][0].getStr("ref");
            const auto parentEntity=editor::worldEntity(std::stoull(ref));
            const auto childEntity=editor::worldEntity(std::stoull(childRef));
            live.world().add<Parent>(childEntity,Parent{parentEntity});
            grouped.keys={"parent"};grouped.path.clear();
            const auto nested=inspector.entities(&live,"run-1",0,100,"",grouped);
            auto rowFor=[](const Json& result,const std::string& id){
                for(const auto& row:result["rows"].items())if(row.getStr("ref")==id)return row;
                return Json{};
            };
            AIVERSE_TEST_CHECK(rowFor(nested,ref).getInt("children")==1 && rowFor(nested,childRef).getInt("depth")==1);
            grouped.collapsed.insert(std::stoull(ref));
            AIVERSE_TEST_CHECK(rowFor(inspector.entities(&live,"run-1",0,100,"",grouped),childRef).isNull());
            const auto orphan=inspector.entities(&live,"run-1",0,1,"ref:"+childRef,grouped);
            AIVERSE_TEST_CHECK(orphan["rows"][0].getStr("ref")==childRef && orphan["rows"][0].getInt("depth")==0);
            grouped.collapsed.clear();
            live.world().add<Parent>(parentEntity,Parent{childEntity});
            const auto cycle=inspector.entities(&live,"run-1",0,100,"",grouped);
            AIVERSE_TEST_CHECK(cycle.getInt("total")==nested.getInt("total"));
            live.world().destroy(editor::worldEntity(std::stoull(ref)));
            AIVERSE_TEST_CHECK(inspector.selectedRef(&live,"run-1").empty());
            AIVERSE_TEST_CHECK(inspector.entities(nullptr,"run-2",0,100,"")["rows"].size()==0);
        }
        const auto plan=studio::studioGameLaunch(session.workspace(),true);
        AIVERSE_TEST_CHECK(plan.map && *plan.map==0);
        AIVERSE_TEST_CHECK(plan.project==session.workspace().project.dir);
        AIVERSE_TEST_CHECK(studio::studioPlaySourceMatches(plan,session.workspace()));
        auto stalePlan=plan;++stalePlan.sourceRevision;
        AIVERSE_TEST_CHECK(!studio::studioPlaySourceMatches(stalePlan,session.workspace()));
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
        // Capture is pinned to a native document identity, not the scene's display name.
        const Json scope=call("studio.snapshot")["result"]["graphDocument"];
        AIVERSE_TEST_CHECK(scope.isObject() && scope["revision"].isString() && scope["epoch"].isString());
        Json captureParams=Json::object();captureParams.set("scope",scope);
        const auto captureBefore=session.workspace().map.toJson();
        const auto captureUndo=session.workspace().map.undoDepth();
        const auto capture=call("studio.cavernsGraphInspect",captureParams);
        AIVERSE_TEST_CHECK(!capture.has("error"));
        AIVERSE_TEST_CHECK(capture["result"]["scope"]==scope);
        bool caveRegistered=false;
        for(const auto& op:session.workspace().ops()) caveRegistered|=op.name=="terraweave_caverns_paint"&&!op.guiOnly;
        AIVERSE_TEST_CHECK(capture["result"].getStr("status")==(caveRegistered?"absent":"unavailable"));
        AIVERSE_TEST_CHECK(session.workspace().map.toJson()==captureBefore && session.workspace().map.undoDepth()==captureUndo);
        Json malicious=captureParams;malicious.set("mode",Json::string("carve"));
        AIVERSE_TEST_CHECK(call("studio.cavernsGraphInspect",malicious).has("error"));
        const auto projectState=call("studio.project")["result"];
        AIVERSE_TEST_CHECK(projectState["maps"].size()==1 && projectState.getStr("currentMap")=="maps/scene.json");
        const auto prior=session.workspace().map.toJson();
        AIVERSE_TEST_CHECK(call("studio.newMap",Json::parse(R"({"name":"new","width":0,"height":32})")).has("error"));
        AIVERSE_TEST_CHECK(session.workspace().map.toJson()==prior);
        AIVERSE_TEST_CHECK(call("studio.openMap",Json::parse(R"({"file":"../rules.json"})")).has("error"));
        // Catalog writes reject stale documents and duplicate IDs; dotted IDs are literal.
        auto catalogRule=Json::parse(R"({"group":"resources","id":"test.crystal","original":null,"value":{"name":"Crystal"}})");
        catalogRule.set("project",Json::string(session.workspace().project.dir));
        const auto createdRule=call("studio.projectRule",catalogRule);
        AIVERSE_TEST_CHECK(!createdRule.has("error"));
        AIVERSE_TEST_CHECK(call("studio.projectRule",catalogRule).has("error"));
        catalogRule.set("original",createdRule["result"]["text"]);
        catalogRule.set("value",Json::parse(R"({"name":"Blue crystal"})"));
        AIVERSE_TEST_CHECK(!call("studio.projectRule",catalogRule).has("error"));
        AIVERSE_TEST_CHECK(call("studio.projectRule",catalogRule).has("error"));
        AIVERSE_TEST_CHECK(session.workspace().project.rulesJson["resources"]["test.crystal"].getStr("name")=="Blue crystal");
        fs::copy_file(project/"maps"/"scene.json",project/"maps"/"local-copy.json");
        bool foundLocal=false;
        const auto projectCatalog=call("studio.project")["result"];
        for(const auto& map:projectCatalog["maps"].items())foundLocal |= map.getStr("file")=="maps/local-copy.json";
        AIVERSE_TEST_CHECK(foundLocal);
        AIVERSE_TEST_CHECK(!call("studio.openMap",Json::parse(R"({"file":"maps/local-copy.json"})")).has("error"));
        AIVERSE_TEST_CHECK(!call("studio.openMap",Json::parse(R"({"file":"maps/scene.json"})")).has("error"));
        fs::remove(project/"maps"/"local-copy.json");
        const auto revision=session.workspace().map.revision;
        AIVERSE_TEST_CHECK(!call("studio.newMap",Json::parse(R"({"name":"New document","width":32,"height":24})")).has("error"));
        AIVERSE_TEST_CHECK(session.workspace().map.data.w==32 && session.workspace().map.dirty);
        AIVERSE_TEST_CHECK(session.workspace().map.path.empty());
        AIVERSE_TEST_CHECK(session.workspace().map.revision>revision);
        AIVERSE_TEST_CHECK(call("studio.cavernsGraphInspect",captureParams).has("error"));
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
        AIVERSE_TEST_CHECK(call("studio.cavernsGraphInspect",captureParams).has("error")); // map reopened, old scope stays invalid
        const auto restored=session.workspace().map.toJson();
        std::ofstream(project/"maps"/"new.json") << "invalid map JSON";
        AIVERSE_TEST_CHECK(call("studio.openMap",Json::parse(R"({"file":"maps/new.json"})")).has("error"));
        AIVERSE_TEST_CHECK(session.workspace().map.toJson()==restored);

    }
    fs::remove_all(root);
}
}

static void worldSceneInspectorProperties() {
    using namespace aiverse;
    namespace fs=std::filesystem;
    const auto root=fs::canonical(fs::temp_directory_path())/("studio-world-scene-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto project=root/"fixture";
    fs::create_directories(project);
    std::ofstream(project/"game.json")<<R"({"module":"actor","rules":"rules.json","maps":[{"file":"map.json"}]})";
    std::ofstream(project/"rules.json")<<R"({"actor":{}})";
    const Json original=Json::parse(R"({"players":[{"name":"One","team":1},{"name":"Two","team":2}],"colliders":[{"id":1,"min":[0,0,0],"max":[1,1,1]},{"id":2,"center":[24,1,0],"half":[1,1,1],"yaw":0.25,"marker":7}],"spawns":[[-6,0,-6],[4,0,4]],"custom":{"marker":7}})");
    std::ofstream(project/"map.json")<<original.dump();
    {
        studio::StudioSession session(root.string(),project.string());
        auto call=[&](const char* method,Json params=Json::object()){
            Json req=Json::object();req.set("id",Json::number(int64_t(1)));req.set("method",Json::string(method));
            req.set("params",std::move(params));return session.dispatch(req);
        };
        const auto initial=call("studio.snapshot")["result"];
        AIVERSE_TEST_CHECK(initial["scene"].getBool("worldSpace")&&initial["scene"].getBool("mapProperties"));
        const auto rulesReply=call("studio.rules");
        AIVERSE_TEST_CHECK(!rulesReply.has("error"));
        for(const auto* key:{"types","resolved"}){
            const auto& types=rulesReply["result"][key];
            AIVERSE_TEST_CHECK(types.isObject() && types.size()==0);
        }
        Json network=Json::object();
        network.set("project",initial["project"]["path"]);
        network.set("mode",Json::string("listen"));
        const auto hostArgs=studio::studioNetworkArguments(session.workspace(),network);
        AIVERSE_TEST_CHECK((hostArgs==std::vector<std::string>{"--project",session.workspace().project.dir,"--map-index","0","--listen"}));
        network.set("mode",Json::string("connect"));
        network.set("address",Json::string("ws://[::1]:27101"));
        const auto joinArgs=studio::studioNetworkArguments(session.workspace(),network);
        AIVERSE_TEST_CHECK(joinArgs.size()==6&&joinArgs[4]=="--connect"&&joinArgs[5]=="ws://[::1]:27101");
        for(const char* invalid:{"udp://","udp://localhost:65536","ws://[::1]:0","ws://[::1","http://localhost","udp://local host"}){
            bool rejected=false;
            try{studio::validateStudioServerAddress(invalid);}catch(const std::runtime_error&){rejected=true;}
            AIVERSE_TEST_CHECK(rejected);
        }
        Json params=Json::object();params.set("scope",initial["graphDocument"]);
        const auto summary=call("studio.mapProperties",params)["result"];
        AIVERSE_TEST_CHECK(!summary.getBool("grid")&&summary["worldSpace"].getInt("colliders")==2&&summary["worldSpace"].getInt("spawns")==2);
        params.set("changes",Json::parse(R"({"name":"Partial","size":[20,18]})"));
        AIVERSE_TEST_CHECK(call("studio.mapPropertiesSet",params).has("error")&&session.workspace().map.toJson()==original);
        params.set("changes",Json::parse(R"({"name":"Harbor terminal"})"));
        const auto renamed=call("studio.mapPropertiesSet",params)["result"];
        AIVERSE_TEST_CHECK(renamed["scene"].getStr("name")=="Harbor terminal");
        AIVERSE_TEST_CHECK(call("studio.mapPropertiesSet",params).has("error"));
        params.set("scope",renamed["graphDocument"]);
        const auto depth=session.workspace().map.undoDepth();
        AIVERSE_TEST_CHECK(!call("studio.mapPropertiesSet",params).has("error")&&session.workspace().map.undoDepth()==depth);
        AIVERSE_TEST_CHECK(!call("studio.undo").has("error")&&session.workspace().map.toJson()==original);
        AIVERSE_TEST_CHECK(!call("studio.redo").has("error")&&!call("studio.save").has("error"));
        editor::MapDoc disk;std::string error;Json expected=original;expected.set("name",Json::string("Harbor terminal"));
        AIVERSE_TEST_CHECK(disk.load((project/"map.json").string(),&error)&&disk.toJson()==expected);
        disk.undo();AIVERSE_TEST_CHECK(disk.toJson()==original);
        bool rejected=false;
        try{(void)session.saveMapAs((project/"other.json").string());}catch(const std::exception&){rejected=true;}
        AIVERSE_TEST_CHECK(rejected&&!fs::exists(project/"other.json"));
        Json bounds=Json::parse(R"({"entityId":"collider:0","componentId":"BoxCollider","values":{"minX":2,"maxX":5}})");
        const auto scope=call("studio.snapshot")["result"]["graphDocument"];
        bounds.set("scope",scope);
        Json invalid=bounds;invalid.set("entityIds",Json::parse(R"(["collider:0","collider:999"])"));
        AIVERSE_TEST_CHECK(call("studio.setProperties",invalid).has("error")&&session.workspace().map.toJson()==expected);
        const auto boundsDepth=session.workspace().map.undoDepth();
        const auto applied=call("studio.setProperties",bounds);
        if(applied.has("error"))std::cerr<<applied.getStr("error")<<"\n";
        AIVERSE_TEST_CHECK(!applied.has("error")&&session.workspace().map.undoDepth()==boundsDepth+1);
        const auto edited=session.workspace().map.toJson();
        AIVERSE_TEST_CHECK(edited["colliders"][size_t(0)]["min"][size_t(0)].integer()==2&&edited["colliders"][size_t(0)]["max"][size_t(0)].integer()==5);
        AIVERSE_TEST_CHECK(call("studio.setProperties",bounds).has("error")&&session.workspace().map.toJson()==edited);
        bounds.set("scope",call("studio.snapshot")["result"]["graphDocument"]);
        AIVERSE_TEST_CHECK(!call("studio.setProperties",bounds).has("error")&&session.workspace().map.undoDepth()==boundsDepth+1);
        AIVERSE_TEST_CHECK(!call("studio.undo").has("error")&&session.workspace().map.toJson()==expected);
        Json oriented=Json::parse(R"({"entityId":"collider:1","componentId":"OrientedBoxCollider","values":{"rotationY":-90,"scaleX":6}})");
        oriented.set("scope",call("studio.snapshot")["result"]["graphDocument"]);
        const auto orientedEdit=call("studio.setProperties",oriented);
        if(orientedEdit.has("error"))std::cerr<<orientedEdit.getStr("error")<<"\n";
        AIVERSE_TEST_CHECK(!orientedEdit.has("error"));
        Json expectedPose=expected;
        auto& box=expectedPose.member("colliders").at(1);
        box.member("half").at(0)=Json::number(int64_t(3));
        box.set("yaw",Json::number(0.75));
        AIVERSE_TEST_CHECK(session.workspace().map.toJson()==expectedPose);
        oriented.set("scope",call("studio.snapshot")["result"]["graphDocument"]);
        oriented.set("values",Json::parse(R"({"rotationY":0,"scaleZ":5000})"));
        AIVERSE_TEST_CHECK(call("studio.setProperties",oriented).has("error")&&session.workspace().map.toJson()==expectedPose);
        AIVERSE_TEST_CHECK(!call("studio.undo").has("error")&&session.workspace().map.toJson()==expected);
    }
    fs::remove_all(root);
}

static void actorProjectMapSelection() {
    using namespace aiverse;
    const auto read=[](const std::string& file,std::string& out) {
        if(file=="game.json") out=R"({"module":"actor","maps":[{"file":"first.json"},{"file":"second.json"}]})";
        else if(file=="rules.json") out="{}";
        else if(file=="first.json" || file=="second.json") out=Json::string(file).dump();
        else return false;
        return true;
    };
    AIVERSE_TEST_CHECK(actor::readProject(read).map.str()=="first.json");
    AIVERSE_TEST_CHECK(actor::readProject(read,1).map.str()=="second.json");
    bool invalid=false;
    try {(void)actor::readProject(read,2);} catch(const JsonError&) {invalid=true;}
    AIVERSE_TEST_CHECK(invalid);
}
static void localPlayToolContracts(aiverse::editor::Workspace& ws) {
    using namespace aiverse;
    auto call = [&](const char* name, const Json& args) { return ws.call(name, args, "editor"); };
    const auto edit = Json::parse(R"({"expectedProject":"/fixture","epoch":"2","ref":"8","expectedTick":42,"field":"hp","expectedRaw":"65536","value":"2"})");
    AIVERSE_TEST_CHECK(call("studio_runtime_edit",edit).getStr("error").find("connected local Studio")!=std::string::npos);
    std::string route;
    editor::registerStudioPlayTools(ws,[&](const std::string& method,const Json& args){route=method;return args;});
    AIVERSE_TEST_CHECK(call("studio_runtime_edit",edit)==edit && route=="studio.gameEdit");
    route.clear();
    Json invalid=edit;
    invalid.set("field",Json::string("owner"));
    AIVERSE_TEST_CHECK(call("studio_runtime_edit",invalid).has("error") && route.empty());
    invalid=edit;
    invalid.erase("expectedTick");
    AIVERSE_TEST_CHECK(call("studio_runtime_edit",invalid).has("error") && route.empty());
    invalid=edit;
    invalid.set("expectedTick",Json::number(0.5));
    AIVERSE_TEST_CHECK(call("studio_runtime_edit",invalid).has("error") && route.empty());
    auto apply=Json::parse(R"({"expectedProject":"/fixture","token":"t"})");
    apply.set("entities",Json::array());
    AIVERSE_TEST_CHECK(call("studio_play_review_apply",apply).has("error") && route.empty());
    apply.erase("entities");
    AIVERSE_TEST_CHECK(call("studio_play_review_apply",apply)==apply && route=="studio.keepPlayChanges");
    editor::registerStudioPlayTools(ws);
    AIVERSE_TEST_CHECK(call("studio_play_review_apply",apply).has("error"));
}

int main() {
    using namespace aiverse;
    aiverse::studio::StudioSession session(".");
    actorProjectMapSelection();
    // Fixed map seeds round-trip independently of the match seed and the fixed-point coordinate range.
    gen::GenRequest generated;
    std::string seedError;
    AIVERSE_TEST_CHECK(gen::parseRequest(Json::parse(R"({"seed":9007199254740993})"),42,2,generated,&seedError));
    AIVERSE_TEST_CHECK(generated.seed==9007199254740993ULL);
    AIVERSE_TEST_CHECK(gen::parseRequest(Json::parse(R"({"seed":9223372036854775807})"),42,2,generated,&seedError));
    AIVERSE_TEST_CHECK(generated.seed==9223372036854775807ULL);
    AIVERSE_TEST_CHECK(gen::parseRequest(Json::parse(R"({"seed":"match"})"),42,2,generated,&seedError));
    AIVERSE_TEST_CHECK(generated.seed==42);
    worldSceneInspectorProperties();
    localPlayToolContracts(session.workspace());
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
    // The composited Game window uses the same oriented device profile as the runtime.
    for (bool rotated : {false,true}) {
        const int width=rotated?852:393, height=rotated?393:852;
        std::vector<uint32_t> pixels(size_t(width)*height);
        auto at=[&](int x,int y){return pixels[size_t(y)*width+x];};
        for (bool safe : {false,true}) {
            studio::StudioGameDevice device{"iphone-15",rotated,safe};
            studio::drawDeviceMask(pixels.data(),width,height,1,device.json());
            AIVERSE_TEST_CHECK(at(0,0)==0xff0c0c0e && at(width/2,height/2)==0);
            AIVERSE_TEST_CHECK(at(rotated?29:196,rotated?196:29)==0xff000000);
            AIVERSE_TEST_CHECK(at(rotated?40:110,rotated?90:40)==(safe?0x46461310u:0u));
            AIVERSE_TEST_CHECK(at(rotated?59:110,rotated?90:59)==(safe?0xe6e6bd48u:0u));
        }
        studio::drawDeviceMask(pixels.data(),width,height,2,studio::StudioGameDevice{}.json());
        AIVERSE_TEST_CHECK(std::all_of(pixels.begin(),pixels.end(),[](uint32_t pixel){return pixel==0;}));
    }
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
    AIVERSE_TEST_CHECK(call(R"({"id":51,"method":"studio.buildingPublishMaterial","params":{"style":"scifi","slot":"hull","name":"test"}})").has("error"));
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
    AIVERSE_TEST_CHECK(call(R"({"id":50,"method":"studio.cavernsGraphInspect","params":{}})").has("error"));
    planetWorldProcessAuthoringWorkflow();
    authoredEntityWorkflow();
    entityWorkflow();
    regionOverviewContract();mapCompoundHistoryContract();mapHistoryPersistenceContract();projectFileEditContract();projectDocumentJournalContract();projectOwnerSaveContract();projectHistoryContract();projectHistoryPersistenceContract();projectHistoryBudgetContract();assetHistorySessionContract();agentScopedHistoryContract();projectFileRecoveryWorkflow();
    caveGraphCaptureWorkflow();
    std::cout << "studio session contract PASS\n";
}
