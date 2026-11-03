#pragma once
#include "apps/client/UiModel.h"
#include "engine/ui/UiLayer.h"
#include "engine/i18n/Localization.h"
#include "modules/rts/view/RegionSwipe.h"
#include "modules/rts/view/EntityActionControls.h"

namespace rts::editor {
class Editor;
struct Viewport;
// Hosts the client's RML documents over the editor's existing play-test simulation.
class GameHud {
public:
    void prepare(Editor&, const Viewport&, float density, bool touch, game::RegionSwipe&, int pointAbility, const game::EntityActionControls&);
    void render(Editor&, const Viewport&);
    bool blocked() const { return blocked_; }
    bool pointerOverUi() const { return ui_.pointerOverUi(); }
    int takePointAbility() { int a = pointAbility_; pointAbility_ = -1; return a; }
    // structure type chosen on the command card, waiting for a ground click (GameWindow issues kCmdBuild); -1 = none
    int placeType() const { return placeType_; }
    void clearPlace() { placeType_ = -1; }
    bool finish(Editor&); // deferred restart/stop: never destroy a simulation during an input callback
    void reset();
private:
    void init(Editor&);
    void input(const Viewport&);
    int action(std::function<void()> fn, bool enabled = true);
    void commandCard(Editor&, client::UiState&);   // train / research / queue / build, as the client's card
    rts::ui::UiLayer ui_;
    i18n::Localization loc_;
    client::UiState bound_;
    Rml::DataModelHandle model_;
    std::vector<std::function<void()>> actions_;
    std::string project_;
    int pointAbility_ = -1, request_ = 0, placeType_ = -1;
    int shadows_ = 0, materials_ = 2, particles_ = 2, volume_ = 100, music_ = 100;
    bool blocked_ = false, howto_ = false;
    bool pressed_[3]{};
};
} // namespace rts::editor
