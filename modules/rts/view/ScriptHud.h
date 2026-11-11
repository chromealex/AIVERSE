#pragma once
// Presentation-only bindings to deterministic script variables. Buttons issue normal
// player-ability commands; scripts remain responsible for validating purchases.
#include "modules/rts/RtsModule.h"
#include "engine/i18n/Localization.h"

namespace aiverse::game {
struct ScriptHudItem {
    std::string label, description, badge, image, status, frameImage, frameSprite;
    int frameIndex = 0, frameColumns = 1;
    float progress = -1;
    int ability = -1;
    bool pointTarget = false;
    bool enabled = true;
};
struct ScriptHudPanel { std::string title, layout; std::vector<ScriptHudItem> items; };

inline std::vector<ScriptHudPanel> scriptHud(const Json& manifest, const GameState& gs,
        const Rules& rules, uint8_t player, const i18n::Localization& loc) {
    auto value = [&](std::string key) {
        const auto at = key.find("{player}");
        if (at != std::string::npos) key.replace(at, 8, std::to_string(player));
        auto it = gs.trig.vars.find(key);
        return it == gs.trig.vars.end() ? Fixed{} : it->second;
    };
    std::vector<ScriptHudPanel> out;
    for (const auto& panel : manifest["scriptHud"].items()) {
        if (panel.has("visibleVar") && value(panel.getStr("visibleVar")) <= Fixed{}) continue;
        ScriptHudPanel p; p.title = loc.tr(panel.getStr("title")); p.layout=panel.getStr("layout");
        for (const auto& item : panel["items"].items()) {
            if (item.has("visibleVar") && value(item.getStr("visibleVar")) <= Fixed{}) continue;
            const Json* text = &item;
            if (item.has("optionVar")) {
                const auto index = value(item.getStr("optionVar")).toInt();
                const auto& options = manifest["scriptHudOptions"][item.getStr("options")];
                if (index < 1 || size_t(index) > options.size()) continue;
                text = &options[size_t(index - 1)];
            }
            i18n::Args args;
            for (const auto& [key, var] : item["args"].members()) args.num(key, value(var.str()).toInt());
            ScriptHudItem row;
            row.label = loc.tr(text->getStr("label"), args);
            row.description = text->has("description") ? loc.tr(text->getStr("description"), args) : "";
            row.badge = text->has("badge") ? loc.tr(text->getStr("badge")) : "";
            row.image = text->getStr("image");
            row.frameImage = text->getStr("frameImage");
            row.frameSprite = text->getStr("frameSprite");
            row.frameColumns = int(std::clamp<int64_t>(text->getInt("frameColumns", 1), 1, 1024));
            row.frameIndex = int(std::clamp<int64_t>(text->getInt("frameIndex", 0), 0, row.frameColumns - 1));
            if(item.has("cooldownVar")) {
                const auto left=value(item.getStr("cooldownVar")).toInt();
                const auto total=value(item.getStr("durationVar")).toInt();
                row.status=loc.tr(left>0?"hud.cooldown_seconds":"hud.ready",i18n::Args().num("seconds",left));
                row.progress=total>0?std::clamp(1.f-float(left)/float(total),0.f,1.f):1.f;
            }
            if(item.has("progressVar")) {
                const auto amount=value(item.getStr("progressVar"));
                const auto maximum=item.has("progressMaxVar")?value(item.getStr("progressMaxVar")):Fixed::fromInt(1);
                row.progress=maximum>Fixed{}?float(std::clamp(amount.toDouble()/maximum.toDouble(),0.0,1.0)):0.f;
            }
            row.ability = rules.findPlayerAbility(item.getStr("ability"));
            if (row.ability >= 0) row.pointTarget = rules.playerAbilities[size_t(row.ability)].target == TargetKind::Point;
            row.enabled = !item.has("enabledVar") || value(item.getStr("enabledVar")) > Fixed{};
            p.items.push_back(std::move(row));
        }
        if (!p.items.empty()) out.push_back(std::move(p));
    }
    std::stable_sort(out.begin(),out.end(),[](const auto& a,const auto& b){return (a.layout=="cards")<(b.layout=="cards");});
    return out;
}
inline bool scriptHudAbility(const Json& manifest, const std::string& id) {
    for (const auto& panel : manifest["scriptHud"].items())
        for (const auto& item : panel["items"].items())
            if (item.getStr("ability") == id) return true;
    return false;
}
} // namespace aiverse::game
