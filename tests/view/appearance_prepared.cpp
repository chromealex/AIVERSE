// Stage 111 deferred: host-resolved appearance snapshots are published
// without replaying texture callbacks or building another animation clock.
#include "engine/view/AppearanceAssetRegistry.h"
#include "../AlwaysCheck.h"
#include "../cloth/FakeDevice.h"
#include <string>
#include <memory>
#include <array>

namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}

struct MaterialFixtureDevice : FakeDevice {
    int allocationsBeforeFailure=-1;
    gfx::TextureHandle createTexture(const gfx::TextureDesc& d) override {
        if(allocationsBeforeFailure==0)return {};
        if(allocationsBeforeFailure>0)--allocationsBeforeFailure;
        return FakeDevice::createTexture(d);
    }
    bool waitForResourceIdle() override{return true;}
};
// This fixture verifies ownership and fences only; it does not execute a shader.
struct TransferFixtureDevice : MaterialFixtureDevice {
    uint64_t submitted=1,completed=0;
    bool rejectCopy=false;
    int copies=0,created=0;
    gfx::TextureHandle createTexture(const gfx::TextureDesc& d) override {
        ++created;return MaterialFixtureDevice::createTexture(d);
    }
    bool supportsStorageBufferTextureCopy()const override{return true;}
    uint64_t gpuSubmissionFence()const override{return inFrame?submitted:0;}
    uint64_t gpuCompletedFence()const override{return completed;}
    bool bufferInfo(gfx::BufferHandle h,gfx::BufferInfo& out)const override{
        auto it=buffers.find(h.id);if(it==buffers.end())return false;
        out={it->second.size(),1};return true;
    }
    bool textureInfo(gfx::TextureHandle h,gfx::TextureInfo& out)const override{
        auto it=textures.find(h.id);if(it==textures.end())return false;
        out={it->second.w,it->second.h,gfx::PixelFormat::RGBA8};return true;
    }
    bool copyStorageBufferToTexture2D(gfx::BufferHandle from,gfx::TextureHandle to,
            const gfx::BufferTextureCopy2D& c)override{
        auto b=buffers.find(from.id);auto t=textures.find(to.id);
        if(rejectCopy||!inFrame||b==buffers.end()||t==textures.end()||
           !gfx::validBufferTextureCopy2D(c,b->second.size(),t->second.w,t->second.h))return false;
        ++copies;return true;
    }
};
static void checkGpuGraphImagePublication(){
    using namespace aiverse;
    using namespace render::texture_graph;
    TransferFixtureDevice dev;
    render::RenderDeviceLifetime epoch(dev);
    render::ModelImageRollbackPool rollback;
    render::Model model;model.materials.resize(1);
    view::AppearanceAssetRegistry registry;
    view::AppearanceAssetRegistry::Asset asset;asset.model=&model;asset.materials=model.materials;
    AIVERSE_TEST_CHECK(registry.publishPrepared("gpu",asset));
    const auto old=registry.find("gpu");
    Program program;program.size=8;program.outputs={{"ao",0},{"color",1},{"height",0}};
    const auto layout=gfx::rgba8CopyLayout(8,8);
    gfx::BufferDesc bd;bd.type=gfx::BufferType::Storage;bd.size=layout.bytes;
    auto source=dev.createBuffer(bd);
    const gfx::BufferTextureCopy2D copy{8,8,0,layout.rowPitchBytes};
    GraphGpuTextureCopy ao,color,failed;
    // No active recording frame => no copy or texture allocation.
    AIVERSE_TEST_CHECK(!queueGraphGpuTextureCopy(dev,epoch.lease(),program,"gpu1","ao",source,copy,ao));
    AIVERSE_TEST_CHECK(dev.created==0&&!ao.image.tex.valid());
    dev.beginFrame(1,1);
    AIVERSE_TEST_CHECK(queueGraphGpuTextureCopy(dev,epoch.lease(),program,"gpu1","ao",source,copy,ao));
    AIVERSE_TEST_CHECK(queueGraphGpuTextureCopy(dev,epoch.lease(),program,"gpu1","color",source,copy,color));
    auto aoHandle=ao.image.tex;auto colorHandle=color.image.tex;
    dev.rejectCopy=true;
    AIVERSE_TEST_CHECK(!queueGraphGpuTextureCopy(dev,epoch.lease(),program,"gpu2","ao",source,copy,failed));
    AIVERSE_TEST_CHECK(failed.fence==dev.submitted&&failed.image.tex.valid()&&!failed.encoded);
    dev.endFrame();
    GraphGpuTextureCopy* rows[]{&ao,&color};
    auto publish=[&](uint64_t version,bool accepted){
        return registry.publishGpuTextureGraphMaterial("gpu",version,old->modelPublicationGeneration,
            0,model,dev,epoch.lease(),rollback,program,rows,"gpu1",accepted);
    };
    AIVERSE_TEST_CHECK(!publish(old->version,true)&&model.images.empty());
    AIVERSE_TEST_CHECK(ao.image.tex==aoHandle&&color.image.tex==colorHandle);
    dev.completed=dev.submitted;
    AIVERSE_TEST_CHECK(!graphGpuTextureCopyComplete(failed,dev));
    AIVERSE_TEST_CHECK(!publish(old->version,false)&&model.images.empty());
    AIVERSE_TEST_CHECK(!publish(old->version+1,true)&&model.images.empty());
    AIVERSE_TEST_CHECK(publish(old->version,true));
    const auto next=registry.find("gpu");
    AIVERSE_TEST_CHECK(model.images.size()==2&&model.images[0].tex==aoHandle&&model.images[1].tex==colorHandle);
    AIVERSE_TEST_CHECK(!ao.image.tex.valid()&&!color.image.tex.valid()&&dev.created==3&&dev.copies==2);
    AIVERSE_TEST_CHECK(next->materials[0].occlusionTex==0&&next->materials[0].baseTex==1);
    AIVERSE_TEST_CHECK(old->materials[0].baseTex==-1&&next->version>old->version);
    AIVERSE_TEST_CHECK(!publish(next->version,true)&&model.images.size()==2);
    // The actual live snapshot can be replaced without a temporary
    // registry. Keep the pinned old snapshot and shared base untouched.
    auto live=next;
    auto mirror=next->materials;
    GraphGpuTextureCopy liveAo,liveColor;
    dev.rejectCopy=false;++dev.submitted;dev.beginFrame(1,1);
    AIVERSE_TEST_CHECK(queueGraphGpuTextureCopy(dev,epoch.lease(),program,"gpu3","ao",source,copy,liveAo));
    AIVERSE_TEST_CHECK(queueGraphGpuTextureCopy(dev,epoch.lease(),program,"gpu3","color",source,copy,liveColor));
    dev.endFrame();dev.completed=dev.submitted;
    GraphGpuTextureCopy* liveRows[]{&liveAo,&liveColor};
    const view::LiveAppearanceMaterialTarget stamp{live->modelPublicationGeneration,live->version,0};
    auto direct=[&](view::LiveAppearanceMaterialTarget target,std::vector<render::Material>* targetMirror){
        return view::AppearanceAssetRegistry::publishGpuTextureGraphSnapshot(live,target,model,dev,
            epoch.lease(),rollback,program,liveRows,"gpu3",true,targetMirror);
    };
    AIVERSE_TEST_CHECK(!direct({stamp.publication,stamp.version+1,0},&mirror));
    AIVERSE_TEST_CHECK(!direct({stamp.publication+1,stamp.version,0},&mirror));
    AIVERSE_TEST_CHECK(!direct(stamp,&model.materials));
    AIVERSE_TEST_CHECK(live==next&&model.images.size()==2&&liveAo.image.tex.valid());
    AIVERSE_TEST_CHECK(direct(stamp,&mirror));
    AIVERSE_TEST_CHECK(live->version==stamp.version+1&&live->modelPublicationGeneration==stamp.publication);
    AIVERSE_TEST_CHECK(live->materials[0].occlusionTex==2&&live->materials[0].baseTex==3);
    AIVERSE_TEST_CHECK(mirror[0].occlusionTex==2&&mirror[0].baseTex==3&&model.images.size()==4);
    AIVERSE_TEST_CHECK(model.materials[0].baseTex==-1&&next->materials[0].baseTex==1&&registry.find("gpu")==next);
    AIVERSE_TEST_CHECK(!liveAo.image.tex.valid()&&!liveColor.image.tex.valid());
    AIVERSE_TEST_CHECK(!direct(stamp,&mirror)&&model.images.size()==4);
    // An unadopted failed candidate stays producer-owned through completion.
    dev.destroyTexture(failed.image.tex);failed.image.tex={};
    dev.destroyBuffer(source);
    registry.clear();render::releaseModel(dev,model);
    AIVERSE_TEST_CHECK(dev.textures.empty()&&dev.buffers.empty());
}
int main() {
    using namespace aiverse;
    checkGpuGraphImagePublication();
    render::Model model;
    model.path="models/unit.glb";
    model.materials.resize(1);
    model.materials[0].name="Body";
    auto graph=render::AnimationGraph::compile(model,Json::parse(
        R"({"nodes":[{"id":"bind","kind":"bind"}],"output":"bind"})"));

    view::AppearanceAssetRegistry registry;
    view::AppearanceAssetRegistry::Asset staged;
    staged.model=&model;
    staged.materials=model.materials;
    staged.materials[0].roughness=0.3f;
    staged.graph=graph;
    staged.sourceId=42;
    staged.graphDefinition="bind graph";
    staged.sources={"materials.json",model.path};

    std::string error;
    AIVERSE_TEST_CHECK(registry.publishPrepared("character",staged,&error));
    auto first=registry.find("character");
    AIVERSE_TEST_CHECK(first && first->version==1 && first->generation>0);
    AIVERSE_TEST_CHECK(first->materials[0].roughness==0.3f);
    AIVERSE_TEST_CHECK(first->graph==graph);
    view::Object packet;
    AIVERSE_TEST_CHECK(first->packet(render::mat4{},packet));
    AIVERSE_TEST_CHECK(packet.graph==graph && packet.draw.materials==&first->materials);

    // A material-only publication pins old materials while retaining
    // the exact compiled graph pointer/generation of the active player.
    staged.materials[0].roughness=0.6f;
    AIVERSE_TEST_CHECK(registry.invalidateSources({"materials.json"})==1);
    AIVERSE_TEST_CHECK(registry.publishPrepared("character",staged,&error));
    auto next=registry.find("character");
    AIVERSE_TEST_CHECK(next && next!=first && next->version>first->version);
    AIVERSE_TEST_CHECK(next->generation==first->generation && next->graph==first->graph);
    AIVERSE_TEST_CHECK(first->materials[0].roughness==0.3f);
    AIVERSE_TEST_CHECK(next->materials[0].roughness==0.6f);

    // Reject an invalid prepared node-map without modifying a working
    // appearance, dirty status or animation generation.
    AIVERSE_TEST_CHECK(registry.invalidateSources({model.path})==1);
    const auto good=registry.find("character");
    staged.nodeMaterial={500};
    AIVERSE_TEST_CHECK(!registry.publishPrepared("character",staged,&error));
    AIVERSE_TEST_CHECK(!error.empty());
    AIVERSE_TEST_CHECK(registry.dirty("character"));
    AIVERSE_TEST_CHECK(registry.find("character")==good);
    staged.nodeMaterial.clear();

    auto other=render::AnimationGraph::compile(model,Json::parse(
        R"({"nodes":[{"id":"bind2","kind":"bind"}],"output":"bind2"})"));
    staged.graph=other;
    AIVERSE_TEST_CHECK(registry.publishPrepared("character",staged,&error));
    auto changed=registry.find("character");
    AIVERSE_TEST_CHECK(changed->graph==other && changed->generation>good->generation);
    AIVERSE_TEST_CHECK(good->graph==graph);
    // Runtime publication appends only validated procedural images, pins
    // the previous snapshot, and preserves animation/model generations.
    MaterialFixtureDevice device;
    render::RenderDeviceLifetime epoch(device);
    render::ModelImageRollbackPool rollback;
    render::texture_graph::Program program;program.size=8;
    program.outputs={{"ao",0},{"color",1}};
    std::vector<render::Image> images(2);
    for(auto& image:images){image.width=image.height=8;image.rgba.assign(8*8*4,255);}
    const auto revision=model.materialRevision;
    auto publish=[&](uint64_t version,std::string_view generation){
        return registry.publishTextureGraphMaterial("character",version,
            changed->modelPublicationGeneration,0,model,device,epoch.lease(),
            rollback,program,images,generation,&error);
    };
    AIVERSE_TEST_CHECK(!publish(good->version,"stale"));
    AIVERSE_TEST_CHECK(model.images.empty()&&device.textures.empty());
    device.allocationsBeforeFailure=1;
    AIVERSE_TEST_CHECK(!publish(changed->version,"partial"));
    AIVERSE_TEST_CHECK(registry.find("character")==changed&&model.images.empty());
    AIVERSE_TEST_CHECK(rollback.pendingImages()==1&&device.textures.size()==1);
    AIVERSE_TEST_CHECK(rollback.drain(device)&&device.textures.empty());
    device.allocationsBeforeFailure=-1;
    AIVERSE_TEST_CHECK(publish(changed->version,"valid"));
    auto bound=registry.find("character");
    AIVERSE_TEST_CHECK(bound->version>changed->version&&bound->graph==changed->graph);
    AIVERSE_TEST_CHECK(bound->generation==changed->generation&&bound->modelPublicationGeneration==changed->modelPublicationGeneration);
    AIVERSE_TEST_CHECK(model.materialRevision!=revision&&model.images.size()==2);
    AIVERSE_TEST_CHECK(bound->materials[0].occlusionTex==0&&bound->materials[0].baseTex==1);
    AIVERSE_TEST_CHECK(changed->materials[0].baseTex==-1);
    AIVERSE_TEST_CHECK(!publish(bound->version,"valid"));
    AIVERSE_TEST_CHECK(registry.find("character")==bound&&model.images.size()==2);
    for(size_t i=2;i<view::AppearanceAssetRegistry::maxProceduralImages;i+=2){
        AIVERSE_TEST_CHECK(publish(registry.find("character")->version,"gen_"+std::to_string(i)));
    }
    AIVERSE_TEST_CHECK(!publish(registry.find("character")->version,"over_budget"));
    AIVERSE_TEST_CHECK(model.images.size()==view::AppearanceAssetRegistry::maxProceduralImages);
    // The current registry snapshot and caller-pinned earlier snapshot remain
    // roots. A copied shader graph texture binding is a root as well.
    render::Material shaderRoot;shaderRoot.graphTextures[0]=2;
    std::array<std::span<const render::Material>,2> roots{bound->materials,std::span<const render::Material>(&shaderRoot,1)};
    bool refused=false;try{registry.collectTextureGraphImages(model,device,epoch.lease(),rollback,{false,0,roots});}
    catch(const std::exception&){refused=true;}AIVERSE_TEST_CHECK(refused&&rollback.pendingImages()==0);
    refused=false;try{registry.collectTextureGraphImages(model,device,epoch.lease(),rollback,{true,1,roots});}
    catch(const std::exception&){refused=true;}AIVERSE_TEST_CHECK(refused&&rollback.pendingImages()==0);
    const auto current=registry.find("character");const auto beforeCollect=model.materialRevision;
    const auto reclaimed=registry.collectTextureGraphImages(model,device,epoch.lease(),rollback,{true,0,roots});
    AIVERSE_TEST_CHECK(reclaimed.detached==19&&reclaimed.live==5&&reclaimed.tombstones==19);
    AIVERSE_TEST_CHECK(reclaimed.pendingRetirement==19&&device.textures.size()==24);
    AIVERSE_TEST_CHECK(model.images.size()==24&&model.materialRevision!=beforeCollect);
    AIVERSE_TEST_CHECK(model.images[0].tex.valid()&&model.images[1].tex.valid()&&model.images[2].tex.valid());
    AIVERSE_TEST_CHECK(model.images[3].proceduralGraphRetired&&!model.images[3].tex.valid());
    AIVERSE_TEST_CHECK(model.images[22].tex.valid()&&model.images[23].tex.valid());
    AIVERSE_TEST_CHECK(!publish(current->version,"retirement_backpressure"));
    AIVERSE_TEST_CHECK(rollback.drain(device)&&device.textures.size()==5);
    AIVERSE_TEST_CHECK(publish(current->version,"after_collection"));
    AIVERSE_TEST_CHECK(registry.find("character")->materials[0].baseTex==25&&model.images.size()==26);
    registry.clear();
    AIVERSE_TEST_CHECK(!registry.find("character"));
    // Caller-pinned snapshot remains renderable until the host detaches it
    // and then releases model GPU/CPU storage.
    AIVERSE_TEST_CHECK(changed->packet(render::mat4{},packet));
    packet={};bound.reset();
    render::releaseModel(device,model);
    AIVERSE_TEST_CHECK(device.textures.empty());
}
