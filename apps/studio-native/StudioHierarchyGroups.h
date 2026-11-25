#pragma once
#include <algorithm>
#include "apps/editor/core/WorldInspect.h"

namespace aiverse::studio {
// Keep project entries first so existing bridge callers retain their indices.
inline editor::WorldConfig studioHierarchyConfig(const Json& manifest) {
    auto config = editor::parseWorldConfig(manifest);
    for (const editor::WorldConfig::Grouping& builtin : {
             editor::WorldConfig::Grouping{"Category → type", {"category", "type"}},
             editor::WorldConfig::Grouping{"Component", {"component"}},
             editor::WorldConfig::Grouping{"Origin → type", {"origin", "type"}}}) {
        if (std::none_of(config.groupings.begin(), config.groupings.end(),
                         [&](const auto& group) { return group.by == builtin.by; }))
            config.groupings.push_back(builtin);
    }
    return config;
}

// An entity belongs to every ECS component it owns. Other keys are single-valued.
template<class Visit>
void visitHierarchyLabels(const editor::WorldModel& world, const game::Rules& rules,
                          const editor::WorldRow& row, const std::string& key, Visit visit) {
    if (key == "component" && row.comps.any()) {
        row.eachComp([&](int i) { visit(editor::worldComponents()[size_t(i)].title); });
    } else {
        visit(editor::worldGroupValue(world, rules, row, key));
    }
}
}
