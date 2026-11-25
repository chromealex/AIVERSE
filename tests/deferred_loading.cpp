#include "engine/view/game/GameView.h"
#include <iostream>
#include <map>
#include <thread>
#include <future>
#include "engine/render/Animation.h"
#include "engine/render/graph/TextureGraphImageReclamation.h"
#include <stdexcept>
using namespace aiverse;
class FakeDevice final : public aiverse::gfx::Device {
public:
    std::vector<std::pair<int, int>> created;
    int alive = 0;
    aiverse::gfx::DeviceInfo info_;
    std::string err_;
    const aiverse::gfx::DeviceInfo& info() const override { return info_; }
    aiverse::gfx::BufferHandle createBuffer(const aiverse::gfx::BufferDesc&) override { return {1}; }
    void updateBuffer(aiverse::gfx::BufferHandle, const void*, size_t, size_t) override {}
    void destroyBuffer(aiverse::gfx::BufferHandle) override {}
    aiverse::gfx::TextureHandle createTexture(const aiverse::gfx::TextureDesc& d) override { created.push_back({d.width, d.height}); ++alive; return {uint32_t(created.size())}; }
    void updateTexture(aiverse::gfx::TextureHandle, int, int, int, int, const void*, int) override {}
    void destroyTexture(aiverse::gfx::TextureHandle) override { --alive; }
    aiverse::gfx::ShaderHandle createShader(const aiverse::gfx::ShaderDesc&) override { return {1}; }
    void destroyShader(aiverse::gfx::ShaderHandle) override {}
    aiverse::gfx::PipelineHandle createPipeline(const aiverse::gfx::PipelineDesc&) override { return {1}; }
    void destroyPipeline(aiverse::gfx::PipelineHandle) override {}
    const std::string& lastError() const override { return err_; }
    // The fake device executes all commands synchronously and queues no GPU
    // work, so every submitted operation is already idle on return.
    bool waitForResourceIdle() override { return true; }
    void beginFrame(int, int) override {}
    void endFrame() override {}
    void beginPass(const aiverse::gfx::PassDesc&) override {}
    void endPass() override {}
    void setViewport(const aiverse::gfx::Rect&) override {}
    void setScissor(const aiverse::gfx::Rect&) override {}
    void setPipeline(aiverse::gfx::PipelineHandle) override {}
    void setVertexBuffer(aiverse::gfx::BufferHandle, size_t) override {}
    void setIndexBuffer(aiverse::gfx::BufferHandle, size_t) override {}
    void setTexture(int, aiverse::gfx::TextureHandle) override {}
    void setUniforms(int, const void*, size_t) override {}
    void draw(uint32_t, uint32_t) override {}
    void drawIndexed(uint32_t, uint32_t, int32_t, uint32_t) override {}
};
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int aiverseMain(int, char**) {
    FakeDevice dev;
    game::Rules rules;
    for (const char* id : {"a", "b", "missing"}) {
        game::UnitType t; t.id=id; t.name=id;
        t.view=Json::parse(std::string("{\"model\":\"")+id+".obj\"}");
        rules.types.push_back(t);
    }
    rules.types[0].view = Json::parse(R"({"height":2,"model":[{"file":"a.obj","height":1},{"file":"a.obj","materials":{"*":{"color":[1,0,0]}}}]})");
    std::map<std::string,int> reads;
    auto reader=[&](const std::string& path,std::string& out) {
        ++reads[path];
        if(path=="a.obj" || path=="b.obj") { out="v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"; return true; }
        return false;
    };
    game::GameView view;
    view.setDeferredLoading(true);
    check(view.init(dev,rules,reader),"init");
    check(reads["a.obj"]==0 && reads["b.obj"]==0,"menu read type assets");
    view.loadType(0);
    check(view.typeLoaded(0) && !view.typeLoaded(1),"map subset readiness");
    check(reads["a.obj"]==1 && reads["b.obj"]==0,"unselected model read");
    view.loadType(0); check(reads["a.obj"]==1,"reloaded cached type");
    view.setAsyncReader(reader);
    for (int i=0; !view.prepareType(1); ++i) {
        check(i<10000,"async preparation timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(!view.typeLoaded(1),"preparation should not finalize a type");
    view.loadType(1); check(reads["b.obj"]==1 && view.modelOf(1),"later spawn not loaded");
    view.loadType(2); view.loadType(2); view.loadType(65535);
    check(reads["missing.obj"]==1 && !view.warnings().empty(),"missing model retry or no warning");
    view.shutdown(dev);
    check(view.init(dev,rules,reader),"reinit");
    check(!view.typeLoaded(0),"stale readiness after reinit");
    view.loadType(0);check(reads["a.obj"]==2,"reinit failed to reload");
    view.shutdown(dev);
    view.setDeferredLoading(false);
    check(view.init(dev,rules,reader),"eager init");
    check(view.typeLoaded(0)&&view.typeLoaded(1),"editor eager loading changed");
    view.shutdown(dev);
    rules.types[0].view = Json::parse(R"({"streaming":{"initialModels":1},"model":["a.obj",{"file":"a.obj","materials":{"*":{"color":[1,0,0]}}},"b.obj"]})");
    reads.clear();
    view.setDeferredLoading(true);
    check(view.init(dev,rules,reader),"streaming init");
    while (!view.prepareType(0)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    view.loadType(0);
    check(view.modelChoiceCount(0)==1 && view.pendingModelVariants()==2,"initial unique model limit");
    check(reads["a.obj"]==1 && reads["b.obj"]==0,"background model loaded before gameplay");
    const auto* firstModel=view.modelOf(0);
    for(int i=0;view.pendingModelVariants();++i) {
        check(i<10000,"background timeout");
        view.streamModelVariants(0.1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(view.modelChoiceCount(0)==3,"variants not completed");
    check(view.modelOf(0)==firstModel && view.modelOf(0,1)==firstModel,"existing choice reordered");
    check(reads["a.obj"]==1 && reads["b.obj"]==1,"background cache failure");
    view.shutdown(dev);
    view.setDeferredLoading(false);
    check(view.init(dev,rules,reader) && view.modelChoiceCount(0)==3,"eager mode obeyed streaming cap");
    view.shutdown(dev);
    // Editor residency policy: a type loaded for a previous map can be
    // retired while currently referenced models remain valid, then loaded
    // again through the same on-demand path.
    rules.types[0].view = Json::parse(R"({"model":"a.obj"})");
    rules.types[1].view = Json::parse(R"({"model":"b.obj"})");
    view.setDeferredLoading(true);
    reads.clear();
    check(view.init(dev,rules,reader),"editor residency init");
    check(view.assetResidencyStats().loadedTypes==0,"editor should not eagerly load whole rules catalog");
    view.loadType(0);
    view.loadType(1);
    const auto beforeTrim = view.assetResidencyStats();
    check(beforeTrim.loadedTypes==2 && beforeTrim.cachedModels>=2,"resident models tracked");
    {
        const auto read=view.readModelOf(1);
        check(read.valid() && view.activeModelReadLeases()==1,"choice reader must retain exact cache lease");
        check(!view.readModelOf(65535) && !view.readModelOf(1,999),"invalid choices must not acquire reader");
        check(view.activeModelReadLeases()==1,"invalid choice leaked a reader");
        view.trimTypes(std::vector<bool>{true,false,false},true);
        check(view.typeLoaded(1) && read.valid(),"trim must refuse before detaching live choice");
        const auto generation=view.modelResources().generation();
        const auto beforeAlive=dev.alive;
        check(!view.prepareType(2),"reader must defer CPU preparation publication");
        view.loadType(2);
        check(!view.typeLoaded(2),"reader must defer type publication");
        size_t refreshed=99;
        check(!view.reloadAppearanceTypes({1},false,&refreshed)&&refreshed==0,
              "reader must refuse appearance staging");
        check(view.reloadModelSources({"b.obj"})==0,"reader must defer source reload");
        bool initRefused=false,shutdownRefused=false;
        try { (void)view.init(dev,rules,reader); }
        catch(const std::logic_error&) { initRefused=true; }
        try { view.shutdown(dev); }
        catch(const std::logic_error&) { shutdownRefused=true; }
        check(initRefused&&shutdownRefused,"reader must refuse init and shutdown");
        check(read.valid()&&view.typeLoaded(1)&&view.modelResources().generation()==generation&&dev.alive==beforeAlive,
              "refused mutations must preserve model generation and GPU owners");
    }
    check(view.activeModelReadLeases()==0,"choice reader leaked after scope");
    view.trimTypes(std::vector<bool>{true,false,false},true);
    check(!view.readModelOf(1),"trimmed choice remained readable");
    const auto afterTrim = view.assetResidencyStats();
    check(view.typeLoaded(0) && !view.typeLoaded(1) && view.modelOf(0)!=nullptr,
          "trimming unused type must not discard the currently edited map type");
    check(afterTrim.loadedTypes==1 && afterTrim.cachedModels<beforeTrim.cachedModels,
          "GPU model cache should shrink when no live type owns an asset");
    const int bReads=reads["b.obj"];
    view.loadType(1);
    check(view.typeLoaded(1) && reads["b.obj"]==bReads+1,
          "trimmed type must reload on demand");
    view.shutdown(dev);
    // Deferred *type* loading must not reduce the number of model variants
    // available to the editor. Client streaming keeps its original default.
    rules.types[0].view = Json::parse(R"({"streaming":{"initialModels":1},"model":["a.obj",{"file":"a.obj","materials":{"*":{"color":[1,0,0]}}},"b.obj"]})");
    view.setDeferredLoading(true);
    view.setDeferAppearanceVariants(false);
    check(view.init(dev,rules,reader),"editor full appearance init");
    view.loadType(0);
    check(view.modelChoiceCount(0)==3 && view.pendingModelVariants()==0,
          "lazy editor type must expose every model variant");
    view.shutdown(dev);
    view.setDeferAppearanceVariants(true);
    // Real cache/device-epoch setup, no GPU graph dispatch. A trusted apply
    // callback may refuse or throw, but must not reenter the same View.
    {
        render::RenderDeviceLifetime lifetime(dev);
        game::GameView guarded;
        guarded.setGpuDeviceLease(lifetime.lease());
        guarded.setDeferredLoading(true);
        check(guarded.init(dev,rules,reader),"callback guard init");
        guarded.loadType(0);
        const auto target=guarded.graphMaterialTarget(0,0,0);
        check(bool(target),"callback guard fixture target");
        const auto generation=guarded.modelResources().generation();
        bool invoked=false;
        std::string error;
        const bool applied=guarded.applyGraphMaterial(0,0,target->identity,
            [&](auto&,auto&,auto&,auto&,const auto&,auto&,std::string*) -> bool {
                invoked=true;
                check(guarded.modelMutationBlocked(),"callback mutation flag");
                check(!guarded.readModelOf(0),"callback must refuse nested reader");
                check(!guarded.graphMaterialTarget(0,0,0),"callback must refuse nested target");
                check(!guarded.collectAutomaticGraphImages(),"callback must refuse nested sweep");
                guarded.trimTypes(std::vector<bool>{false,false,false},true);
                check(guarded.typeLoaded(0),"callback must not trim live target");
                bool refused=false;
                try { guarded.setAutomaticGraphImageCollection({}); }
                catch(const std::logic_error&) { refused=true; }
                check(refused,"callback must not replace root provider");
                throw std::runtime_error("intentional callback refusal");
            },&error);
        check(invoked&&!applied&&error=="intentional callback refusal","callback refusal evidence");
        check(!guarded.modelMutationBlocked()&&guarded.graphMaterialTarget(0,0,0)&&
              guarded.modelResources().generation()==generation,"callback guard must unwind");
        guarded.shutdown(dev);
    }
    // Legacy raw-index attachment roots are deliberately unpinned here:
    // a complete explicit collector must still enumerate them. Production
    // graph publication uses pins; this fixture injects an old-style image
    // through the trusted transaction callback to exercise the root audit.
    {
        using namespace render::texture_graph;
        render::RenderDeviceLifetime lifetime(dev);
        game::GameView attached;
        rules.types[0].view=Json::parse(R"({"model":"a.obj"})");
        rules.types[1].view=Json::parse(R"({"model":"b.obj","attach":[{"model":"a.obj"}]})");
        attached.setGpuDeviceLease(lifetime.lease());
        attached.setDeferredLoading(true);
        check(attached.init(dev,rules,reader),"attachment root init");
        attached.loadType(0);
        auto target=attached.graphMaterialTarget(0,0,0);
        check(bool(target),"attachment source material target");
        int slot=-1;
        std::string error;
        check(attached.applyGraphMaterial(0,0,target->identity,
            [&](auto&,auto&,render::Model& model,gfx::Device& device,
                const auto&,auto&,std::string*) {
                check(!model.materials.empty(),"attachment source materials");
                render::Image image;
                image.name="#texture-graph/legacy-attachment";
                image.proceduralGraphImage=true;
                image.width=image.height=1;
                gfx::TextureDesc desc;
                desc.width=desc.height=1;
                image.tex=device.createTexture(desc);
                slot=int(model.images.size());
                model.images.push_back(std::move(image));
                model.materials[0].baseTex=slot;
                model.markMaterialChanged();
                return true;
            },&error),"inject legacy graph image");
        attached.loadType(1); // copies source model materials into attachment
        check(attached.typeLoaded(1),"attachment host resident");
        target=attached.graphMaterialTarget(0,0,0);
        check(bool(target),"attachment target after copy");
        check(attached.applyGraphMaterial(0,0,target->identity,
            [&](auto&,auto&,render::Model& model,auto&,const auto&,auto&,std::string*) {
                model.materials[0].baseTex=-1;
                model.markMaterialChanged();
                return true;
            },&error),"detach base root");
        target=attached.graphMaterialTarget(0,0,0);
        check(bool(target),"attachment target before collection");
        const GraphImageRoots proof{true,0,{},false,false};
        const auto retained=attached.collectGraphMaterialImages(0,0,0,target->identity,proof);
        check(retained.detached==0&&retained.live==1,"live attachment must retain legacy graph image");
        attached.trimTypes(std::vector<bool>{true,false,false},true);
        target=attached.graphMaterialTarget(0,0,0);
        check(bool(target),"attachment target after trim");
        const auto reclaimed=attached.collectGraphMaterialImages(0,0,0,target->identity,proof);
        check(reclaimed.detached==1&&reclaimed.tombstones==1,"removed attachment permits image collection");
        {
            const auto read=attached.readModelOf(0);
            check(read.valid()&&slot>=0&&read.get()->images[size_t(slot)].proceduralGraphRetired,
                  "attachment image tombstone");
        }
        attached.shutdown(dev);
    }
    // Projectile appearance shares the same deferred model owner and trim rules.
    game::WeaponDef weapon;weapon.id="bow";
    rules.types[0].weapons={weapon};
    rules.types[0].view=Json::parse(R"({"model":"a.obj","projectiles":{"bow":{"model":"b.obj","scale":0.5,"forward":"+z","tint":[1,0.5,0.2,1]}}})");
    reads.clear();
    check(view.init(dev,rules,reader),"projectile appearance init");
    view.setAsyncReader(reader);
    for(int i=0;!view.prepareType(0);++i) {
        check(i<10000,"projectile preparation timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    view.loadType(0);
    check(view.projectileModelOf(0,0)!=nullptr && reads["b.obj"]==1,"projectile asset not loaded once");
    const auto* shot=view.projectileModelOf(0,0);
    view.trimTypes(std::vector<bool>{true,false,false},true);
    check(view.projectileModelOf(0,0)==shot,"trim removed live projectile model");
    check(view.reloadModelSources({"b.obj"})>0 && view.projectileModelOf(0,0)!=nullptr,"projectile source reload");
    view.shutdown(dev);
    {
        using namespace game::view_adapter;
        using namespace render;
        auto appearance=parseProjectileAppearance(Json::parse(R"({"model":"b.obj","forward":"+z","scale":0.5})"));
        const float3 launch{0,2,0}, muzzle{1,5,0}, impact{10,1,0};
        check(length(straightShotPoint(launch,muzzle,launch,impact)-muzzle)<1e-5f,"straight shot starts at muzzle");
        check(length(straightShotPoint(launch,muzzle,{5,2,0},impact)-float3{5.5f,3,0})<1e-5f,"straight shot half height");
        check(length(straightShotPoint(launch,muzzle,{12,2,0},impact)-impact)<1e-5f,"straight shot clamps to target height");
        check(length(straightShotPoint(launch,muzzle,launch,{0,1,0})-float3{0,1,0})<1e-5f,"coincident shot footprint");
        auto matrix=projectileFlightFrame({1,2,3},{0,1,0},{0,1,0})*appearance.axisFix;
        auto front=matrix.transformPoint({0,0,1})-matrix.transformPoint({0,0,0});
        check(length(front-float3{0,1,0})<0.001f,"vertical projectile orientation");
        auto diagonal=normalize(float3{2,-1,3});
        matrix=projectileFlightFrame({1,2,3},diagonal,{0,1,0});
        front=matrix.transformPoint({1,0,0})-matrix.transformPoint({0,0,0});
        check(length(front-diagonal)<0.001f,"ballistic projectile pitch/yaw");
        game::Projectile projectile;projectile.sourceType=0;projectile.weapon=0;
        check(projectileAppearanceSlot(rules,projectile)==0,"weapon appearance without live shooter");
        projectile.effList=0;
        check(projectileAppearanceSlot(rules,projectile)==-1,"ability projectile must not use weapon appearance");
        bool rejected=false;try{(void)parseProjectileAppearance(Json::parse(R"({"model":"b.obj","scale":-1})"));}catch(const std::exception&){rejected=true;}
        check(rejected,"invalid projectile scale accepted");
    }
    auto cache = std::make_shared<render::ModelTextureCache>();
    int imageReads = 0;
    auto images = [&](const std::string&, std::string& bytes) {
        ++imageReads;
        // One uncompressed 24-bit TGA pixel.
        bytes.assign(18, '\0'); bytes[2]=2; bytes[12]=1; bytes[14]=1; bytes[16]=24;
        bytes += std::string("\x11\x22\x33",3); return true;
    };
    render::Image imageA, imageB;
    auto decodeA=std::async(std::launch::async,[&] { return cache->read("models/a/../tex/a.tga",images,imageA); });
    auto decodeB=std::async(std::launch::async,[&] { return cache->read("models/tex/a.tga",images,imageB); });
    check(decodeA.get() && decodeB.get(),"concurrent cache decode");
    check(imageReads==1 && imageA.sharedRgba==imageB.sharedRgba,"duplicate decoded pixels");
    check(cache->decodedBytes()>=4,"CPU decoded cache bytes tracked");
    const int beforeTextures=dev.alive;
    render::Model texturedA,texturedB;
    texturedA.images.push_back(imageA);texturedB.images.push_back(imageB);
    render::uploadModel(dev,texturedA);render::uploadModel(dev,texturedB);
    check(dev.alive==beforeTextures+1 && texturedA.images[0].tex.id==texturedB.images[0].tex.id,"duplicate GPU texture");
    imageB.nearest=true;render::Model nearest;nearest.images.push_back(imageB);render::uploadModel(dev,nearest);
    check(dev.alive==beforeTextures+2,"sampler modes aliased");
    render::releaseModel(dev,texturedA);render::releaseModel(dev,texturedB);render::releaseModel(dev,nearest);
    check(dev.alive==beforeTextures+2,"model destroyed shared texture");
    // Explicit model release must return the shared-image leases so a
    // residency trim can free abandoned GPU images and decoded source bytes.
    cache->trim(dev);
    check(dev.alive==beforeTextures && cache->decodedBytes()==0 && cache->gpuCount()==0,
          "trim did not release unreferenced GPU textures and CPU decoded cache");
    cache->shutdown(dev);
    check(dev.alive==beforeTextures,"shared texture shutdown leak");
    render::Model skin;
    render::Node node; node.skin=0; node.mesh=0; skin.nodes.push_back(node);
    render::Skin bones; bones.joints.push_back(0); bones.inverseBind.push_back(render::mat4{}); skin.skins.push_back(bones);
    skin.bindWorld.push_back(render::mat4{});
    check(render::prepareAnimationBake(skin),"CPU animation bake");
    check(!skin.baked.tex.valid() && !skin.baked.pending.empty(),"CPU bake used GPU");
    check(render::bakeAnimations(dev,skin),"prepared animation upload");
    check(skin.baked.tex.valid() && skin.baked.pending.empty(),"animation staging not freed");
    std::cout<<"deferred loading: subset, cache, later spawn, missing asset, reinit, eager mode, editor trim/reload + memory metrics passed\n";
    return 0;
}
