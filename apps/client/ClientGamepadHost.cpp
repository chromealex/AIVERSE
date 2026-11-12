// GameClient side of the gamepad (ClientGamepad.h): polling, snap targets, event injection, the cursor.
#include "GameClient.h"
#include "imgui.h"

namespace aiverse::client {

void GameClient::updateGamepad(float dt) {
    padPanX_ = padPanY_ = 0;
    static bool configured = false;   // game.json is loaded once per process
    if (!configured) { gamepad_.configure(manifest_); configured = true; }
    int n = platform::pollGamepads();
    if (n == 0) return;
    int idx = 0;
    while (idx < platform::kMaxGamepads && !platform::gamepad(idx).connected) idx++;
    std::vector<ClientGamepad::Target> targets;
    const std::string& want = gamepad_.snapTargets();
    if (sim_ && screen_ == Screen::Playing && want != "none")
        for (const auto& o : scene_.objects) {
            if (o.type >= rules_.types.size()) continue;
            bool structure = rules_.types[o.type].structure;
            if ((want == "structures" && !structure) || (want == "units" && structure)) continue;
            float2 sp;
            if (objectScreen(o, 0.5f, sp)) targets.push_back({sp.x, sp.y});
        }
    auto f = gamepad_.update(platform::gamepad(idx), dt, float(platform::windowWidth()), float(platform::windowHeight()), targets);
    padInjecting_ = true;
    for (const auto& e : f.events) event(e);
    padInjecting_ = false;
    padPanX_ = f.panX;
    padPanY_ = f.panY;
}

void GameClient::drawGamepadCursor() {
    if (!gamepad_.active()) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 c(gamepad_.cursorX(), gamepad_.cursorY());
    dl->AddCircle(c, 14.f, IM_COL32(0, 0, 0, 160), 24, 5.f);
    dl->AddCircle(c, 14.f, IM_COL32(255, 230, 120, 255), 24, 2.5f);
    dl->AddCircleFilled(c, 3.f, IM_COL32(255, 230, 120, 255));
}

} // namespace aiverse::client
