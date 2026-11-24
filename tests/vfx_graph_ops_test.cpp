// SpectraFX editor/MCP graph authoring end-to-end regression.
// No GPU or genre module required. Uses the same Workspace::call entry as MCP.
#include "apps/editor/core/Workspace.h"
#include "apps/editor/core/TextureGraphGpuPreview.h"
#include "cloth/FakeDevice.h"
#include <set>
#include "engine/render/graph/GraphTelemetry.h"
#include "engine/render/graph/GraphWatch.h"
#include "engine/render/graph/GraphGpuProfile.h"
#include "engine/render/graph/TextureGraphGpu.h"
#include "engine/render/graph/TextureGraphMaterialBinding.h"
#include "engine/fx/SpectraFX.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using namespace aiverse;

static void requireGraphTest(bool yes, const char* reason) {
    if (!yes) throw std::runtime_error(reason);
}

// A synthetic readback fixture verifies only scheduling and owner lifecycle,
// not actual shader execution. The authored scalar graph evaluates to white.
struct DagFixtureDevice : FakeDevice {
    std::set<uint32_t> pipelines;
    BufferReadFn pending;
    size_t readBytes=0;
    uint64_t completed=0;
    bool reject=false;
    DagFixtureDevice(){info_.backend=gfx::Backend::Vulkan;computeEnabled=true;beginFrame(1,1);}
    uint64_t gpuSubmissionFence()const override{return 1;}
    uint64_t gpuCompletedFence()const override{return completed;}
    gfx::ComputeHandle createCompute(const gfx::ComputeDesc& d)override{
        auto h=FakeDevice::createCompute(d);if(h.valid())pipelines.insert(h.id);return h;
    }
    void destroyCompute(gfx::ComputeHandle h)override{pipelines.erase(h.id);}
    bool dispatchChecked(uint32_t x,uint32_t y=1)override{
        if(reject)return false;dispatch(x,y);return true;
    }
    bool readBufferRangeAsync(gfx::BufferHandle h,size_t offset,size_t size,BufferReadFn fn)override{
        if(pending||!buffers.contains(h.id)||offset+size>buffers.at(h.id).size())return false;
        pending=std::move(fn);readBytes=size;return true;
    }
    void deliver(){auto fn=std::move(pending);pending={};fn(std::vector<uint8_t>(readBytes,255));}
};
static void checkMaterializedOwner(){
    DagFixtureDevice dev;
    editor::TextureGraphGpuPreview owner;
    const auto args=Json::parse(R"({"action":"start","materialized":true,"requireGpu":true,"bakeSet":true,"graph":{"version":1,"size":8,"nodes":[{"id":"a","op":"constant","inputs":[],"properties":{"value":1}},{"id":"b","op":"multiply","inputs":["a","a"]}],"outputs":{"ao":"a","height":"b","roughness":"a"}}})");
    owner.start(args,"fixture");owner.tick(dev,"fixture");
    requireGraphTest(owner.status().getInt("materializedNodePasses")==2&&owner.status().getInt("materializedPackPasses")==1&&bool(dev.pending),"materialized owner did not encode DAG and first pack");
    requireGraphTest(!dev.buffers.empty()&&!dev.pipelines.empty(),"materialized resources not retained for pending read");
    dev.deliver();owner.tick(dev,"fixture");owner.tick(dev,"fixture");
    requireGraphTest(owner.status().getInt("materializedNodePasses")==2&&owner.status().getInt("materializedPackPasses")==2&&bool(dev.pending),"second output recomputed shared DAG");
    dev.deliver();owner.tick(dev,"fixture");
    auto status=owner.status();
    requireGraphTest(status.getStr("state")=="ready"&&status.getInt("completedOutputs")==3&&status.getInt("reusedOutputs")==1,"materialized output set failed");
    requireGraphTest(dev.buffers.empty()&&dev.pipelines.empty(),"completed materialized resources not released");
    auto started=owner.start(args,"fixture");owner.tick(dev,"fixture");
    Json cancel=Json::object();cancel.set("request",started["request"]);
    owner.cancel(cancel,"fixture");
    requireGraphTest(owner.status().getBool("draining")&&!dev.buffers.empty(),"cancel freed in-flight GPU storage");
    dev.deliver();owner.tick(dev,"fixture");
    requireGraphTest(!owner.status().getBool("busy")&&dev.buffers.empty()&&dev.pipelines.empty(),"cancel did not retire materialized storage");
    dev.reject=true;owner.start(args,"fixture");owner.tick(dev,"fixture");
    requireGraphTest(owner.status().getStr("state")=="failed"&&owner.status().getBool("draining")&&!dev.buffers.empty(),"partial dispatch rejection lost retirement");
    dev.completed=1;owner.tick(dev,"fixture");
    requireGraphTest(!owner.status().getBool("busy")&&dev.buffers.empty()&&dev.pipelines.empty(),"failed dispatch fence did not retire");
    owner.shutdownAfterIdle(dev);
}
int main() {
    checkMaterializedOwner();
    {
        using namespace render::texture_graph;
        const auto p=compile(Json::parse(R"({"version":1,"size":8,"nodes":[{"id":"c","op":"color","inputs":[],"properties":{"value":[1,0,0]}},{"id":"s","op":"constant","inputs":[],"properties":{"value":0.5}}],"outputs":{"color":"c","height":"s","ao":"s"}})"));
        std::vector<size_t> targets;for(const auto& pair:p.outputs)targets.push_back(pair.second);
        auto images=evaluateMany(p,targets);
        auto plan=prepareMaterialImages(p,images,"fixture_1");
        requireGraphTest(plan.entries.size()==2,"only bindable graph outputs should stage");
        requireGraphTest(plan.entries[0].slot==&render::Material::occlusionTex&&plan.entries[1].slot==&render::Material::baseTex,"ordered material slots incorrect");
        requireGraphTest(plan.entries[1].image.name=="#texture-graph/fixture_1/color","generation identity missing");
        bool rejected=false;try{(void)prepareMaterialImages(p,images,"../bad");}catch(const std::invalid_argument&){rejected=true;}
        requireGraphTest(rejected,"invalid generation accepted");
        images[0].rgba.pop_back();rejected=false;
        try{(void)prepareMaterialImages(p,images,"fixture_2");}catch(const std::invalid_argument&){rejected=true;}
        requireGraphTest(rejected,"incomplete texture pixels accepted");
    }
    {
        using namespace render::texture_graph;
        const auto p=compile(Json::parse(R"({"version":1,"size":8,"nodes":[{"id":"h","op":"gradient","inputs":[],"properties":{"axis":"x"}},{"id":"r","op":"multiply","inputs":["h","h"]},{"id":"n","op":"normal","inputs":["h"]},{"id":"a","op":"invert","inputs":["r"]}],"outputs":{"height":"h","normal":"n","ao":"a"}})"));
        const auto h=p.outputs.at("height"),n=p.outputs.at("normal"),a=p.outputs.at("ao");
        auto all=evaluateMany(p,{n,a,h,n});
        requireGraphTest(all.size()==4,"lifetime output order or duplicates lost");
        requireGraphTest(all[0].rgba==evaluate(p,n).rgba&&all[1].rgba==evaluate(p,a).rgba&&
                all[2].rgba==evaluate(p,h).rgba&&all[3].rgba==all[0].rgba,
                "lifetime multi-output pixels differ from individual outputs");
        requireGraphTest(all[2].rgba[0]==16&&all[2].rgba[7*4]==239,"gradient output changed");
    }
    const auto mark = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() / ("aiverse-spectrafx-ops-" + std::to_string(mark));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code e; fs::remove_all(path, e); } } cleanup{root};
    fs::create_directories(root);

    editor::Workspace ws;
    ws.project.dir = root.string();
    const auto call = [&](const char* name, const Json& args) { return ws.call(name, args, "test-spectrafx"); };
    {
        auto material=Json::parse(R"({"name":"fixture_material","material":{"roughness":0.5},"createOnly":true})");
        requireGraphTest(call("material_set",material).getBool("ok"),"create-only material failed");
        requireGraphTest(call("material_set",material).has("error"),"create-only overwrote existing material");
        const auto file=root/"materials.json";const auto before=Json::parseFile(file.string());
        const auto lock=fs::path(file.string()+".aiverse-write-lock");fs::create_directory(lock);
        requireGraphTest(call("material_delete",Json::parse(R"({"name":"fixture_material"})")).has("error"),"material delete ignored writer lock");
        requireGraphTest(Json::parseFile(file.string())==before,"busy writer modified material library");
        fs::remove(lock);
        requireGraphTest(call("material_delete",Json::parse(R"({"name":"fixture_material"})")).getBool("ok"),"material delete failed");
        material.set("createOnly",Json::fromBool(false));material.set("requireExisting",Json::fromBool(true));
        requireGraphTest(call("material_set",material).has("error"),"patch recreated deleted material");
    }
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
    requireGraphTest(catalog.getBool("ok") && catalog["domains"].size()==5, "generic graph catalog missing domains");
    requireGraphTest(call("graph_list", Json::parse(R"({"domain":"unknown"})")).has("error"), "unknown graph domain accepted");
    Json generic = Json::object();
    generic.set("domain",Json::string("spectrafx"));
    generic.set("name",Json::string("generic_spark"));
    generic.set("graph",graph);
    requireGraphTest(call("graph_validate",generic).getBool("ok"),"generic native validation failed");
    requireGraphTest(call("graph_dependencies",generic)["result"]["edges"].size()==2,"generic dependency edges incorrect");
    requireGraphTest(call("graph_save",generic).has("error"),"unversioned write accepted without explicit consent");
    generic.set("allowUnversionedWrite",Json::fromBool(true));
    requireGraphTest(call("graph_save",generic).getBool("ok"),"generic native save failed");
    requireGraphTest(call("graph_get",generic)["result"]["graph"]==graph,"generic graph readback mismatch");
    requireGraphTest(call("graph_node_catalog",generic)["result"]["nodes"].isArray(),"generic native node catalog missing");
    requireGraphTest(call("graph_save",generic).has("error"),"legacy create consent overwrote a published graph");
    Json newerFx=graph;newerFx.member("parameters").set("size",Json::number(0.5));
    generic.set("expected",graph);generic.set("graph",newerFx);
    requireGraphTest(call("graph_save",generic).getBool("ok"),"matching FX original rejected");
    requireGraphTest(call("graph_save",generic).has("error"),"stale FX original accepted");
    requireGraphTest(call("graph_get",generic)["result"]["graph"]==newerFx,"stale FX save changed asset");
    generic.erase("expected");generic.set("graph",graph);
    requireGraphTest(call("fx_graph_delete",generic).getBool("ok"),"generic fixture cleanup failed");

    const auto shaderDraft=Json::parse(R"({"domain":"shader","graph":{"version":1,"domain":"material","nodes":[{"id":"input","op":"input"}],"outputs":{"color":"input"}},"edits":[{"action":"add","value":{"id":"tint","op":"constant","value":[1,1,1,1]}},{"action":"setOutput","output":"color","node":"tint"}]})");
    const auto shaderEdited=call("graph_edit",shaderDraft);
    requireGraphTest(shaderEdited.getBool("ok")&&shaderEdited["result"].getBool("valid"),"shader batch validation failed");
    requireGraphTest(shaderEdited["result"].getInt("applied")==2&&!shaderEdited["result"].getBool("saved"),"shader batch metadata wrong");
    requireGraphTest(shaderEdited["result"]["graph"]["outputs"].getStr("color")=="tint","shader output not changed");
    requireGraphTest(shaderDraft["graph"]["nodes"].size()==1,"input draft mutated");
    requireGraphTest(!fs::exists(root/"shaders"),"shader draft wrote files");
    Json shaderSave=Json::object();shaderSave.set("name",Json::string("cas_shader"));
    shaderSave.set("graph",shaderDraft["graph"]);
    requireGraphTest(call("shader_graph_set",shaderSave).getBool("ok"),"shader create failed");
    requireGraphTest(call("shader_graph_set",shaderSave).has("error"),"shader blind overwrite accepted");
    shaderSave.set("expected",shaderDraft["graph"]);shaderSave.set("graph",shaderEdited["result"]["graph"]);
    const auto shaderFile=root/"shaders"/"cas_shader.json";
    const auto lockPath=fs::path(shaderFile.string()+".aiverse-write-lock");
    fs::create_directory(lockPath);
    requireGraphTest(call("shader_graph_set",shaderSave).has("error"),"busy shader lock ignored");
    fs::remove(lockPath);
    requireGraphTest(call("shader_graph_set",shaderSave).getBool("ok"),"matching shader original rejected");
    requireGraphTest(call("shader_graph_set",shaderSave).has("error"),"stale shader original accepted");
    requireGraphTest(Json::parseFile(shaderFile.string())==shaderEdited["result"]["graph"],"stale shader save changed asset");
    // An in-project leaf alias must not bypass the shared transaction guard.
    const auto alias=root/"shaders"/"alias_shader.json";
    std::error_code linkError;
    fs::create_symlink(shaderFile.filename(),alias,linkError);
    if (!linkError) {
        Json aliasSave=shaderSave;
        aliasSave.set("name",Json::string("alias_shader"));
        aliasSave.set("expected",shaderEdited["result"]["graph"]);
        aliasSave.set("graph",shaderDraft["graph"]);
        requireGraphTest(call("shader_graph_set",aliasSave).has("error"),"shader alias overwrite accepted");
        requireGraphTest(fs::is_symlink(fs::symlink_status(alias)),"shader alias replaced");
        requireGraphTest(Json::parseFile(shaderFile.string())==shaderEdited["result"]["graph"],"shader alias changed target");
        fs::remove(alias);
    } else {
        // Windows hosts may not grant the privilege to create symbolic links.
        std::cout<<"Shader symlink fixture unavailable: "<<linkError.message()<<'\n';
#ifndef _WIN32
        requireGraphTest(false,"could not create shader symlink fixture");
#endif
    }
    fs::remove(shaderFile);fs::remove(root/"shaders");

    // Deletion is a graph asset operation, never directory removal.
    const auto directoryTarget=root/"fx"/"graphs"/"directory_target.vfx.json";
    fs::create_directories(directoryTarget);
    Json directoryArgs=Json::object();
    directoryArgs.set("name",Json::string("directory_target"));
    requireGraphTest(call("fx_graph_delete",directoryArgs).has("error"),"graph deletion accepted a directory");
    requireGraphTest(fs::is_directory(directoryTarget),"graph deletion removed a directory");
    directoryArgs.set("graph",graph);
    requireGraphTest(call("fx_graph_set",directoryArgs).has("error"),"graph save accepted a directory");
    requireGraphTest(fs::is_directory(directoryTarget),"graph save replaced a directory");
    fs::remove(directoryTarget);
    auto badBatch=shaderDraft;badBatch.member("edits").push(Json::parse(R"({"action":"add","value":{"id":"tint","op":"constant"}})"));
    requireGraphTest(call("graph_edit",badBatch).has("error"),"duplicate ID batch accepted");
    const auto motionDraft=Json::parse(R"({"domain":"motionweave","graph":{"nodes":[{"id":"bind","kind":"bind"}],"output":"bind"},"edits":[{"action":"addState","value":{"id":"idle","output":"bind","transitions":[]}},{"action":"setInitial","node":"idle"},{"action":"addTransition","state":"idle","value":{"to":"idle","fade":0.2,"when":[]}}]})");
    const auto motionEdited=call("graph_edit",motionDraft);
    if(!motionEdited.getBool("ok"))std::cerr<<"Motion draft diagnostics: "<<motionEdited.dump()<<"\n";
    requireGraphTest(motionEdited.getBool("ok")&&motionEdited["result"]["graph"]["states"][size_t(0)]["transitions"].size()==1,"animation state draft failed");
    requireGraphTest(!motionEdited["result"].getBool("valid"),"model-less motion graph unexpectedly validated");
    requireGraphTest(!fs::exists(root/"animations"),"animation draft wrote files");
    const auto modelDraft=Json::parse(R"({"domain":"modeling","document":{"version":1,"parameters":{},"materials":[],"nodes":[{"id":"root"}],"clips":[],"skins":[],"graph":null},"edits":[{"op":"put","collection":"nodes","id":"body","value":{"parent":"root"}},{"op":"parameters","values":{"size":1}}]})");
    const auto modelEdited=call("graph_edit",modelDraft);
    requireGraphTest(modelEdited.getBool("ok")&&modelEdited["result"]["document"]["nodes"].size()==2,"native model draft batch failed");
    requireGraphTest(!fs::exists(root/"models"),"model draft wrote files");
    auto fxBatch=generic;fxBatch.set("edits",Json::parse(R"([{"action":"setProperty","node":"spawn","property":"burst","value":12},{"action":"setProperty","node":"init","property":"life","value":2}])"));
    const auto fxEdited=call("graph_edit",fxBatch);
    requireGraphTest(fxEdited.getBool("ok")&&fxEdited["result"].getBool("valid")&&fxEdited["result"].getInt("applied")==2,"native FX batch failed");
    requireGraphTest(!fs::exists(root/"fx"/"graphs"/"generic_spark.vfx.json"),"FX draft wrote files");

    auto typed=shaderDraft;typed.erase("edits");typed.set("node",Json::string("input"));
    requireGraphTest(call("graph_property_schema",typed)["result"]["fields"].has("position"),"property schema missing position");
    typed.set("edit",Json::parse(R"({"action":"setProperty","node":"input","property":"position","value":[10,20]})"));
    requireGraphTest(call("graph_edit",typed)["result"]["graph"]["nodes"][size_t(0)]["position"].size()==2,"typed move failed");
    typed.member("edit").set("property",Json::string("id"));
    requireGraphTest(call("graph_edit",typed).has("error"),"typed edit changed immutable ID");
    auto dependencies=Json::parse(R"({"domain":"motionweave","graph":{"nodes":[{"id":"sub","kind":"subgraph","graph":"a"}],"graphs":{"a":{"nodes":[{"id":"loop","kind":"subgraph","graph":"a"}]}}}})");
    const auto resolved=call("graph_asset_dependencies",dependencies);
    requireGraphTest(resolved.getBool("ok")&&resolved["result"]["issues"].size()==1,"embedded recursion not detected");
    auto textureRef=Json::parse(R"({"domain":"shader","graph":{"nodes":[{"id":"tex","op":"sampleTexture","texture":"../outside.png"}]}})");
    requireGraphTest(call("graph_asset_dependencies",textureRef)["result"]["references"][size_t(0)].getStr("status")=="invalid-path","unsafe path not flagged");

    const auto live=call("graph_live_snapshot",Json::parse(R"({"domain":"spectrafx"})"));
    requireGraphTest(live.has("available")&&!live.getBool("available"),"headless host must not fabricate live owner");
    const auto caps=call("graph_runtime_capabilities",Json::object());
    requireGraphTest(!caps.getBool("nativeEditorSnapshotOwner"),"headless host claims GUI owner");
    requireGraphTest(caps.getBool("ok")&&!caps.getBool("liveAttached"),"runtime capability misreported");
    Json probe=Json::object();probe.set("domain",Json::string("spectrafx"));probe.set("graph",graph);probe.set("steps",Json::number(int64_t(6)));
    const auto sampled=call("graph_runtime_probe",probe);
    requireGraphTest(sampled.getBool("ok")&&sampled["frames"].size()==6,"headless FX runtime probe failed");
    requireGraphTest(!sampled.getBool("liveAttached")&&!sampled.getBool("gpuMeasured"),"probe mislabeled");
    requireGraphTest(sampled["frames"][size_t(5)].getInt("particles")>0,"expected native particles");
    probe.set("steps",Json::number(int64_t(61)));
    requireGraphTest(call("graph_runtime_probe",probe).has("error"),"unbounded probe accepted");

    graph_telemetry::History history;
    const graph_telemetry::Identity track{"world-1","42","revision-1","spectrafx"};
    for(uint64_t i=1;i<=121;++i)requireGraphTest(history.publish(track,i,Json::object()),"history publish failed");
    requireGraphTest(history.read(track).size()==120&&history.read(track)[size_t(0)].getStr("sequence")=="2","history bound failed");
    requireGraphTest(!history.publish(track,121,Json::object()),"duplicate sequence accepted");
    requireGraphTest(history.read({"world-2","42","revision-1","spectrafx"}).size()==0,"world identity crossed");
    history.clearWorld("world-1");requireGraphTest(history.read(track).size()==0,"world history not cleared");
    Json debug=Json::object();debug.set("action",Json::string("start"));debug.set("domain",Json::string("spectrafx"));debug.set("graph",graph);
    const auto begun=call("graph_debug_session",debug);
    requireGraphTest(begun.getBool("active")&&!begun.getStr("session").empty(),"FX isolated session not started");
    Json step=Json::object();step.set("action",Json::string("step"));step.set("session",begun["session"]);
    const auto stepped=call("graph_debug_session",step);
    requireGraphTest(stepped.getInt("step")==1&&stepped["frames"].size()==1,"debug step failed");
    step.set("action",Json::string("continue"));step.set("steps",Json::number(int64_t(3)));
    requireGraphTest(call("graph_debug_session",step).getInt("step")==4,"debug continuation did not retain state");
    step.set("action",Json::string("stop"));requireGraphTest(!call("graph_debug_session",step).getBool("active"),"session stop failed");
    step.set("action",Json::string("step"));requireGraphTest(call("graph_debug_session",step).has("error"),"stopped session accepted");

    graph_telemetry::Watch watch;
    auto watchSample=Json::parse(R"({"available":true,"domain":"spectrafx","worldEpoch":"1","instanceSerial":"5","programIdentity":"instance:5","sequence":"1","particles":3})");
    watch.start("spectrafx","preview",watchSample);
    requireGraphTest(watch.running(),"watch not started");
    requireGraphTest(!watch.capture(watchSample),"duplicate sample accepted");
    watchSample.set("sequence",Json::string("2"));
    requireGraphTest(watch.capture(watchSample)&&watch.status()["frames"].size()==2,"watch capture failed");
    watchSample.set("instanceSerial",Json::string("6"));
    requireGraphTest(!watch.capture(watchSample)&&!watch.running(),"watch followed replacement");
    requireGraphTest(watch.status()["frames"].size()==2,"watch lost stopped history");
    watch.clear();requireGraphTest(watch.status()["frames"].size()==0,"watch clear failed");
    requireGraphTest(!call("graph_live_watch",Json::parse(R"({"action":"status"})")).getBool("available"),"headless watch claims owner");

    requireGraphTest(!call("graph_live_profile",Json::parse(R"({"action":"read","entity":"42"})")).getBool("available"),"headless profile claims live owner");

    struct FakeScopeDevice {
        uint64_t frameSerial() const{return 11;}
        bool beginGpuScope(uint64_t){return allow;}
        bool allow=true;
    } fakeScope;
    graph_gpu_profile::Capture gpuProfile;
    gfx::DeviceInfo fakeInfo;fakeInfo.backend=gfx::Backend::Metal;fakeInfo.name="test metadata only";
    gpuProfile.start(fakeInfo,"fixture");
    const auto scopeTag=gpuProfile.begin(fakeScope,"viewport",2,true);
    requireGraphTest(scopeTag!=0&&!gpuProfile.report().getBool("available"),"pending GPU query treated as complete");
    requireGraphTest(gpuProfile.complete({scopeTag,2.5,true},12)==2,"adaptive scope result routing failed");
    requireGraphTest(gpuProfile.report()["samples"].size()==1,"completed GPU sample missing");
    const auto invalidTag=gpuProfile.begin(fakeScope,"fx",1,false);
    gpuProfile.complete({invalidTag,0,false},13);
    requireGraphTest(gpuProfile.report()["samples"].size()==1,"invalid GPU query recorded");
    const auto cancelled=gpuProfile.begin(fakeScope,"fx",1,false);
    gpuProfile.stop();gpuProfile.start(fakeInfo,"fixture");
    gpuProfile.complete({cancelled,1.5,true},14);
    requireGraphTest(!gpuProfile.report().getBool("available"),"cancelled query crossed capture epoch");
    fakeScope.allow=false;
    requireGraphTest(gpuProfile.begin(fakeScope,"fx",1,false)==0,"unsupported timing accepted");
    requireGraphTest(!call("graph_gpu_profile",Json::parse(R"({"action":"read"})")).getBool("available"),"headless GPU owner fabricated");

    auto tex=Json::parse(R"({"domain":"texture","name":"fixture","graph":{"version":1,"size":8,"nodes":[{"id":"rough","op":"constant","inputs":[],"properties":{"value":0.5}},{"id":"metal","op":"constant","inputs":[],"properties":{"value":1}},{"id":"packed","op":"metallicRoughness","inputs":["rough","metal"]},{"id":"normal","op":"normal","inputs":["rough"]}],"outputs":{"metallicRoughness":"packed","normal":"normal"}},"output":"metallicRoughness","textureName":"fixture"})");
    requireGraphTest(call("graph_validate",tex).getBool("ok"),"native texture DAG validation failed");
    auto pixels=call("texture_graph_preview",tex);
    requireGraphTest(pixels.getBool("ok")&&pixels.getStr("rgbaHex").size()==8*8*8&&pixels.getStr("rgbaHex").substr(0,8)=="ff80ffff","packed PBR channel layout wrong");
    tex.set("output",Json::string("normal"));
    requireGraphTest(call("texture_graph_preview",tex).getStr("rgbaHex").substr(0,8)=="8080ffff","flat height normal wrong");
    requireGraphTest(call("graph_save",tex).getBool("ok"),"new texture graph save failed");
    requireGraphTest(call("graph_save",tex)["result"].has("error")||!call("graph_save",tex).getBool("ok"),"texture graph overwrite without expected accepted");
    tex.set("expected",tex["graph"]);
    requireGraphTest(call("graph_save",tex).getBool("ok"),"expected texture document save failed");
    requireGraphTest(call("texture_graph_bake",tex).getBool("ok"),"texture graph PNG bake failed");
    requireGraphTest(fs::is_regular_file(root/"content"/"textures"/"fixture_normal.png"),"texture bake missing file");
    requireGraphTest(call("texture_graph_bake",tex).has("error"),"texture bake overwrite accepted");
    auto brokenTexture=tex;
    brokenTexture.member("graph").member("nodes").at(2).member("inputs").at(0)=Json::string("normal");
    requireGraphTest(!call("graph_validate",brokenTexture).getBool("ok"),"texture type mismatch accepted");

    const auto textureCatalog=call("texture_graph_catalog",Json::object());
    requireGraphTest(textureCatalog.getBool("ok")&&textureCatalog["outputTypes"].getStr("normal")=="normal","texture output catalog missing types");
    const auto textureTemplates=call("texture_graph_templates",Json::object());
    requireGraphTest(textureTemplates.getBool("ok")&&textureTemplates["templates"].size()==6,"texture starter library missing");
    for(const auto& row:textureTemplates["templates"].items()){
        Json args=Json::object();args.set("graph",row["graph"]);
        requireGraphTest(call("texture_graph_validate",args).getBool("ok"),"invalid texture template");
    }
    tex.set("textureName",Json::string("fixture_set"));
    const auto bakedSet=call("texture_graph_bake_set",tex);
    requireGraphTest(bakedSet.getBool("ok")&&bakedSet["files"].size()==2,"PBR set bake failed");
    for(const auto& row:bakedSet["files"].items()){
        requireGraphTest(!row.getBool("sRGB"),"numeric texture marked sRGB");
        requireGraphTest(row.getStr("backend")=="cpu","CPU set provenance mislabeled");
        requireGraphTest(fs::is_regular_file(root/row.getStr("path")),"PBR set file missing");
    }
    requireGraphTest(call("texture_graph_bake_set",tex).has("error"),"PBR set overwrite accepted");
    requireGraphTest(!fs::exists(root/"content"/"textures"/".fixture_set.stage"),"PBR staging leaked");

    auto instance=tex;
    instance.member("graph").set("parameters",Json::parse(R"({"roughness":{"default":0.5,"bindings":[{"node":"rough","property":"value"}]}})"));
    instance.set("output",Json::string("metallicRoughness"));
    instance.set("overrides",Json::parse(R"({"roughness":0.25})"));
    const auto variantPixels=call("texture_graph_preview",instance);
    requireGraphTest(variantPixels.getStr("rgbaHex").substr(0,8)=="ff40ffff","parameter override not evaluated");
    requireGraphTest(call("texture_graph_preview",instance).getBool("cacheHit"),"identical resolved preview cache miss");
    instance.set("overrides",Json::parse(R"({"roughness":0.75})"));
    requireGraphTest(call("texture_graph_preview",instance).getStr("rgbaHex").substr(0,8)=="ffbfffff","parameter cache returned stale pixels");
    instance.set("overrides",Json::parse(R"({"roughness":2})"));
    requireGraphTest(call("texture_graph_preview",instance).has("error"),"invalid override accepted");
    requireGraphTest(fs::is_regular_file(root/"content"/"textures"/"fixture_set"/"texture-set.json"),"bake provenance missing");
    requireGraphTest(call("texture_graph_cache",Json::parse(R"({"clear":true})")).getInt("entries")==0,"cache clear failed");

    const auto gpuTexture=render::texture_graph::compile(tex["graph"]);
    const auto gpuPacked=render::texture_graph::gpu::compile(gpuTexture,gpuTexture.outputs.at("metallicRoughness"),8);
    requireGraphTest(!gpuPacked.msl.empty()&&gpuPacked.msl.find("cs_main")!=std::string::npos,"GPU texture source missing");
    const auto gpuNormal=render::texture_graph::gpu::compile(gpuTexture,gpuTexture.outputs.at("normal"),8);
    requireGraphTest(!gpuNormal.msl.empty()&&gpuNormal.msl.find("evaluatePoint")!=std::string::npos,"GPU normal source missing");
    requireGraphTest(gpuNormal.glsl.find("binding=12")!=std::string::npos&&gpuNormal.glsl.find("gl_GlobalInvocationID")!=std::string::npos,"Vulkan storage ABI missing");
    auto unsupportedTexture=render::texture_graph::compile(Json::parse(R"({"version":1,"size":128,"nodes":[{"id":"p","op":"pattern","inputs":[],"properties":{"pattern":"brick"}}],"outputs":{"color":"p"}})"));
    requireGraphTest(!render::texture_graph::gpu::compile(unsupportedTexture,0,128).msl.empty(),"GPU brick pattern source missing");
    for(const auto& pattern:render::texturePatterns()){
        auto doc=Json::parse(R"({"version":1,"size":128,"nodes":[{"id":"p","op":"pattern","inputs":[],"properties":{}},{"id":"mask","op":"patternMask","inputs":[],"properties":{}},{"id":"normal","op":"normal","inputs":["mask"]}],"outputs":{"color":"p","normal":"normal"}})");
        doc.member("nodes").at(0).member("properties").set("pattern",Json::string(pattern));
        doc.member("nodes").at(1).member("properties").set("pattern",Json::string(pattern));
        const auto program=render::texture_graph::compile(doc);
        const auto colorPlan=render::texture_graph::gpu::plan(program,0,128);
        requireGraphTest(!colorPlan.first.msl.empty()&&!colorPlan.first.glsl.empty()&&!colorPlan.twoPass,"pattern plan source missing");
        const auto normalPlan=render::texture_graph::gpu::plan(program,2,128);
        requireGraphTest(normalPlan.twoPass&&normalPlan.scratchBytes==65536,"normal scratch plan wrong");
        requireGraphTest(normalPlan.first.glsl.find("floatBitsToUint")!=std::string::npos&&normalPlan.second.glsl.find("binding=13")!=std::string::npos,"normal plan ABI missing");
    }
    const auto sharedTexture=render::texture_graph::compile(Json::parse(R"({"version":1,"size":128,"nodes":[{"id":"h","op":"gradient","inputs":[]},{"id":"n","op":"normal","inputs":["h"]},{"id":"r","op":"constant","inputs":[]}],"outputs":{"height":"h","normal":"n","roughness":"r"}})"));
    const auto materializedSources=render::texture_graph::gpu::compileMaterializedDag(sharedTexture,{0,1,2},128);
    requireGraphTest(materializedSources.nodes.size()==3&&materializedSources.packs.size()==3,"materialized sources missing");
    requireGraphTest(materializedSources.nodes[0].msl.find("float* result [[buffer(0)]]")!=std::string::npos,"scalar producer ABI incorrect");
    requireGraphTest(materializedSources.nodes[1].glsl.find("input0[y*n+(x+1u)%n]")!=std::string::npos&&materializedSources.nodes[1].glsl.find("vec4 result[]")!=std::string::npos,"normal materialized shader missing");
    requireGraphTest(materializedSources.totalBytes==materializedSources.plan.allocatedBytes+128u*128u*4u,"materialized pack buffer budget missing");
    const auto blendTexture=render::texture_graph::compile(Json::parse(R"({"version":1,"size":8,"nodes":[{"id":"a","op":"color","inputs":[],"properties":{"value":[0,0,0]}},{"id":"b","op":"color","inputs":[],"properties":{"value":[1,1,1]}},{"id":"t","op":"constant","inputs":[]},{"id":"mix","op":"blendColor","inputs":["a","b","t"]}],"outputs":{"color":"mix"}})"));
    const auto multiSource=render::texture_graph::gpu::compileMaterializedDag(blendTexture,{3},8);
    requireGraphTest(multiSource.nodes[3].msl.find("float* input2 [[buffer(3)]]")!=std::string::npos&&multiSource.nodes[3].glsl.find("mix(v0,v1,v2.x)")!=std::string::npos,"multi-input expression or binding missing");
    requireGraphTest(multiSource.packs[0].glsl.find("0xff000000u")!=std::string::npos,"RGBA8 pack missing");
    const auto sharedHeight=render::texture_graph::gpu::plan(sharedTexture,0,128,true);
    const auto sharedNormal=render::texture_graph::gpu::plan(sharedTexture,1,128,true);
    requireGraphTest(sharedHeight.twoPass&&sharedNormal.twoPass&&sharedHeight.scratchBytes==sharedNormal.scratchBytes,"shared scalar plan missing");
    requireGraphTest(sharedHeight.first.msl==sharedNormal.first.msl&&sharedHeight.first.glsl==sharedNormal.first.glsl,"scalar producer differs");
    requireGraphTest(sharedHeight.second.msl.find("height[tid]")!=std::string::npos&&sharedHeight.second.glsl.find("binding=13")!=std::string::npos,"scalar packing ABI missing");
    requireGraphTest(!render::texture_graph::gpu::plan(sharedTexture,0,128).twoPass&&!render::texture_graph::gpu::plan(sharedTexture,2,128,true).twoPass,"independent scalar should stay fused");
    Json planArgs=Json::object();planArgs.set("action",Json::string("plan"));planArgs.set("graph",tex["graph"]);planArgs.set("bakeSet",Json::fromBool(true));planArgs.set("resolution",Json::number(int64_t(8)));
    const auto materialized=call("texture_graph_gpu_preview",planArgs);
    requireGraphTest(materialized.getBool("ok")&&materialized.getStr("state")=="planned"&&!materialized.getBool("executed"),"headless materialized plan missing or fabricated execution");
    requireGraphTest(materialized["outputs"].size()==2&&materialized["passes"].size()>0&&materialized["slotBytes"].size()>0,"materialized schedule missing slots or outputs");
    requireGraphTest(materialized.getInt("packShaderCount")==2&&materialized.getInt("resultBytes")==8*8*4,"materialized source metadata missing");
    const auto headlessGpuTexture=call("texture_graph_gpu_preview",Json::parse(R"({"action":"read"})"));
    requireGraphTest(headlessGpuTexture.getStr("state")=="unavailable","headless texture GPU preview fabricated");
    requireGraphTest(!call("texture_graph_gpu_preview",Json::parse(R"({"action":"cancel","request":"1"})")).getBool("ok"),"headless GPU cancellation fabricated");

    auto fullTexture=render::texture_graph::compile(Json::parse(R"({"version":1,"size":1024,"nodes":[{"id":"height","op":"constant","inputs":[],"properties":{"value":0.5}},{"id":"normal","op":"normal","inputs":["height"]}],"outputs":{"normal":"normal"}})"));
    const auto fullPlan=render::texture_graph::gpu::plan(fullTexture,1,1024);
    requireGraphTest(fullPlan.twoPass&&fullPlan.scratchBytes==4*1024*1024,"full-size GPU height allocation plan incorrect");
    requireGraphTest(fullPlan.first.msl.find("1024u")!=std::string::npos&&fullPlan.second.glsl.find("1024u")!=std::string::npos,"full-size GPU shader resolution mismatch");
    requireGraphTest(fullPlan.scratchBytes/render::texture_graph::gpu::readbackBlockBytes==64,"full-size readback chunk count wrong");
    requireGraphTest(!call("texture_graph_gpu_preview",Json::parse(R"({"action":"publish","request":"1","textureName":"forbidden_headless"})")).getBool("ok"),"headless GPU publication accepted");

    auto setStart=Json::object();setStart.set("action",Json::string("start"));setStart.set("graph",tex["graph"]);setStart.set("bakeSet",Json::fromBool(true));
    requireGraphTest(call("texture_graph_gpu_preview",setStart).getStr("state")=="unavailable","headless GPU set owner fabricated");
    const auto manifest=Json::parseFile((root/"content"/"textures"/"fixture_set"/"texture-set.json").string());
    requireGraphTest(manifest["files"].size()==2&&manifest["files"][size_t(0)].getStr("backend")=="cpu","shared texture set provenance missing");

    Json create = Json::object();
    create.set("name", Json::string("spark"));
    create.set("graph", graph);
    auto created = call("fx_graph_set", create);
    requireGraphTest(created.getBool("ok"), "valid graph was rejected");
    requireGraphTest(fs::exists(root/"fx"/"graphs"/"spark.vfx.json"), "graph file not created");
    requireGraphTest(!ws.changes.empty() && ws.changes.back().kind == editor::Change::Fx, "missing editor FX invalidation");

    // Separate draft publication guards both the exact draft and the destination.
    Json draft=Json::object();draft.set("name",Json::string("draft_cas"));draft.set("graph",graph);
    requireGraphTest(call("fx_graph_draft_set",draft).getBool("ok"),"draft create failed");
    requireGraphTest(call("fx_graph_draft_set",draft).has("error"),"draft blind overwrite accepted");
    draft.set("expected",graph);draft.set("graph",newerFx);
    requireGraphTest(call("fx_graph_draft_set",draft).getBool("ok"),"matching draft original rejected");
    Json publish=Json::object();publish.set("name",Json::string("draft_cas"));publish.set("expectedDraft",graph);
    requireGraphTest(call("fx_graph_draft_publish",publish).has("error"),"stale draft publication accepted");
    publish.set("expectedDraft",newerFx);
    requireGraphTest(call("fx_graph_draft_publish",publish).getBool("ok"),"matching draft publish failed");
    requireGraphTest(call("fx_graph_get",publish)["graph"]==newerFx,"published draft content mismatch");
    requireGraphTest(call("fx_graph_delete",publish).getBool("ok"),"published draft cleanup failed");

    Json name = Json::object();
    name.set("name", Json::string("spark"));
    auto got = call("fx_graph_get", name);
    requireGraphTest(got["graph"].isObject() && got["validation"].getBool("ok"), "graph_get failed");
    requireGraphTest(got["validation"].has("metalGpuEligible") &&
            !got["validation"].getBool("metalGpuEligible"),
            "smoke sprite should expose CPU fallback diagnostics");
    requireGraphTest(!got["validation"].getStr("metalGpuReason").empty(),
            "CPU fallback reason was not reported");
    auto all = call("fx_graph_list", Json::object());
    requireGraphTest(all["graphs"].isArray() && all["graphs"].size()==1, "graph list did not find asset");
    requireGraphTest(all["graphs"][size_t(0)].getBool("ok"), "list reported graph compiler failure");

    Json validation = Json::object();
    validation.set("name", Json::string("spark"));
    validation.set("overrides", Json::parse(R"({"size":1.25})"));
    const auto checked = call("fx_graph_validate", validation);
    requireGraphTest(checked.getBool("ok") && checked.getInt("emitters")==1, "graph variant validation failed");

    Json bake = Json::object();
    bake.set("name", Json::string("spark"));
    bake.set("effect", Json::string("spark_baked"));
    bake.set("overrides", Json::parse(R"({"size":1.25})"));
    const auto baked = call("fx_graph_bake", bake);
    requireGraphTest(baked.getBool("ok") && fs::exists(root/"fx"/"spark_baked.json"), "graph bake failed");
    const auto effect = Json::parseFile((root/"fx"/"spark_baked.json").string());
    fx::EffectDef parsed;
    std::vector<std::string> errors;
    requireGraphTest(fx::parseEffect(effect, "fx/spark_baked.json", parsed, errors), "baked asset invalid");
    requireGraphTest(parsed.emitters.size()==1 && parsed.emitters[0].size.min==1.25f, "override lost in bake");
    const auto refused = call("fx_graph_bake", bake);
    requireGraphTest(refused.has("error"), "bake overwrote an effect without permission");

    Json invalid = graph;
    invalid.set("version", Json::number(int64_t(3))); // v2 is now supported
    create.set("graph", invalid);
    const auto rejected = call("fx_graph_set", create);
    requireGraphTest(!rejected.getBool("ok"), "invalid graph should not be saved");
    requireGraphTest(call("fx_graph_get", name)["validation"].getBool("ok"), "invalid graph changed last-good asset");

    name.set("name", Json::string("../unsafe"));
    const auto escape = call("fx_graph_delete", name);
    requireGraphTest(escape.has("error"), "path traversal accepted");

    name.set("name", Json::string("spark"));
    requireGraphTest(call("fx_graph_delete", name).getBool("ok"), "graph delete failed");
    requireGraphTest(!fs::exists(root/"fx"/"graphs"/"spark.vfx.json"), "graph file still present");
    requireGraphTest(fs::exists(root/"fx"/"spark_baked.json"), "deleting graph deleted baked effect");

    std::cout << "PASS SpectraFX MCP graph CRUD, validation, variants, bake, path safety and old-effect isolation\n";
}
