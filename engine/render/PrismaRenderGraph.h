#pragma once
// PRISMA render graph: deterministic, GPU-free render-pass dependency and lifetime planner.
// This is a *planner*, not an RHI executor. Call compile() before allocating GPU targets.
// No FPS, RTS, world state, gfx::Device or gameplay ECS dependencies.
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <queue>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace aiverse::render {

enum class PrismaGraphFormat : uint8_t { RGBA8, RGBA16F, RG16F, R32F, Depth32F };
enum class PrismaGraphPassKind : uint8_t { Raster, Compute, Copy };
using PrismaGraphId = uint32_t;
inline constexpr PrismaGraphId kInvalidPrismaGraphId = std::numeric_limits<PrismaGraphId>::max();

struct PrismaGraphResource {
    std::string name;
    PrismaGraphFormat format = PrismaGraphFormat::RGBA16F;
    uint32_t width = 1, height = 1;
    bool external = false;    // supplied by caller, never allocated/aliased by the graph
    bool history = false;     // owned history persists across frames; external history is caller-managed
    bool allowAlias = true;
};
struct PrismaGraphPass {
    std::string name;
    PrismaGraphPassKind kind = PrismaGraphPassKind::Raster;
    std::vector<PrismaGraphId> reads, writes;
    std::vector<PrismaGraphId> after; // explicit pass ordering, not resource IDs
};
struct PrismaGraphLifetime {
    int first = -1, last = -1; // positions in the *sorted execution order*
    int slot = -1;             // external or unused resources have no internal slot
};
struct PrismaGraphSlot {
    PrismaGraphFormat format = PrismaGraphFormat::RGBA16F;
    uint32_t width = 0, height = 0;
    int lastUse = -1;
    bool dedicated = false;   // history or aliasing-disabled resource
    uint64_t bytes = 0;
};
struct PrismaGraphPlan {
    bool valid = false;
    std::string error;
    std::vector<PrismaGraphId> order; // pass IDs, stable topological order
    std::vector<PrismaGraphLifetime> resources;
    std::vector<PrismaGraphSlot> slots;
    uint64_t bytes = 0;             // peak physical allocations, excluding external targets
};

// Stable IDs: resources and passes never change indices unless clear() is called.
// Unique-writer SSA-style resources make read/write hazards explicit:
// read + write of the *same* resource in one pass is rejected (use ping-pong).
class PrismaRenderGraph {
public:
    PrismaGraphId addResource(PrismaGraphResource r) {
        const auto id = static_cast<PrismaGraphId>(resources_.size());
        resources_.push_back(std::move(r));
        return id;
    }
    PrismaGraphId addPass(PrismaGraphPass p) {
        const auto id = static_cast<PrismaGraphId>(passes_.size());
        passes_.push_back(std::move(p));
        return id;
    }
    void clear() { resources_.clear(); passes_.clear(); }
    const std::vector<PrismaGraphResource>& resources() const { return resources_; }
    const std::vector<PrismaGraphPass>& passes() const { return passes_; }

    PrismaGraphPlan compile() const {
        PrismaGraphPlan plan;
        const size_t nr = resources_.size(), np = passes_.size();
        plan.resources.resize(nr);
        const auto fail = [&](const std::string& msg) {
            PrismaGraphPlan bad;
            bad.resources.resize(nr);
            bad.error = msg;
            return bad;
        };
        if (nr > 65535 || np > 65535) return fail("PRISMA graph: node count exceeds 65535");
        std::unordered_map<std::string, bool> names;
        for (const auto& r : resources_) {
            if (r.name.empty() || !names.emplace(r.name, true).second)
                return fail("PRISMA graph: duplicate/empty resource name '" + r.name + "'");
            if (r.width == 0 || r.height == 0 || r.width > 16384 || r.height > 16384)
                return fail("PRISMA graph: resource '" + r.name + "' has invalid dimensions");
        }
        names.clear();
        for (const auto& p : passes_)
            if (p.name.empty() || !names.emplace(p.name, true).second)
                return fail("PRISMA graph: duplicate/empty pass name '" + p.name + "'");

        std::vector<int> producer(nr, -1);
        // Validate every use and establish exactly one writer per resource.
        for (size_t pi = 0; pi < np; ++pi) {
            const auto& pass = passes_[pi];
            for (auto w : pass.writes) {
                if (w >= nr) return fail("PRISMA graph: pass '" + pass.name + "' writes an invalid resource");
                if (producer[w] >= 0)
                    return fail("PRISMA graph: multiple writers for resource '" + resources_[w].name + "'");
                producer[w] = static_cast<int>(pi);
            }
            for (auto r : pass.reads) {
                if (r >= nr) return fail("PRISMA graph: pass '" + pass.name + "' reads an invalid resource");
                if (std::find(pass.writes.begin(), pass.writes.end(), r) != pass.writes.end())
                    return fail("PRISMA graph: pass '" + pass.name + "' reads and writes '" +
                                resources_[r].name + "' (use a new ping-pong resource)");
            }
        }
        for (size_t ri = 0; ri < nr; ++ri)
            if (producer[ri] < 0 && !resources_[ri].external)
                return fail("PRISMA graph: transient/history resource '" + resources_[ri].name +
                            "' has no producer");

        std::vector<std::vector<PrismaGraphId>> edges(np);
        std::vector<uint32_t> indegree(np, 0);
        auto edge = [&](PrismaGraphId from, PrismaGraphId to) {
            auto& out = edges[from];
            if (std::find(out.begin(), out.end(), to) == out.end()) {
                out.push_back(to);
                ++indegree[to];
            }
        };
        for (size_t pi = 0; pi < np; ++pi) {
            const auto& pass = passes_[pi];
            for (auto dep : pass.after) {
                if (dep >= np || dep == pi)
                    return fail("PRISMA graph: pass '" + pass.name + "' has invalid/self after dependency");
                edge(dep, static_cast<PrismaGraphId>(pi));
            }
            for (auto r : pass.reads) {
                const int writer = producer[r];
                if (writer < 0) {
                    if (!resources_[r].external)
                        return fail("PRISMA graph: pass '" + pass.name + "' reads an unwritten resource");
                } else {
                    edge(static_cast<PrismaGraphId>(writer), static_cast<PrismaGraphId>(pi));
                }
            }
        }

        // Minimal pass ID first: deterministic and stable when independent passes are present.
        std::priority_queue<PrismaGraphId, std::vector<PrismaGraphId>,
                            std::greater<PrismaGraphId>> ready;
        for (size_t pi = 0; pi < np; ++pi) if (indegree[pi] == 0) ready.push(static_cast<PrismaGraphId>(pi));
        while (!ready.empty()) {
            auto id = ready.top(); ready.pop();
            plan.order.push_back(id);
            for (auto next : edges[id]) if (--indegree[next] == 0) ready.push(next);
        }
        if (plan.order.size() != np) return fail("PRISMA graph: cycle detected in pass dependencies");

        for (size_t orderIndex = 0; orderIndex < np; ++orderIndex) {
            const auto& p = passes_[plan.order[orderIndex]];
            const auto use = [&](PrismaGraphId id) {
                auto& l = plan.resources[id];
                if (l.first < 0) l.first = static_cast<int>(orderIndex);
                l.last = static_cast<int>(orderIndex);
            };
            for (auto id : p.reads) use(id);
            for (auto id : p.writes) use(id);
        }

        // Reuse targets only if their lifetimes do not overlap and their
        // dimensions/format match exactly. Persistent history gets its own slot.
        std::vector<PrismaGraphId> used;
        for (size_t ri = 0; ri < nr; ++ri)
            if (!resources_[ri].external && plan.resources[ri].first >= 0)
                used.push_back(static_cast<PrismaGraphId>(ri));
        std::stable_sort(used.begin(), used.end(), [&](auto a, auto b) {
            return plan.resources[a].first < plan.resources[b].first;
        });
        for (auto ri : used) {
            const auto& r = resources_[ri];
            const auto& life = plan.resources[ri];
            int slot = -1;
            if (r.allowAlias && !r.history) {
                for (size_t si = 0; si < plan.slots.size(); ++si) {
                    const auto& s = plan.slots[si];
                    if (!s.dedicated && s.lastUse < life.first && s.format == r.format &&
                        s.width == r.width && s.height == r.height) {
                        slot = static_cast<int>(si);
                        break;
                    }
                }
            }
            if (slot < 0) {
                slot = static_cast<int>(plan.slots.size());
                const uint64_t pixelBytes = bytesPerPixel(r.format);
                const uint64_t bytes = uint64_t(r.width) * uint64_t(r.height) * pixelBytes;
                plan.slots.push_back({r.format, r.width, r.height, life.last,
                                      r.history || !r.allowAlias, bytes});
                plan.bytes += bytes;
            } else {
                plan.slots[slot].lastUse = life.last;
            }
            plan.resources[ri].slot = slot;
        }
        plan.valid = true;
        return plan;
    }

    static constexpr uint32_t bytesPerPixel(PrismaGraphFormat f) {
        switch (f) {
            case PrismaGraphFormat::RGBA8: return 4;
            case PrismaGraphFormat::RGBA16F: return 8;
            case PrismaGraphFormat::RG16F: return 4;
            case PrismaGraphFormat::R32F: return 4;
            case PrismaGraphFormat::Depth32F: return 4;
        }
        return 0;
    }

private:
    std::vector<PrismaGraphResource> resources_;
    std::vector<PrismaGraphPass> passes_;
};
} // namespace aiverse::render
