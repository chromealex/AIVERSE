#pragma once
#include "apps/editor/core/GameWizard.h"
#include <algorithm>
#include <stdexcept>

namespace aiverse::studio {
// Only a path selected through the native artwork picker can be used as a content source.
inline editor::WizardSpec studioWizardSpec(const std::string& engineDir, const Json& params, const std::string& approvedArt) {
    editor::WizardSpec spec;
    spec.title = params.getStr("title", spec.title);
    spec.idea = params.getStr("idea");
    spec.genre = params.getStr("genre", spec.genre);
    if (spec.genre != "auto") {
        const auto catalog = editor::wizardCatalog(engineDir);
        const auto& templates = catalog["templates"].items();
        if (std::none_of(templates.begin(), templates.end(), [&](const auto& item) { return item.getStr("id") == spec.genre; }))
            throw std::runtime_error("Choose a template from the wizard catalog");
    }
    for (const auto& feature : params["features"].items()) spec.features.push_back(feature.str());
    spec.orientation = params.getStr("orientation", spec.orientation);
    spec.tutorial = params.getStr("tutorial", spec.tutorial);
    spec.mapPreset = params.getStr("mapPreset", spec.mapPreset);
    spec.levels = int(std::clamp<int64_t>(params.getInt("levels", spec.levels), 0, 40));
    spec.artDir = params.getStr("artDir");
    if (!spec.artDir.empty() && spec.artDir != approvedArt) throw std::runtime_error("Choose the artwork folder again");
    spec.verifyLevels = params.getBool("verifyLevels", spec.verifyLevels);
    return spec;
}
}
