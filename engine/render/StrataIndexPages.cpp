// STRATA Stage 15.2: conservative, append-or-fence-recycle indexed pages.
// A "page" is an immutable drawable cluster-node index range within ONE
// fixed-size Storage/index RHI buffer. No per-primitive IB change is needed.
// This path is Metal/Vulkan-only; ES3 retains immutable generation swaps.
#include "StrataIndexResidency.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

namespace aiverse::render::strata {
struct IndexResidency::IncrementalStorage {
    struct Range {
        uint32_t first = 0; // uint32 INDEX offset in the fixed GPU arena
        uint32_t count = 0;
        uint64_t frame = 0; // only retired ranges carry a fence serial
    };
    std::vector<Range> free;
    std::vector<Range> retired;
    uint32_t highWater = 0;
    uint32_t capacity = 0;
    size_t uploadBudget = 0; // 0 means unthrottled
};

IndexResidency::IndexResidency() = default;
IndexResidency::~IndexResidency() = default;

bool IndexResidency::initializeIncremental(size_t uploadBudgetBytes,
                                           std::string* error) {
    if (!device_ || !asset_ || !device_->gpuHealthy() ||
        !device_->canUpdateSharedBuffers() ||
        (device_->info().backend != gfx::Backend::Metal &&
         device_->info().backend != gfx::Backend::Vulkan)) {
        if (error) *error = "STRATA incremental storage unavailable on this RHI";
        return false;
    }
    const auto capacity = uint32_t(stats_.budgetBytes / sizeof(uint32_t));
    if (!capacity) {
        if (error) *error = "STRATA incremental capacity too small";
        return false;
    }
    auto pages = std::make_unique<IncrementalStorage>();
    pages->capacity = capacity;
    pages->uploadBudget = uploadBudgetBytes;

    // Full arena reserved once; only root contents are transferred now.
    // Vulkan Storage includes INDEX usage and uses mapped snapshot-safe
    // versions; Metal Storage updates use ordered GPU blit commands.
    gfx::BufferDesc desc{};
    desc.type = gfx::BufferType::Storage;
    desc.usage = gfx::Usage::Dynamic;
    desc.size = size_t(capacity) * sizeof(uint32_t);
    desc.label = "STRATA incremental indexed page arena";
    const auto handle = device_->createBuffer(desc);
    if (!handle.valid()) {
        ++stats_.failedUploads;
        if (error) *error = "STRATA incremental GPU arena creation failed";
        return false;
    }

    std::vector<std::vector<IndexSpan>> initial = spans_;
    uint32_t at = 0, roots = 0;
    for (size_t p = 0; p < asset_->primitives.size(); ++p) {
        const auto& geometry = asset_->primitives[p].geometry;
        for (uint32_t root : geometry.roots) {
            const auto& node = geometry.nodes[root];
            std::vector<uint32_t> diskRoot;
            const uint32_t* bytes=geometry.indices.data()+node.firstIndex;
            if (diskPages_) {
                if (!diskPages_->readRoot(uint32_t(p),root,diskRoot,error) ||
                    diskRoot.size()!=node.indexCount) {
                    device_->destroyBuffer(handle);
                    ++stats_.failedUploads;
                    if(error && error->empty())
                        *error="STRATA pinned disk root page is invalid";
                    return false;
                }
                bytes=diskRoot.data();
            }
            if (node.indexCount > capacity - at ||
                !device_->updateSharedBufferChecked(
                    handle, bytes,
                    size_t(node.indexCount) * sizeof(uint32_t),
                    size_t(at) * sizeof(uint32_t))) {
                device_->destroyBuffer(handle);
                ++stats_.failedUploads;
                if (error) *error = "STRATA incremental root page upload failed";
                return false;
            }
            initial[p][root] = {at, node.indexCount};
            stats_.gpuTransferBytes += uint64_t(node.indexCount)*sizeof(uint32_t);
            at += node.indexCount;
            ++roots;
        }
    }
    pages->highWater = at;
    incremental_ = std::move(pages);
    spans_ = std::move(initial);
    buffer_ = handle;
    stats_.incremental = true;
    stats_.arenaBytes = desc.size;
    stats_.activeBytes = uint64_t(at) * sizeof(uint32_t);
    stats_.residentNodes = roots;
    stats_.uploads = roots;
    stats_.uploadCalls = roots;
    stats_.uploadedBytes = stats_.activeBytes;
    stats_.generations = 1;
    if(diskPages_){
        const auto disk=diskPages_->stats();
        stats_.diskCompletedReads=disk.completedReads;
        stats_.diskBytesRead=disk.bytesRead;
        stats_.diskFailures=disk.failedReads;
        stats_.diskPending=disk.queued;
        stats_.diskReadyBytes=disk.readyBytes;
    }
    return true;
}

bool IndexResidency::updateIncremental(
        const std::vector<std::vector<uint8_t>>& wanted,
        const std::vector<IndexDemand>& priorities,
        uint64_t deferred, std::string* error,
        size_t uploadAllowanceBytes) {
    if (!incremental_ || !device_ || !buffer_.valid() ||
        !device_->gpuHealthy() || !device_->canUpdateSharedBuffers()) {
        if (error) *error = "STRATA indexed page upload outside safe RHI epoch";
        return false;
    }
    if(diskPages_){
        std::vector<std::pair<uint32_t,uint32_t>> pending;
        for(uint32_t p=0;p<wanted.size();++p)
            for(uint32_t n=0;n<wanted[p].size();++n)
                if(wanted[p][n] && !spans_[p][n].valid())
                    pending.emplace_back(p,n);
        diskPages_->retainRequested(pending);
    }
    using Range = IncrementalStorage::Range;
    // Work against drafts: a failed upload MUST retain the live offsets and
    // old GPU renderable frontier, even if earlier fresh-slot writes succeeded.
    auto next = spans_;
    auto free = incremental_->free;
    auto retired = incremental_->retired;
    uint32_t highWater = incremental_->highWater;
    const uint64_t frame = device_->frameSerial();
    const uint64_t completed = device_->completedFrameSerial();

    if (completed) {
        for (auto it = retired.begin(); it != retired.end();) {
            if (it->frame && it->frame <= completed) {
                free.push_back({it->first, it->count, 0});
                it = retired.erase(it);
            } else ++it;
        }
    }
    std::sort(free.begin(), free.end(),
              [](const Range& a, const Range& b) { return a.first < b.first; });
    // Merge adjacent completed holes; never coalesce across live or un-fenced
    // retired spans. Count/offset arithmetic stays inside capacity.
    std::vector<Range> holes;
    for (const auto& range : free) {
        if (!range.count) continue;
        if (!holes.empty() &&
            uint64_t(holes.back().first) + holes.back().count == range.first)
            holes.back().count += range.count;
        else holes.push_back(range);
    }
    free = std::move(holes);

    uint64_t evicted = 0;
    std::vector<Range> newlyRetired;
    for (size_t p = 0; p < next.size(); ++p)
        for (size_t n = 0; n < next[p].size(); ++n) {
            const auto span = next[p][n];
            if (!span.valid()) continue;
            if (wanted[p][n]) continue;
            // Never make this memory available for current-epoch uploads:
            // the old geometry may be read by an in-flight GPU submission.
            newlyRetired.push_back({span.firstIndex, span.indexCount, frame});
            next[p][n] = {};
            ++evicted;
        }

    // An unknown serial cannot later be proven completed. Rather than
    // leak permanently quarantined holes, preserve the current drawable
    // frontier until the RHI starts reporting real frame serials.
    if (frame == 0 && !newlyRetired.empty()) {
        ++stats_.fenceReuseDeferrals;
        stats_.deferredDemands += deferred + 1;
        return true;
    }

    struct Job {
        uint32_t primitive = 0, node = 0;
        Range range{};
        std::vector<uint32_t> diskIndices;
    };
    std::vector<Job> jobs;
    uint64_t newBytes = 0, bandwidthDeferrals = 0;
    uint64_t fenceDeferrals = 0, diskDeferrals = 0;

    // Allocate from completed holes or from never-touched tail space.
    // Used only on temporary drafts; a rejected whole sibling group rolls
    // back its allocations in one assignment.
    auto allocate = [&](std::vector<Range>& available, uint32_t& cursor,
                        uint32_t count, Range& result) {
        for (size_t i = 0; i < available.size(); ++i) {
            if (available[i].count < count) continue;
            result = {available[i].first, count, 0};
            available[i].first += count;
            available[i].count -= count;
            if (!available[i].count) available.erase(available.begin() + long(i));
            return true;
        }
        if (count > incremental_->capacity - cursor) return false;
        result = {cursor, count, 0};
        cursor += count;
        return true;
    };

    // Existing Stage 15 wanted masks are built from descending priorities.
    // Traverse each demand's ancestor path in the SAME order. No child may
    // become drawable unless every one of its siblings was uploaded.
    for (const IndexDemand& demand : priorities) {
        const uint32_t p = demand.primitive;
        const auto& nodes = asset_->primitives[p].geometry.nodes;
        const auto& parents = parent_[p];
        std::vector<uint32_t> path;
        for (uint32_t id = demand.node; id != invalidNode; id = parents[id])
            path.push_back(id);
        std::reverse(path.begin(), path.end());
        for (size_t depth = 1; depth < path.size(); ++depth) {
            const uint32_t parentId = path[depth - 1];
            const auto& parent = nodes[parentId];
            if (!next[p][parentId].valid()) break;
            bool allWanted = true;
            uint64_t bytes = 0;
            for (uint32_t child = 0; child < parent.childCount; ++child) {
                const uint32_t id = parent.firstChild + child;
                if (!wanted[p][id]) { allWanted = false; break; }
                if (!next[p][id].valid())
                    bytes += uint64_t(nodes[id].indexCount) * sizeof(uint32_t);
            }
            if (!allWanted) break;
            if (!bytes) continue;

            // Disk page readiness is an independent constraint from GPU
            // budget/slot availability. Queue all missing siblings, but do
            // not block the renderer thread or expose an incomplete group.
            std::map<uint32_t,std::vector<uint32_t>> payloads;
            if(diskPages_){
                bool readyGroup=true;
                for(uint32_t child=0;child<parent.childCount;++child){
                    const uint32_t id=parent.firstChild+child;
                    if(next[p][id].valid())continue;
                    std::vector<uint32_t> page;
                    if(!diskPages_->ready(p,id,page)){
                        (void)diskPages_->request(p,id);
                        readyGroup=false;
                    }else if(page.size()!=nodes[id].indexCount){
                        if(error)*error="STRATA disk page index count mismatch";
                        return false;
                    }else payloads.emplace(id,std::move(page));
                }
                if(!readyGroup){
                    ++diskDeferrals;
                    break; // existing coarse parent remains drawable
                }
            }

            const size_t allowed=std::min(
                incremental_->uploadBudget ? incremental_->uploadBudget
                                           : SIZE_MAX,
                uploadAllowanceBytes);
            if (bytes > allowed || newBytes > allowed - bytes) {
                ++bandwidthDeferrals;
                break;
            }
            auto draftFree = free;
            uint32_t draftCursor = highWater;
            std::vector<Job> group;
            bool fits = true;
            for (uint32_t child = 0; child < parent.childCount; ++child) {
                const uint32_t id = parent.firstChild + child;
                if (next[p][id].valid()) continue;
                Range range;
                if (!allocate(draftFree, draftCursor, nodes[id].indexCount,
                              range)) {
                    fits = false;
                    break;
                }
                Job job{p,id,range,{}};
                if(diskPages_){
                    auto payload=payloads.find(id);
                    if(payload==payloads.end()){
                        if(error)*error="STRATA disk sibling payload disappeared";
                        return false;
                    }
                    job.diskIndices=std::move(payload->second);
                }
                group.push_back(std::move(job));
            }
            if (!fits) {
                ++fenceDeferrals;
                break; // keep the complete resident parent
            }
            free = std::move(draftFree);
            highWater = draftCursor;
            for (const Job& job : group) {
                next[job.primitive][job.node] =
                    {job.range.first, job.range.count};
                jobs.push_back(job);
            }
            newBytes += bytes;
        }
    }

    // Only newly allocated, non-overlapping pages are written. No pointer
    // referenced by the current or previous draw can be modified in place.
    // A rejected update can leave bytes in UNUSED ranges but never publishes
    // their addresses as drawable geometry.
    for (const Job& job : jobs) {
        const auto& geometry = asset_->primitives[job.primitive].geometry;
        const auto& node = geometry.nodes[job.node];
        const uint32_t* bytes=diskPages_ ? job.diskIndices.data()
                                        : geometry.indices.data()+node.firstIndex;
        if (!device_->updateSharedBufferChecked(
                buffer_, bytes,
                size_t(node.indexCount) * sizeof(uint32_t),
                size_t(job.range.first) * sizeof(uint32_t))) {
            ++stats_.failedUploads;
            if (error) *error = "STRATA incremental page upload rejected by RHI";
            return false;
        }
        stats_.gpuTransferBytes+=uint64_t(node.indexCount)*sizeof(uint32_t);
    }

    for (const Range& range : newlyRetired)
        retired.push_back(range);
    incremental_->free = std::move(free);
    incremental_->retired = std::move(retired);
    incremental_->highWater = highWater;
    spans_ = std::move(next);
    // Only after all GPU writes and the residency table are published can
    // verified disk pages be removed from the bounded staging cache.
    if(diskPages_)
        for(const auto& job:jobs)
            diskPages_->consume(job.primitive,job.node);

    uint64_t active = 0;
    uint32_t count = 0;
    for (const auto& primitive : spans_)
        for (const auto& span : primitive)
            if (span.valid()) {
                ++count;
                active += uint64_t(span.indexCount) * sizeof(uint32_t);
            }
    stats_.activeBytes = active;
    stats_.residentNodes = count;
    stats_.evictions += evicted;
    stats_.uploads += jobs.size();
    stats_.uploadedBytes += newBytes;
    stats_.uploadCalls += jobs.size();
    stats_.uploadBudgetDeferrals += bandwidthDeferrals;
    stats_.fenceReuseDeferrals += fenceDeferrals;
    stats_.deferredDemands += deferred + bandwidthDeferrals +
                              fenceDeferrals + diskDeferrals;
    if(diskPages_){
        const auto disk=diskPages_->stats();
        stats_.diskCompletedReads=disk.completedReads;
        stats_.diskBytesRead=disk.bytesRead;
        stats_.diskFailures=disk.failedReads;
        stats_.diskPending=disk.queued;
        stats_.diskReadyBytes=disk.readyBytes;
    }
    // No generation increment: the exact GPU RHI buffer HANDLE is stable.
    // Vulkan may internally version the mapped storage for GPU safety.
    return true;
}

void IndexResidency::shutdownIncremental(gfx::Device& device) {
    if (device_ != &device) return;
    if (buffer_.valid()) device.destroyBuffer(buffer_);
    buffer_ = {};
    incremental_.reset();
}
} // namespace aiverse::render::strata
