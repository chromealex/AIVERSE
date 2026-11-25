#pragma once
#include "apps/editor/core/GameWizard.h"
#include <algorithm>
#include <stdexcept>

namespace aiverse::studio {
// Only a path selected through the native artwork picker can be used as a content source.
inline editor::WizardSpec studioWizardSpec(const std::string& engineDir, const Json& params, const std::string& approvedArt) {
    Json fields = Json::object();
    for (const char* key : {"title", "idea", "genre", "features", "orientation", "tutorial", "mapPreset", "levels",
                            "studio", "version", "languages", "platforms", "seed", "mapSize", "difficultyCurve",
                            "bossEvery", "keepTemplateMaps", "startResourcesPercent", "tutorialForced", "tutorialSkippable",
                            "tutorialDim", "hints", "metaIdea", "currencies", "upgrades", "shop", "daily", "profileScreen",
                            "xpPerLevel", "ads", "interstitialEvery", "purchases", "stores", "brandingFromArt", "paletteFromArt",
                            "verifyLevels", "artDir"}) {
        if (params.has(key)) fields.set(key, params[key]);
    }
    auto spec = editor::WizardSpec::fromJson(fields);
    if (!spec.artDir.empty() && spec.artDir != approvedArt) throw std::runtime_error("Choose the artwork folder again");
    const auto catalog = editor::wizardCatalog(engineDir);
    if (spec.genre != "auto") {
        const auto& templates = catalog["templates"].items();
        if (std::none_of(templates.begin(), templates.end(), [&](const auto& item) { return item.getStr("id") == spec.genre; }))
            throw std::runtime_error("Choose a template from the wizard catalog");
    }
    auto checkSelection = [&](const char* field, const std::vector<std::string>& selected) {
        const auto& choices = catalog[field].items();
        for (const auto& id : selected) {
            if (std::none_of(choices.begin(), choices.end(), [&](const auto& option) { return option.str() == id; }))
                throw std::runtime_error(std::string("Unknown wizard option in ") + field);
        }
    };
    checkSelection("languages", spec.languages);
    checkSelection("platforms", spec.platforms);
    checkSelection("stores", spec.stores);
    checkSelection("mapPresets", {spec.mapPreset});
    checkSelection("mapSizes", {spec.mapSize});
    checkSelection("tutorialModes", {spec.tutorial});
    return spec;
}
}
