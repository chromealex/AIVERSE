// Script commands (rules "commands", docs/scripting.md § Script commands) in the client: a command card
// section for the selection, hotkeys, target picking for the first point / unit argument, and the
// commands view scripts send with view.send. Everything goes out as kCmdScript through issue(); the
// simulation validates it again (RtsScriptCommands.cpp).
#include <algorithm>
#include <cstdio>

#include "GameClient.h"
#include "modules/rts/RtsScript.h"

namespace rts::client {

using platform::Event;
using platform::Key;

namespace {

void pushSection(UiState& s, const UiSection& sec) {   // same layout as ClientUi.cpp
    UiAction h;
    h.header = true; h.label = sec.title; h.kind = sec.kind; h.id = -1;
    s.card.push_back(h);
    for (UiAction a : sec.actions) { a.kind = sec.kind; s.card.push_back(std::move(a)); }
}

Key hotkeyOf(const std::string& h) {
    if (h.size() != 1) return Key::Unknown;
    const char c = char(std::toupper(static_cast<unsigned char>(h[0])));
    if (c >= 'A' && c <= 'Z') return Key(int(Key::A) + (c - 'A'));
    if (c >= '0' && c <= '9') return Key(int(Key::Num0) + (c - '0'));
    return Key::Unknown;
}

} // namespace

const std::vector<game::ScriptCommandDef>* GameClient::scriptCommands() {
    game::ScriptEngine* se = sim_ ? mod().scripts() : nullptr;
    return se && !se->commands().empty() ? &se->commands() : nullptr;
}

// Shown when the command needs no units, or one of the own selected units is of its "targets".
bool GameClient::scriptCommandFits(const game::ScriptCommandDef& d) {
    if (!d.card) return false;   // "card": false: only view scripts / HUD documents send it
    if (!d.needUnits) return true;
    World& w = sim_->world();
    for (Entity e : selection_) {
        if (!w.alive(e) || !w.has<Unit>(e) || w.get<Owner>(e).player != human_) continue;
        const uint16_t t = w.get<Unit>(e).type;
        if ((d.types.empty() && !d.tags) || std::find(d.types.begin(), d.types.end(), t) != d.types.end() || (d.tags && (rules_.types[t].tags & d.tags))) return true;
    }
    return false;
}

void GameClient::buildScriptCommands(UiState& s) {
    const auto* cs = scriptCommands();
    if (!cs || spectator_) return;
    UiSection sec{"", "grid", {}};
    game::ScriptEngine* se = mod().scripts();
    for (size_t i = 0; i < cs->size(); ++i) {
        const auto& d = (*cs)[i];
        if (!scriptCommandFits(d)) continue;
        UiAction a;
        a.label = d.label; a.hotkey = d.hotkey;   // the card shows `tip` as a red "why not" line: only for the cooldown
        for (auto& [r, v] : d.cost) {
            char b[64];
            std::snprintf(b, sizeof b, "%s%lld %s", a.cost.empty() ? "" : "  ", (long long)v.toInt(), rules_.resources[r].name.c_str());
            a.cost += b;
        }
        const Fixed cd = se->commandCooldown(*sim_, human_, int(i));
        a.enabled = cd.raw <= 0;
        if (!a.enabled) { char b[32]; std::snprintf(b, sizeof b, "%.0f s", cd.toDouble() + 0.5); a.tip = b; }
        a.active = mode_ == Mode::ScriptCmd && scriptCmd_ == int(i);
        a.id = addAction([this, i] { startScriptCommand(int(i)); }, a.enabled);
        sec.actions.push_back(a);
    }
    if (!sec.actions.empty()) pushSection(s, sec);
}

// Card click / hotkey: commands with a point / unit argument wait for a click; others go out now.
void GameClient::startScriptCommand(int index) {
    const auto* cs = scriptCommands();
    if (!cs || index < 0 || size_t(index) >= cs->size()) return;
    if ((*cs)[size_t(index)].pick >= 0) { mode_ = Mode::ScriptCmd; scriptCmd_ = index; return; }
    issueScriptCommand(index, kNullEntity, Vec3{}, false);
}

void GameClient::issueScriptCommand(int index, Entity target, Vec3 point, bool havePoint) {
    const auto* cs = scriptCommands();
    if (!cs || index < 0 || size_t(index) >= cs->size()) return;
    const auto& d = (*cs)[size_t(index)];
    Command c;
    c.type = game::kCmdScript;
    c.param = index;
    c.units = selection_;
    for (size_t k = 0; k < d.args.size(); ++k) {
        const auto& a = d.args[k];
        const bool picked = int(k) == d.pick;
        switch (a.kind) {
        case game::ScriptArgDef::Point:
            if (picked && !havePoint) return;
            c.args.push_back(point.x.raw); c.args.push_back(point.z.raw);
            break;
        case game::ScriptArgDef::Unit: case game::ScriptArgDef::Entity:
            if (picked && !target.valid()) return;   // a unit argument needs a unit under the cursor
            c.args.push_back(int64_t(target.packed()));
            break;
        default: c.args.push_back(a.def); break;
        }
    }
    issue(c);
}

bool GameClient::scriptCommandClick(const Event& e) {
    if (mode_ == Mode::Normal) {   // rules "mapClick": the game's tap command takes plain map clicks
        // a click, not a press: the tap goes out on release unless the pointer moved (a left drag pans the
        // camera instead), as touch taps / drags and uGUI's pointer-click / drag do (main.cpp MouseMove / MouseUp)
        const auto* cs = scriptCommands();
        if (!cs || game::mapClickCommand(*cs) < 0 || spectator_) return false;
        mapTapPending_ = true; mapTapX_ = e.x; mapTapY_ = e.y;
        return true;
    }
    if (mode_ != Mode::ScriptCmd) return false;
    float3 g;
    Vec3 p{};
    const bool have = groundAt(e.x, e.y, g);
    if (have) p = planet() ? simPoint(g) : Vec3{Fixed::fromRatio(int64_t(g.x * 256), 256), Fixed{}, Fixed::fromRatio(int64_t(g.z * 256), 256)};
    issueScriptCommand(scriptCmd_, pick(e.x, e.y), p, have);
    if (!(e.mods & platform::kModShift)) mode_ = Mode::Normal;
    return true;
}

// A press on a script HUD element carrying the attribute map-drag (OpenWar's ability discs: uGUI drag sources) that is
// released over the map sends the mapClick tap at the release point: the element's press armed the game's aim (its own
// mousedown event), the drop is the tap that casts. Released over the UI: nothing (the HUD element handles it).
void GameClient::mapDragEvent(const Event& e, bool uiWants) {
    using platform::EventType;
    if ((e.type == EventType::MouseDown && e.button == 0) || e.type == EventType::TouchBegin) {
        if (mapDrag_ && e.type == EventType::TouchBegin) return;   // a second finger
        mapDrag_ = uiWants && screen_ == Screen::Playing && ui_.pointerHasAttribute("map-drag");
        mapDragOverUi_ = true; mapDragTouch_ = e.type == EventType::TouchBegin ? e.touchId : -1;
        return;
    }
    if (!mapDrag_) return;
    const bool touch = e.type == EventType::TouchMove || e.type == EventType::TouchEnd || e.type == EventType::TouchCancel;
    if (touch && e.touchId != mapDragTouch_) return;
    if (e.type == EventType::MouseMove || e.type == EventType::TouchMove) { mapDragOverUi_ = ui_.pointerOverUi(); return; }
    if (e.type == EventType::FocusLost || e.type == EventType::TouchCancel || screen_ != Screen::Playing) { mapDrag_ = false; return; }
    if (e.type == EventType::MouseUp || e.type == EventType::TouchEnd) {
        mapDrag_ = false;
        // touch: UiLayer already left the hover state on release, so the last move decides
        const bool overUi = e.type == EventType::MouseUp ? ui_.pointerOverUi() : mapDragOverUi_;
        if (!overUi) mapClickTap(e.x, e.y);
    }
}

bool GameClient::mapClickTap(float x, float y) {
    const auto* cs = scriptCommands();
    const int i = cs ? game::mapClickCommand(*cs) : -1;
    if (i < 0 || spectator_ || mode_ != Mode::Normal) return false;
    float3 g;
    if (!groundAt(x, y, g)) return true;
    const Vec3 p = planet() ? simPoint(g) : Vec3{Fixed::fromRatio(int64_t(g.x * 256), 256), Fixed{}, Fixed::fromRatio(int64_t(g.z * 256), 256)};
    if (mod().scripts()->commandCooldown(*sim_, human_, i).raw <= 0) issueScriptCommand(i, kNullEntity, p, true);
    return true;
}

bool GameClient::scriptCommandKey(Key k) {
    const auto* cs = scriptCommands();
    if (!cs || k == Key::Unknown) return false;
    for (size_t i = 0; i < cs->size(); ++i) {
        const auto& d = (*cs)[i];
        if (hotkeyOf(d.hotkey) != k || !scriptCommandFits(d)) continue;
        if (mod().scripts()->commandCooldown(*sim_, human_, int(i)).raw > 0) return true;
        startScriptCommand(int(i));
        return true;
    }
    return false;
}

std::string GameClient::scriptCommandHint() {
    const auto* cs = scriptCommands();
    if (mode_ != Mode::ScriptCmd || !cs || scriptCmd_ < 0 || size_t(scriptCmd_) >= cs->size()) return {};
    return (*cs)[size_t(scriptCmd_)].label;
}

void GameClient::issueViewCommands() {
    for (auto& c : viewScripts_.takeCommands()) if (!spectator_) issue(std::move(c));
}

} // namespace rts::client
