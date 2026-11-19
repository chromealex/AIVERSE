// PHYSIS Stage 23: atomic tick-boundary reconciliation of authored ECS fields.
// Host owns this registry alongside its Simulation snapshots and save/replay.
// Call only at a simulation barrier, never during World iteration or rendering.
#pragma once
#include "PhysisFieldAssets.h"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::physis {

struct RuntimeFieldBinding {
    std::string id;
    AuthoredFieldKind kind{};
    Entity entity{};
};

struct RuntimeFieldRegistry {
    std::vector<RuntimeFieldBinding> fields; // stable ID order
};

inline void io(Archive& archive, RuntimeFieldBinding& binding) {
    archive(binding.id, binding.kind, binding.entity);
}
inline void io(Archive& archive, RuntimeFieldRegistry& registry) {
    archive(registry.fields);
}

inline void validateRuntimeBindings(const World& world,
                                    const RuntimeFieldRegistry& registry) {
    for (std::size_t i = 0; i < registry.fields.size(); ++i) {
        const auto& b = registry.fields[i];
        if (b.id.empty() || (i && registry.fields[i-1].id >= b.id))
            throw std::invalid_argument("PHYSIS runtime bindings must have unique sorted IDs");
        if (!world.alive(b.entity))
            throw std::invalid_argument("PHYSIS runtime field entity is not alive");
        const bool valid =
            b.kind == AuthoredFieldKind::Wind ? world.has<WindZone>(b.entity) :
            b.kind == AuthoredFieldKind::Storm ? world.has<StormEvent>(b.entity) :
            b.kind == AuthoredFieldKind::Gravity ? world.has<GravityField>(b.entity) : false;
        if (!valid) throw std::invalid_argument("PHYSIS runtime field component mismatch");
    }
}

inline void replaceRuntimeField(World& world, Entity entity, const AuthoredField& asset) {
    // Existing same-kind entities retain their identity and unrelated components.
    switch (asset.kind) {
        case AuthoredFieldKind::Wind:
            world.add<WindZone>(entity, std::get<WindZone>(asset.data)); break;
        case AuthoredFieldKind::Storm:
            world.add<StormEvent>(entity, std::get<StormEvent>(asset.data)); break;
        case AuthoredFieldKind::Gravity:
            world.add<GravityField>(entity, std::get<GravityField>(asset.data)); break;
    }
}

// All-or-nothing on failure: stage both ECS and ID registry, then publish.
// Existing fields with matching ID/kind retain Entity IDs; type changes replace
// the entity. The host MUST snapshot registry together with World for rollback.
inline void reconcileRuntimeFields(World& world, RuntimeFieldRegistry& registry,
                                   const std::vector<AuthoredField>& desired) {
    validateRuntimeBindings(world, registry);
    std::vector<const AuthoredField*> sorted;
    sorted.reserve(desired.size());
    for (const auto& asset : desired) {
        if (asset.id.empty()) throw std::invalid_argument("PHYSIS empty authored field ID");
        sorted.push_back(&asset);
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto* a, const auto* b) {
        return a->id < b->id;
    });
    for (std::size_t i = 1; i < sorted.size(); ++i)
        if (sorted[i-1]->id == sorted[i]->id)
            throw std::invalid_argument("PHYSIS duplicate authored field ID");

    World staged(world);
    RuntimeFieldRegistry next;
    next.fields.reserve(sorted.size());
    for (const auto& old : registry.fields) {
        const auto it = std::lower_bound(sorted.begin(), sorted.end(), old.id,
            [](const AuthoredField* a, const std::string& id) { return a->id < id; });
        if (it == sorted.end() || (*it)->id != old.id || (*it)->kind != old.kind)
            staged.destroy(old.entity);
    }
    for (const auto* asset : sorted) {
        const auto it = std::lower_bound(registry.fields.begin(), registry.fields.end(),
            asset->id, [](const RuntimeFieldBinding& a, const std::string& id) {
                return a.id < id;
            });
        const Entity entity = (it != registry.fields.end() &&
            it->id == asset->id && it->kind == asset->kind)
                ? it->entity : staged.create();
        replaceRuntimeField(staged, entity, *asset);
        next.fields.push_back({asset->id, asset->kind, entity});
    }
    // World move and registry swap are the publication barrier.
    world = std::move(staged);
    registry.fields.swap(next.fields);
}

// Opt-in rollback-owned registry: World snapshots now include authored bindings.
// Caller must register RuntimeFieldRegistry with persistent ECS for file saves.
inline void reconcileTrackedRuntimeFields(
    World& world, const std::vector<AuthoredField>& desired) {
    RuntimeFieldRegistry current;
    if (const auto* existing = world.resourceConst<RuntimeFieldRegistry>())
        current = *existing;
    // Stage BOTH resource and component mutations in a separate World.
    World staged(world);
    reconcileRuntimeFields(staged, current, desired);
    staged.setResource<RuntimeFieldRegistry>(std::move(current));
    world = std::move(staged);
}

} // namespace aiverse::physis
