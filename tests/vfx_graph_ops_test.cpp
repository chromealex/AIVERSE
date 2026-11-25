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
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using namespace aiverse;

static void checkGraphRequirement(bool yes, const char* reason) {
    if (!yes) throw std::runtime_error(reason);
}

// A synthetic readback fixture verifies only scheduling and owner lifecycle,
// not actual shader execution. The authored scalar graph evaluates to white.
struct DagFixtureDevice : FakeDevice {
    std::set<uint32_t> pipelines;
    BufferReadFn pending;
    size_t readBytes=0;
    uint64_t completed=0,submitted=1;
    bool reject=false;
    DagFixtureDevice(){info_.backend=gfx::Backend::Vulkan;computeEnabled=true;beginFrame(1,1);}
    uint64_t gpuSubmissionFence()const override{return submitted;}
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
    checkGraphRequirement(owner.status().getInt("materializedNodePasses")==2&&owner.status().getInt("materializedPackPasses")==1&&bool(dev.pending),"materialized owner did not encode DAG and first pack");
    checkGraphRequirement(!dev.buffers.empty()&&!dev.pipelines.empty(),"materialized resources not retained for pending read");
    dev.deliver();owner.tick(dev,"fixture");owner.tick(dev,"fixture");
    checkGraphRequirement(owner.status().getInt("materializedNodePasses")==2&&owner.status().getInt("materializedPackPasses")==2&&bool(dev.pending),"second output recomputed shared DAG");
    dev.deliver();owner.tick(dev,"fixture");
    auto status=owner.status();
    checkGraphRequirement(status.getStr("state")=="ready"&&status.getInt("completedOutputs")==3&&status.getInt("reusedOutputs")==1,"materialized output set failed");
    checkGraphRequirement(status.getBool("busy")&&status.getInt("materializedIdleBytes")==0&&!dev.buffers.empty(),"cache accepted before completed GPU fence");
    dev.completed=dev.submitted;owner.tick(dev,"fixture");
    status=owner.status();
    checkGraphRequirement(!status.getBool("busy")&&status.getInt("materializedIdleBytes")>0&&status.getInt("materializedIdlePipelines")>0,"completed materialized cache not retained");
    ++dev.submitted;
    auto started=owner.start(args,"fixture");owner.tick(dev,"fixture");
    checkGraphRequirement(owner.status().getBool("materializedBufferCacheHit")&&owner.status().getInt("materializedPipelineCacheHits")>0,"matching retired GPU cache not reused");
    Json cancel=Json::object();cancel.set("request",started["request"]);
    owner.cancel(cancel,"fixture");
    checkGraphRequirement(owner.status().getBool("draining")&&!dev.buffers.empty(),"cancel freed in-flight GPU storage");
    dev.deliver();owner.tick(dev,"fixture");
    checkGraphRequirement(owner.status().getBool("busy")&&!dev.buffers.empty(),"cancel retired before completed GPU fence");
    dev.completed=dev.submitted;owner.tick(dev,"fixture");
    checkGraphRequirement(!owner.status().getBool("busy")&&dev.buffers.empty()&&dev.pipelines.empty(),"cancel did not retire materialized storage");
    ++dev.submitted;dev.reject=true;owner.start(args,"fixture");owner.tick(dev,"fixture");
    checkGraphRequirement(owner.status().getStr("state")=="failed"&&owner.status().getBool("draining")&&!dev.buffers.empty(),"partial dispatch rejection lost retirement");
    dev.completed=dev.submitted;owner.tick(dev,"fixture");
    checkGraphRequirement(!owner.status().getBool("busy")&&dev.buffers.empty()&&dev.pipelines.empty(),"failed dispatch fence did not retire");
    owner.shutdownAfterIdle(dev);
}
// Fake readback bytes only exercise the request lifetime and exact destination
// acceptance gate. Real shader/copy/mip execution needs the native GPU fixture.
struct ResidentTextureFixture : DagFixtureDevice {
    TextureReadFn pendingTexture;
    gfx::TextureHandle readTarget{};
    std::set<uint32_t> copied;
    size_t copyCount=0;
    bool rejectCopy=false,mipRequested=false;
    bool supportsStorageBufferTextureCopy()const override{return true;}
    bool bufferInfo(gfx::BufferHandle h,gfx::BufferInfo& out)const override{
        const auto it=buffers.find(h.id);if(it==buffers.end())return false;
        out={it->second.size(),1};return true;
    }
    bool textureInfo(gfx::TextureHandle h,gfx::TextureInfo& out)const override{
        const auto it=textures.find(h.id);if(it==textures.end())return false;
        out={it->second.w,it->second.h,gfx::PixelFormat::RGBA8};return true;
    }
    bool copyStorageBufferToTexture2D(gfx::BufferHandle from,gfx::TextureHandle to,
            const gfx::BufferTextureCopy2D& copy)override{
        const auto it=buffers.find(from.id);auto t=textures.find(to.id);
        if(rejectCopy||it==buffers.end()||t==textures.end()||
           !gfx::validBufferTextureCopy2D(copy,it->second.size(),t->second.w,t->second.h))return false;
        mipRequested|=copy.generateMipmaps;copied.insert(to.id);++copyCount;return true;
    }
    bool readTextureAsync(gfx::TextureHandle h,gfx::TextureReadbackDesc desc,TextureReadFn fn)override{
        if(pendingTexture||desc.mipLevel!=0||desc.arrayLayer!=0||!copied.contains(h.id)||!textures.contains(h.id))return false;
        readTarget=h;pendingTexture=std::move(fn);return true;
    }
    void deliverTexture(bool valid=true){
        const auto& t=textures.at(readTarget.id);
        gfx::TextureReadbackResult result;
        result.width=t.w;result.height=t.h;result.format=gfx::PixelFormat::RGBA8;
        result.pixels.assign(size_t(t.w)*t.h*4u,255);
        if(!valid)result.pixels[0]=0;
        auto fn=std::move(pendingTexture);pendingTexture={};fn(std::move(result));
    }
    bool waitForResourceIdle()override{return true;}
};
static void checkResidentTextureOwner(){
    ResidentTextureFixture dev;
    render::RenderDeviceLifetime epoch(dev);
    editor::TextureGraphGpuPreview owner;
    const auto args=Json::parse(R"({"action":"start","materialized":true,"fusePointwise":true,
        "gpuTextureTransfer":true,"gpuTextureMipmaps":true,"bakeSet":true,
        "graph":{"version":1,"size":8,"nodes":[{"id":"a","op":"constant","inputs":[],"properties":{"value":1}}],
        "outputs":{"ao":"a","height":"a","mask":"a"}}})");
    bool refused=false;
    try{owner.start(args,"fixture");}catch(const std::invalid_argument&){refused=true;}
    checkGraphRequirement(refused,"resident request accepted without native lease");
    auto started=owner.start(args,"fixture",epoch.lease());
    owner.tick(dev,"fixture");
    checkGraphRequirement(bool(dev.pendingTexture)&&!dev.pending&&dev.copyCount==1&&dev.mipRequested,
            "resident owner did not validate copied texture");
    dev.deliverTexture();owner.tick(dev,"fixture");
    checkGraphRequirement(owner.status().getInt("gpuTextureAccepted")==0&&owner.status().getBool("busy"),
            "accepted texture before its frame fence");
    dev.completed=dev.submitted;++dev.submitted;
    owner.tick(dev,"fixture");owner.tick(dev,"fixture");
    checkGraphRequirement(bool(dev.pendingTexture)&&dev.copyCount==2&&dev.textures.size()==2,
            "duplicate bindable output failed to get its own GPU texture");
    dev.deliverTexture();dev.completed=dev.submitted;++dev.submitted;
    owner.tick(dev,"fixture");
    const auto ready=owner.status();
    checkGraphRequirement(ready.getBool("gpuTextureSetReady")&&!ready.getBool("busy")&&
            ready.getInt("gpuTextureAccepted")==2&&ready.getInt("completedOutputs")==3,
            "resident set not accepted atomically");
    checkGraphRequirement(ready.getInt("reusedOutputs")==1,"nonbindable duplicate did not reuse accepted bytes");
    refused=false;
    try{owner.start(args,"fixture",epoch.lease());}catch(const std::runtime_error&){refused=true;}
    checkGraphRequirement(refused,"unadopted resident set silently replaced");
    render::Model model;model.materials.resize(1);
    view::AppearanceAssetRegistry registry;
    view::AppearanceAssetRegistry::Asset asset;asset.model=&model;asset.materials=model.materials;
    checkGraphRequirement(registry.publishPrepared("fixture",asset),"fixture appearance missing");
    const auto old=registry.find("fixture");
    render::ModelImageRollbackPool rollback;
    std::string error;
    const auto request=started.getStr("request");
    const auto existing=dev.textures.size();
    checkGraphRequirement(!owner.publishCompletedGpuMaterial("other",request,registry,"fixture",
        old->version,old->modelPublicationGeneration,0,model,dev,epoch.lease(),rollback,&error),
        "stale project published a resident set");
    auto live=old;auto mirror=old->materials;
    const view::LiveAppearanceMaterialTarget stamp{old->modelPublicationGeneration,old->version,0};
    checkGraphRequirement(owner.publishCompletedGpuSnapshot("fixture",request,live,stamp,
        model,dev,epoch.lease(),rollback,&mirror,&error),"resident set could not be adopted by live snapshot");
    checkGraphRequirement(live->version==old->version+1&&live->modelPublicationGeneration==old->modelPublicationGeneration&&
        live->materials[0].occlusionTex==0&&mirror[0].occlusionTex==0&&
        old->materials[0].occlusionTex==-1&&model.materials[0].occlusionTex==-1,
        "live appearance snapshot or compatibility mirror was not atomically updated");
    checkGraphRequirement(owner.status().getBool("gpuTexturePublished")&&model.images.size()==2&&
        dev.textures.size()==existing&&dev.copied.contains(model.images[0].tex.id)&&
        dev.copied.contains(model.images[1].tex.id),"adoption allocated replacement GPU textures");
    checkGraphRequirement(!owner.publishCompletedGpuMaterial("fixture",request,registry,"fixture",
        old->version,old->modelPublicationGeneration,0,model,dev,epoch.lease(),rollback,&error),
        "resident set published twice");
    // Cancel a second request before its callback or fence is complete.
    started=owner.start(args,"fixture",epoch.lease());owner.tick(dev,"fixture");
    checkGraphRequirement(bool(dev.pendingTexture)&&dev.textures.size()==existing+1,"second resident request missing");
    Json cancel=Json::object();cancel.set("request",started["request"]);
    owner.cancel(cancel,"fixture");dev.deliverTexture();owner.tick(dev,"fixture");
    checkGraphRequirement(owner.status().getBool("busy")&&dev.textures.size()==existing+1,
        "cancel destroyed a resident texture before completion");
    dev.completed=dev.submitted;++dev.submitted;owner.tick(dev,"fixture");
    checkGraphRequirement(!owner.status().getBool("busy")&&dev.textures.size()==existing,
        "cancel did not retire candidate without touching adopted images");
    // Strict GPU mode rejects actual copied bytes that disagree with CPU.
    owner.start(args,"fixture",epoch.lease());owner.tick(dev,"fixture");
    dev.deliverTexture(false);dev.completed=dev.submitted;++dev.submitted;
    owner.tick(dev,"fixture");
    checkGraphRequirement(owner.status().getStr("state")=="failed"&&!owner.status().getBool("gpuTextureSetReady")&&
        dev.textures.size()==existing,"copy parity failure fell back or leaked candidate");
    // A partially created destination remains owned until its copy fence.
    dev.rejectCopy=true;owner.start(args,"fixture",epoch.lease());owner.tick(dev,"fixture");
    checkGraphRequirement(owner.status().getStr("state")=="failed"&&owner.status().getBool("draining")&&
        dev.textures.size()==existing+1,"partial GPU copy failure lost retirement");
    dev.completed=dev.submitted;owner.tick(dev,"fixture");
    checkGraphRequirement(!owner.status().getBool("busy")&&dev.textures.size()==existing,"partial copy did not retire");
    owner.shutdownAfterIdle(dev);registry.clear();render::releaseModel(dev,model);
    checkGraphRequirement(dev.textures.empty()&&dev.buffers.empty()&&dev.pipelines.empty(),"resident fixture leaked resources");
}
int main() {
    checkMaterializedOwner();
    checkResidentTextureOwner();
    {
        using namespace render::texture_graph;
        const auto p=compile(Json::parse(R"({"version":1,"size":8,"nodes":[{"id":"c","op":"color","inputs":[],"properties":{"value":[1,0,0]}},{"id":"s","op":"constant","inputs":[],"properties":{"value":0.5}}],"outputs":{"color":"c","height":"s","ao":"s"}})"));
        std::vector<size_t> targets;for(const auto& pair:p.outputs)targets.push_back(pair.second);
        auto images=evaluateMany(p,targets);
        auto plan=prepareMaterialImages(p,images,"fixture_1");
        checkGraphRequirement(plan.entries.size()==2,"only bindable graph outputs should stage");
        checkGraphRequirement(plan.entries[0].slot==&render::Material::occlusionTex&&plan.entries[1].slot==&render::Material::baseTex,"ordered material slots incorrect");
        checkGraphRequirement(plan.entries[1].image.name=="#texture-graph/fixture_1/color","generation identity missing");
        bool rejected=false;try{(void)prepareMaterialImages(p,images,"../bad");}catch(const std::invalid_argument&){rejected=true;}
        checkGraphRequirement(rejected,"invalid generation accepted");
        images[0].rgba.pop_back();rejected=false;
        try{(void)prepareMaterialImages(p,images,"fixture_2");}catch(const std::invalid_argument&){rejected=true;}
        checkGraphRequirement(rejected,"incomplete texture pixels accepted");
    }
    {
        using namespace render::texture_graph;
        const auto p=compile(Json::parse(R"({"version":1,"size":8,"nodes":[{"id":"h","op":"gradient","inputs":[],"properties":{"axis":"x"}},{"id":"r","op":"multiply","inputs":["h","h"]},{"id":"n","op":"normal","inputs":["h"]},{"id":"a","op":"invert","inputs":["r"]}],"outputs":{"height":"h","normal":"n","ao":"a"}})"));
        const auto h=p.outputs.at("height"),n=p.outputs.at("normal"),a=p.outputs.at("ao");
        auto all=evaluateMany(p,{n,a,h,n});
        checkGraphRequirement(all.size()==4,"lifetime output order or duplicates lost");
        checkGraphRequirement(all[0].rgba==evaluate(p,n).rgba&&all[1].rgba==evaluate(p,a).rgba&&
                all[2].rgba==evaluate(p,h).rgba&&all[3].rgba==all[0].rgba,
                "lifetime multi-output pixels differ from individual outputs");
        checkGraphRequirement(all[2].rgba[0]==16&&all[2].rgba[7*4]==239,"gradient output changed");
    }
    const auto mark = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() / ("aiverse-spectrafx-ops-" + std::to_string(mark));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code e; fs::remove_all(path, e); } } cleanup{root};
    fs::create_directories(root);

    editor::Workspace ws;
    ws.project.dir = root.string();
    const auto call = [&](const char* name, const Json& args) { return ws.call(name, args, "test-spectrafx"); };
    {
        auto args=Json::parse(R"({"asset":{"format":"aiverse.spectrafx.volume","version":1,
            "simulation":{"dimension":8,"origin":[-1,-1,-1],"cellSize":0.25,"pressureIterations":2},
            "sources":[{"position":[0,0,0],"radius":0.75,"smoke":10,"temperature":6,"fuel":1}]},
            "ticks":3,"ray":{"eye":[0,0,-3],"direction":[0,0,1],"distance":8}})");
        args.set("expectedProjectPath",Json::string(ws.project.dir));
        auto validated=call("fx_volume_validate",args);
        checkGraphRequirement(validated.getBool("ok")&&validated["bytes"].toDouble()>0,"native volume asset validation");
        auto probe=call("fx_volume_probe",args);
        checkGraphRequirement(probe.getBool("ok")&&probe.getInt("ticks")==3&&probe["density"].toDouble()>0&&
                probe["ray"]["alpha"].toDouble()>0,"actual CPU volume diagnostic");
        args.set("ticks",Json::number(int64_t(9)));
        auto invalid=call("fx_volume_probe",args);
        checkGraphRequirement(invalid.has("error")||!invalid.getBool("ok"),"probe tick cap");
        checkGraphRequirement(!fs::exists(root/"fx"),"volume probe must not write files");
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

    // In-memory bake is a shared Studio/MCP edit, not a write operation.
    {
        fs::create_directories(root/"content");
        std::ofstream model(root/"content"/"tetra.obj");
        model<<"v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\n"
               "f 1 3 2\nf 1 2 4\nf 1 4 3\nf 2 3 4\n";
        model.close();
        auto request=Json::parse(R"({"action":"bakeSdf","node":"collision","model":"content/tetra.obj","dimension":8,
            "graph":{"version":2,"nodes":[{"id":"spawn","type":"Spawn","properties":{"burst":1}},
            {"id":"init","type":"Initialize","properties":{"life":1,"speed":0,"spread":0}},
            {"id":"collision","type":"CollisionModule","properties":{"ground":false,"bounce":0.7}},
            {"id":"out","type":"SpriteOutput","properties":{"texture":"builtin:soft"}}],
            "links":[{"from":"spawn","to":"init"},{"from":"init","to":"collision"},{"from":"collision","to":"out"}],"outputs":["out"]}})");
        auto baked=call("fx_graph_edit",request);
        checkGraphRequirement(baked.getBool("ok")&&baked["validation"].getBool("ok"),"native model SDF edit");
        const auto& props=baked["graph"]["nodes"][size_t(2)]["properties"];
        checkGraphRequirement(props["staticSdf"]["samples"].size()==512&&props.getBool("ground",true)==false,"SDF and previous properties");
        checkGraphRequirement(!fs::exists(root/"fx"),"SDF edit must not publish files");
        request.set("dimension",Json::number(int64_t(32)));
        auto dense=call("fx_graph_edit",request);
        checkGraphRequirement(dense.getBool("ok")&&dense["validation"].getBool("ok")&&
                dense["graph"]["nodes"][size_t(2)]["properties"]["staticSdf"]["samples"].size()==32768,
                "32 cubed Studio/MCP SDF bake");
        checkGraphRequirement(!fs::exists(root/"fx"),"dense bake stays in local graph");
        fx::StaticSdfVolume volume;
        volume.dimension=3;volume.spacing=1;volume.samples.resize(27,-.25f);
        std::string bytes;
        checkGraphRequirement(fx::encodeSdfBinary(volume,bytes),"encode fixture");
        std::ofstream binary(root/"content"/"test.sdfbin",std::ios::binary);
        binary.write(bytes.data(),std::streamsize(bytes.size()));binary.close();
        auto import=request;
        import.set("action",Json::string("importSdf"));
        import.set("sdfAsset",Json::string("content/test.sdfbin"));
        auto imported=call("fx_graph_edit",import);
        checkGraphRequirement(imported.getBool("ok")&&imported["validation"].getBool("ok")&&
                imported["graph"]["nodes"][size_t(2)]["properties"]["staticSdf"]["samples"].size()==27,
                "binary SDF import through shared Workspace");
        checkGraphRequirement(!fs::exists(root/"fx"),"import does not publish");
        import.set("action",Json::string("linkSdf"));
        auto linked=call("fx_graph_edit",import);
        checkGraphRequirement(linked.getBool("ok")&&linked["validation"].getBool("ok")&&
                linked["graph"]["nodes"][size_t(2)]["properties"].getStr("staticSdf")=="content/test.sdfbin",
                "persistent external SDF link");
        checkGraphRequirement(!fs::exists(root/"fx"),"link does not publish");
        Json save=Json::object();save.set("name",Json::string("linked_sdf"));save.set("graph",linked["graph"]);
        checkGraphRequirement(call("fx_graph_set",save).getBool("ok"),"publish external link");
        Json name=Json::object();name.set("name",Json::string("linked_sdf"));
        auto reopened=call("fx_graph_get",name);
        checkGraphRequirement(reopened["validation"].getBool("ok")&&
                reopened["graph"]["nodes"][size_t(2)]["properties"].getStr("staticSdf")=="content/test.sdfbin",
                "persistent link reopens without inline samples");
        fs::remove_all(root/"fx");
        volume.dimension=64;volume.samples.assign(64u*64u*64u,-.25f);
        checkGraphRequirement(fx::encodeSdfBinary(volume,bytes),"encode external 64 cubed");
        std::ofstream denseBinary(root/"content"/"dense.sdfbin",std::ios::binary);
        denseBinary.write(bytes.data(),std::streamsize(bytes.size()));denseBinary.close();
        import.set("sdfAsset",Json::string("content/dense.sdfbin"));
        auto denseLink=call("fx_graph_edit",import);
        checkGraphRequirement(denseLink.getBool("ok")&&denseLink["validation"].getBool("ok")&&
                denseLink["graph"].dump(-1).size()<4096,"high-resolution link stays compact");
        import.set("action",Json::string("importSdf"));
        auto denseImport=call("fx_graph_edit",import);
        checkGraphRequirement(denseImport.has("error")||!denseImport.getBool("ok"),"high-resolution inline import denied");
        checkGraphRequirement(!fs::exists(root/"fx"),"dense link/import does not publish");
        import.set("action",Json::string("linkSdf"));
        import.set("sdfAsset",Json::string("content/../test.sdfbin"));
        auto badImport=call("fx_graph_edit",import);
        checkGraphRequirement(badImport.has("error")||!badImport.getBool("ok"),"SDF import traversal denied");
        request.set("dimension",Json::number(int64_t(33)));
        auto invalidDimension=call("fx_graph_edit",request);
        checkGraphRequirement(invalidDimension.has("error")||!invalidDimension.getBool("ok"),"oversized bake rejected");
        request.set("dimension",Json::number(int64_t(8)));
        request.set("model",Json::string("content/../escape.obj"));
        auto rejected=call("fx_graph_edit",request);
        checkGraphRequirement(rejected.has("error")||!rejected.getBool("ok"),"SDF traversal rejected");
    }

    {
        auto req=Json::parse(R"({"action":"newVolume","preset":"magic"})");
        auto created=call("fx_graph_edit",req);
        checkGraphRequirement(created.getBool("ok")&&created["validation"].getBool("ok"),"new volume graph through Workspace");
        Json save=Json::object();save.set("name",Json::string("volume_magic"));save.set("graph",created["graph"]);
        checkGraphRequirement(call("fx_graph_set",save).getBool("ok"),"publish volume graph");
        Json name=Json::object();name.set("name",Json::string("volume_magic"));
        auto reopened=call("fx_graph_get",name);
        checkGraphRequirement(reopened["validation"].getBool("ok")&&reopened["graph"]==created["graph"],"reopen volume graph");
        name.set("effect",Json::string("volume_legacy"));
        checkGraphRequirement(!call("fx_graph_bake",name).getBool("ok")&&!fs::exists(root/"fx"/"volume_legacy.json"),"legacy bake rejects volume");
        Json remove=Json::object();remove.set("name",Json::string("volume_magic"));call("fx_graph_delete",remove);
    }
    const auto catalog = call("graph_catalog", Json::object());
    checkGraphRequirement(catalog.getBool("ok") && catalog["domains"].size()==5, "generic graph catalog missing domains");
    checkGraphRequirement(call("graph_list", Json::parse(R"({"domain":"unknown"})")).has("error"), "unknown graph domain accepted");
    Json generic = Json::object();
    generic.set("domain",Json::string("spectrafx"));
    generic.set("name",Json::string("generic_spark"));
    generic.set("graph",graph);
    checkGraphRequirement(call("graph_validate",generic).getBool("ok"),"generic native validation failed");
    checkGraphRequirement(call("graph_dependencies",generic)["result"]["edges"].size()==2,"generic dependency edges incorrect");
    checkGraphRequirement(call("graph_save",generic).has("error"),"unversioned write accepted without explicit consent");
    generic.set("allowUnversionedWrite",Json::fromBool(true));
    checkGraphRequirement(call("graph_save",generic).getBool("ok"),"generic native save failed");
    checkGraphRequirement(call("graph_get",generic)["result"]["graph"]==graph,"generic graph readback mismatch");
    checkGraphRequirement(call("graph_node_catalog",generic)["result"]["nodes"].isArray(),"generic native node catalog missing");
    checkGraphRequirement(call("fx_graph_delete",generic).getBool("ok"),"generic fixture cleanup failed");

    const auto shaderDraft=Json::parse(R"({"domain":"shader","graph":{"version":1,"domain":"material","nodes":[{"id":"input","op":"input"}],"outputs":{"color":"input"}},"edits":[{"action":"add","value":{"id":"tint","op":"constant","value":[1,1,1,1]}},{"action":"setOutput","output":"color","node":"tint"}]})");
    const auto shaderEdited=call("graph_edit",shaderDraft);
    checkGraphRequirement(shaderEdited.getBool("ok")&&shaderEdited["result"].getBool("valid"),"shader batch validation failed");
    checkGraphRequirement(shaderEdited["result"].getInt("applied")==2&&!shaderEdited["result"].getBool("saved"),"shader batch metadata wrong");
    checkGraphRequirement(shaderEdited["result"]["graph"]["outputs"].getStr("color")=="tint","shader output not changed");
    checkGraphRequirement(shaderDraft["graph"]["nodes"].size()==1,"input draft mutated");
    checkGraphRequirement(!fs::exists(root/"shaders"),"shader draft wrote files");
    auto badBatch=shaderDraft;badBatch.member("edits").push(Json::parse(R"({"action":"add","value":{"id":"tint","op":"constant"}})"));
    checkGraphRequirement(call("graph_edit",badBatch).has("error"),"duplicate ID batch accepted");
    const auto motionDraft=Json::parse(R"({"domain":"motionweave","graph":{"nodes":[{"id":"bind","kind":"bind"}],"output":"bind"},"edits":[{"action":"addState","value":{"id":"idle","output":"bind"}},{"action":"setInitial","node":"idle"},{"action":"addTransition","state":"idle","value":{"to":"idle","fade":0.2,"when":[]}}]})");
    const auto motionEdited=call("graph_edit",motionDraft);
    checkGraphRequirement(motionEdited.getBool("ok")&&motionEdited["result"]["graph"]["states"][size_t(0)]["transitions"].size()==1,"animation state draft failed");
    checkGraphRequirement(!motionDraft["graph"].has("states"),"motion edit mutated input");
    auto invalidMotion=motionDraft;invalidMotion.member("graph").set("states",Json::object());
    checkGraphRequirement(call("graph_edit",invalidMotion).has("error"),"non-array state collection accepted");
    checkGraphRequirement(!motionEdited["result"].getBool("valid"),"model-less motion graph unexpectedly validated");
    checkGraphRequirement(!fs::exists(root/"animations"),"animation draft wrote files");
    const auto modelDraft=Json::parse(R"({"domain":"modeling","document":{"version":1,"parameters":{},"materials":[],"nodes":[{"id":"root"}],"clips":[],"skins":[],"graph":null},"edits":[{"op":"put","collection":"nodes","id":"body","value":{"parent":"root"}},{"op":"parameters","values":{"size":1}}]})");
    const auto modelEdited=call("graph_edit",modelDraft);
    checkGraphRequirement(modelEdited.getBool("ok")&&modelEdited["result"]["document"]["nodes"].size()==2,"native model draft batch failed");
    checkGraphRequirement(!fs::exists(root/"models"),"model draft wrote files");
    auto fxBatch=generic;fxBatch.set("edits",Json::parse(R"([{"action":"setProperty","node":"spawn","property":"burst","value":12},{"action":"setProperty","node":"init","property":"life","value":2}])"));
    const auto fxEdited=call("graph_edit",fxBatch);
    checkGraphRequirement(fxEdited.getBool("ok")&&fxEdited["result"].getBool("valid")&&fxEdited["result"].getInt("applied")==2,"native FX batch failed");
    checkGraphRequirement(!fs::exists(root/"fx"/"graphs"/"generic_spark.vfx.json"),"FX draft wrote files");

    auto typed=shaderDraft;typed.erase("edits");typed.set("node",Json::string("input"));
    checkGraphRequirement(call("graph_property_schema",typed)["result"]["fields"].has("position"),"property schema missing position");
    typed.set("edit",Json::parse(R"({"action":"setProperty","node":"input","property":"position","value":[10,20]})"));
    checkGraphRequirement(call("graph_edit",typed)["result"]["graph"]["nodes"][size_t(0)]["position"].size()==2,"typed move failed");
    typed.member("edit").set("property",Json::string("id"));
    checkGraphRequirement(call("graph_edit",typed).has("error"),"typed edit changed immutable ID");
    auto dependencies=Json::parse(R"({"domain":"motionweave","graph":{"nodes":[{"id":"sub","kind":"subgraph","graph":"a"}],"graphs":{"a":{"nodes":[{"id":"loop","kind":"subgraph","graph":"a"}]}}}})");
    const auto resolved=call("graph_asset_dependencies",dependencies);
    checkGraphRequirement(resolved.getBool("ok")&&resolved["result"]["issues"].size()==1,"embedded recursion not detected");
    auto textureRef=Json::parse(R"({"domain":"shader","graph":{"nodes":[{"id":"tex","op":"sampleTexture","texture":"../outside.png"}]}})");
    checkGraphRequirement(call("graph_asset_dependencies",textureRef)["result"]["references"][size_t(0)].getStr("status")=="invalid-path","unsafe path not flagged");

    const auto live=call("graph_live_snapshot",Json::parse(R"({"domain":"spectrafx"})"));
    checkGraphRequirement(live.has("available")&&!live.getBool("available"),"headless host must not fabricate live owner");
    const auto caps=call("graph_runtime_capabilities",Json::object());
    checkGraphRequirement(!caps.getBool("nativeEditorSnapshotOwner"),"headless host claims GUI owner");
    checkGraphRequirement(caps.getBool("ok")&&!caps.getBool("liveAttached"),"runtime capability misreported");
    Json probe=Json::object();probe.set("domain",Json::string("spectrafx"));probe.set("graph",graph);probe.set("steps",Json::number(int64_t(6)));
    const auto sampled=call("graph_runtime_probe",probe);
    checkGraphRequirement(sampled.getBool("ok")&&sampled["frames"].size()==6,"headless FX runtime probe failed");
    checkGraphRequirement(!sampled.getBool("liveAttached")&&!sampled.getBool("gpuMeasured"),"probe mislabeled");
    checkGraphRequirement(sampled["frames"][size_t(5)].getInt("particles")>0,"expected native particles");
    probe.set("steps",Json::number(int64_t(61)));
    checkGraphRequirement(call("graph_runtime_probe",probe).has("error"),"unbounded probe accepted");

    graph_telemetry::History history;
    const graph_telemetry::Identity track{"world-1","42","revision-1","spectrafx"};
    for(uint64_t i=1;i<=121;++i)checkGraphRequirement(history.publish(track,i,Json::object()),"history publish failed");
    checkGraphRequirement(history.read(track).size()==120&&history.read(track)[size_t(0)].getStr("sequence")=="2","history bound failed");
    checkGraphRequirement(!history.publish(track,121,Json::object()),"duplicate sequence accepted");
    checkGraphRequirement(history.read({"world-2","42","revision-1","spectrafx"}).size()==0,"world identity crossed");
    history.clearWorld("world-1");checkGraphRequirement(history.read(track).size()==0,"world history not cleared");
    Json debug=Json::object();debug.set("action",Json::string("start"));debug.set("domain",Json::string("spectrafx"));debug.set("graph",graph);
    const auto begun=call("graph_debug_session",debug);
    checkGraphRequirement(begun.getBool("active")&&!begun.getStr("session").empty(),"FX isolated session not started");
    Json step=Json::object();step.set("action",Json::string("step"));step.set("session",begun["session"]);
    const auto stepped=call("graph_debug_session",step);
    checkGraphRequirement(stepped.getInt("step")==1&&stepped["frames"].size()==1,"debug step failed");
    step.set("action",Json::string("continue"));step.set("steps",Json::number(int64_t(3)));
    checkGraphRequirement(call("graph_debug_session",step).getInt("step")==4,"debug continuation did not retain state");
    step.set("action",Json::string("stop"));checkGraphRequirement(!call("graph_debug_session",step).getBool("active"),"session stop failed");
    step.set("action",Json::string("step"));checkGraphRequirement(call("graph_debug_session",step).has("error"),"stopped session accepted");

    graph_telemetry::Watch watch;
    auto watchSample=Json::parse(R"({"available":true,"domain":"spectrafx","worldEpoch":"1","instanceSerial":"5","programIdentity":"instance:5","sequence":"1","particles":3})");
    watch.start("spectrafx","preview",watchSample);
    checkGraphRequirement(watch.running(),"watch not started");
    checkGraphRequirement(!watch.capture(watchSample),"duplicate sample accepted");
    watchSample.set("sequence",Json::string("2"));
    checkGraphRequirement(watch.capture(watchSample)&&watch.status()["frames"].size()==2,"watch capture failed");
    watchSample.set("instanceSerial",Json::string("6"));
    checkGraphRequirement(!watch.capture(watchSample)&&!watch.running(),"watch followed replacement");
    checkGraphRequirement(watch.status()["frames"].size()==2,"watch lost stopped history");
    watch.clear();checkGraphRequirement(watch.status()["frames"].size()==0,"watch clear failed");
    checkGraphRequirement(!call("graph_live_watch",Json::parse(R"({"action":"status"})")).getBool("available"),"headless watch claims owner");

    checkGraphRequirement(!call("graph_live_profile",Json::parse(R"({"action":"read","entity":"42"})")).getBool("available"),"headless profile claims live owner");

    struct FakeScopeDevice {
        uint64_t frameSerial() const{return 11;}
        bool beginGpuScope(uint64_t){return allow;}
        bool allow=true;
    } fakeScope;
    graph_gpu_profile::Capture gpuProfile;
    gfx::DeviceInfo fakeInfo;fakeInfo.backend=gfx::Backend::Metal;fakeInfo.name="test metadata only";
    gpuProfile.start(fakeInfo,"fixture");
    const auto scopeTag=gpuProfile.begin(fakeScope,"viewport",2,true);
    checkGraphRequirement(scopeTag!=0&&!gpuProfile.report().getBool("available"),"pending GPU query treated as complete");
    checkGraphRequirement(gpuProfile.complete({scopeTag,2.5,true},12)==2,"adaptive scope result routing failed");
    checkGraphRequirement(gpuProfile.report()["samples"].size()==1,"completed GPU sample missing");
    const auto invalidTag=gpuProfile.begin(fakeScope,"fx",1,false);
    gpuProfile.complete({invalidTag,0,false},13);
    checkGraphRequirement(gpuProfile.report()["samples"].size()==1,"invalid GPU query recorded");
    const auto cancelled=gpuProfile.begin(fakeScope,"fx",1,false);
    gpuProfile.stop();gpuProfile.start(fakeInfo,"fixture");
    gpuProfile.complete({cancelled,1.5,true},14);
    checkGraphRequirement(!gpuProfile.report().getBool("available"),"cancelled query crossed capture epoch");
    fakeScope.allow=false;
    checkGraphRequirement(gpuProfile.begin(fakeScope,"fx",1,false)==0,"unsupported timing accepted");
    checkGraphRequirement(!call("graph_gpu_profile",Json::parse(R"({"action":"read"})")).getBool("available"),"headless GPU owner fabricated");

    auto tex=Json::parse(R"({"domain":"texture","name":"fixture","graph":{"version":1,"size":8,"nodes":[{"id":"rough","op":"constant","inputs":[],"properties":{"value":0.5}},{"id":"metal","op":"constant","inputs":[],"properties":{"value":1}},{"id":"packed","op":"metallicRoughness","inputs":["rough","metal"]},{"id":"normal","op":"normal","inputs":["rough"]}],"outputs":{"metallicRoughness":"packed","normal":"normal"}},"output":"metallicRoughness","textureName":"fixture"})");
    checkGraphRequirement(call("graph_validate",tex).getBool("ok"),"native texture DAG validation failed");
    auto pixels=call("texture_graph_preview",tex);
    checkGraphRequirement(pixels.getBool("ok")&&pixels.getStr("rgbaHex").size()==8*8*8&&pixels.getStr("rgbaHex").substr(0,8)=="ff80ffff","packed PBR channel layout wrong");
    tex.set("output",Json::string("normal"));
    checkGraphRequirement(call("texture_graph_preview",tex).getStr("rgbaHex").substr(0,8)=="8080ffff","flat height normal wrong");
    checkGraphRequirement(call("graph_save",tex).getBool("ok"),"new texture graph save failed");
    checkGraphRequirement(call("graph_save",tex)["result"].has("error")||!call("graph_save",tex).getBool("ok"),"texture graph overwrite without expected accepted");
    tex.set("expected",tex["graph"]);
    checkGraphRequirement(call("graph_save",tex).getBool("ok"),"expected texture document save failed");
    checkGraphRequirement(call("texture_graph_bake",tex).getBool("ok"),"texture graph PNG bake failed");
    checkGraphRequirement(fs::is_regular_file(root/"content"/"textures"/"fixture_normal.png"),"texture bake missing file");
    checkGraphRequirement(call("texture_graph_bake",tex).has("error"),"texture bake overwrite accepted");
    auto brokenTexture=tex;
    brokenTexture.member("graph").member("nodes").at(2).member("inputs").at(0)=Json::string("normal");
    checkGraphRequirement(!call("graph_validate",brokenTexture).getBool("ok"),"texture type mismatch accepted");

    const auto textureCatalog=call("texture_graph_catalog",Json::object());
    checkGraphRequirement(textureCatalog.getBool("ok")&&textureCatalog["outputTypes"].getStr("normal")=="normal","texture output catalog missing types");
    const auto textureTemplates=call("texture_graph_templates",Json::object());
    checkGraphRequirement(textureTemplates.getBool("ok")&&textureTemplates["templates"].size()==6,"texture starter library missing");
    for(const auto& row:textureTemplates["templates"].items()){
        Json args=Json::object();args.set("graph",row["graph"]);
        checkGraphRequirement(call("texture_graph_validate",args).getBool("ok"),"invalid texture template");
    }
    tex.set("textureName",Json::string("fixture_set"));
    const auto bakedSet=call("texture_graph_bake_set",tex);
    checkGraphRequirement(bakedSet.getBool("ok")&&bakedSet["files"].size()==2,"PBR set bake failed");
    for(const auto& row:bakedSet["files"].items()){
        checkGraphRequirement(!row.getBool("sRGB"),"numeric texture marked sRGB");
        checkGraphRequirement(row.getStr("backend")=="cpu","CPU set provenance mislabeled");
        checkGraphRequirement(fs::is_regular_file(root/row.getStr("path")),"PBR set file missing");
    }
    checkGraphRequirement(call("texture_graph_bake_set",tex).has("error"),"PBR set overwrite accepted");
    checkGraphRequirement(!fs::exists(root/"content"/"textures"/".fixture_set.stage"),"PBR staging leaked");

    auto instance=tex;
    instance.member("graph").set("parameters",Json::parse(R"({"roughness":{"default":0.5,"bindings":[{"node":"rough","property":"value"}]}})"));
    instance.set("output",Json::string("metallicRoughness"));
    instance.set("overrides",Json::parse(R"({"roughness":0.25})"));
    const auto variantPixels=call("texture_graph_preview",instance);
    checkGraphRequirement(variantPixels.getStr("rgbaHex").substr(0,8)=="ff40ffff","parameter override not evaluated");
    checkGraphRequirement(call("texture_graph_preview",instance).getBool("cacheHit"),"identical resolved preview cache miss");
    instance.set("overrides",Json::parse(R"({"roughness":0.75})"));
    checkGraphRequirement(call("texture_graph_preview",instance).getStr("rgbaHex").substr(0,8)=="ffbfffff","parameter cache returned stale pixels");
    instance.set("overrides",Json::parse(R"({"roughness":2})"));
    checkGraphRequirement(call("texture_graph_preview",instance).has("error"),"invalid override accepted");
    checkGraphRequirement(fs::is_regular_file(root/"content"/"textures"/"fixture_set"/"texture-set.json"),"bake provenance missing");
    checkGraphRequirement(call("texture_graph_cache",Json::parse(R"({"clear":true})")).getInt("entries")==0,"cache clear failed");

    const auto gpuTexture=render::texture_graph::compile(tex["graph"]);
    const auto gpuPacked=render::texture_graph::gpu::compile(gpuTexture,gpuTexture.outputs.at("metallicRoughness"),8);
    checkGraphRequirement(!gpuPacked.msl.empty()&&gpuPacked.msl.find("cs_main")!=std::string::npos,"GPU texture source missing");
    const auto gpuNormal=render::texture_graph::gpu::compile(gpuTexture,gpuTexture.outputs.at("normal"),8);
    checkGraphRequirement(!gpuNormal.msl.empty()&&gpuNormal.msl.find("evaluatePoint")!=std::string::npos,"GPU normal source missing");
    checkGraphRequirement(gpuNormal.glsl.find("binding=12")!=std::string::npos&&gpuNormal.glsl.find("gl_GlobalInvocationID")!=std::string::npos,"Vulkan storage ABI missing");
    auto unsupportedTexture=render::texture_graph::compile(Json::parse(R"({"version":1,"size":128,"nodes":[{"id":"p","op":"pattern","inputs":[],"properties":{"pattern":"brick"}}],"outputs":{"color":"p"}})"));
    checkGraphRequirement(!render::texture_graph::gpu::compile(unsupportedTexture,0,128).msl.empty(),"GPU brick pattern source missing");
    for(const auto& pattern:render::texturePatterns()){
        auto doc=Json::parse(R"({"version":1,"size":128,"nodes":[{"id":"p","op":"pattern","inputs":[],"properties":{}},{"id":"mask","op":"patternMask","inputs":[],"properties":{}},{"id":"normal","op":"normal","inputs":["mask"]}],"outputs":{"color":"p","normal":"normal"}})");
        doc.member("nodes").at(0).member("properties").set("pattern",Json::string(pattern));
        doc.member("nodes").at(1).member("properties").set("pattern",Json::string(pattern));
        const auto program=render::texture_graph::compile(doc);
        const auto colorPlan=render::texture_graph::gpu::plan(program,0,128);
        checkGraphRequirement(!colorPlan.first.msl.empty()&&!colorPlan.first.glsl.empty()&&!colorPlan.twoPass,"pattern plan source missing");
        const auto normalPlan=render::texture_graph::gpu::plan(program,2,128);
        checkGraphRequirement(normalPlan.twoPass&&normalPlan.scratchBytes==65536,"normal scratch plan wrong");
        checkGraphRequirement(normalPlan.first.glsl.find("floatBitsToUint")!=std::string::npos&&normalPlan.second.glsl.find("binding=13")!=std::string::npos,"normal plan ABI missing");
    }
    const auto sharedTexture=render::texture_graph::compile(Json::parse(R"({"version":1,"size":128,"nodes":[{"id":"h","op":"gradient","inputs":[]},{"id":"n","op":"normal","inputs":["h"]},{"id":"r","op":"constant","inputs":[]}],"outputs":{"height":"h","normal":"n","roughness":"r"}})"));
    const auto materializedSources=render::texture_graph::gpu::compileMaterializedDag(sharedTexture,{0,1,2},128);
    checkGraphRequirement(materializedSources.nodes.size()==3&&materializedSources.packs.size()==3,"materialized sources missing");
    checkGraphRequirement(materializedSources.nodes[0].msl.find("float* result [[buffer(0)]]")!=std::string::npos,"scalar producer ABI incorrect");
    checkGraphRequirement(materializedSources.nodes[1].glsl.find("input0[y*n+(x+1u)%n]")!=std::string::npos&&materializedSources.nodes[1].glsl.find("vec4 result[]")!=std::string::npos,"normal materialized shader missing");
    checkGraphRequirement(materializedSources.totalBytes==materializedSources.plan.allocatedBytes+128u*128u*4u,"materialized pack buffer budget missing");
    const auto blendTexture=render::texture_graph::compile(Json::parse(R"({"version":1,"size":8,"nodes":[{"id":"a","op":"color","inputs":[],"properties":{"value":[0,0,0]}},{"id":"b","op":"color","inputs":[],"properties":{"value":[1,1,1]}},{"id":"t","op":"constant","inputs":[]},{"id":"mix","op":"blendColor","inputs":["a","b","t"]}],"outputs":{"color":"mix"}})"));
    const auto multiSource=render::texture_graph::gpu::compileMaterializedDag(blendTexture,{3},8);
    checkGraphRequirement(multiSource.nodes[3].msl.find("float* input2 [[buffer(3)]]")!=std::string::npos&&multiSource.nodes[3].glsl.find("mix(v0,v1,v2.x)")!=std::string::npos,"multi-input expression or binding missing");
    checkGraphRequirement(multiSource.packs[0].glsl.find("0xff000000u")!=std::string::npos,"RGBA8 pack missing");
    const auto fusedSource=render::texture_graph::gpu::compileMaterializedDag(blendTexture,{3},8,render::texture_graph::gpu::materializedBudgetBytes,true);
    checkGraphRequirement(fusedSource.reachableNodes==4&&fusedSource.fusedNodes==3&&fusedSource.plan.schedule.passes.size()==1,"pointwise region did not fuse");
    checkGraphRequirement(fusedSource.nodes[3].glsl.find("mix(fusedNode0,fusedNode1,fusedNode2.x)")!=std::string::npos,"fused shared expression missing");
    checkGraphRequirement(fusedSource.nodes[3].msl.find("buffer(1)")==std::string::npos&&fusedSource.totalBytes<multiSource.totalBytes,"fused leaf region retained intermediate buffers");
    const auto pinnedFusion=render::texture_graph::gpu::compileMaterializedDag(sharedTexture,{0,1,2},128,render::texture_graph::gpu::materializedBudgetBytes,true);
    checkGraphRequirement(pinnedFusion.fusedNodes==0&&pinnedFusion.plan.schedule.passes.size()==3,"fusion crossed pinned output or normal boundary");
    const auto sharedHeight=render::texture_graph::gpu::plan(sharedTexture,0,128,true);
    const auto sharedNormal=render::texture_graph::gpu::plan(sharedTexture,1,128,true);
    checkGraphRequirement(sharedHeight.twoPass&&sharedNormal.twoPass&&sharedHeight.scratchBytes==sharedNormal.scratchBytes,"shared scalar plan missing");
    checkGraphRequirement(sharedHeight.first.msl==sharedNormal.first.msl&&sharedHeight.first.glsl==sharedNormal.first.glsl,"scalar producer differs");
    checkGraphRequirement(sharedHeight.second.msl.find("height[tid]")!=std::string::npos&&sharedHeight.second.glsl.find("binding=13")!=std::string::npos,"scalar packing ABI missing");
    checkGraphRequirement(!render::texture_graph::gpu::plan(sharedTexture,0,128).twoPass&&!render::texture_graph::gpu::plan(sharedTexture,2,128,true).twoPass,"independent scalar should stay fused");
    Json planArgs=Json::object();planArgs.set("action",Json::string("plan"));planArgs.set("graph",tex["graph"]);planArgs.set("bakeSet",Json::fromBool(true));planArgs.set("resolution",Json::number(int64_t(8)));
    const auto materialized=call("texture_graph_gpu_preview",planArgs);
    checkGraphRequirement(materialized.getBool("ok")&&materialized.getStr("state")=="planned"&&!materialized.getBool("executed"),"headless materialized plan missing or fabricated execution");
    checkGraphRequirement(materialized["outputs"].size()==2&&materialized["passes"].size()>0&&materialized["slotBytes"].size()>0,"materialized schedule missing slots or outputs");
    checkGraphRequirement(materialized.getInt("packShaderCount")==2&&materialized.getInt("resultBytes")==8*8*4,"materialized source metadata missing");
    const auto headlessGpuTexture=call("texture_graph_gpu_preview",Json::parse(R"({"action":"read"})"));
    checkGraphRequirement(headlessGpuTexture.getStr("state")=="unavailable","headless texture GPU preview fabricated");
    checkGraphRequirement(!call("texture_graph_gpu_preview",Json::parse(R"({"action":"cancel","request":"1"})")).getBool("ok"),"headless GPU cancellation fabricated");
    checkGraphRequirement(call("texture_graph_gpu_preview",Json::parse(R"({"action":"materialTarget","typeIndex":0,"choiceIndex":0,"materialIndex":0})")).getStr("state")=="unavailable","headless live material target fabricated");
    checkGraphRequirement(!call("texture_graph_gpu_preview",Json::parse(R"({"action":"applyMaterial","request":"1","typeIndex":0,"choiceIndex":0,"materialIndex":0,"expectedVersion":"1","expectedModelPublication":"1"})")).getBool("ok"),"headless live material adoption fabricated");

    auto fullTexture=render::texture_graph::compile(Json::parse(R"({"version":1,"size":1024,"nodes":[{"id":"height","op":"constant","inputs":[],"properties":{"value":0.5}},{"id":"normal","op":"normal","inputs":["height"]}],"outputs":{"normal":"normal"}})"));
    const auto fullPlan=render::texture_graph::gpu::plan(fullTexture,1,1024);
    checkGraphRequirement(fullPlan.twoPass&&fullPlan.scratchBytes==4*1024*1024,"full-size GPU height allocation plan incorrect");
    checkGraphRequirement(fullPlan.first.msl.find("1024u")!=std::string::npos&&fullPlan.second.glsl.find("1024u")!=std::string::npos,"full-size GPU shader resolution mismatch");
    checkGraphRequirement(fullPlan.scratchBytes/render::texture_graph::gpu::readbackBlockBytes==64,"full-size readback chunk count wrong");
    checkGraphRequirement(!call("texture_graph_gpu_preview",Json::parse(R"({"action":"publish","request":"1","textureName":"forbidden_headless"})")).getBool("ok"),"headless GPU publication accepted");

    auto setStart=Json::object();setStart.set("action",Json::string("start"));setStart.set("graph",tex["graph"]);setStart.set("bakeSet",Json::fromBool(true));
    checkGraphRequirement(call("texture_graph_gpu_preview",setStart).getStr("state")=="unavailable","headless GPU set owner fabricated");
    const auto manifest=Json::parseFile((root/"content"/"textures"/"fixture_set"/"texture-set.json").string());
    checkGraphRequirement(manifest["files"].size()==2&&manifest["files"][size_t(0)].getStr("backend")=="cpu","shared texture set provenance missing");

    Json create = Json::object();
    create.set("name", Json::string("spark"));
    create.set("graph", graph);
    auto created = call("fx_graph_set", create);
    checkGraphRequirement(created.getBool("ok"), "valid graph was rejected");
    checkGraphRequirement(fs::exists(root/"fx"/"graphs"/"spark.vfx.json"), "graph file not created");
    checkGraphRequirement(!ws.changes.empty() && ws.changes.back().kind == editor::Change::Fx, "missing editor FX invalidation");

    Json name = Json::object();
    name.set("name", Json::string("spark"));
    auto got = call("fx_graph_get", name);
    checkGraphRequirement(got["graph"].isObject() && got["validation"].getBool("ok"), "graph_get failed");
    checkGraphRequirement(got["validation"].has("metalGpuEligible") &&
            !got["validation"].getBool("metalGpuEligible"),
            "smoke sprite should expose CPU fallback diagnostics");
    checkGraphRequirement(!got["validation"].getStr("metalGpuReason").empty(),
            "CPU fallback reason was not reported");
    auto all = call("fx_graph_list", Json::object());
    checkGraphRequirement(all["graphs"].isArray() && all["graphs"].size()==1, "graph list did not find asset");
    checkGraphRequirement(all["graphs"][size_t(0)].getBool("ok"), "list reported graph compiler failure");

    Json validation = Json::object();
    validation.set("name", Json::string("spark"));
    validation.set("overrides", Json::parse(R"({"size":1.25})"));
    const auto checked = call("fx_graph_validate", validation);
    checkGraphRequirement(checked.getBool("ok") && checked.getInt("emitters")==1, "graph variant validation failed");

    // MCP shares the normal Workspace graph registry. The live preview
    // operation is discoverable even without a GPU; standalone MCP must
    // reject it explicitly instead of generating a synthetic frame.
    const Json tools=ws.toolList();
    bool discoveredPreview=false;
    for(const auto& t:tools.items())
        if(t.getStr("name")=="fx_graph_preview")discoveredPreview=true;
    checkGraphRequirement(discoveredPreview,"FX preview absent from MCP tools/list");
    Json previewStatus=Json::object();
    previewStatus.set("action",Json::string("status"));
    checkGraphRequirement(call("fx_graph_preview",previewStatus).has("error"),
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
    checkGraphRequirement(linked.getBool("ready")&&previewCalls==1,
            "MCP graph load did not reach native preview owner");
    checkGraphRequirement(seen["graph"].isObject()&&seen["graph"].getInt("version")==1,
            "MCP graph file not resolved by Workspace");
    Json previewStep=Json::object();
    previewStep.set("action",Json::string("step"));
    checkGraphRequirement(call("fx_graph_preview",previewStep).getBool("ready")&&
            seen.getStr("action")=="step","MCP step not forwarded");
    const int forwarded=previewCalls;
    Json unsafe=previewLoad;
    unsafe.set("graphName",Json::string("../escape"));
    checkGraphRequirement(call("fx_graph_preview",unsafe).has("error")&&
            previewCalls==forwarded,"unsafe graph name reached native preview");
    Json ambiguous=previewLoad;
    ambiguous.set("graph",graph);
    checkGraphRequirement(call("fx_graph_preview",ambiguous).has("error")&&
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
        checkGraphRequirement(call("fx_graph_preview",escaped).has("error")&&
                previewCalls==forwarded,"symlink outside project reached GPU runtime");
    }
    std::error_code removeError;
    fs::remove(outside,removeError);
    ws.setSpectraPreviewHandler({});
    checkGraphRequirement(call("fx_graph_preview",previewStatus).has("error"),
            "detached Studio preview must not be callable from standalone MCP");

    Json preset=Json::parse(R"JSON({"format":"aiverse.spectrafx.replay","version":1,"tick":25,"speed":1,"loop":false,"grid":true,"axes":true,"background":"dark","camera":{"yaw":0,"pitch":0,"distance":6,"targetX":0,"targetY":1,"targetZ":0,"ortho":false},"parameters":{}})JSON");
    preset.set("projectPath",Json::string(root.string()));
    Json v2=graph;v2.set("version",Json::number(int64_t(2)));
    preset.set("graphSignature",Json::string(v2.dump(-1)));
    Json savePreset=Json::object();savePreset.set("name",Json::string("first"));savePreset.set("preset",preset);
    checkGraphRequirement(!call("fx_replay_preset_save",savePreset).has("error"),"replay asset save failed");
    checkGraphRequirement(fs::exists(root/"content"/"spectrafx"/"replays"/"first.spectra-replay.json"),"missing replay content asset");
    checkGraphRequirement(call("fx_replay_preset_save",savePreset).has("error"),"replay history overwritten");
    checkGraphRequirement(call("fx_replay_preset_list",Json::object())["presets"].size()==1,"replay listing failed");
    Json getPreset=Json::object();getPreset.set("name",Json::string("first"));
    checkGraphRequirement(call("fx_replay_preset_get",getPreset)["preset"].getInt("tick")==25,"replay read failed");
    Json checkPreset=Json::object();checkPreset.set("preset",call("fx_replay_preset_get",getPreset)["preset"]);
    checkGraphRequirement(call("fx_replay_preset_check",checkPreset).getStr("state")=="matching","empty direct manifest mismatch");
    const auto texture=root/"content"/"spark.png";
    {std::ofstream f(texture,std::ios::binary);f<<"abc";}
    {std::ofstream f(texture.string()+".meta");f<<R"({"guid":"0123456789abcdef0123456789abcdef"})";}
    Json dependencyGraph=v2;dependencyGraph.set("testAsset",Json::string("content/spark.png"));
    Json dependencyPreset=preset;dependencyPreset.set("graphSignature",Json::string(dependencyGraph.dump(-1)));
    Json dependencySave=Json::object();dependencySave.set("name",Json::string("with_asset"));dependencySave.set("preset",dependencyPreset);
    checkGraphRequirement(!call("fx_replay_preset_save",dependencySave).has("error"),"dependency preset save failed");
    Json dependencyGet=Json::object();dependencyGet.set("name",Json::string("with_asset"));
    const auto baseline=call("fx_replay_preset_get",dependencyGet);
    checkGraphRequirement(baseline["compatibility"].getStr("state")=="matching","resource baseline mismatch");
    checkGraphRequirement(baseline["preset"]["dependencies"][size_t(0)].getStr("guid")=="0123456789abcdef0123456789abcdef","GUID not recorded");
    {std::ofstream f(texture,std::ios::binary|std::ios::trunc);f<<"xyz";}
    checkGraphRequirement(call("fx_replay_preset_get",dependencyGet)["compatibility"].getStr("state")=="changed","same-size edit not detected");
    fs::remove(texture);
    checkGraphRequirement(call("fx_replay_preset_get",dependencyGet)["compatibility"].getStr("state")=="unavailable","missing asset not detected");
    // A glTF references a material-like JSON asset through an explicit content
    // path and external buffers through local URIs; cycles terminate.
    const auto modelFile=root/"content"/"ship.gltf";
    const auto material=root/"content"/"ship.material.json";
    const auto buffer=root/"content"/"ship.bin";
    {std::ofstream f(modelFile);f<<R"({"asset":{"version":"2.0"},"buffers":[{"uri":"ship.bin"}],"extras":{"material":"content/ship.material.json"}})";}
    {std::ofstream f(material);f<<R"({"model":"content/ship.gltf"})";}
    {std::ofstream f(buffer,std::ios::binary);f<<"123";}
    Json nestedGraph=v2;nestedGraph.set("testAsset",Json::string("content/ship.gltf"));
    Json nestedPreset=preset;nestedPreset.set("graphSignature",Json::string(nestedGraph.dump(-1)));
    Json nestedSave=Json::object();nestedSave.set("name",Json::string("nested"));nestedSave.set("preset",nestedPreset);
    checkGraphRequirement(!call("fx_replay_preset_save",nestedSave).has("error"),"nested dependency save failed");
    Json nestedGet=Json::object();nestedGet.set("name",Json::string("nested"));
    const auto nested=call("fx_replay_preset_get",nestedGet);
    checkGraphRequirement(nested["preset"].getInt("dependencyVersion")==3&&nested["preset"]["dependencies"].size()==3,
            "nested manifest did not resolve unique cycle-safe dependencies");
    checkGraphRequirement(nested["compatibility"].getStr("state")=="matching","nested baseline mismatch");
    {std::ofstream f(buffer,std::ios::binary|std::ios::trunc);f<<"456";}
    checkGraphRequirement(call("fx_replay_preset_get",nestedGet)["compatibility"].getStr("state")=="changed","external glTF buffer change missed");
    {std::ofstream f(modelFile,std::ios::trunc);f<<R"({"buffers":[{"uri":"../../outside.bin"}]})";}
    checkGraphRequirement(call("fx_replay_preset_get",nestedGet)["compatibility"].getStr("state")=="unavailable","escaping glTF URI accepted");
    // GLB JSON chunk is read without loading the binary body.
    const auto glb=root/"content"/"ship.glb";
    std::string glbJson=R"({"asset":{"version":"2.0"},"images":[{"uri":"ship.bin"}]})";
    while(glbJson.size()%4)glbJson+=' ';
    {std::ofstream f(glb,std::ios::binary);
     auto word=[&](uint32_t v){for(unsigned shift=0;shift<32;shift+=8)f.put(char((v>>shift)&255));};
     word(0x46546c67u);word(2);word(uint32_t(20+glbJson.size()));
     word(uint32_t(glbJson.size()));word(0x4e4f534au);f<<glbJson;}
    {std::ofstream f(root/"content"/"ship.obj");f<<"mtllib ship.mtl\n";}
    {std::ofstream f(root/"content"/"ship.mtl");f<<"map_Kd ship.bin\n";}
    Json formatGraph=v2;
    formatGraph.set("testAsset",Json::parse(R"(["content/ship.glb","content/ship.obj"])"));
    Json formatPreset=preset;formatPreset.set("graphSignature",Json::string(formatGraph.dump(-1)));
    Json formatSave=Json::object();formatSave.set("name",Json::string("formats"));formatSave.set("preset",formatPreset);
    checkGraphRequirement(!call("fx_replay_preset_save",formatSave).has("error"),"GLB/OBJ manifest save failed");
    Json formatGet=Json::object();formatGet.set("name",Json::string("formats"));
    const auto formats=call("fx_replay_preset_get",formatGet);
    checkGraphRequirement(formats["compatibility"].getStr("state")=="matching"&&formats["preset"]["dependencies"].size()==4,
            "GLB/OBJ/MTL dependencies incomplete");
    {std::ofstream f(root/"content"/"ship.mtl");f<<"map_Kd -s 1 1 1 ship.bin\n";}
    checkGraphRequirement(call("fx_replay_preset_get",formatGet)["compatibility"].getStr("state")=="unavailable",
            "unsupported MTL options silently accepted");
    const auto movedFrom=root/"content"/"relocate.png";
    const auto movedTo=root/"content"/"relocated.png";
    const std::string moveMeta=R"({"guid":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})";
    {std::ofstream f(movedFrom,std::ios::binary);f<<"pixels";}
    {std::ofstream f(movedFrom.string()+".meta");f<<moveMeta;}
    Json movedGraph=v2;movedGraph.set("testAsset",Json::string("content/relocate.png"));
    Json movedPreset=preset;movedPreset.set("graphSignature",Json::string(movedGraph.dump(-1)));
    Json movedSave=Json::object();movedSave.set("name",Json::string("relocation"));movedSave.set("preset",movedPreset);
    checkGraphRequirement(!call("fx_replay_preset_save",movedSave).has("error"),"relocation baseline save failed");
    fs::rename(movedFrom,movedTo);fs::rename(movedFrom.string()+".meta",movedTo.string()+".meta");
    Json movedGet=Json::object();movedGet.set("name",Json::string("relocation"));
    const auto plan=call("fx_replay_preset_relocate_preview",movedGet);
    checkGraphRequirement(plan.getStr("state")=="ready"&&plan["changes"][size_t(0)].getStr("to")=="content/relocated.png",
            "GUID relocation plan failed");
    checkGraphRequirement(call("fx_replay_preset_get",movedGet)["preset"].getStr("graphSignature")==movedGraph.dump(-1),
            "read-only relocation changed stored preset");
    Json movedCheck=Json::object();movedCheck.set("preset",plan["candidate"]);
    checkGraphRequirement(call("fx_replay_preset_check",movedCheck).getStr("state")=="matching","candidate dependency closure mismatch");
    Json applyPlan=movedGet;
    applyPlan.set("newName",Json::string("relocation_v2"));
    applyPlan.set("planRevision",Json::string("stale"));
    checkGraphRequirement(call("fx_replay_preset_relocate_apply",applyPlan).has("error"),"stale plan applied");
    applyPlan.set("planRevision",plan["planRevision"]);
    checkGraphRequirement(!call("fx_replay_preset_relocate_apply",applyPlan).has("error"),"relocated preset publication failed");
    Json newGet=Json::object();newGet.set("name",Json::string("relocation_v2"));
    checkGraphRequirement(call("fx_replay_preset_get",newGet)["compatibility"].getStr("state")=="matching","published relocation mismatch");
    checkGraphRequirement(call("fx_replay_preset_relocate_apply",applyPlan).has("error"),"relocated preset overwritten");
    checkGraphRequirement(call("fx_replay_preset_get",movedGet)["preset"].getStr("graphSignature")==movedGraph.dump(-1),
            "relocation apply modified source");
    {std::ofstream f(movedFrom,std::ios::binary);f<<"pixels";}
    {std::ofstream f(movedFrom.string()+".meta");f<<moveMeta;}
    applyPlan.set("newName",Json::string("relocation_v3"));
    checkGraphRequirement(call("fx_replay_preset_relocate_apply",applyPlan).has("error"),"conflicting plan applied");
    checkGraphRequirement(call("fx_replay_preset_relocate_preview",movedGet).getStr("state")=="conflict",
            "duplicate GUID accepted");
    // Export the relocated v2 recipe after removing the deliberate GUID
    // collision. The target has the same asset at a different content path.
    fs::remove(movedFrom);fs::remove(movedFrom.string()+".meta");
    Json portableGet=Json::object();portableGet.set("name",Json::string("relocation_v2"));
    const auto portable=call("fx_replay_preset_portable_export",portableGet)["portable"];
    checkGraphRequirement(portable.isObject()&&!portable["recipe"].has("projectPath"),"portable export retained project identity");
    const auto target=root/"portable-target";
    fs::create_directories(target/"content");
    fs::copy_file(movedTo,target/"content"/"target.png");
    fs::copy_file(movedTo.string()+".meta",target/"content"/"target.png.meta");
    ws.project.dir=target.string();
    Json portableArgs=Json::object();portableArgs.set("portable",portable);
    const auto portablePlan=call("fx_replay_preset_portable_preview",portableArgs);
    checkGraphRequirement(portablePlan.getStr("state")=="ready"&&portablePlan["candidate"].getStr("projectPath")==target.string(),
            "cross-project portable remap failed");
    portableArgs.set("newName",Json::string("imported"));
    portableArgs.set("planRevision",Json::string("stale"));
    checkGraphRequirement(call("fx_replay_preset_portable_apply",portableArgs).has("error"),"stale portable plan applied");
    portableArgs.set("planRevision",portablePlan["planRevision"]);
    checkGraphRequirement(!call("fx_replay_preset_portable_apply",portableArgs).has("error"),"portable apply failed");
    Json imported=Json::object();imported.set("name",Json::string("imported"));
    checkGraphRequirement(call("fx_replay_preset_get",imported)["compatibility"].getStr("state")=="matching",
            "imported preset dependencies mismatch");
    checkGraphRequirement(call("fx_replay_preset_portable_apply",portableArgs).has("error"),"portable import overwrote existing name");
    fs::remove(target/"content"/"target.png");
    checkGraphRequirement(call("fx_replay_preset_portable_preview",portableArgs).getStr("state")=="conflict",
            "portable import missed absent target resource");
    ws.project.dir=root.string();
    getPreset.set("expectedGraphSignature",Json::string("other"));
    checkGraphRequirement(call("fx_replay_preset_get",getPreset).has("error"),"stale graph revision accepted");
    savePreset.set("name",Json::string("../escape"));
    checkGraphRequirement(call("fx_replay_preset_save",savePreset).has("error"),"unsafe replay name accepted");
    savePreset.set("name",Json::string("second"));savePreset.set("expectedProjectPath",Json::string("other"));
    checkGraphRequirement(call("fx_replay_preset_save",savePreset).has("error"),"stale project accepted");

    Json bake = Json::object();
    bake.set("name", Json::string("spark"));
    bake.set("effect", Json::string("spark_baked"));
    bake.set("overrides", Json::parse(R"({"size":1.25})"));
    const auto baked = call("fx_graph_bake", bake);
    checkGraphRequirement(baked.getBool("ok") && fs::exists(root/"fx"/"spark_baked.json"), "graph bake failed");
    const auto effect = Json::parseFile((root/"fx"/"spark_baked.json").string());
    fx::EffectDef parsed;
    std::vector<std::string> errors;
    checkGraphRequirement(fx::parseEffect(effect, "fx/spark_baked.json", parsed, errors), "baked asset invalid");
    checkGraphRequirement(parsed.emitters.size()==1 && parsed.emitters[0].size.min==1.25f, "override lost in bake");
    const auto refused = call("fx_graph_bake", bake);
    checkGraphRequirement(refused.has("error"), "bake overwrote an effect without permission");

    Json invalid = graph;
    invalid.set("version", Json::number(int64_t(3))); // v2 is now supported
    create.set("graph", invalid);
    const auto rejected = call("fx_graph_set", create);
    checkGraphRequirement(!rejected.getBool("ok"), "invalid graph should not be saved");
    checkGraphRequirement(call("fx_graph_get", name)["validation"].getBool("ok"), "invalid graph changed last-good asset");

    name.set("name", Json::string("../unsafe"));
    const auto escape = call("fx_graph_delete", name);
    checkGraphRequirement(escape.has("error"), "path traversal accepted");

    name.set("name", Json::string("spark"));
    checkGraphRequirement(call("fx_graph_delete", name).getBool("ok"), "graph delete failed");
    checkGraphRequirement(!fs::exists(root/"fx"/"graphs"/"spark.vfx.json"), "graph file still present");
    checkGraphRequirement(fs::exists(root/"fx"/"spark_baked.json"), "deleting graph deleted baked effect");

    std::cout << "PASS SpectraFX MCP graph CRUD, native preview bridge, validation, variants, bake and path safety\n";
}
