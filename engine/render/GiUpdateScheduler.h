// GI invalidation scheduler (presentation only). No ECS authoritative state and no GPU calls.
// A stable probe lattice can be shared by static caches, dynamic relighting and world-edit adapters.
#pragma once
#include "Math3D.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace aiverse::render {

enum class GiDirtyReason : uint8_t {
    Lighting = 1, Geometry = 2, Material = 4, Environment = 8
};
constexpr uint8_t giDirtyBit(GiDirtyReason reason) { return static_cast<uint8_t>(reason); }

struct GiBounds {
    float3 min{}, max{};
};

struct GiProbeTicket {
    uint32_t index = 0;
    uint64_t revision = 0;
    uint8_t reasons = 0;
    float3 position{};
};

// Single-threaded scheduler. ECS systems publish dirty world bounds after their structural
// barrier; the renderer consumes tickets on its thread. No reference to gameplay ownership.
// A ticket is not acknowledged until its new probe coefficients are actually published.
// Invalidating a cell in flight keeps it dirty and prevents stale completion from clearing it.
class GiUpdateScheduler {
public:
    void configure(float3 origin, float spacing, int nx, int ny, int nz) {
        if (!finite(origin) || !std::isfinite(spacing) || spacing < .25f || spacing > 1000.0f ||
            nx < 1 || ny < 1 || nz < 1 || nx > 256 || ny > 256 || nz > 256 ||
            uint64_t(nx) * uint64_t(ny) * uint64_t(nz) > 20000u)
            throw std::invalid_argument("invalid GI probe lattice");
        origin_ = origin;
        spacing_ = spacing;
        nx_ = nx; ny_ = ny; nz_ = nz;
        slots_.assign(size_t(nx_) * size_t(ny_) * size_t(nz_), Slot{});
        serial_ = 0;
        batches_ = 0;
        invalidateAll(GiDirtyReason::Geometry);
    }

    bool configured() const { return !slots_.empty(); }
    size_t size() const { return slots_.size(); }
    size_t pending() const {
        size_t count = 0;
        for (const auto& slot : slots_) if (slot.reasons != 0) ++count;
        return count;
    }
    size_t inFlight() const {
        size_t count = 0;
        for (const auto& slot : slots_) if (slot.inFlight) ++count;
        return count;
    }

    // Use after loading an already up-to-date GI cache. Invalidate outstanding tickets.
    void resetClean() {
        for (auto& slot : slots_) {
            slot.reasons = 0;
            slot.inFlight = false;
            slot.revision = ++serial_;
        }
    }

    void invalidateAll(GiDirtyReason reason) {
        const uint8_t bit = giDirtyBit(reason);
        for (auto& slot : slots_) mark(slot, bit);
    }

    // influenceRadius extends the affected AABB by the light's influence radius or
    // a conservative transport radius; never assume only the edited voxel is affected.
    size_t invalidate(GiBounds bounds, GiDirtyReason reason, float influenceRadius = 0,
                      std::vector<uint32_t>* affectedIndices = nullptr) {
        if (!finite(bounds.min) || !finite(bounds.max) ||
            bounds.min.x > bounds.max.x || bounds.min.y > bounds.max.y || bounds.min.z > bounds.max.z ||
            !std::isfinite(influenceRadius) || influenceRadius < 0)
            throw std::invalid_argument("invalid GI dirty bounds");
        if (!configured()) return 0;

        const float3 lower{bounds.min.x - influenceRadius, bounds.min.y - influenceRadius, bounds.min.z - influenceRadius};
        const float3 upper{bounds.max.x + influenceRadius, bounds.max.y + influenceRadius, bounds.max.z + influenceRadius};
        int a[3]{}, b[3]{};
        const float lo[3] = {lower.x, lower.y, lower.z};
        const float hi[3] = {upper.x, upper.y, upper.z};
        const float base[3] = {origin_.x, origin_.y, origin_.z};
        const int dims[3] = {nx_, ny_, nz_};
        for (int axis = 0; axis < 3; ++axis) {
            // Probe i is at origin + (i + 0.5) * spacing.
            const double first = (double(lo[axis]) - base[axis]) / spacing_ - 0.5;
            const double last = (double(hi[axis]) - base[axis]) / spacing_ - 0.5;
            if (last < 0 || first > dims[axis] - 1) return 0;
            a[axis] = int(std::max(0.0, std::ceil(first)));
            b[axis] = int(std::min(double(dims[axis] - 1), std::floor(last)));
            if (a[axis] > b[axis]) return 0;
        }

        size_t affected = 0;
        for (int z = a[2]; z <= b[2]; ++z)
            for (int y = a[1]; y <= b[1]; ++y)
                for (int x = a[0]; x <= b[0]; ++x) {
                    const auto id = uint32_t(index(x, y, z));
                    mark(slots_[id], giDirtyBit(reason));
                    if (affectedIndices) affectedIndices->push_back(id);
                    ++affected;
                }
        return affected;
    }

    // Camera-priority for three batches, oldest-first for the fourth to prevent
    // a continuously changing near field from starving remote dirty cells.
    // All tie breaks are stable lattice indices, regardless of container iteration.
    std::vector<GiProbeTicket> claim(size_t budget, float3 focus) {
        if (!finite(focus)) throw std::invalid_argument("nonfinite GI focus");
        std::vector<GiProbeTicket> result;
        if (budget == 0 || !configured()) return result;
        std::vector<uint32_t> candidates;
        candidates.reserve(slots_.size());
        for (uint32_t i = 0; i < slots_.size(); ++i)
            if (slots_[i].reasons && !slots_[i].inFlight) candidates.push_back(i);
        if (candidates.empty()) return result;
        const bool oldestFirst = ((batches_++ & 3u) == 3u);
        const auto distance2 = [&](uint32_t index) {
            const auto p = position(index);
            const double dx = double(p.x) - focus.x, dy = double(p.y) - focus.y, dz = double(p.z) - focus.z;
            return dx * dx + dy * dy + dz * dz;
        };
        const auto closer = [&](uint32_t a, uint32_t b) {
            if (oldestFirst && slots_[a].revision != slots_[b].revision)
                return slots_[a].revision < slots_[b].revision;
            const double da = distance2(a), db = distance2(b);
            return da == db ? a < b : da < db;
        };
        const size_t count = std::min(budget, candidates.size());
        if (count < candidates.size())
            std::partial_sort(candidates.begin(), candidates.begin() + count, candidates.end(), closer);
        else
            std::sort(candidates.begin(), candidates.end(), closer);
        result.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            const uint32_t id = candidates[i];
            auto& slot = slots_[id];
            slot.inFlight = true;
            result.push_back({id, slot.revision, slot.reasons, position(id)});
        }
        return result;
    }

    // False means the ticket was stale, or was already acknowledged/aborted.
    // A stale completion releases its slot but deliberately keeps all dirty bits.
    bool complete(const GiProbeTicket& ticket) {
        if (ticket.index >= slots_.size()) return false;
        auto& slot = slots_[ticket.index];
        if (!slot.inFlight) return false;
        slot.inFlight = false;
        if (slot.revision != ticket.revision) return false;
        slot.reasons = 0;
        return true;
    }

    bool abort(const GiProbeTicket& ticket) {
        if (ticket.index >= slots_.size() || !slots_[ticket.index].inFlight) return false;
        slots_[ticket.index].inFlight = false;
        return true;
    }

    float3 position(uint32_t i) const {
        if (i >= slots_.size()) throw std::out_of_range("GI probe index");
        const int x = int(i % uint32_t(nx_));
        const int y = int((i / uint32_t(nx_)) % uint32_t(ny_));
        const int z = int(i / uint32_t(nx_ * ny_));
        return origin_ + float3{float(x) + .5f, float(y) + .5f, float(z) + .5f} * spacing_;
    }

private:
    struct Slot { uint64_t revision = 0; uint8_t reasons = 0; bool inFlight = false; };
    static bool finite(float3 v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }
    void mark(Slot& slot, uint8_t bit) {
        slot.reasons = uint8_t(slot.reasons | bit);
        slot.revision = ++serial_;
    }
    size_t index(int x, int y, int z) const {
        return size_t(x) + size_t(nx_) * (size_t(y) + size_t(ny_) * size_t(z));
    }

    float3 origin_{};
    float spacing_ = 1;
    int nx_ = 0, ny_ = 0, nz_ = 0;
    uint64_t serial_ = 0, batches_ = 0;
    std::vector<Slot> slots_;
};

} // namespace aiverse::render
