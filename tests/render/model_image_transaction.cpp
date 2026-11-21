// Stage 112 deferred native test: rollback survives pending/failed GPU fence.
#include "engine/render/ModelImageUploadTransaction.h"
#include "engine/render/ModelResourceCache.h"
#include "engine/render/ModelStagingTransaction.h"
#include "engine/render/ModelAssetProvider.h"
#include "engine/render/OffscreenTargetLifetime.h"
#include "tests/AlwaysCheck.h"
#include <set>
#include <string>
#include <stdexcept>
namespace aiverse::platform {bool readContentFile(const std::string&,std::string&){return false;}}
using namespace aiverse;
struct Device : gfx::Device {
    gfx::DeviceInfo inf{};
    std::string err;
    std::set<uint32_t> live;
    uint32_t serial=1;
    bool complete=false,failed=false,idle=false;
    int destroyed=0;
    const gfx::DeviceInfo& info() const override{return inf;}
    const std::string& lastError() const override{return err;}
    bool waitForResourceIdle() override{return idle;}
    uint64_t beginResourceRetirement() override{return 9;}
    bool resourceRetirementComplete(uint64_t) override{return complete;}
    gfx::ResourceRetirementStatus resourceRetirementStatus(uint64_t) override {
        return failed?gfx::ResourceRetirementStatus::Failed:
               complete?gfx::ResourceRetirementStatus::Complete:
                        gfx::ResourceRetirementStatus::Pending;
    }
    gfx::BufferHandle createBuffer(const gfx::BufferDesc&) override{return {};}
    void updateBuffer(gfx::BufferHandle,const void*,size_t,size_t) override{}
    void destroyBuffer(gfx::BufferHandle) override{}
    gfx::TextureHandle createTexture(const gfx::TextureDesc&) override {
        const uint32_t n=serial++;live.insert(n);return {n};
    }
    void updateTexture(gfx::TextureHandle,int,int,int,int,const void*,int) override{}
    void destroyTexture(gfx::TextureHandle h) override {
        AIVERSE_TEST_CHECK(live.erase(h.id)==1);++destroyed;
    }
    gfx::ShaderHandle createShader(const gfx::ShaderDesc&) override{return {};}
    void destroyShader(gfx::ShaderHandle) override{}
    gfx::PipelineHandle createPipeline(const gfx::PipelineDesc&) override{return {};}
    void destroyPipeline(gfx::PipelineHandle) override{}
    void beginFrame(int,int) override{}
    void endFrame() override{}
    void beginPass(const gfx::PassDesc&) override{}
    void endPass() override{}
    void setViewport(const gfx::Rect&) override{}
    void setScissor(const gfx::Rect&) override{}
    void setPipeline(gfx::PipelineHandle) override{}
    void setVertexBuffer(gfx::BufferHandle,size_t) override{}
    void setIndexBuffer(gfx::BufferHandle,size_t) override{}
    void setTexture(int,gfx::TextureHandle) override{}
    void setUniforms(int,const void*,size_t) override{}
    void draw(uint32_t,uint32_t) override{}
    void drawIndexed(uint32_t,uint32_t,int32_t,uint32_t) override{}
};
render::Image makeImage(const char* name) {
    render::Image img;img.name=name;img.width=img.height=1;
    img.rgba={255,128,64,255};return img;
}
int main(){
    Device device;
    render::Model first,second;
    render::ModelImageRollbackPool pool;
    render::RenderDeviceLifetime epoch(device);
    {
        render::ModelImageUploadTransaction stage(device,pool,epoch.lease());
        AIVERSE_TEST_CHECK(stage.stage(first,makeImage("a.png"))==0);
        AIVERSE_TEST_CHECK(stage.stage(first,makeImage("a.png"))==0);
        AIVERSE_TEST_CHECK(stage.stage(second,makeImage("b.png"))==0);
        AIVERSE_TEST_CHECK(first.images.empty()&&second.images.empty());
        AIVERSE_TEST_CHECK(stage.pendingImages()==2);
    } // abort
    AIVERSE_TEST_CHECK(first.images.empty()&&second.images.empty());
    AIVERSE_TEST_CHECK(pool.pendingImages()==2&&device.destroyed==0);
    AIVERSE_TEST_CHECK(!pool.collect(device)); // first fence
    AIVERSE_TEST_CHECK(!pool.collect(device)); // pending
    device.failed=true;
    AIVERSE_TEST_CHECK(!pool.collect(device)); // failed fence
    AIVERSE_TEST_CHECK(!pool.drain(device)); // idle not yet proven
    AIVERSE_TEST_CHECK(device.destroyed==0);
    device.idle=true;
    AIVERSE_TEST_CHECK(pool.drain(device)); // full idle recovery
    AIVERSE_TEST_CHECK(pool.pendingImages()==0&&device.destroyed==2);
    device.failed=false;device.idle=false;
    {
        render::ModelImageUploadTransaction stage(device,pool,epoch.lease());
        AIVERSE_TEST_CHECK(stage.stage(first,makeImage("c.png"))==0);
        AIVERSE_TEST_CHECK(stage.stage(second,makeImage("d.png"))==0);
        AIVERSE_TEST_CHECK(stage.commit()==2);
    }
    AIVERSE_TEST_CHECK(first.images.size()==1&&second.images.size()==1);
    AIVERSE_TEST_CHECK(pool.pendingImages()==0&&device.destroyed==2);
    device.idle=true;
    render::releaseModel(device,first);
    render::releaseModel(device,second);
    AIVERSE_TEST_CHECK(device.destroyed==4&&device.live.empty());
    // Stage 113: speculative Model import may reuse an ALREADY resident
    // shared texture but must never add a missing GPU key to that cache.
    auto shared=std::make_shared<render::ModelTextureCache>();
    auto resident=makeImage("shared.png");
    resident.textureKey="textures/shared.png";
    resident.textureCache=shared;
    shared->upload(device,resident);
    AIVERSE_TEST_CHECK(resident.tex.valid() && shared->gpuCount()==1);
    // The importer keeps only its Model leases. Drop the temporary probe
    // lease so the cache can trim after the retired Model completes.
    resident.textureLease.reset();
    auto scratch=std::make_unique<render::Model>();
    auto hit=makeImage("shared.png");
    hit.textureKey="textures/shared.png";
    hit.textureCache=shared;
    scratch->images.push_back(std::move(hit));
    auto miss=makeImage("fresh.png");
    miss.textureKey="textures/fresh.png";
    miss.textureCache=shared;
    scratch->images.push_back(std::move(miss));
    AIVERSE_TEST_CHECK(render::uploadModel(device,*scratch,false,false));
    AIVERSE_TEST_CHECK(scratch->images[0].borrowedTexture);
    AIVERSE_TEST_CHECK(scratch->images[0].tex==resident.tex);
    AIVERSE_TEST_CHECK(!scratch->images[1].borrowedTexture);
    AIVERSE_TEST_CHECK(scratch->images[1].tex.valid());
    AIVERSE_TEST_CHECK(shared->gpuCount()==1); // failed import cannot pollute shared residency

    // An unpublished Model with borrowed/dedicated textures must be
    // released only after its OWN GPU completion marker, not at abort.
    render::ModelResourceCache owner;
    owner.bindResourceDevice(device,epoch.lease());
    device.idle=false;
    device.complete=false;
    const int before=device.destroyed;
    owner.retireUnpublished(device,scratch);
    AIVERSE_TEST_CHECK(!scratch && owner.pendingRetirements()==1);
    AIVERSE_TEST_CHECK(!owner.collectRetired(device)); // submit marker
    AIVERSE_TEST_CHECK(device.destroyed==before);
    shared->trim(device); // lease held by retired Model, still resident
    AIVERSE_TEST_CHECK(shared->gpuCount()==1);
    device.complete=true;
    AIVERSE_TEST_CHECK(owner.collectRetired(device));
    AIVERSE_TEST_CHECK(owner.pendingRetirements()==0);
    AIVERSE_TEST_CHECK(device.destroyed==before+1); // dedicated miss destroyed, alias borrowed
    shared->trim(device);
    AIVERSE_TEST_CHECK(shared->gpuCount()==0 && device.live.empty());

    // Commit moves scratch map nodes into the live cache without changing
    // the stable Model*; duplicate cache keys reject the whole transfer.
    render::ModelResourceCache::ModelMap stagedModels;
    auto stable=std::make_unique<render::Model>();
    auto* ptr=stable.get();
    stagedModels.emplace("models/stable.glb",std::move(stable));
    owner.publishStagedModels(device,stagedModels);
    AIVERSE_TEST_CHECK(stagedModels.empty());
    AIVERSE_TEST_CHECK(owner.find("models/stable.glb")==ptr);
    render::ModelResourceCache::ModelMap duplicate;
    duplicate.emplace("models/stable.glb",std::make_unique<render::Model>());
    bool rejectedDuplicate=false;
    try {owner.publishStagedModels(device,duplicate);}
    catch(const std::logic_error&){rejectedDuplicate=true;}
    AIVERSE_TEST_CHECK(rejectedDuplicate && duplicate.size()==1);
    duplicate.clear();
    device.idle=true;
    owner.shutdown(device);

    // Stage 114: two speculative Models upload the SAME missing GPU source.
    // Preparing a shared-cache plan must not change global GPU residency;
    // committing adopts one already-uploaded image and returns the other
    // dedicated handle for a real GPU-fenced retirement batch.
    render::Model left,right;
    auto leftImage=makeImage("atlas.png");
    leftImage.textureKey="textures/new-atlas.png";
    leftImage.textureCache=shared;
    auto rightImage=makeImage("atlas.png");
    rightImage.textureKey="textures/new-atlas.png";
    rightImage.textureCache=shared;
    AIVERSE_TEST_CHECK(render::uploadStandaloneImage(device,leftImage));
    AIVERSE_TEST_CHECK(render::uploadStandaloneImage(device,rightImage));
    AIVERSE_TEST_CHECK(leftImage.tex!=rightImage.tex);
    left.images.push_back(std::move(leftImage));
    right.images.push_back(std::move(rightImage));
    AIVERSE_TEST_CHECK(shared->gpuCount()==0);
    auto planned=shared->prepareGpuPromotion({&left,&right});
    AIVERSE_TEST_CHECK(planned.newSharedTextures()==1 && planned.bindings()==2);
    AIVERSE_TEST_CHECK(shared->gpuCount()==0);
    AIVERSE_TEST_CHECK(!left.images[0].borrowedTexture && !right.images[0].borrowedTexture);
    auto duplicates=planned.commit();
    AIVERSE_TEST_CHECK(shared->gpuCount()==1 && duplicates.size()==1);
    AIVERSE_TEST_CHECK(left.images[0].borrowedTexture && right.images[0].borrowedTexture);
    AIVERSE_TEST_CHECK(left.images[0].tex==right.images[0].tex);
    AIVERSE_TEST_CHECK(left.images[0].textureLease==right.images[0].textureLease);
    const auto duplicateId=duplicates.front().tex.id;
    const int destroyedBeforePromotion=device.destroyed;
    device.complete=false;device.idle=false;
    pool.retire(device,std::move(duplicates));
    AIVERSE_TEST_CHECK(pool.pendingImages()==1);
    AIVERSE_TEST_CHECK(!pool.collect(device));
    AIVERSE_TEST_CHECK(device.live.contains(duplicateId));
    device.complete=true;
    AIVERSE_TEST_CHECK(pool.collect(device));
    AIVERSE_TEST_CHECK(!device.live.contains(duplicateId));
    AIVERSE_TEST_CHECK(device.destroyed==destroyedBeforePromotion+1);
    render::releaseModel(device,left);
    render::releaseModel(device,right);
    AIVERSE_TEST_CHECK(shared->gpuCount()==1); // lease owner still resident
    shared->trim(device);
    AIVERSE_TEST_CHECK(shared->gpuCount()==0 && device.live.empty());

    // Aborted promotion plan never published cache entries or changed
    // Model ownership; the staged Model's normal rollback path still owns
    // and retires its dedicated GPU texture.
    render::Model aborted;
    auto abortedImage=makeImage("reject.png");
    abortedImage.textureKey="textures/reject.png";
    abortedImage.textureCache=shared;
    AIVERSE_TEST_CHECK(render::uploadStandaloneImage(device,abortedImage));
    aborted.images.push_back(std::move(abortedImage));
    {
        auto uncommitted=shared->prepareGpuPromotion({&aborted});
        AIVERSE_TEST_CHECK(uncommitted.newSharedTextures()==1);
    }
    AIVERSE_TEST_CHECK(shared->gpuCount()==0);
    AIVERSE_TEST_CHECK(!aborted.images[0].borrowedTexture);
    device.idle=true;
    render::releaseModel(device,aborted);
    AIVERSE_TEST_CHECK(device.live.empty());

    // Stage 114: material images are still outside Model::images when
    // the shared-promotion plan is prepared. One plan must support their
    // FUTURE append indices and deduplicate the same source across Models.
    render::Model materialA,materialB;
    device.complete=false;device.idle=false;
    {
        render::ModelImageUploadTransaction tx(device,pool,epoch.lease());
        auto a=makeImage("mat.png");
        a.textureKey="textures/material.png";a.textureCache=shared;
        auto b=makeImage("mat.png");
        b.textureKey="textures/material.png";b.textureCache=shared;
        AIVERSE_TEST_CHECK(tx.stage(materialA,std::move(a))==0);
        AIVERSE_TEST_CHECK(tx.stage(materialB,std::move(b))==0);
        const auto refs=tx.pendingImageReferences();
        AIVERSE_TEST_CHECK(refs.size()==2);
        std::vector<render::ModelTextureCache::PendingImage> proposed;
        for(const auto& ref:refs)
            proposed.push_back({ref.model,ref.appendIndex,ref.staged});
        auto promotion=shared->prepareGpuPromotion({},proposed);
        AIVERSE_TEST_CHECK(promotion.newSharedTextures()==1 && promotion.bindings()==2);
        AIVERSE_TEST_CHECK(shared->gpuCount()==0);
        AIVERSE_TEST_CHECK(materialA.images.empty() && materialB.images.empty());
        AIVERSE_TEST_CHECK(tx.commit()==2);
        auto duplicates=promotion.commit();
        AIVERSE_TEST_CHECK(duplicates.size()==1);
        AIVERSE_TEST_CHECK(shared->gpuCount()==1);
        AIVERSE_TEST_CHECK(materialA.images[0].borrowedTexture);
        AIVERSE_TEST_CHECK(materialB.images[0].borrowedTexture);
        AIVERSE_TEST_CHECK(materialA.images[0].tex==materialB.images[0].tex);
        pool.retire(device,std::move(duplicates));
    }
    AIVERSE_TEST_CHECK(!pool.collect(device));
    device.complete=true;
    AIVERSE_TEST_CHECK(pool.collect(device));
    render::releaseModel(device,materialA);
    render::releaseModel(device,materialB);
    shared->trim(device);
    AIVERSE_TEST_CHECK(shared->gpuCount()==0 && device.live.empty());

    // A scratch Model can receive new authored material images in the
    // SAME transaction. Its Model::images vector may relocate when the
    // material append reserves space; promotion bindings must address
    // Model+index rather than dangling Image* pointers.
    render::Model mixed;
    auto original=makeImage("original.png");
    original.textureKey="textures/original.png";
    original.textureCache=shared;
    AIVERSE_TEST_CHECK(render::uploadStandaloneImage(device,original));
    mixed.images.push_back(std::move(original));
    mixed.images.shrink_to_fit();
    {
        render::ModelImageUploadTransaction tx(device,pool,epoch.lease());
        auto extra=makeImage("override.png");
        extra.textureKey="textures/override.png";
        extra.textureCache=shared;
        AIVERSE_TEST_CHECK(tx.stage(mixed,std::move(extra))==1);
        std::vector<render::ModelTextureCache::PendingImage> proposed;
        for(const auto& ref:tx.pendingImageReferences())
            proposed.push_back({ref.model,ref.appendIndex,ref.staged});
        auto plan=shared->prepareGpuPromotion({&mixed},proposed);
        AIVERSE_TEST_CHECK(plan.newSharedTextures()==2 && plan.bindings()==2);
        AIVERSE_TEST_CHECK(tx.commit()==1);
        auto redundant=plan.commit();
        AIVERSE_TEST_CHECK(redundant.empty());
        AIVERSE_TEST_CHECK(mixed.images.size()==2);
        AIVERSE_TEST_CHECK(mixed.images[0].borrowedTexture);
        AIVERSE_TEST_CHECK(mixed.images[1].borrowedTexture);
        AIVERSE_TEST_CHECK(shared->gpuCount()==2);
    }
    device.idle=true;
    render::releaseModel(device,mixed);
    shared->trim(device);
    AIVERSE_TEST_CHECK(shared->gpuCount()==0 && device.live.empty());

    // Generic native Model staging: a successful import publishes the
    // unchanged Model* and a shared GPU texture only after commit().
    render::ModelResourceCache native;
    render::ModelImageRollbackPool nativeRollback;
    native.bindResourceDevice(device,epoch.lease());
    auto nativeModel=std::make_unique<render::Model>();
    nativeModel->path="models/native.glb";
    auto nativeImage=makeImage("native.png");
    nativeImage.textureKey="textures/native.png";
    nativeImage.textureCache=native.textures();
    nativeModel->images.push_back(std::move(nativeImage));
    render::Model* nativePointer=nativeModel.get();
    const std::string nativeKey=nativeModel->path;
    {
        render::ModelStagingTransaction tx(
            device,native,nativeRollback,epoch.lease());
        AIVERSE_TEST_CHECK(tx.stage(nativeKey,std::move(nativeModel))==nativePointer);
        AIVERSE_TEST_CHECK(!native.find("models/native.glb"));
        AIVERSE_TEST_CHECK(native.textures()->gpuCount()==0);
        AIVERSE_TEST_CHECK(tx.commit()==1);
    }
    AIVERSE_TEST_CHECK(native.find("models/native.glb")==nativePointer);
    AIVERSE_TEST_CHECK(native.textures()->gpuCount()==1);
    AIVERSE_TEST_CHECK(nativePointer->images[0].borrowedTexture);

    // Aborted native import never becomes a cache tombstone and holds
    // partially uploaded images until the Model's own completion fence.
    device.complete=false;device.idle=false;
    {
        render::ModelStagingTransaction tx(
            device,native,nativeRollback,epoch.lease());
        auto invalid=std::make_unique<render::Model>();
        invalid->path="models/cancelled.glb";
        auto pic=makeImage("cancelled.png");
        pic.textureKey="textures/cancelled.png";
        pic.textureCache=native.textures();
        invalid->images.push_back(std::move(pic));
        AIVERSE_TEST_CHECK(tx.stage("models/cancelled.glb",std::move(invalid)));
        AIVERSE_TEST_CHECK(!native.contains("models/cancelled.glb"));
    }
    AIVERSE_TEST_CHECK(!native.contains("models/cancelled.glb"));
    AIVERSE_TEST_CHECK(native.pendingRetirements()==1);
    AIVERSE_TEST_CHECK(native.textures()->gpuCount()==1); // no aborted GPU-cache key
    AIVERSE_TEST_CHECK(!native.collectRetired(device));
    device.complete=true;
    AIVERSE_TEST_CHECK(native.collectRetired(device));
    AIVERSE_TEST_CHECK(native.pendingRetirements()==0);
    device.idle=true;
    native.shutdown(device);
    AIVERSE_TEST_CHECK(device.live.empty());

    // Authoring hosts publish a new validated generation before retiring
    // last-good. A frame reader forbids publication and GPU release.
    render::ModelAssetProvider provider;
    provider.setGpuDeviceLease(epoch.lease());
    auto oldModel=std::make_unique<render::Model>();
    oldModel->images.push_back(makeImage("preview-old.png"));
    auto* old=provider.publishPrepared(device,"preview/1",std::move(oldModel));
    AIVERSE_TEST_CHECK(old && old->images[0].tex.valid());
    {
        auto lease=provider.resources().acquireModelRead(old);
        AIVERSE_TEST_CHECK(lease);
        bool rejected=false;
        try {provider.publishPrepared(device,"preview/2",std::make_unique<render::Model>());}
        catch(const std::logic_error&) {rejected=true;}
        AIVERSE_TEST_CHECK(rejected && provider.find("preview/1")==old);
        AIVERSE_TEST_CHECK(provider.find("preview/2")==nullptr);
    }
    auto nextModel=std::make_unique<render::Model>();
    nextModel->images.push_back(makeImage("preview-next.png"));
    auto* next=provider.publishPrepared(device,"preview/2",std::move(nextModel));
    device.complete=false;device.idle=false;
    const int beforeRetire=device.destroyed;
    provider.invalidate(device,{"preview/1"},[&](const auto& affected) {
        AIVERSE_TEST_CHECK(affected.size()==1 && affected[0].model==old);
    });
    AIVERSE_TEST_CHECK(provider.find("preview/2")==next);
    AIVERSE_TEST_CHECK(provider.find("preview/1")==nullptr);
    AIVERSE_TEST_CHECK(!provider.collectRetired(device) && device.destroyed==beforeRetire);
    device.complete=true;
    AIVERSE_TEST_CHECK(provider.collectRetired(device));
    AIVERSE_TEST_CHECK(device.destroyed==beforeRetire+1);
    device.idle=true;
    provider.shutdown(device);
    AIVERSE_TEST_CHECK(device.live.empty());

    // Stage 114 offscreen targets use the same per-batch fence semantics.
    // A resize or window destruction must not immediately release GPU
    // textures referenced by previous frame command buffers.
    render::OffscreenTargetRetirement targets(device,epoch.lease());
    gfx::TextureDesc targetDesc;
    targetDesc.width=targetDesc.height=4;
    targetDesc.renderTarget=true;
    auto colorTarget=device.createTexture(targetDesc);
    targetDesc.format=gfx::PixelFormat::Depth;
    auto depthTarget=device.createTexture(targetDesc);
    const int oldDestroyed=device.destroyed;
    device.failed=false;device.complete=false;device.idle=false;
    targets.retire(colorTarget,depthTarget);
    AIVERSE_TEST_CHECK(targets.pendingPairs()==1);
    AIVERSE_TEST_CHECK(!targets.collect());
    AIVERSE_TEST_CHECK(device.destroyed==oldDestroyed);
    device.failed=true;
    AIVERSE_TEST_CHECK(!targets.collect()); // failed token is NOT completion
    AIVERSE_TEST_CHECK(!targets.drain()); // failed waitForResourceIdle blocks release
    AIVERSE_TEST_CHECK(device.destroyed==oldDestroyed);
    device.idle=true;
    AIVERSE_TEST_CHECK(targets.drain());
    AIVERSE_TEST_CHECK(targets.pendingPairs()==0);
    AIVERSE_TEST_CHECK(device.destroyed==oldDestroyed+2);
    AIVERSE_TEST_CHECK(device.live.empty());

    // Stage 115/116 native import invariant: a failing GPU Device must not
    // discard CPU vertex/index data and falsely publish a visible Model.
    render::Model noGeometryUpload;
    noGeometryUpload.path="models/gpu-buffer-failure.glb";
    noGeometryUpload.vertices.resize(3);
    noGeometryUpload.indices={0,1,2};
    AIVERSE_TEST_CHECK(!render::uploadModel(device,noGeometryUpload,false,false));
    AIVERSE_TEST_CHECK(!noGeometryUpload.vertices.empty());
    AIVERSE_TEST_CHECK(!noGeometryUpload.indices.empty());

    // Stage 116: one owner handles different active render-target shapes:
    // shadow depth only, SSR color+depth and water reflection color+depth.
    // Each group is created before old handles are retired, and the GPU
    // completion token—not elapsed frames—authorizes destruction.
    device.failed=false;device.complete=false;device.idle=false;
    targetDesc.format=gfx::PixelFormat::Depth;
    const auto oldShadow=device.createTexture(targetDesc);
    targetDesc.format=gfx::PixelFormat::RGBA8;
    const auto oldSceneColor=device.createTexture(targetDesc);
    targetDesc.format=gfx::PixelFormat::Depth;
    const auto oldSceneDepth=device.createTexture(targetDesc);
    targetDesc.format=gfx::PixelFormat::RGBA16F;
    const auto oldReflectionColor=device.createTexture(targetDesc);
    targetDesc.format=gfx::PixelFormat::Depth;
    const auto oldReflectionDepth=device.createTexture(targetDesc);
    const int beforeMulti=device.destroyed;
    targets.retire(oldShadow,{});
    targets.retire(oldSceneColor,oldSceneDepth);
    targets.retire(oldReflectionColor,oldReflectionDepth);
    AIVERSE_TEST_CHECK(targets.pendingPairs()==3);
    AIVERSE_TEST_CHECK(device.live.size()==5);
    AIVERSE_TEST_CHECK(!targets.collect());
    AIVERSE_TEST_CHECK(device.destroyed==beforeMulti);
    device.complete=true;
    AIVERSE_TEST_CHECK(targets.collect());
    AIVERSE_TEST_CHECK(targets.pendingPairs()==0);
    AIVERSE_TEST_CHECK(device.destroyed==beforeMulti+5);
    AIVERSE_TEST_CHECK(device.live.empty());

    // A failed partial two-attachment allocation still enters the same
    // fenced queue with only its valid first handle.
    device.complete=false;
    const auto orphanColor=device.createTexture(targetDesc);
    targets.retire(orphanColor,{});
    AIVERSE_TEST_CHECK(!targets.collect());
    AIVERSE_TEST_CHECK(device.live.contains(orphanColor.id));
    device.complete=true;
    AIVERSE_TEST_CHECK(targets.collect());
    AIVERSE_TEST_CHECK(device.live.empty());

    epoch.retire();
    bool stale=false;
    try {
        render::ModelImageUploadTransaction stage(device,pool,epoch.lease());
    }catch(const std::logic_error&) {stale=true;}
    AIVERSE_TEST_CHECK(stale);
}
