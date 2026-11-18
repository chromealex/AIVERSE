#pragma once
// PRISMA typed, GPU-free dispatch contract for compiled RenderGraph plans.
// The host owns RHI allocation, transitions and encoder lifetime. This layer
// validates bindings and dispatches passes in compiled order; it does NOT issue GPU commands.
#include "PrismaRenderGraph.h"
#include <cstddef>
#include <string>
#include <vector>

namespace aiverse::render {

template <typename Handle>
struct PrismaGraphBindings {
    // Exactly one entry per graph resource; external resources use this array,
    // while internal resources resolve through plan.resources[id].slot.
    std::vector<Handle> external;
    // One host-owned allocation for each unique internal physical slot.
    std::vector<Handle> slots;
};

template <typename Handle>
struct PrismaGraphDispatchView {
    const PrismaRenderGraph& graph;
    const PrismaGraphPlan& plan;
    const PrismaGraphBindings<Handle>& bindings;

    Handle resource(PrismaGraphId id) const {
        if (id >= graph.resources().size()) return Handle{};
        if (graph.resources()[id].external) return bindings.external[id];
        const int slot = plan.resources[id].slot;
        return slot < 0 ? Handle{} : bindings.slots[static_cast<size_t>(slot)];
    }
};

struct PrismaGraphDispatchResult {
    bool success = false;
    size_t executedPasses = 0;
    std::string error;
};

// Callback signature: bool(const PrismaGraphPass&, PrismaGraphId passId,
//                          const PrismaGraphDispatchView<Handle>&).
// False aborts dispatch; no pass after the failing pass is invoked.
// Binding validation is complete before the FIRST callback, so a missing
// resource can never cause a partially executed frame.
// Handle{} means invalid; use a nonzero typed handle (or wrapper) from the host.
template <typename Handle, typename Callback>
PrismaGraphDispatchResult dispatchPrismaGraph(
    const PrismaRenderGraph& graph, const PrismaGraphPlan& plan,
    const PrismaGraphBindings<Handle>& bindings, Callback&& callback) {
    PrismaGraphDispatchResult result;
    const auto& resources = graph.resources();
    const auto& passes = graph.passes();
    if (!plan.valid || plan.resources.size() != resources.size() ||
        plan.order.size() != passes.size()) {
        result.error = "PRISMA dispatch: invalid or incompatible compiled plan";
        return result;
    }
    if (bindings.external.size() != resources.size() ||
        bindings.slots.size() != plan.slots.size()) {
        result.error = "PRISMA dispatch: binding array sizes do not match plan";
        return result;
    }
    PrismaGraphDispatchView<Handle> view{graph, plan, bindings};
    for (size_t id = 0; id < resources.size(); ++id) {
        const auto& r = resources[id];
        const int slot = plan.resources[id].slot;
        if (!r.external && (slot < 0 || static_cast<size_t>(slot) >= bindings.slots.size())) {
            result.error = "PRISMA dispatch: missing internal slot for " + r.name;
            return result;
        }
        if (view.resource(static_cast<PrismaGraphId>(id)) == Handle{}) {
            result.error = "PRISMA dispatch: unbound resource " + r.name;
            return result;
        }
    }
    // Defend against stale/mutated plans and repeated IDs.
    std::vector<bool> seen(passes.size(), false);
    for (auto id : plan.order) {
        if (id >= passes.size() || seen[id]) {
            result.error = "PRISMA dispatch: invalid or duplicate pass ID";
            return result;
        }
        seen[id] = true;
        for (auto rid : passes[id].reads)
            if (rid >= resources.size()) {
                result.error = "PRISMA dispatch: invalid read ID";
                return result;
            }
        for (auto rid : passes[id].writes)
            if (rid >= resources.size()) {
                result.error = "PRISMA dispatch: invalid write ID";
                return result;
            }
    }
    for (auto id : plan.order) {
        if (!callback(passes[id], id, view)) {
            result.error = "PRISMA dispatch: callback failed for " + passes[id].name;
            return result;
        }
        ++result.executedPasses;
    }
    result.success = true;
    return result;
}

} // namespace aiverse::render
