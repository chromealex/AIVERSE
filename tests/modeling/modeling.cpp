#include "tools/modeling/Modeling.h"
#include "apps/editor/core/ModelingOps.h"
#include "engine/render/AnimationGraph.h"
#include "engine/render/AnimationRig.h"
#include <iostream>
#include <filesystem>
#include <cmath>
using namespace aiverse;
using namespace aiverse::modeling;
namespace aiverse::platform {
bool readContentFile(const std::string &, std::string &) { return false; }
} // namespace aiverse::platform
int checks = 0;
void check(bool b, const char *s) {
    ++checks;
    if (!b)
        throw std::runtime_error(s);
}
template <class F> void rejects(F f, const char *s) {
    bool threw = false;
    try {
        f();
    } catch (const std::exception &) {
        threw = true;
    }
    check(threw, s);
}
double volume(const Mesh &m) {
    double v = 0;
    for (size_t i = 0; i < m.indices.size(); i += 3)
        v += dot(m.vertices[m.indices[i]].pos,
                 cross(m.vertices[m.indices[i + 1]].pos, m.vertices[m.indices[i + 2]].pos)) /
             6.;
    return v;
}
int main() {
    try {
        auto doc = robotDocument();
        auto b = build(doc);
        check(b.model->nodes.size() == 34, "robot hierarchy");
        check(b.model->clips.size() == 4, "robot clips");
        auto bytes = exportGlb(*b.model);
        std::vector<std::string> warnings;
        auto loaded = loadModel(
            "robot.glb",
            [&](const std::string &, std::string &s) {
                s = bytes;
                return true;
            },
            &warnings);
        check(loaded->nodes.size() == b.model->nodes.size(), "GLB hierarchy roundtrip");
        check(loaded->clips.size() == 4, "GLB clips roundtrip");
        check(loaded->indices.size() == b.model->indices.size(), "GLB mesh roundtrip");
        auto pose = sample(*loaded, Json::parse(R"({"clip":"Walk","time":0.25})"));
        check(std::abs(pose["nodes"][4]["rotation"][2].toDouble()) > .1, "roundtrip animation moves joints");
        auto box = geometry(Json::parse(R"({"kind":"box"})"));
        check(std::abs(volume(box) - 1) < 1e-5, "box winding and volume");
        for (auto kind : {"sphere", "cylinder", "cone", "bevelBox"}) {
            auto g = Json::object();
            g.set("kind", Json::string(kind));
            check(volume(geometry(g)) > 0, "primitive outward winding");
        }
        auto concave =
            geometry(Json::parse(R"({"kind":"extrude","profile":[[0,0],[2,0],[2,1],[1,1],[1,2],[0,2]],"height":2})"));
        check(std::abs(volume(concave) - 6) < 1e-4, "concave extrude volume");
        rejects([] { geometry(Json::parse(R"({"kind":"extrude","profile":[[0,0],[1,1],[0,1],[1,0]]})")); },
                "self intersection rejected");
        auto bevel = box;
        modifier(bevel, Json::parse(R"({"kind":"bevel","amount":0.1})"));
        check(volume(bevel) > 0.5 && volume(bevel) < 1, "convex bevel removes corners");
        auto smooth = box;
        modifier(smooth, Json::parse(R"({"kind":"smoothSubdivision","levels":2})"));
        check(smooth.indices.size() == box.indices.size() * 16, "Loop subdivision topology");
        modifier(smooth, Json::parse(R"({"kind":"uvAtlas"})"));
        for (auto &v : smooth.vertices)
            check(v.uv.x >= 0 && v.uv.x <= 1 && v.uv.y >= 0 && v.uv.y <= 1, "atlas UV range");
        auto offset = box;
        for (auto &v : offset.vertices)
            v.pos.x += .5f;
        check(std::abs(volume(booleanMesh(box, offset, "union")) - 1.5) < .001, "CSG union volume");
        check(std::abs(volume(booleanMesh(box, offset, "subtract")) - .5) < .001, "CSG subtraction volume");
        check(std::abs(volume(booleanMesh(box, offset, "intersect")) - .5) < .001, "CSG intersection volume");
        auto bad = doc;
        bad.member("nodes").at(0).set("parent", Json::string("left0"));
        rejects([&] { build(bad); }, "cycle rejected");
        auto commands = Json::parse(R"([{"op":"parameters","values":{"legLength":1.1}}])");
        auto changed = edit(doc, commands);
        auto longer = build(changed);
        check(longer.model->nodes[size_t(longer.model->findNode("left0.shin"))].t.y < -.5f,
              "parameters rebuild transforms");
        check(doc["parameters"]["legLength"].toDouble() < 1, "edit preserves input");
        auto skin = emptyDocument();
        skin.set("nodes",
                 Json::parse(
                     R"([{"id":"joint"},{"id":"mesh","geometry":{"kind":"box"},"skin":"skin","bindJoint":"joint"}])"));
        skin.set("skins", Json::parse(R"([{"id":"skin","joints":["joint"]}])"));
        auto sk = build(skin);
        bytes = exportGlb(*sk.model);
        loaded = loadModel(
            "skin.glb",
            [&](const std::string &, std::string &s) {
                s = bytes;
                return true;
            },
            &warnings);
        check(loaded->skins.size() == 1 && loaded->vertices[0].weights[0] == 255, "skin roundtrip");
        auto graph = Json::parse(R"({"nodes":[{"id":"walk","kind":"clip","clip":"Walk"}],"output":"walk"})");
        doc.set("graph", graph);
        auto baked = bake(doc, Json::parse(R"({"clip":"Baked","duration":1,"fps":12})"));
        check(baked["channels"].size() == doc["nodes"].size() * 3, "graph baked all node transforms");
        auto angular = Json::parse(
            R"({"nodes":[{"id":"base","kind":"bind"},{"id":"rig","kind":"rig","input":"base","units":[{"op":"transform","bone":"body","rotation":[0,0,1]},{"op":"angularLimit","bone":"body","min":[0,0,-0.2],"max":[0,0,0.2]}]}],"output":"rig"})");
        auto anglePose = sample(*b.model, Json::parse("{\"graph\":" + angular.dump(-1) + "}"));
        check(std::abs(anglePose["nodes"][0]["rotation"][2].toDouble() - std::sin(.1)) < .001,
              "joint angular limits clamp rotation");
        auto generated = generateClip(
            doc,
            Json::parse(
                R"({"clip":"Turn","duration":2,"fps":12,"tracks":[{"node":"turret","wave":"spin","axis":[0,1,0],"amplitude":1,"frequency":0.5}]})"));
        check(generated["channels"][0]["times"].size() == 25, "procedural clip generation");
        auto automatic = autoWeights(skin, Json::parse(R"({"node":"mesh","skin":"skin"})"));
        check(automatic["nodes"][1]["weights"].size() == 36, "automatic editable vertex weights");
        build(automatic);
        auto duplicate = duplicateParts(doc, Json::parse(R"({"node":"left0","prefix":"copy."})"));
        check(duplicate["nodes"].size() == doc["nodes"].size() + 5, "duplicate preserves rigid subtree");
        build(duplicate);
        namespace fs = std::filesystem;
        auto root = fs::temp_directory_path() / "aiverse-modeling-test";
        fs::remove_all(root);
        fs::create_directories(root);
        editor::Workspace ws;
        ws.project.dir = root.string();
        auto call = [&](const char *op, Json p) {
            auto r = ws.call(op, p);
            if (r.has("error"))
                throw std::runtime_error(r.getStr("error"));
            return r;
        };
        auto p = Json::parse(R"({"name":"robot","preset":"robot"})");
        auto state = call("modeling_create", p);
        check(state.getInt("revision") == 1, "initial revision");
        p.set("expectedRevision", Json::number(int64_t(1)));
        p.set("commands", commands);
        state = call("modeling_edit", p);
        check(state.getInt("revision") == 2, "revision increments");
        check(ws.call("modeling_edit", p).has("error"), "stale edits rejected");
        p.set("expectedRevision", Json::number(int64_t(2)));
        state = call("modeling_undo", p);
        check(state.getInt("revision") == 3, "undo increases revision");
        p.set("expectedRevision", Json::number(int64_t(3)));
        state = call("modeling_redo", p);
        check(state.getInt("revision") == 4, "redo increases revision");
        p.set("expectedRevision", Json::number(int64_t(4)));
        call("modeling_export", p);
        check(fs::file_size(root / "models/robot.glb") > 100, "export persisted");
        p.set("name", Json::string("../outside"));
        check(ws.call("modeling_create", p).has("error"), "path traversal rejected");
        p.set("name", Json::string("robot"));
        p.set("expectedRevision", Json::number(int64_t(4)));
        p.set("commands", Json::parse(R"([{"op":"put","collection":"nodes","id":"body","value":{"parent":"body"}}])"));
        check(ws.call("modeling_edit", p).has("error"), "invalid transaction rejected");
        check(call("modeling_get", p).getInt("revision") == 4, "failed transaction retains revision");
        p.set("async", Json::fromBool(true));
        auto job = call("modeling_build", p);
        auto jp = Json::object();
        jp.set("jobId", job["jobId"]);
        Json status;
        do {
            status = call("modeling_job_status", jp);
        } while (status.getBool("running"));
        check(status["result"].getBool("ok"), "background build result");
        p.set("target", Json::string("robot_lod"));
        p.set("cell", Json::number(.08));
        auto lod = call("modeling_lod", p);
        check(lod["document"]["nodes"].size() == doc["nodes"].size(), "LOD preserves node names");
        auto published = call("modeling_graph_publish", p);
        check(published["graph"].getStr("model") == "models/robot.glb", "publish uses existing animation asset API");
        auto nativeGraph = call("animation_graph_get", p);
        check(nativeGraph.has("graph") && !nativeGraph.has("document"), "native graph API is not shadowed");
        auto imported = call("modeling_graph_import", p);
        check(imported.getInt("revision") == 5 && imported["document"]["graph"].isObject(),
              "graph roundtrip into model source");
        auto list = ws.toolList().dump(-1);
        check(list.find("modeling_preview") != std::string::npos && list.find("animation_bake") != std::string::npos,
              "headless MCP discovery");
        fs::remove_all(root);
        std::cout << checks << " modeling checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
