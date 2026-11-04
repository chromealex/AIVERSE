// Dear ImGui integration: platform events -> ImGuiIO, draw data -> RHI. Same code on every backend.
#pragma once
#include "../gfx/Gfx.h"
#include "../platform/Platform.h"

struct ImGuiContext;

namespace rts::render {

// Each layer owns its ImGui context and makes it current in init / handleEvent / newFrame / render /
// shutdown, so two layers can live in one process (the editor hosting the game client's overlays);
// a host that interleaves them restores its own context afterwards.
class ImGuiLayer {
public:
    // fontPath: TTF from content (needs Cyrillic for Russian UI); falls back to ImGui's built-in font.
    bool init(gfx::Device& dev, const char* fontPath = "fonts/ui.ttf", float fontSize = 16.0f);
    void shutdown();
    // Returns true if ImGui wants this event (the game should ignore it).
    bool handleEvent(const platform::Event& e);
    void newFrame(double dt);
    void render(); // inside an active render pass
    bool wantsMouse() const;
    bool wantsKeyboard() const;

private:
    void updateTextures();
    void bind() const;
    gfx::Device* dev_ = nullptr;
    ImGuiContext* ctx_ = nullptr;
    gfx::ShaderHandle shader_;
    gfx::PipelineHandle pipeline_;
    gfx::BufferHandle vb_, ib_;
    int64_t primaryTouch_ = -1;
};

} // namespace rts::render
