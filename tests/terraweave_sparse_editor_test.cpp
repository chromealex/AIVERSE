// Shared native editor/MCP sparse world terrain operation, undo/redo,
// negative/far-world chunk creation and atomic cross-border sculpt.
#include "apps/editor/core/Workspace.h"
#include <cstdint>
#include <stdexcept>
#include <iostream>
using namespace aiverse;
using namespace aiverse::editor;
static void check(bool ok,const char* why){
    if(!ok)throw std::runtime_error(why);
}
static Json cmd(const char* mode,int64_t x,int64_t z,int64_t chunk=64) {
    Json p=Json::object();
    p.set("xCm",Json::number(x));
    p.set("zCm",Json::number(z));
    p.set("mode",Json::string(mode));
    p.set("chunkMetres",Json::number(chunk));
    p.set("radiusCm",Json::number(int64_t(2500)));
    p.set("depthCm",Json::number(int64_t(100)));
    return p;
}
int main() {
    Workspace ws;
    ws.project.dir=".";
    ws.map.reset(4,4);
    const auto start=ws.map.revision;
    auto result=ws.call("terraweave_sparse_paint",
                        cmd("create",-100000,200000),"editor");
    check(result.getBool("ok") &&
          result.getInt("chunkCount")==1 &&
          ws.map.revision>start,"sparse distant click failed");
    check(ws.map.data.w==4 && ws.map.data.h==4 &&
          ws.map.data.corners.empty(),
          "sparse authoring changed legacy fixed RTS map");

    result=ws.call("terraweave_sparse_paint",
                   cmd("raise",6400,3200),"editor");
    check(result.getBool("ok") &&
          result.getInt("createdChunks")>=2 &&
          result.getInt("earthRevision")==1,
          "sparse stroke did not create adjoining tiles");
    const Json after=ws.map.data.extra["terraweaveSparse"];
    check(after.isObject() && after["chunks"].isArray() &&
          after["heights"].isArray() && after["heights"].size()>0,
          "sparse terrain did not serialize its chunks and soil edits");
    check(ws.map.toJson().has("terraweaveSparse"),
          "authoring terrain lost from map document JSON");
    ws.map.undo();
    const Json before=ws.map.data.extra["terraweaveSparse"];
    check(before["chunks"].size()==1 && before["heights"].size()==0,
          "sparse stroke is not independently undoable");
    ws.map.redo();
    check(ws.map.data.extra["terraweaveSparse"]==after,
          "redo did not reproduce exact sparse terrain state");

    // A failed edit must leave the persistent document unchanged.
    auto invalid=cmd("raise",6400,3200);
    invalid.set("depthCm",Json::number(int64_t(0)));
    result=ws.call("terraweave_sparse_paint",invalid,"mcp");
    check(result.has("error") &&
          ws.map.data.extra["terraweaveSparse"]==after,
          "invalid brush partially modified saved sparse terrain");

    // Same authored chunks must survive FPS document save/load too:
    // generic MapDoc::toJson historically returned fps verbatim.
    Workspace fps;
    fps.project.dir=".";
    fps.map.reset(4,4);
    fps.map.data.fps=Json::object();
    fps.map.data.fps.set("colliders",Json::array());
    fps.map.data.fps.set("spawns",Json::array());
    auto fpsCreate=fps.call("terraweave_sparse_paint",
                            cmd("create",-6401,-6401),"mcp");
    check(fpsCreate.getBool("ok") &&
          fps.map.toJson().has("terraweaveSparse"),
          "FPS map JSON dropped terrain created in the world");
    MapDoc restoredFps;
    std::string error;
    check(restoredFps.loadJson(fps.map.toJson(),&error) &&
          restoredFps.data.extra["terraweaveSparse"]==
               fps.map.data.extra["terraweaveSparse"],
          "FPS sparse terrain did not persist through load");
    std::cout<<"PASS sparse editor/MCP click anywhere, auto-neighbors, "
                "undo/redo, no fixed map resize, atomic validation\n";
}
