#include "StrataIndexResidency.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace aiverse::render::strata {
namespace {
bool fail(std::string* error, const char* why) {
    if (error) *error = why;
    return false;
}
constexpr size_t kMaxBudgetBytes = 64u * 1024u * 1024u;

bool validGeometry(const Asset& g) {
    if (!validate(g.nodes, g.roots) ||
        g.bounds.size() != g.nodes.size() || g.indices.empty())
        return false;
    for (size_t i = 0; i < g.nodes.size(); ++i) {
        const auto& n = g.nodes[i];
        if (!n.resident || n.indexCount > g.indices.size() ||
            n.firstIndex > g.indices.size() - n.indexCount)
            return false;
        for (unsigned axis = 0; axis < 3; ++axis)
            if (!std::isfinite(g.bounds[i].min[axis]) ||
                !std::isfinite(g.bounds[i].max[axis]) ||
                g.bounds[i].min[axis] > g.bounds[i].max[axis])
                return false;
    }
    return true;
}
} // namespace

std::vector<SceneIndexBudgetGrant> apportionSceneIndexBudget(
        std::vector<SceneIndexBudgetRequest> requests,
        uint64_t sceneBudgetBytes) {
    // Admit as many pinned-root sources as possible under pressure.
    // Stable identity ordering makes load repeatable across sessions.
    std::stable_sort(requests.begin(), requests.end(),
        [](const SceneIndexBudgetRequest& a,
           const SceneIndexBudgetRequest& b) {
            return a.rootBytes==b.rootBytes
                ? a.key<b.key : a.rootBytes<b.rootBytes;
        });
    std::vector<SceneIndexBudgetGrant> grants;
    grants.reserve(requests.size());
    uint64_t remaining=sceneBudgetBytes;
    for(const auto& request:requests){
        uint64_t allocated=0;
        if(!request.key.empty() && request.rootBytes &&
           request.rootBytes<=request.maxBytes){
            if(!sceneBudgetBytes)
                allocated=request.maxBytes;
            else if(request.rootBytes<=remaining){
                allocated=request.rootBytes;
                remaining-=request.rootBytes;
            }
        }
        grants.push_back({request.key,allocated});
    }
    if(!sceneBudgetBytes)return grants;
    uint64_t headroom=0;
    for(size_t i=0;i<requests.size();++i){
        if(!grants[i].bytes)continue;
        const uint64_t room=requests[i].maxBytes-grants[i].bytes;
        headroom=room>UINT64_MAX-headroom ? UINT64_MAX : headroom+room;
    }
    const uint64_t spare=remaining;
    if(headroom)
        for(size_t i=0;i<requests.size();++i){
            if(!grants[i].bytes)continue;
            const uint64_t room=requests[i].maxBytes-grants[i].bytes;
            // Bound the floating-point intermediate BEFORE conversion to
            // uint64; giant external budgets must not cause UB.
            const long double scaled=
                (static_cast<long double>(spare)*
                 static_cast<long double>(room))/
                static_cast<long double>(headroom);
            const uint64_t share=scaled>=static_cast<long double>(room)
                ? room : uint64_t(scaled);
            const uint64_t added=std::min(remaining,share);
            grants[i].bytes+=added;
            remaining-=added;
        }
    // Distribute integer rounding leftovers, never exceeding a cap.
    for(size_t i=0;i<requests.size() && remaining;++i){
        if(!grants[i].bytes)continue;
        const uint64_t room=requests[i].maxBytes-grants[i].bytes;
        const uint64_t added=std::min(remaining,room);
        grants[i].bytes+=added;
        remaining-=added;
    }
    return grants;
}

bool IndexResidency::initialize(gfx::Device& device, const ModelAsset& asset,
                                size_t budgetBytes, std::string* error,
                                bool preferIncremental, size_t uploadBudgetBytes,
                                std::shared_ptr<DiskIndexPages> diskPages) {
    if (error) error->clear();
    if (device_ || asset_ || buffer_.valid())
        return fail(error, "Strata index residency is already initialized");
    if (!device.gpuHealthy() || asset.primitives.empty() ||
        !asset.skipped.empty() || budgetBytes < sizeof(uint32_t) * 3 ||
        budgetBytes > kMaxBudgetBytes)
        return fail(error, "invalid Strata index residency budget or asset");

    std::vector<std::vector<uint32_t>> parents;
    parents.reserve(asset.primitives.size());
    uint64_t rootsBytes = 0;
    for (const auto& entry : asset.primitives) {
        const auto& g = entry.geometry;
        if (!validGeometry(g)) return fail(error, "invalid Strata index residency hierarchy");
        std::vector<uint32_t> p(g.nodes.size(), invalidNode);
        for (size_t id = 0; id < g.nodes.size(); ++id) {
            const auto& n = g.nodes[id];
            const auto& bounds = g.bounds[id];
            for (uint32_t k = 0; k < n.childCount; ++k) {
                const uint32_t child = n.firstChild + k;
                if (p[child] != invalidNode)
                    return fail(error, "duplicate Strata parent");
                p[child] = uint32_t(id);
                if (n.geometricError < g.nodes[child].geometricError)
                    return fail(error, "nonconservative streaming parent LOD error");
                for (unsigned axis = 0; axis < 3; ++axis)
                    if (bounds.min[axis] > g.bounds[child].min[axis] ||
                        bounds.max[axis] < g.bounds[child].max[axis])
                        return fail(error, "streamed parent bounds omit child");
            }
        }
        for (uint32_t root : g.roots) {
            if (p[root] != invalidNode)
                return fail(error, "Strata root has a parent");
            rootsBytes += uint64_t(g.nodes[root].indexCount) * sizeof(uint32_t);
            if (rootsBytes > budgetBytes)
                return fail(error, "pinned Strata roots exceed residency budget");
        }
        parents.push_back(std::move(p));
    }

    device_ = &device;
    asset_ = &asset;
    parent_ = std::move(parents);
    spans_.resize(asset.primitives.size());
    for (size_t i = 0; i < asset.primitives.size(); ++i)
        spans_[i].resize(asset.primitives[i].geometry.nodes.size());
    stats_.budgetBytes = budgetBytes;
    stats_.pinnedRootBytes = rootsBytes;
    const bool useIncremental = preferIncremental &&
        (device.info().backend == gfx::Backend::Metal ||
         device.info().backend == gfx::Backend::Vulkan) &&
        device.canUpdateSharedBuffers();
    if(diskPages && (!useIncremental || !diskPages->valid())) {
        device_=nullptr;
        asset_=nullptr;
        parent_.clear();
        spans_.clear();
        stats_={};
        return fail(error,"STRATA disk stream requires ready incremental Metal/Vulkan RHI");
    }
    diskPages_=std::move(diskPages);
    if (!(useIncremental ? initializeIncremental(uploadBudgetBytes, error)
                          : update({}, error))) {
        // Neither mode may expose a half-uploaded resident root frontier.
        // The private type is destroyed only in StrataIndexPages.cpp.
        if (incremental_) shutdownIncremental(device);
        diskPages_.reset();
        device_ = nullptr;
        asset_ = nullptr;
        parent_.clear();
        spans_.clear();
        stats_ = {};
        return false;
    }
    return true;
}

bool IndexResidency::update(const std::vector<IndexDemand>& demands,
                            std::string* error,
                            size_t uploadAllowanceBytes) {
    if (error) error->clear();
    if (!device_ || !asset_ || !device_->gpuHealthy())
        return fail(error, "Strata residency device is unavailable");

    std::vector<std::vector<uint8_t>> wanted;
    wanted.reserve(asset_->primitives.size());
    uint64_t bytes = 0;
    for (const auto& entry : asset_->primitives) {
        wanted.emplace_back(entry.geometry.nodes.size(), 0);
        auto& mask = wanted.back();
        for (uint32_t root : entry.geometry.roots) {
            mask[root] = 1;
            bytes += uint64_t(entry.geometry.nodes[root].indexCount) * 4u;
        }
    }
    if (bytes > stats_.budgetBytes)
        return fail(error, "pinned roots exceed current budget");

    // Most important camera/shadow/reflection demands first. Stable key
    // ordering makes cache contents deterministic under equal priorities.
    auto sorted = demands;
    for (const auto& d : sorted)
        if (d.primitive >= asset_->primitives.size() ||
            d.node >= asset_->primitives[d.primitive].geometry.nodes.size() ||
            !std::isfinite(d.priority) || d.priority < 0)
            return fail(error, "invalid Strata residency demand");
    std::stable_sort(sorted.begin(), sorted.end(),
        [](const IndexDemand& a, const IndexDemand& b) {
            if (a.priority != b.priority) return a.priority > b.priority;
            if (a.primitive != b.primitive) return a.primitive < b.primitive;
            return a.node < b.node;
        });

    uint64_t deferred = 0;
    for (const auto& d : sorted) {
        const auto& nodes = asset_->primitives[d.primitive].geometry.nodes;
        auto& mask = wanted[d.primitive];
        // Build an ancestor path and promote full sibling groups in order.
        // This guarantees a resident parent until ALL direct children are
        // physically present, so selection cannot create a coverage hole.
        std::vector<uint32_t> path;
        for (uint32_t n = d.node; n != invalidNode; n = parent_[d.primitive][n])
            path.push_back(n);
        std::reverse(path.begin(), path.end());
        bool fulfilled = true;
        for (size_t depth = 1; depth < path.size(); ++depth) {
            const auto& parent = nodes[path[depth - 1]];
            uint64_t extra = 0;
            for (uint32_t child = 0; child < parent.childCount; ++child) {
                const uint32_t id = parent.firstChild + child;
                if (!mask[id]) extra += uint64_t(nodes[id].indexCount) * 4u;
            }
            if (extra > stats_.budgetBytes - bytes) {
                fulfilled = false;
                break;
            }
            for (uint32_t child = 0; child < parent.childCount; ++child)
                mask[parent.firstChild + child] = 1;
            bytes += extra;
        }
        if (!fulfilled) ++deferred;
    }

    if (incremental_)
        return updateIncremental(wanted, sorted, deferred, error,
                                 uploadAllowanceBytes);

    bool unchanged = buffer_.valid();
    uint64_t oldNodes = 0, newNodes = 0, evictions = 0;
    for (size_t p = 0; p < wanted.size(); ++p)
        for (size_t n = 0; n < wanted[p].size(); ++n) {
            const bool old = spans_[p][n].valid();
            const bool next = wanted[p][n] != 0;
            oldNodes += old;
            newNodes += next;
            evictions += old && !next;
            if (old != next) unchanged = false;
        }
    if (unchanged) {
        stats_.deferredDemands += deferred;
        return true;
    }

    std::vector<std::vector<IndexSpan>> nextSpans;
    nextSpans.reserve(wanted.size());
    std::vector<uint32_t> packed;
    packed.reserve(size_t(bytes / sizeof(uint32_t)));
    for (size_t p = 0; p < wanted.size(); ++p) {
        const auto& g = asset_->primitives[p].geometry;
        nextSpans.emplace_back(g.nodes.size());
        for (size_t n = 0; n < wanted[p].size(); ++n) {
            if (!wanted[p][n]) continue;
            const auto& node = g.nodes[n];
            if (packed.size() > uint64_t(UINT32_MAX) - node.indexCount)
                return fail(error, "Strata packed index offset exceeds RHI limit");
            nextSpans[p][n] = {uint32_t(packed.size()), node.indexCount};
            packed.insert(packed.end(), g.indices.begin() + node.firstIndex,
                          g.indices.begin() + node.firstIndex + node.indexCount);
        }
    }
    if (packed.empty() || packed.size() * sizeof(uint32_t) > stats_.budgetBytes)
        return fail(error, "Strata residency plan exceeded budget");

    gfx::BufferDesc desc{};
    desc.type = gfx::BufferType::Index;
    desc.usage = gfx::Usage::Immutable;
    desc.size = packed.size() * sizeof(uint32_t);
    desc.data = packed.data();
    desc.label = "STRATA budgeted immutable cluster indices";
    // Full payload upload must succeed before the active handle changes.
    // Backends retain in-flight storage after destroyBuffer; no live bytes
    // are overwritten and a failed allocation preserves the old frontier.
    const gfx::BufferHandle nextBuffer = device_->createBuffer(desc);
    if (!nextBuffer.valid()) {
        ++stats_.failedUploads;
        return fail(error, "STRATA residency RHI index upload failed");
    }
    const gfx::BufferHandle previous = buffer_;
    buffer_ = nextBuffer;
    spans_ = std::move(nextSpans);
    stats_.activeBytes = desc.size;
    stats_.residentNodes = uint32_t(newNodes);
    stats_.evictions += evictions;
    stats_.uploads += uint64_t(newNodes) - (oldNodes - evictions);
    stats_.deferredDemands += deferred;
    ++stats_.generations;
    if (previous.valid()) device_->destroyBuffer(previous);
    return true;
}

IndexSpan IndexResidency::span(uint32_t primitive, uint32_t node) const {
    if (!valid() || primitive >= spans_.size() ||
        node >= spans_[primitive].size()) return {};
    return spans_[primitive][node];
}

void IndexResidency::shutdown(gfx::Device& device) {
    if (device_ && device_ != &device) return;
    if (incremental_) shutdownIncremental(device);
    else if (buffer_.valid()) device.destroyBuffer(buffer_);
    diskPages_.reset();
    buffer_ = {};
    device_ = nullptr;
    asset_ = nullptr;
    spans_.clear();
    parent_.clear();
    stats_ = {};
}
} // namespace aiverse::render::strata
