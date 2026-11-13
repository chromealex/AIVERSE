#include "modules/rts/view/RtsView.h"
#include <iostream>
#include <map>
#include <thread>
#include <future>
#include "engine/render/Animation.h"
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
    game::RtsView view;
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
    const int beforeTextures=dev.alive;
    render::Model texturedA,texturedB;
    texturedA.images.push_back(imageA);texturedB.images.push_back(imageB);
    render::uploadModel(dev,texturedA);render::uploadModel(dev,texturedB);
    check(dev.alive==beforeTextures+1 && texturedA.images[0].tex.id==texturedB.images[0].tex.id,"duplicate GPU texture");
    imageB.nearest=true;render::Model nearest;nearest.images.push_back(imageB);render::uploadModel(dev,nearest);
    check(dev.alive==beforeTextures+2,"sampler modes aliased");
    render::releaseModel(dev,texturedA);render::releaseModel(dev,texturedB);render::releaseModel(dev,nearest);
    check(dev.alive==beforeTextures+2,"model destroyed shared texture");
    cache->shutdown(dev);check(dev.alive==beforeTextures,"shared texture leak");
    render::Model skin;
    render::Node node; node.skin=0; node.mesh=0; skin.nodes.push_back(node);
    render::Skin bones; bones.joints.push_back(0); bones.inverseBind.push_back(render::mat4{}); skin.skins.push_back(bones);
    skin.bindWorld.push_back(render::mat4{});
    check(render::prepareAnimationBake(skin),"CPU animation bake");
    check(!skin.baked.tex.valid() && !skin.baked.pending.empty(),"CPU bake used GPU");
    check(render::bakeAnimations(dev,skin),"prepared animation upload");
    check(skin.baked.tex.valid() && skin.baked.pending.empty(),"animation staging not freed");
    std::cout<<"deferred loading: subset, cache, later spawn, missing asset, reinit, eager mode passed\n";
    return 0;
}
