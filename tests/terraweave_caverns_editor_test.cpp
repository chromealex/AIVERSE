#include "apps/editor/core/Workspace.h"
#include "engine/world/TerraWeaveCavernsArchive.h"
#include <iostream>
#include <stdexcept>
using namespace aiverse;
using namespace aiverse::editor;
using namespace aiverse::terraweave::caverns;
static void check(bool value,const char* what){if(!value)throw std::runtime_error(what);}
static Json paint(const char* mode,int64_t x=0,int64_t y=-1000,int64_t z=0,int64_t radius=400){
    Json p=Json::object();
    p.set("mode",Json::string(mode));
    p.set("xCm",Json::number(x));p.set("yCm",Json::number(y));
    p.set("zCm",Json::number(z));p.set("radiusCm",Json::number(radius));
    return p;
}
int main(){
    Workspace ws;ws.project.dir=".";ws.map.reset(4,4);
    const auto before=ws.map.revision;
    auto a=ws.call("terraweave_caverns_paint",paint("carve",-750,-2000,800),"editor");
    check(a.getBool("ok")&&a.getInt("stampCount")==1,"carve operation missing");
    check(ws.map.revision>before,"map revision not incremented");
    const auto original=ws.map.data.extra["terraweaveCaverns"];
    auto snapshot=caveSnapshotFromJson(original);
    check(snapshot.stamps.size()==1&&snapshot.stamps.front().mode==CaveStampMode::Carve,
          "carve not persisted");
    check(ws.map.toJson().has("terraweaveCaverns"),"map serialization missing caverns");

    auto b=ws.call("terraweave_caverns_paint",paint("fill",-750,-2000,800,200),"editor");
    check(b.getBool("ok")&&b.getInt("stampCount")==2,"fill operation missing");
    const auto saved=ws.map.data.extra["terraweaveCaverns"];
    ws.map.undo();
    check(ws.map.data.extra["terraweaveCaverns"]==original,"undo did not restore original");
    ws.map.redo();
    check(ws.map.data.extra["terraweaveCaverns"]==saved,"redo did not restore edited stamps");

    MapDoc restored;std::string error;
    check(restored.loadJson(ws.map.toJson(),&error),"loadJson failed");
    check(restored.data.extra["terraweaveCaverns"]==saved,
          "cavern state did not survive roundtrip");
    const auto get=ws.call("terraweave_caverns_paint",paint("get"),"mcp");
    check(get.getBool("ok")&&get["snapshot"]["stamps"].size()==2,"get failed");

    auto invalid=paint("carve");invalid.set("radiusCm",Json::number(int64_t(0)));
    const auto bad=ws.call("terraweave_caverns_paint",invalid,"mcp");
    check(bad.has("error")&&ws.map.data.extra["terraweaveCaverns"]==saved,
          "invalid edit changed document");

    Json erase=Json::object();erase.set("mode",Json::string("erase"));
    erase.set("stampId",Json::number(b.getInt("stampId")));
    const auto deleted=ws.call("terraweave_caverns_paint",erase,"mcp");
    check(deleted.getBool("ok")&&deleted.getInt("stampCount")==1,"erase failed");
    auto after=caveSnapshotFromJson(ws.map.data.extra["terraweaveCaverns"]);
    check(after.sourceRevision>snapshot.sourceRevision,"source revision not bumped");
    std::cout<<"caverns editor authoring undo redo persistence contract OK\n";
}
