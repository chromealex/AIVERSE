// Priority-based budget allocation for presentation ECS AnimationGraph poses.
// Assignment is frame-local, deterministic under equal scores, genre-agnostic.
#pragma once
#include "EcsAnimationSystems.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace aiverse::lib::animation {
struct AnimationSignificanceComponent {
    // Supplied by camera/visibility host. Approximate on-screen object height.
    float pixels = 0.f;
    float importance = 1.f; // e.g. hero = 10, background = 0.5
    bool visible = true;
    bool alwaysEvaluate = false; // cinematics / player / hand attachments
};
struct AnimationBudgetPolicy {
    size_t fullRateSlots = 64;
    size_t mediumRateSlots = 192;
    float mediumInterval = 1.f/30.f;
    float lowInterval = .1f;
    float hiddenInterval = 1.e9f;
};
struct AnimationBudgetStats {
    size_t full = 0, medium = 0, low = 0, hidden = 0, forced = 0;
};
// Call once after camera visibility and before stepAnimationPresentation.
// For unannotated entities, leave their existing AnimationBudget untouched.
// No allocation to gameplay simulation, damage, hitboxes or replication.
inline AnimationBudgetStats assignAnimationBudgets(
        World& presentation, const AnimationBudgetPolicy& policy={}) {
    struct Candidate { Entity entity; float significance; bool force; };
    AnimationBudgetStats stats;
    if (!(std::isfinite(policy.mediumInterval) &&
          policy.mediumInterval>0.f &&
          std::isfinite(policy.lowInterval) &&
          policy.lowInterval>=policy.mediumInterval &&
          std::isfinite(policy.hiddenInterval) &&
          policy.hiddenInterval>=policy.lowInterval))
        return stats;
    const auto* source=presentation.poolIfExists<AnimationSignificanceComponent>();
    if (!source) return stats;
    auto* budgets=presentation.prepareExistingWrite<AnimationBudgetComponent>();
    if (!budgets) return stats;
    std::vector<Candidate> visible;
    visible.reserve(source->size());
    for (size_t i=0;i<source->size();++i) {
        const Entity e=source->entityAt(i);
        if (!presentation.alive(e) || !budgets->has(e.index)) continue;
        const auto& s=source->dataAt(i);
        auto* budget=budgets->tryGet(e.index);
        if (!s.visible) {
            budget->visible=false;
            budget->interval=policy.hiddenInterval;
            ++stats.hidden;
            continue;
        }
        budget->visible=true;
        const float pixels=std::isfinite(s.pixels)?std::max(0.f,s.pixels):0.f;
        const float importance=std::isfinite(s.importance)?std::max(0.f,s.importance):0.f;
        visible.push_back({e,pixels*importance,s.alwaysEvaluate});
    }
    std::stable_sort(visible.begin(),visible.end(),
        [](const Candidate& a,const Candidate& b) {
            if (a.force!=b.force) return a.force>b.force;
            if (a.significance!=b.significance) return a.significance>b.significance;
            return a.entity.packed()<b.entity.packed();
        });
    size_t fullAllocated=0,mediumAllocated=0;
    for (const auto& c:visible) {
        auto* budget=budgets->tryGet(c.entity.index);
        if (!budget) continue;
        if (c.force) {
            budget->interval=0.f;
            ++stats.full; ++stats.forced;
        } else if (fullAllocated<policy.fullRateSlots) {
            budget->interval=0.f;
            ++stats.full; ++fullAllocated;
        } else if (mediumAllocated<policy.mediumRateSlots) {
            budget->interval=policy.mediumInterval;
            ++stats.medium; ++mediumAllocated;
        } else {
            budget->interval=policy.lowInterval;
            ++stats.low;
        }
    }
    return stats;
}
} // namespace aiverse::lib::animation
