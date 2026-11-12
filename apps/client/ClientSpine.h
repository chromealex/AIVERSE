// Spine skeletons in the client UI (engine/spine): each "view" renders one animated skeleton into
// its own texture every frame, shown in RmlUi documents as <img src="#spine:NAME"/> (hero portraits,
// emotions, the waiting-screen plane of Bunker Wars).
//
// Declared in game.json:
//   "spine": { "hero1": { "skeleton": "spine/hero1.json", "atlas": "spine/hero1.atlas",   // atlas optional
//                         "width": 512, "height": 512, "scale": 1, "skin": "default",
//                         "animation": "hero1_idle1", "loop": true,
//                         "idle": ["hero1_idle1", "hero1_idle2"],   // optional: random idle after each loop
//                         "mix": 0.2, "pad": 8 } }
// or created by UI scripts; the `spine` Lua table (ui/*.lua, documents):
//   spine.create(name, {skeleton=..., width=..., ...})   -> true / false, error
//   spine.play(name, animation, loop[, track])   spine.add(name, animation, loop[, delay[, track]])
//   spine.skin(name, skin)   spine.animations(name) -> {..}   spine.remove(name)
//   game.on("spine", fn(e))   events: {view, type = "event"|"complete", animation, name, int, float, string}
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include "engine/data/Json.h"
#include "engine/gfx/Gfx.h"
#include "engine/render/TextureCache.h"
#include "engine/spine/SpineRender.h"

struct lua_State;

namespace aiverse::ui { class UiLayer; }

namespace aiverse::client {

class ClientSpine {
public:
    void init(gfx::Device& dev, ui::UiLayer& ui, const Json& manifest);
    void shutdown();
    bool create(const std::string& name, const Json& opts, std::string& error);
    bool remove(const std::string& name);
    bool play(const std::string& name, const std::string& anim, bool loop, int track, bool add, float delay);
    bool skin(const std::string& name, const std::string& skin);
    std::vector<std::string> animations(const std::string& name) const;
    // Updates the poses and renders every view into its texture (outside any pass).
    void render(float dt);
    // spine events -> the UI scripts (GameClient::luaEmit)
    std::function<void(const Json&)> onEvent;
    size_t count() const { return views_.size(); }

private:
    struct View {
        std::unique_ptr<spine::SpinePlayer> player;
        gfx::TextureHandle target;
        int width = 0, height = 0;
        float pad = 8;
        std::vector<std::string> idle;
        unsigned rnd = 1;
        bool nextIdle = false;
        bool fitted = false;
        float bounds[4] = {};
    };
    gfx::Device* dev_ = nullptr;
    ui::UiLayer* ui_ = nullptr;
    render::TextureCache textures_;
    spine::SpineRenderer renderer_;
    bool ready_ = false;
    std::map<std::string, View> views_;
};

// The `spine` table for the client's UI Lua state.
void openSpineLua(lua_State* L, ClientSpine* spine);

} // namespace aiverse::client
