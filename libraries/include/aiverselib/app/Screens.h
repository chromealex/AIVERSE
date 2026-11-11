#pragma once
#include "Binding.h"
#include <algorithm>
#include <optional>
#include <set>

namespace aiverse::lib::app {
enum class ScreenKind { Hud, Build, Unit, Inventory, Technology, Pause, Results };
struct ScreenRow { std::string id, labelKey, value, action, reasonKey; bool enabled = true; };
struct Screen { std::string id, titleKey; ScreenKind kind = ScreenKind::Hud; bool modal = false; std::vector<ScreenRow> rows; };
inline bool validScreen(const Screen& screen) {
    if (screen.id.empty() || screen.titleKey.empty()) return false;
    std::set<std::string> ids;
    for (const auto& row : screen.rows) if (row.id.empty() || !ids.insert(row.id).second) return false;
    return true;
}
// Rebuild only when the host's snapshot changes; translator returns localized text.
template<class Translate>
std::string screenMarkup(const Screen& screen, Translate translate) {
    if (!validScreen(screen)) return {};
    std::string out = "<div class=\"screen\"><h1>" + escapeMarkup(translate(screen.titleKey)) + "</h1>";
    for (const auto& row : screen.rows) {
        out += "<div class=\"row\"><span>" + escapeMarkup(translate(row.labelKey)) + "</span><span>" + escapeMarkup(row.value) + "</span>";
        if (!row.action.empty()) out += "<button id=\"" + escapeMarkup(row.id) + "\"" + (row.enabled ? "" : " disabled=\"disabled\"") + ">" + escapeMarkup(translate(row.labelKey)) + "</button>";
        if (!row.reasonKey.empty()) out += "<span class=\"reason\">" + escapeMarkup(translate(row.reasonKey)) + "</span>";
        out += "</div>";
    }
    return out + "</div>";
}
class ScreenStack {
    std::vector<Screen> screens_;
public:
    bool open(Screen screen) {
        if (!validScreen(screen)) return false;
        auto it = std::find_if(screens_.begin(), screens_.end(), [&](const auto& s) { return s.id == screen.id; });
        if (it != screens_.end()) *it = std::move(screen);
        else screens_.push_back(std::move(screen));
        return true;
    }
    bool close(const std::string& id) {
        auto it = std::find_if(screens_.begin(), screens_.end(), [&](const auto& s) { return s.id == id; });
        if (it == screens_.end()) return false;
        screens_.erase(it); return true;
    }
    bool blocksGameInput() const {
        return std::any_of(screens_.begin(), screens_.end(), [](const auto& s) { return s.modal; });
    }
    std::optional<Action> click(const std::string& screenId, const std::string& rowId) const {
        for (auto it = screens_.rbegin(); it != screens_.rend(); ++it) {
            if (it->id == screenId) {
                for (const auto& r : it->rows) if (r.id == rowId && r.enabled && !r.action.empty()) return Action{r.action, r.id};
                return std::nullopt;
            }
            if (it->modal) return std::nullopt;
        }
        return std::nullopt;
    }
};
}
