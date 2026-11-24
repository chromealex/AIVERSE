// SpectraFX editor/MCP graph authoring end-to-end regression.
// No GPU or genre module required. Uses the same Workspace::call entry as MCP.
#include "apps/editor/core/Workspace.h"
#include "engine/fx/SpectraFX.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using namespace aiverse;

static void require(bool yes, const char* reason) {
    if (!yes) throw std::runtime_error(reason);
}

int main() {
    const auto mark = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() / ("aiverse-spectrafx-ops-" + std::to_string(mark));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code e; fs::remove_all(path, e); } } cleanup{root};
    fs::create_directories(root);

    editor::Workspace ws;
    ws.project.dir = root.string();
    const auto call = [&](const char* name, const Json& args) { return ws.call(name, args, "test-spectrafx"); };
    const Json graph = Json::parse(R"JSON({
      "version":1,
      "parameters":{"size":0.35},
      "nodes":[
        {"id":"spawn","type":"Spawn","properties":{"burst":9}},
        {"id":"init","type":"Initialize","properties":{"life":1,"speed":0,"size":{"$param":"size"}}},
        {"id":"out","type":"SpriteOutput","properties":{"texture":"builtin:smoke"}}
      ],
      "links":[{"from":"spawn","to":"init"},{"from":"init","to":"out"}],
      "outputs":["out"]
    })JSON");

    Json create = Json::object();
    create.set("name", Json::string("spark"));
    create.set("graph", graph);
    auto created = call("fx_graph_set", create);
    require(created.getBool("ok"), "valid graph was rejected");
    require(fs::exists(root/"fx"/"graphs"/"spark.vfx.json"), "graph file not created");
    require(!ws.changes.empty() && ws.changes.back().kind == editor::Change::Fx, "missing editor FX invalidation");

    Json name = Json::object();
    name.set("name", Json::string("spark"));
    auto got = call("fx_graph_get", name);
    require(got["graph"].isObject() && got["validation"].getBool("ok"), "graph_get failed");
    require(got["validation"].has("metalGpuEligible") &&
            !got["validation"].getBool("metalGpuEligible"),
            "smoke sprite should expose CPU fallback diagnostics");
    require(!got["validation"].getStr("metalGpuReason").empty(),
            "CPU fallback reason was not reported");
    auto all = call("fx_graph_list", Json::object());
    require(all["graphs"].isArray() && all["graphs"].size()==1, "graph list did not find asset");
    require(all["graphs"][size_t(0)].getBool("ok"), "list reported graph compiler failure");

    Json validation = Json::object();
    validation.set("name", Json::string("spark"));
    validation.set("overrides", Json::parse(R"({"size":1.25})"));
    const auto checked = call("fx_graph_validate", validation);
    require(checked.getBool("ok") && checked.getInt("emitters")==1, "graph variant validation failed");

    // MCP shares the normal Workspace graph registry. The live preview
    // operation is discoverable even without a GPU; standalone MCP must
    // reject it explicitly instead of generating a synthetic frame.
    const Json tools=ws.toolList();
    bool discoveredPreview=false;
    for(const auto& t:tools.items())
        if(t.getStr("name")=="fx_graph_preview")discoveredPreview=true;
    require(discoveredPreview,"FX preview absent from MCP tools/list");
    Json previewStatus=Json::object();
    previewStatus.set("action",Json::string("status"));
    require(call("fx_graph_preview",previewStatus).has("error"),
            "headless MCP must not fake a GPU preview");
    Json seen;
    int previewCalls=0;
    ws.setSpectraPreviewHandler([&](const Json& p){
        ++previewCalls;seen=p;
        Json response=Json::object();
        response.set("ready",Json::fromBool(true));
        response.set("tick",Json::number(int64_t(1)));
        return response;
    });
    Json previewLoad=Json::object();
    previewLoad.set("action",Json::string("load"));
    previewLoad.set("graphName",Json::string("spark"));
    const auto linked=call("fx_graph_preview",previewLoad);
    require(linked.getBool("ready")&&previewCalls==1,
            "MCP graph load did not reach native preview owner");
    require(seen["graph"].isObject()&&seen["graph"].getInt("version")==1,
            "MCP graph file not resolved by Workspace");
    Json previewStep=Json::object();
    previewStep.set("action",Json::string("step"));
    require(call("fx_graph_preview",previewStep).getBool("ready")&&
            seen.getStr("action")=="step","MCP step not forwarded");
    const int forwarded=previewCalls;
    Json unsafe=previewLoad;
    unsafe.set("graphName",Json::string("../escape"));
    require(call("fx_graph_preview",unsafe).has("error")&&
            previewCalls==forwarded,"unsafe graph name reached native preview");
    Json ambiguous=previewLoad;
    ambiguous.set("graph",graph);
    require(call("fx_graph_preview",ambiguous).has("error")&&
            previewCalls==forwarded,"ambiguous source reached native preview");
    // A harmless-looking graphName cannot cross the project boundary via
    // a symlink to an external JSON file. Platforms that forbid symlink
    // creation simply skip this OS capability-specific assertion.
    const fs::path outside=root.parent_path()/
        ("aiverse-outside-spectra-"+std::to_string(mark)+".json");
    { std::ofstream file(outside); file<<graph.dump(2); }
    std::error_code symlinkError;
    fs::create_symlink(outside,root/"fx"/"graphs"/"outside.vfx.json",symlinkError);
    if(!symlinkError) {
        Json escaped=previewLoad;
        escaped.set("graphName",Json::string("outside"));
        require(call("fx_graph_preview",escaped).has("error")&&
                previewCalls==forwarded,"symlink outside project reached GPU runtime");
    }
    std::error_code removeError;
    fs::remove(outside,removeError);
    ws.setSpectraPreviewHandler({});
    require(call("fx_graph_preview",previewStatus).has("error"),
            "detached Studio preview must not be callable from standalone MCP");

    Json preset=Json::parse(R"JSON({"format":"aiverse.spectrafx.replay","version":1,"tick":25,"speed":1,"loop":false,"grid":true,"axes":true,"background":"dark","camera":{"yaw":0,"pitch":0,"distance":6,"targetX":0,"targetY":1,"targetZ":0,"ortho":false},"parameters":{}})JSON");
    preset.set("projectPath",Json::string(root.string()));
    Json v2=graph;v2.set("version",Json::number(int64_t(2)));
    preset.set("graphSignature",Json::string(v2.dump(-1)));
    Json savePreset=Json::object();savePreset.set("name",Json::string("first"));savePreset.set("preset",preset);
    require(!call("fx_replay_preset_save",savePreset).has("error"),"replay asset save failed");
    require(fs::exists(root/"content"/"spectrafx"/"replays"/"first.spectra-replay.json"),"missing replay content asset");
    require(call("fx_replay_preset_save",savePreset).has("error"),"replay history overwritten");
    require(call("fx_replay_preset_list",Json::object())["presets"].size()==1,"replay listing failed");
    Json getPreset=Json::object();getPreset.set("name",Json::string("first"));
    require(call("fx_replay_preset_get",getPreset)["preset"].getInt("tick")==25,"replay read failed");
    Json checkPreset=Json::object();checkPreset.set("preset",call("fx_replay_preset_get",getPreset)["preset"]);
    require(call("fx_replay_preset_check",checkPreset).getStr("state")=="matching","empty direct manifest mismatch");
    const auto texture=root/"content"/"spark.png";
    {std::ofstream f(texture,std::ios::binary);f<<"abc";}
    {std::ofstream f(texture.string()+".meta");f<<R"({"guid":"0123456789abcdef0123456789abcdef"})";}
    Json dependencyGraph=v2;dependencyGraph.set("testAsset",Json::string("content/spark.png"));
    Json dependencyPreset=preset;dependencyPreset.set("graphSignature",Json::string(dependencyGraph.dump(-1)));
    Json dependencySave=Json::object();dependencySave.set("name",Json::string("with_asset"));dependencySave.set("preset",dependencyPreset);
    require(!call("fx_replay_preset_save",dependencySave).has("error"),"dependency preset save failed");
    Json dependencyGet=Json::object();dependencyGet.set("name",Json::string("with_asset"));
    const auto baseline=call("fx_replay_preset_get",dependencyGet);
    require(baseline["compatibility"].getStr("state")=="matching","resource baseline mismatch");
    require(baseline["preset"]["dependencies"][size_t(0)].getStr("guid")=="0123456789abcdef0123456789abcdef","GUID not recorded");
    {std::ofstream f(texture,std::ios::binary|std::ios::trunc);f<<"xyz";}
    require(call("fx_replay_preset_get",dependencyGet)["compatibility"].getStr("state")=="changed","same-size edit not detected");
    fs::remove(texture);
    require(call("fx_replay_preset_get",dependencyGet)["compatibility"].getStr("state")=="unavailable","missing asset not detected");
    getPreset.set("expectedGraphSignature",Json::string("other"));
    require(call("fx_replay_preset_get",getPreset).has("error"),"stale graph revision accepted");
    savePreset.set("name",Json::string("../escape"));
    require(call("fx_replay_preset_save",savePreset).has("error"),"unsafe replay name accepted");
    savePreset.set("name",Json::string("second"));savePreset.set("expectedProjectPath",Json::string("other"));
    require(call("fx_replay_preset_save",savePreset).has("error"),"stale project accepted");

    Json bake = Json::object();
    bake.set("name", Json::string("spark"));
    bake.set("effect", Json::string("spark_baked"));
    bake.set("overrides", Json::parse(R"({"size":1.25})"));
    const auto baked = call("fx_graph_bake", bake);
    require(baked.getBool("ok") && fs::exists(root/"fx"/"spark_baked.json"), "graph bake failed");
    const auto effect = Json::parseFile((root/"fx"/"spark_baked.json").string());
    fx::EffectDef parsed;
    std::vector<std::string> errors;
    require(fx::parseEffect(effect, "fx/spark_baked.json", parsed, errors), "baked asset invalid");
    require(parsed.emitters.size()==1 && parsed.emitters[0].size.min==1.25f, "override lost in bake");
    const auto refused = call("fx_graph_bake", bake);
    require(refused.has("error"), "bake overwrote an effect without permission");

    Json invalid = graph;
    invalid.set("version", Json::number(int64_t(3))); // v2 is now supported
    create.set("graph", invalid);
    const auto rejected = call("fx_graph_set", create);
    require(!rejected.getBool("ok"), "invalid graph should not be saved");
    require(call("fx_graph_get", name)["validation"].getBool("ok"), "invalid graph changed last-good asset");

    name.set("name", Json::string("../unsafe"));
    const auto escape = call("fx_graph_delete", name);
    require(escape.has("error"), "path traversal accepted");

    name.set("name", Json::string("spark"));
    require(call("fx_graph_delete", name).getBool("ok"), "graph delete failed");
    require(!fs::exists(root/"fx"/"graphs"/"spark.vfx.json"), "graph file still present");
    require(fs::exists(root/"fx"/"spark_baked.json"), "deleting graph deleted baked effect");

    std::cout << "PASS SpectraFX MCP graph CRUD, native preview bridge, validation, variants, bake and path safety\n";
}
