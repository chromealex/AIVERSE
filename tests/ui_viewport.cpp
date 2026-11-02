// Embedded UI: isolated project files, viewport density and the shared client's data bindings.
#include "engine/ui/UiLayer.h"
#include "apps/client/UiModel.h"
#include <cstdio>
#include <RmlUi/Core.h>
#include <map>
class UiTestDevice final : public rts::gfx::Device {
public:
    std::vector<std::pair<int, int>> created;
    int alive = 0;
    rts::gfx::DeviceInfo info_;
    std::string err_;
    const rts::gfx::DeviceInfo& info() const override { return info_; }
    rts::gfx::BufferHandle createBuffer(const rts::gfx::BufferDesc&) override { return {}; }
    void updateBuffer(rts::gfx::BufferHandle, const void*, size_t, size_t) override {}
    void destroyBuffer(rts::gfx::BufferHandle) override {}
    rts::gfx::TextureHandle createTexture(const rts::gfx::TextureDesc& d) override { created.push_back({d.width, d.height}); ++alive; return {uint32_t(created.size())}; }
    void updateTexture(rts::gfx::TextureHandle, int, int, int, int, const void*, int) override {}
    void destroyTexture(rts::gfx::TextureHandle) override { --alive; }
    rts::gfx::ShaderHandle createShader(const rts::gfx::ShaderDesc&) override { return {1}; }
    void destroyShader(rts::gfx::ShaderHandle) override {}
    rts::gfx::PipelineHandle createPipeline(const rts::gfx::PipelineDesc&) override { return {1}; }
    void destroyPipeline(rts::gfx::PipelineHandle) override {}
    const std::string& lastError() const override { return err_; }
    void beginFrame(int, int) override {}
    void endFrame() override {}
    void beginPass(const rts::gfx::PassDesc&) override {}
    void endPass() override {}
    rts::gfx::Rect viewport;
    void setViewport(const rts::gfx::Rect& r) override { viewport = r; }
    std::vector<rts::gfx::Rect> scissors;
    void setScissor(const rts::gfx::Rect& r) override { scissors.push_back(r); }
    void setPipeline(rts::gfx::PipelineHandle) override {}
    void setVertexBuffer(rts::gfx::BufferHandle, size_t) override {}
    void setIndexBuffer(rts::gfx::BufferHandle, size_t) override {}
    void setTexture(int, rts::gfx::TextureHandle) override {}
    void setUniforms(int, const void*, size_t) override {}
    void draw(uint32_t, uint32_t) override {}
    void drawIndexed(uint32_t, uint32_t, int32_t, uint32_t) override {}
};
namespace rts::platform {
int framebufferWidth() { return 1920; }
int framebufferHeight() { return 1080; }
float dpiScale() { return 1; }
bool isTouchDevice() { return false; }
bool readContentFile(const std::string&, std::string&) { return false; }
std::string clipboardText() { return {}; }
void setClipboardText(const std::string&) {}
void startTextInput(const TextInputArea&) {}
void stopTextInput() {}
bool textInputActive() { return false; }
}
int testFontEffects(Rml::RenderManager& manager);
int main() {
    int errors = 0;
    auto check = [&](bool ok, const char* name) { if(!ok) { std::fprintf(stderr,"FAIL: %s\n",name); ++errors; } };
    UiTestDevice dev;
    rts::ui::UiLayer ui;
    std::map<std::string,std::string> files{
        {"engine/ui/keyboard.rml", "<rml><body/></rml>"},
        {"engine/ui/hud.rml", "<rml><body id='fallback'/></rml>"},
        {"ui/hud.rml", R"rml(<rml><head><link type="text/rcss" href="skin.rcss"/></head><body><div data-model="game"><button id="action" data-event-click="act()"/></div></body></rml>)rml"},
        {"ui/clip.rml", R"rml(<rml><head><style>body { margin: 0; } #frame { display: block; position: absolute; left: 10px; top: 20px; width: 200px; height: 200px; transform: scale(0.5); transform-origin: 0 0; }
 #list { position: absolute; left: 40px; top: 60px; width: 100px; height: 80px; overflow: hidden; } #item { display: block; width: 100px; height: 300px; background-color: #00ff00; }</style></head>
 <body><div id="frame"><div id="list"><div id="item"/></div></div></body></rml>)rml"},
        {"ui/skin.rcss", "body { margin: 0; } button { display: block; width: 100dp; height: 40dp; background-color: #ff0000; }"}
    };
    auto read=[&](const std::string& path,std::string& out) { auto i=files.find(path); if(i==files.end())return false; out=i->second; return true; };
    check(ui.init(dev,nullptr,read),"initialization");
    if (!ui.context()) return 1;
    check(testFontEffects(ui.context()->GetRenderManager())==0,"font effect rendering");
    ui.setOnScreenKeyboard(rts::ui::UiLayer::OnScreenKeyboard::Never);
    auto c=ui.context()->CreateDataModel("game");
    rts::client::UiState bound;
    rts::client::bindUiState(c,bound);
    int clicks=0;
    c.BindEventCallback("act",[&](Rml::DataModelHandle,Rml::Event&,const Rml::VariantList&){++clicks;});
    auto* doc=ui.loadDocument("hud.rml");
    check(doc && doc->GetElementById("action"),"project overrides engine document");
    ui.setViewport(800,600,2); ui.update();
    check(ui.context()->GetDimensions()==Rml::Vector2i(800,600),"embedded dimensions");
    check(ui.dpRatio()==2,"embedded density");
    auto* button=doc?doc->GetElementById("action"):nullptr;
    check(button && button->GetBox().GetSize(Rml::BoxArea::Content).x==200,"project CSS and dp scaling");
    auto next=bound; next.screen="playing"; next.scriptTitle="support";
    rts::client::syncUiState(c.GetModelHandle(),bound,next);
    check(bound.screen=="playing" && bound.scriptTitle=="support","shared model synchronization");
    ui.update();
    auto* same=doc->GetElementById("action");
    rts::client::syncUiState(c.GetModelHandle(),bound,next); ui.update();
    check(same==doc->GetElementById("action"),"unchanged model preserves input elements");
    ui.context()->ProcessMouseMove(20,20,0); ui.context()->ProcessMouseButtonDown(0,0); ui.context()->ProcessMouseButtonUp(0,0);
    check(clicks==1,"RML action callback");
    ui.render(); check(dev.viewport.w==800 && dev.viewport.h==600,"render uses embedded target");
    ui.setViewport(360,640,1); ui.update(); ui.render();
    check(dev.viewport.w==360 && dev.viewport.h==640 && ui.dpRatio()==1,"orientation and density change");
    // overflow clipping inside a transformed (scaled) element: the clip mask becomes the scissor of its screen bounds
    if(auto* clip=ui.loadDocument("clip.rml")){
        clip->Show(); ui.update(); dev.scissors.clear(); ui.render();
        bool found=false;
        for(auto& r:dev.scissors) if(r.x==30 && r.y==50 && r.w==50 && r.h==40) found=true;
        if(!found) for(auto& r:dev.scissors) std::fprintf(stderr,"scissor %d %d %d %d\n",r.x,r.y,r.w,r.h);
        check(found,"clip mask under a scale transform -> scissor 30,50 50x40");
        clip->Close(); ui.update();
    } else check(false,"clip document");
    ui.shutdown(); check(dev.alive==0,"UI texture cleanup");
    check(ui.init(dev,nullptr,read),"reinitialize for another play session");
    ui.shutdown(); check(dev.alive==0,"second session texture cleanup");
    std::printf("embedded UI: %d failures\n",errors); return errors?1:0;
}
