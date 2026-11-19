// PHYSIS Stage 16 — read-only spatial acceleration for wind, gravity and storms.
// Index owns a copy-on-write ECS snapshot. Never serialized; rebuild at tick
// boundaries or after world edits/rollback. No Box3D, Terra, Water or genres.
#pragma once
#include "PhysisPresentation.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::physis {

struct FieldIndexSettings {
    std::int32_t tileSizeMeters = 256;
    std::uint32_t maxTilesPerVolume = 1024; // larger volumes use a global list
    // Per field type: prevents unbounded total bucket memory when MANY small
    // overlapping volumes are present (per-volume limits alone are insufficient).
    std::uint32_t maxTotalBucketReferences = 65536;
};

struct FieldIndexStats {
    std::size_t windZones = 0;
    std::size_t shelters = 0;
    std::size_t gravityFields = 0;
    std::size_t storms = 0;
    std::size_t bucketReferences = 0;
    std::size_t globalReferences = 0;
};

inline std::int64_t indexFloorDiv(std::int64_t value, std::int64_t divisor) {
    const auto q = value / divisor;
    return q - (value % divisor < 0 ? 1 : 0);
}

// Generic 2D X/Z uniform buckets. Final Y/radius/time checks remain in the
// exact existing PHYSIS samplers. Field insertion and traversal retain ECS
// iteration order; this is important for bit-exact Fixed additions.
template<class T>
class FieldBuckets {
    struct Entry {
        Entity entity{};
        T data{};
    };
    using Key = std::pair<std::int64_t, std::int64_t>;
    std::vector<Entry> entries_;
    std::map<Key, std::vector<std::size_t>> buckets_;
    std::vector<std::size_t> global_;
    std::size_t bucketReferenceCount_ = 0;
public:
    std::size_t size() const { return entries_.size(); }
    std::size_t bucketReferences() const { return bucketReferenceCount_; }
    std::size_t globalReferences() const { return global_.size(); }

    void add(Entity entity, const T& data, Vec3 center, Fixed radius,
             bool global, const FieldIndexSettings& settings) {
        // No need to store ineffective fields. Other PHYSIS samplers likewise
        // skip negative/nonpositive radii unless explicitly global.
        if (!global && radius <= Fixed{}) return;
        const auto ordinal = entries_.size();
        entries_.push_back({entity, data});
        if (global) {
            global_.push_back(ordinal);
            return;
        }
        const auto span = std::int64_t(settings.tileSizeMeters) * Fixed::kOne;
        const auto limit = std::numeric_limits<std::int64_t>::max();
        const auto minimum = std::numeric_limits<std::int64_t>::min();
        // Saturated/overflow-prone bounds are kept in the global fallback,
        // never silently culled. PHYSIS's precise radial check stays authoritative.
        const auto boundOverflows = [&](std::int64_t coordinate) {
            // Compute max-coordinate / coordinate-min only in the sign range
            // where subtraction is defined (never max - negative int64).
            return (coordinate > 0 && radius.raw > limit - coordinate) ||
                   (coordinate < 0 && radius.raw > coordinate - minimum);
        };
        if (boundOverflows(center.x.raw) || boundOverflows(center.z.raw)) {
            global_.push_back(ordinal);
            return;
        }
        const auto minX = indexFloorDiv(center.x.raw - radius.raw, span);
        const auto maxX = indexFloorDiv(center.x.raw + radius.raw, span);
        const auto minZ = indexFloorDiv(center.z.raw - radius.raw, span);
        const auto maxZ = indexFloorDiv(center.z.raw + radius.raw, span);
        const auto nx = std::uint64_t(maxX - minX) + 1;
        const auto nz = std::uint64_t(maxZ - minZ) + 1;
        // Short-circuit before multiplication; malformed large world ranges
        // must not overflow uint64_t and evade the memory safety budget.
        if (nx > settings.maxTilesPerVolume ||
            nz > settings.maxTilesPerVolume) {
            global_.push_back(ordinal);
            return;
        }
        const auto count = nx * nz; // both already bounded <= 4096
        if (count > settings.maxTilesPerVolume ||
            count > settings.maxTotalBucketReferences - bucketReferenceCount_) {
            // Reference semantics win over memory budget: always visit this
            // field through the stable global list rather than dropping it.
            global_.push_back(ordinal);
            return;
        }
        for (auto z = minZ; z <= maxZ; ++z)
            for (auto x = minX; x <= maxX; ++x)
                buckets_[{x, z}].push_back(ordinal);
        bucketReferenceCount_ += static_cast<std::size_t>(count);
    }

    template<class Visitor>
    void at(Vec3 position, std::int64_t tileSpanRaw, Visitor&& callback) const {
        const Key key{indexFloorDiv(position.x.raw, tileSpanRaw),
                      indexFloorDiv(position.z.raw, tileSpanRaw)};
        const auto found = buckets_.find(key);
        const std::vector<std::size_t>* local =
            found == buckets_.end() ? nullptr : &found->second;
        std::size_t i = 0, j = 0;
        // Merge two already-sorted ordinal streams; all global and local
        // entries are disjoint, so no duplicate wind/force contribution.
        while (i < global_.size() || (local && j < local->size())) {
            const bool takeGlobal =
                !local || j >= local->size() ||
                (i < global_.size() && global_[i] < (*local)[j]);
            const auto ordinal = takeGlobal ? global_[i++] : (*local)[j++];
            const auto& value = entries_[ordinal];
            callback(value.entity, value.data);
        }
    }

    std::size_t candidates(Vec3 position, std::int64_t tileSpanRaw) const {
        const Key key{indexFloorDiv(position.x.raw, tileSpanRaw),
                      indexFloorDiv(position.z.raw, tileSpanRaw)};
        auto it = buckets_.find(key);
        return global_.size() + (it == buckets_.end() ? 0 : it->second.size());
    }
};

class FieldSpatialIndex {
    World snapshot_;
    FieldIndexSettings settings_{};
    std::int64_t tileSpanRaw_ = 0;
    FieldBuckets<WindZone> wind_;
    FieldBuckets<WindShelter> shelter_;
    FieldBuckets<GravityField> gravity_;
    FieldBuckets<StormEvent> storm_;
public:
    explicit FieldSpatialIndex(const World& world, FieldIndexSettings config = {})
        : snapshot_(world), settings_(config) {
        if (config.tileSizeMeters <= 0 || config.tileSizeMeters > 1000000 ||
            config.maxTilesPerVolume == 0 || config.maxTilesPerVolume > 4096 ||
            config.maxTotalBucketReferences == 0 ||
            config.maxTotalBucketReferences > 1048576)
            throw std::invalid_argument("PHYSIS spatial index settings");
        tileSpanRaw_ = std::int64_t(config.tileSizeMeters) * Fixed::kOne;
        snapshot_.eachExistingConst<WindZone>([&](Entity e, const WindZone& v) {
            wind_.add(e, v, v.center, v.radiusMeters, false, settings_);
        });
        snapshot_.eachExistingConst<WindShelter>([&](Entity e, const WindShelter& v) {
            shelter_.add(e, v, v.center, v.radiusMeters, false, settings_);
        });
        snapshot_.eachExistingConst<GravityField>([&](Entity e, const GravityField& v) {
            if (!v.enabled) return;
            const bool global = v.mode == GravityMode::Uniform && v.radiusMeters == Fixed{};
            gravity_.add(e, v, v.center, v.radiusMeters, global, settings_);
        });
        snapshot_.eachExistingConst<StormEvent>([&](Entity e, const StormEvent& v) {
            if (!v.enabled || v.outerRadiusMeters <= v.eyeRadiusMeters ||
                v.eyeRadiusMeters <= Fixed{} || v.heightMeters <= Fixed{}) return;
            storm_.add(e, v, v.center, v.outerRadiusMeters, false, settings_);
        });
    }

    const World& world() const { return snapshot_; }
    FieldIndexStats stats() const {
        return {wind_.size(), shelter_.size(), gravity_.size(), storm_.size(),
                wind_.bucketReferences() + shelter_.bucketReferences() +
                gravity_.bucketReferences() + storm_.bucketReferences(),
                wind_.globalReferences() + shelter_.globalReferences() +
                gravity_.globalReferences() + storm_.globalReferences()};
    }
    std::size_t candidateCount(Vec3 position) const {
        return wind_.candidates(position, tileSpanRaw_) +
               shelter_.candidates(position, tileSpanRaw_) +
               gravity_.candidates(position, tileSpanRaw_) +
               storm_.candidates(position, tileSpanRaw_);
    }

    Vec3 windAt(Vec3 position) const {
        return windAtWith(snapshot_, position,
            [&](auto&& callback) { wind_.at(position, tileSpanRaw_, callback); },
            [&](auto&& callback) { shelter_.at(position, tileSpanRaw_, callback); });
    }

    Vec3 gravityAt(Vec3 position) const {
        return gravityAtWith(snapshot_, position,
            [&](auto&& callback) { gravity_.at(position, tileSpanRaw_, callback); });
    }

    StormInfluence stormsAt(Vec3 position) const {
        const auto* state = snapshot_.resourceConst<State>();
        const auto tick = state ? state->tick : std::uint64_t(0);
        return stormsAtWith(snapshot_, position, tick,
            [&](auto&& callback) { storm_.at(position, tileSpanRaw_, callback); });
    }

    Vec3 windWithStormsAt(Vec3 position) const {
        const Vec3 base = windAt(position);
        if (!snapshot_.resourceConst<State>()) return base;
        const Vec3 swirl = stormsAt(position).wind;
        const Fixed cap = Fixed::fromInt(200);
        return {fxClamp(base.x + swirl.x, -cap, cap),
                fxClamp(base.y + swirl.y, -cap, cap),
                fxClamp(base.z + swirl.z, -cap, cap)};
    }

    // Same copied, read-only 3D payload as sampleNatureField(World,...).
    // Returned values stay pinned to this snapshot even when the source World
    // changes or rolls back. Rebuild the index to observe new ECS state.
    bool sample(Vec3 position, NatureFieldSample& output) const {
        const auto* state = snapshot_.resourceConst<State>();
        if (!state) return false;
        NatureFieldSample result;
        result.position = position;
        result.tick = state->tick;
        result.windVelocity = windWithStormsAt(position);
        result.gravityAcceleration = gravityAt(position);
        result.storms = stormsAt(position);
        const Entity cellEntity = cellAtPosition(snapshot_, position);
        if (const auto* coord = snapshot_.tryGetConst<Cell>(cellEntity)) {
            result.hasAtmosphere = environmentAt(snapshot_, coord->x, coord->z,
                                                  result.environment);
            if (const auto* atmosphere = snapshot_.tryGetConst<Atmosphere>(cellEntity))
                result.pressureHpa = atmosphere->pressure;
            if (const auto* hazard = snapshot_.tryGetConst<HazardCell>(cellEntity)) {
                result.hazards = *hazard;
                result.hasHazards = true;
            }
        }
        output = result;
        return true;
    }
};

} // namespace aiverse::physis
