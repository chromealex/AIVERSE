// SpectraFX editor/MCP graph authoring end-to-end regression.
// No GPU or genre module required. Uses the same Workspace::call entry as MCP.
#include "apps/editor/core/Workspace.h"
#include "engine/render/graph/GraphTelemetry.h"
#include "engine/render/graph/GraphWatch.h"
#include "engine/render/graph/GraphGpuProfile.h"
#include "engine/render/graph/TextureGraphGpu.h"
#include "engine/fx/SpectraFX.h"
#include <chrono>
#include <filesystem>
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

    const auto catalog = call("graph_catalog", Json::object());
    require(catalog.getBool("ok") && catalog["domains"].size()==5, "generic graph catalog missing domains");
    require(call("graph_list", Json::parse(R"({"domain":"unknown"})")).has("error"), "unknown graph domain accepted");
    Json generic = Json::object();
    generic.set("domain",Json::string("spectrafx"));
    generic.set("name",Json::string("generic_spark"));
    generic.set("graph",graph);
    require(call("graph_validate",generic).getBool("ok"),"generic native validation failed");
    require(call("graph_dependencies",generic)["result"]["edges"].size()==2,"generic dependency edges incorrect");
    require(call("graph_save",generic).has("error"),"unversioned write accepted without explicit consent");
    generic.set("allowUnversionedWrite",Json::fromBool(true));
    require(call("graph_save",generic).getBool("ok"),"generic native save failed");
    require(call("graph_get",generic)["result"]["graph"]==graph,"generic graph readback mismatch");
    require(call("graph_node_catalog",generic)["result"]["nodes"].isArray(),"generic native node catalog missing");
    require(call("fx_graph_delete",generic).getBool("ok"),"generic fixture cleanup failed");

    const auto shaderDraft=Json::parse(R"({"domain":"shader","graph":{"version":1,"domain":"material","nodes":[{"id":"input","op":"input"}],"outputs":{"color":"input"}},"edits":[{"action":"add","value":{"id":"tint","op":"constant","value":[1,1,1,1]}},{"action":"setOutput","output":"color","node":"tint"}]})");
    const auto shaderEdited=call("graph_edit",shaderDraft);
    require(shaderEdited.getBool("ok")&&shaderEdited["result"].getBool("valid"),"shader batch validation failed");
    require(shaderEdited["result"].getInt("applied")==2&&!shaderEdited["result"].getBool("saved"),"shader batch metadata wrong");
    require(shaderEdited["result"]["graph"]["outputs"].getStr("color")=="tint","shader output not changed");
    require(shaderDraft["graph"]["nodes"].size()==1,"input draft mutated");
    require(!fs::exists(root/"shaders"),"shader draft wrote files");
    auto badBatch=shaderDraft;badBatch.member("edits").push(Json::parse(R"({"action":"add","value":{"id":"tint","op":"constant"}})"));
    require(call("graph_edit",badBatch).has("error"),"duplicate ID batch accepted");
    const auto motionDraft=Json::parse(R"({"domain":"motionweave","graph":{"nodes":[{"id":"bind","kind":"bind"}],"output":"bind"},"edits":[{"action":"addState","value":{"id":"idle","output":"bind","transitions":[]}},{"action":"setInitial","node":"idle"},{"action":"addTransition","state":"idle","value":{"to":"idle","fade":0.2,"when":[]}}]})");
    const auto motionEdited=call("graph_edit",motionDraft);
    require(motionEdited.getBool("ok")&&motionEdited["result"]["graph"]["states"][size_t(0)]["transitions"].size()==1,"animation state draft failed");
    require(!motionEdited["result"].getBool("valid"),"model-less motion graph unexpectedly validated");
    require(!fs::exists(root/"animations"),"animation draft wrote files");
    const auto modelDraft=Json::parse(R"({"domain":"modeling","document":{"version":1,"parameters":{},"materials":[],"nodes":[{"id":"root"}],"clips":[],"skins":[],"graph":null},"edits":[{"op":"put","collection":"nodes","id":"body","value":{"parent":"root"}},{"op":"parameters","values":{"size":1}}]})");
    const auto modelEdited=call("graph_edit",modelDraft);
    require(modelEdited.getBool("ok")&&modelEdited["result"]["document"]["nodes"].size()==2,"native model draft batch failed");
    require(!fs::exists(root/"models"),"model draft wrote files");
    auto fxBatch=generic;fxBatch.set("edits",Json::parse(R"([{"action":"setProperty","node":"spawn","property":"burst","value":12},{"action":"setProperty","node":"init","property":"life","value":2}])"));
    const auto fxEdited=call("graph_edit",fxBatch);
    require(fxEdited.getBool("ok")&&fxEdited["result"].getBool("valid")&&fxEdited["result"].getInt("applied")==2,"native FX batch failed");
    require(!fs::exists(root/"fx"/"graphs"/"generic_spark.vfx.json"),"FX draft wrote files");

    auto typed=shaderDraft;typed.erase("edits");typed.set("node",Json::string("input"));
    require(call("graph_property_schema",typed)["result"]["fields"].has("position"),"property schema missing position");
    typed.set("edit",Json::parse(R"({"action":"setProperty","node":"input","property":"position","value":[10,20]})"));
    require(call("graph_edit",typed)["result"]["graph"]["nodes"][size_t(0)]["position"].size()==2,"typed move failed");
    typed.member("edit").set("property",Json::string("id"));
    require(call("graph_edit",typed).has("error"),"typed edit changed immutable ID");
    auto dependencies=Json::parse(R"({"domain":"motionweave","graph":{"nodes":[{"id":"sub","kind":"subgraph","graph":"a"}],"graphs":{"a":{"nodes":[{"id":"loop","kind":"subgraph","graph":"a"}]}}}})");
    const auto resolved=call("graph_asset_dependencies",dependencies);
    require(resolved.getBool("ok")&&resolved["result"]["issues"].size()==1,"embedded recursion not detected");
    auto textureRef=Json::parse(R"({"domain":"shader","graph":{"nodes":[{"id":"tex","op":"sampleTexture","texture":"../outside.png"}]}})");
    require(call("graph_asset_dependencies",textureRef)["result"]["references"][size_t(0)].getStr("status")=="invalid-path","unsafe path not flagged");

    const auto live=call("graph_live_snapshot",Json::parse(R"({"domain":"spectrafx"})"));
    require(live.has("available")&&!live.getBool("available"),"headless host must not fabricate live owner");
    const auto caps=call("graph_runtime_capabilities",Json::object());
    require(!caps.getBool("nativeEditorSnapshotOwner"),"headless host claims GUI owner");
    require(caps.getBool("ok")&&!caps.getBool("liveAttached"),"runtime capability misreported");
    Json probe=Json::object();probe.set("domain",Json::string("spectrafx"));probe.set("graph",graph);probe.set("steps",Json::number(int64_t(6)));
    const auto sampled=call("graph_runtime_probe",probe);
    require(sampled.getBool("ok")&&sampled["frames"].size()==6,"headless FX runtime probe failed");
    require(!sampled.getBool("liveAttached")&&!sampled.getBool("gpuMeasured"),"probe mislabeled");
    require(sampled["frames"][size_t(5)].getInt("particles")>0,"expected native particles");
    probe.set("steps",Json::number(int64_t(61)));
    require(call("graph_runtime_probe",probe).has("error"),"unbounded probe accepted");

    graph_telemetry::History history;
    const graph_telemetry::Identity track{"world-1","42","revision-1","spectrafx"};
    for(uint64_t i=1;i<=121;++i)require(history.publish(track,i,Json::object()),"history publish failed");
    require(history.read(track).size()==120&&history.read(track)[size_t(0)].getStr("sequence")=="2","history bound failed");
    require(!history.publish(track,121,Json::object()),"duplicate sequence accepted");
    require(history.read({"world-2","42","revision-1","spectrafx"}).size()==0,"world identity crossed");
    history.clearWorld("world-1");require(history.read(track).size()==0,"world history not cleared");
    Json debug=Json::object();debug.set("action",Json::string("start"));debug.set("domain",Json::string("spectrafx"));debug.set("graph",graph);
    const auto begun=call("graph_debug_session",debug);
    require(begun.getBool("active")&&!begun.getStr("session").empty(),"FX isolated session not started");
    Json step=Json::object();step.set("action",Json::string("step"));step.set("session",begun["session"]);
    const auto stepped=call("graph_debug_session",step);
    require(stepped.getInt("step")==1&&stepped["frames"].size()==1,"debug step failed");
    step.set("action",Json::string("continue"));step.set("steps",Json::number(int64_t(3)));
    require(call("graph_debug_session",step).getInt("step")==4,"debug continuation did not retain state");
    step.set("action",Json::string("stop"));require(!call("graph_debug_session",step).getBool("active"),"session stop failed");
    step.set("action",Json::string("step"));require(call("graph_debug_session",step).has("error"),"stopped session accepted");

    graph_telemetry::Watch watch;
    auto watchSample=Json::parse(R"({"available":true,"domain":"spectrafx","worldEpoch":"1","instanceSerial":"5","programIdentity":"instance:5","sequence":"1","particles":3})");
    watch.start("spectrafx","preview",watchSample);
    require(watch.running(),"watch not started");
    require(!watch.capture(watchSample),"duplicate sample accepted");
    watchSample.set("sequence",Json::string("2"));
    require(watch.capture(watchSample)&&watch.status()["frames"].size()==2,"watch capture failed");
    watchSample.set("instanceSerial",Json::string("6"));
    require(!watch.capture(watchSample)&&!watch.running(),"watch followed replacement");
    require(watch.status()["frames"].size()==2,"watch lost stopped history");
    watch.clear();require(watch.status()["frames"].size()==0,"watch clear failed");
    require(!call("graph_live_watch",Json::parse(R"({"action":"status"})")).getBool("available"),"headless watch claims owner");

    require(!call("graph_live_profile",Json::parse(R"({"action":"read","entity":"42"})")).getBool("available"),"headless profile claims live owner");

    struct FakeScopeDevice {
        uint64_t frameSerial() const{return 11;}
        bool beginGpuScope(uint64_t){return allow;}
        bool allow=true;
    } fakeScope;
    graph_gpu_profile::Capture gpuProfile;
    gfx::DeviceInfo fakeInfo;fakeInfo.backend=gfx::Backend::Metal;fakeInfo.name="test metadata only";
    gpuProfile.start(fakeInfo,"fixture");
    const auto scopeTag=gpuProfile.begin(fakeScope,"viewport",2,true);
    require(scopeTag!=0&&!gpuProfile.report().getBool("available"),"pending GPU query treated as complete");
    require(gpuProfile.complete({scopeTag,2.5,true},12)==2,"adaptive scope result routing failed");
    require(gpuProfile.report()["samples"].size()==1,"completed GPU sample missing");
    const auto invalidTag=gpuProfile.begin(fakeScope,"fx",1,false);
    gpuProfile.complete({invalidTag,0,false},13);
    require(gpuProfile.report()["samples"].size()==1,"invalid GPU query recorded");
    const auto cancelled=gpuProfile.begin(fakeScope,"fx",1,false);
    gpuProfile.stop();gpuProfile.start(fakeInfo,"fixture");
    gpuProfile.complete({cancelled,1.5,true},14);
    require(!gpuProfile.report().getBool("available"),"cancelled query crossed capture epoch");
    fakeScope.allow=false;
    require(gpuProfile.begin(fakeScope,"fx",1,false)==0,"unsupported timing accepted");
    require(!call("graph_gpu_profile",Json::parse(R"({"action":"read"})")).getBool("available"),"headless GPU owner fabricated");

    auto tex=Json::parse(R"({"domain":"texture","name":"fixture","graph":{"version":1,"size":8,"nodes":[{"id":"rough","op":"constant","inputs":[],"properties":{"value":0.5}},{"id":"metal","op":"constant","inputs":[],"properties":{"value":1}},{"id":"packed","op":"metallicRoughness","inputs":["rough","metal"]},{"id":"normal","op":"normal","inputs":["rough"]}],"outputs":{"metallicRoughness":"packed","normal":"normal"}},"output":"metallicRoughness","textureName":"fixture"})");
    require(call("graph_validate",tex).getBool("ok"),"native texture DAG validation failed");
    auto pixels=call("texture_graph_preview",tex);
    require(pixels.getBool("ok")&&pixels.getStr("rgbaHex").size()==8*8*8&&pixels.getStr("rgbaHex").substr(0,8)=="ff80ffff","packed PBR channel layout wrong");
    tex.set("output",Json::string("normal"));
    require(call("texture_graph_preview",tex).getStr("rgbaHex").substr(0,8)=="8080ffff","flat height normal wrong");
    require(call("graph_save",tex).getBool("ok"),"new texture graph save failed");
    require(call("graph_save",tex)["result"].has("error")||!call("graph_save",tex).getBool("ok"),"texture graph overwrite without expected accepted");
    tex.set("expected",tex["graph"]);
    require(call("graph_save",tex).getBool("ok"),"expected texture document save failed");
    require(call("texture_graph_bake",tex).getBool("ok"),"texture graph PNG bake failed");
    require(fs::is_regular_file(root/"content"/"textures"/"fixture_normal.png"),"texture bake missing file");
    require(call("texture_graph_bake",tex).has("error"),"texture bake overwrite accepted");
    auto brokenTexture=tex;
    brokenTexture.member("graph").member("nodes").at(2).member("inputs").at(0)=Json::string("normal");
    require(!call("graph_validate",brokenTexture).getBool("ok"),"texture type mismatch accepted");

    const auto textureCatalog=call("texture_graph_catalog",Json::object());
    require(textureCatalog.getBool("ok")&&textureCatalog["outputTypes"].getStr("normal")=="normal","texture output catalog missing types");
    const auto textureTemplates=call("texture_graph_templates",Json::object());
    require(textureTemplates.getBool("ok")&&textureTemplates["templates"].size()==6,"texture starter library missing");
    for(const auto& row:textureTemplates["templates"].items()){
        Json args=Json::object();args.set("graph",row["graph"]);
        require(call("texture_graph_validate",args).getBool("ok"),"invalid texture template");
    }
    tex.set("textureName",Json::string("fixture_set"));
    const auto bakedSet=call("texture_graph_bake_set",tex);
    require(bakedSet.getBool("ok")&&bakedSet["files"].size()==2,"PBR set bake failed");
    for(const auto& row:bakedSet["files"].items()){
        require(!row.getBool("sRGB"),"numeric texture marked sRGB");
        require(fs::is_regular_file(root/row.getStr("path")),"PBR set file missing");
    }
    require(call("texture_graph_bake_set",tex).has("error"),"PBR set overwrite accepted");
    require(!fs::exists(root/"content"/"textures"/".fixture_set.stage"),"PBR staging leaked");

    auto instance=tex;
    instance.member("graph").set("parameters",Json::parse(R"({"roughness":{"default":0.5,"bindings":[{"node":"rough","property":"value"}]}})"));
    instance.set("output",Json::string("metallicRoughness"));
    instance.set("overrides",Json::parse(R"({"roughness":0.25})"));
    const auto variantPixels=call("texture_graph_preview",instance);
    require(variantPixels.getStr("rgbaHex").substr(0,8)=="ff40ffff","parameter override not evaluated");
    require(call("texture_graph_preview",instance).getBool("cacheHit"),"identical resolved preview cache miss");
    instance.set("overrides",Json::parse(R"({"roughness":0.75})"));
    require(call("texture_graph_preview",instance).getStr("rgbaHex").substr(0,8)=="ffbfffff","parameter cache returned stale pixels");
    instance.set("overrides",Json::parse(R"({"roughness":2})"));
    require(call("texture_graph_preview",instance).has("error"),"invalid override accepted");
    require(fs::is_regular_file(root/"content"/"textures"/"fixture_set"/"texture-set.json"),"bake provenance missing");
    require(call("texture_graph_cache",Json::parse(R"({"clear":true})")).getInt("entries")==0,"cache clear failed");

    const auto gpuTexture=render::texture_graph::compile(tex["graph"]);
    const auto gpuPacked=render::texture_graph::gpu::compile(gpuTexture,gpuTexture.outputs.at("metallicRoughness"),8);
    require(!gpuPacked.msl.empty()&&gpuPacked.msl.find("cs_main")!=std::string::npos,"GPU texture source missing");
    const auto gpuNormal=render::texture_graph::gpu::compile(gpuTexture,gpuTexture.outputs.at("normal"),8);
    require(!gpuNormal.msl.empty()&&gpuNormal.msl.find("evaluatePoint")!=std::string::npos,"GPU normal source missing");
    require(gpuNormal.glsl.find("binding=12")!=std::string::npos&&gpuNormal.glsl.find("gl_GlobalInvocationID")!=std::string::npos,"Vulkan storage ABI missing");
    auto unsupportedTexture=render::texture_graph::compile(Json::parse(R"({"version":1,"size":128,"nodes":[{"id":"p","op":"pattern","inputs":[],"properties":{"pattern":"brick"}}],"outputs":{"color":"p"}})"));
    require(!render::texture_graph::gpu::compile(unsupportedTexture,0,128).msl.empty(),"GPU brick pattern source missing");
    for(const auto& pattern:render::texturePatterns()){
        auto doc=Json::parse(R"({"version":1,"size":128,"nodes":[{"id":"p","op":"pattern","inputs":[],"properties":{}},{"id":"mask","op":"patternMask","inputs":[],"properties":{}},{"id":"normal","op":"normal","inputs":["mask"]}],"outputs":{"color":"p","normal":"normal"}})");
        doc.member("nodes").at(0).member("properties").set("pattern",Json::string(pattern));
        doc.member("nodes").at(1).member("properties").set("pattern",Json::string(pattern));
        const auto program=render::texture_graph::compile(doc);
        const auto colorPlan=render::texture_graph::gpu::plan(program,0,128);
        require(!colorPlan.first.msl.empty()&&!colorPlan.first.glsl.empty()&&!colorPlan.twoPass,"pattern plan source missing");
        const auto normalPlan=render::texture_graph::gpu::plan(program,2,128);
        require(normalPlan.twoPass&&normalPlan.scratchBytes==65536,"normal scratch plan wrong");
        require(normalPlan.first.glsl.find("floatBitsToUint")!=std::string::npos&&normalPlan.second.glsl.find("binding=13")!=std::string::npos,"normal plan ABI missing");
    }
    const auto headlessGpuTexture=call("texture_graph_gpu_preview",Json::parse(R"({"action":"read"})"));
    require(headlessGpuTexture.getStr("state")=="unavailable","headless texture GPU preview fabricated");

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

    std::cout << "PASS SpectraFX MCP graph CRUD, validation, variants, bake, path safety and old-effect isolation\n";
}
