#include "apps/editor/core/Workspace.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace aiverse;
namespace fs = std::filesystem;
void require(bool yes, const char* why) { if (!yes) throw std::runtime_error(why); }
int main() {
    const auto root = fs::temp_directory_path() / ("lens-ops-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    struct Cleanup { fs::path p; ~Cleanup(){ std::error_code e; fs::remove_all(p,e); } } cleanup{root};
    editor::Workspace ws;
    auto call = [&](const char* op, const Json& p = Json::object()) { return ws.call(op,p,"lens-test"); };
    require(call("lens_scene_get").has("error"), "missing project accepted");
    ws.project.dir = root.string();
    auto initial = call("lens_scene_get");
    require(initial.getBool("ok") && !initial.getBool("exists"), "missing scene must be empty");
    const auto empty = initial["scene"];
    Json camera = Json::parse(R"({"id":1,"name":"Capture","camera":{},"output":{"kind":"texture","key":42,"width":128,"height":128}})");
    Json p = Json::object(); p.set("expectedScene",empty); p.set("camera",camera);
    auto saved = call("lens_camera_set",p);
    require(saved.getBool("ok") && saved.getBool("clientCompatible"), "create failed");
    require(!ws.changes.empty() && saved.getBool("reloadRequired"), "missing reload notification");
    auto current = call("lens_scene_get")["scene"];
    require(current["cameras"].size()==1, "camera missing");
    require(call("lens_camera_set",p).has("error"), "stale edit accepted");
    // Opaque revision: preferred concurrency token.
    const auto withRevision = call("lens_scene_get");
    require(initial.getStr("revision")=="missing" && withRevision.getStr("revision").size()==16, "revision reported");
    Json renamed = camera; renamed.set("name", Json::string("ByRevision"));
    Json byRevision = Json::object(); byRevision.set("expectedRevision", withRevision["revision"]); byRevision.set("camera", renamed);
    auto revSaved = call("lens_camera_set", byRevision);
    require(revSaved.getBool("ok") && revSaved.getStr("revision")==call("lens_scene_get").getStr("revision"), "revision write");
    renamed.set("name", Json::string("Again")); byRevision.set("camera", renamed);
    require(call("lens_camera_set", byRevision).has("error"), "stale revision accepted");
    current = call("lens_scene_get")["scene"];
    // A scene re-serialized elsewhere (member order, number spelling) still matches.
    {
        Json reordered = Json::object();
        reordered.set("cameras", current["cameras"]);
        reordered.set("version", Json::numberText("1.0"));
        Json q = Json::object(); q.set("expectedScene", reordered); q.set("camera", camera);
        require(call("lens_camera_set", q).getBool("ok"), "round-tripped expectedScene rejected");
        current = call("lens_scene_get")["scene"];
    }
    p.set("expectedScene", current); camera.set("name",Json::string("Changed")); p.set("camera",camera);
    require(call("lens_camera_set",p).getBool("ok"), "replace failed");
    current=call("lens_scene_get")["scene"];
    p.set("expectedScene",current); camera.set("id",Json::number(int64_t(2)));
    camera.member("output").set("key",Json::number(int64_t(43))); // texture keys are unique per scene
    p.set("camera",camera);
    require(call("lens_camera_set",p).getBool("ok"), "second create failed");
    current=call("lens_scene_get")["scene"];
    require(current["cameras"][0].getStr("name")=="Changed", "unrelated record changed");
    Json bad=current; bad.member("cameras").at(1).set("id",Json::number(int64_t(1)));
    p.set("expectedScene",current); p.set("scene",bad);
    require(call("lens_scene_set",p).has("error"), "duplicate id accepted");
    bad=current; bad.member("cameras").at(0).member("camera").set("near",Json::number(int64_t(-1))); p.set("scene",bad);
    require(call("lens_scene_validate",p).has("error"), "invalid clip accepted");
    require(call("lens_scene_set",p).has("error"), "invalid scene saved");
    require(call("lens_scene_get")["scene"]==current, "failure changed last-good data");
    p.set("id",Json::number(int64_t(99)));
    require(call("lens_camera_delete",p).has("error"), "unknown id accepted");
    p.set("id",Json::number(int64_t(1)));
    require(call("lens_camera_delete",p).getBool("ok"), "delete failed");
    require(call("lens_scene_get")["scene"]["cameras"].size()==1, "delete removed wrong count");
    p.set("scene",empty); p.set("expectedScene",call("lens_scene_get")["scene"]);
    require(call("lens_scene_set",p).getBool("ok"), "empty replace failed");
    p.set("scene",Json::parse(R"({"version":1,"cameras":[{"id":1,"camera":{}}]})"));
    auto screen=call("lens_scene_validate",p);
    require(screen.getBool("ok") && !screen.getBool("clientCompatible"), "screen compatibility misreported");
    require(call("lens_scene_get")["scene"]==empty, "validation wrote data");
    { std::ofstream out(root/"cameras.json"); out << "{broken"; }
    require(call("lens_scene_get").has("error"), "malformed file accepted");
    { std::ofstream out(root/"cameras.json"); out << std::string(1024*1024+1,' '); }
    require(call("lens_scene_get").has("error"), "oversized file accepted");
    fs::remove(root/"cameras.json");
    { std::ofstream out(root/"other.json"); out << empty.dump(); }
    std::error_code ec; fs::create_symlink(root/"other.json",root/"cameras.json",ec);
    if (!ec) require(call("lens_scene_get").has("error"), "symlink accepted");
    unsigned tools=0;
    for (const auto& op:ws.ops()) if(op.name.rfind("lens_",0)==0) { ++tools; require(!op.guiOnly,"authoring needs GUI"); }
    require(tools==5,"MCP registry missing LENS tools");
    std::cout << "PASS LENS operations: CRUD, stale edits, validation, bounds, symlinks, registry\n";
}
