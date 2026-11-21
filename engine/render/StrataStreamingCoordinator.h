#pragma once
// STRATA Stage 15.4: cross-world, genre-independent streaming coordination.
// GPU reservations count staged STRATA INDEX allocations (not source VB/IB,
// Vulkan internal versions or GPU in-flight versions). Device identity is
// borrowed; callers must release leases before Device teardown.
#include "../gfx/Gfx.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace aiverse::render::strata {

struct DeviceIndexBudgetStats {
    uint64_t limitBytes=0; // 0 = no global cap
    uint64_t reservedBytes=0;
    uint32_t activeLeases=0;
    uint64_t frameUploadLimitBytes=0, frameUploadRemainingBytes=0;
    uint64_t currentFrameSerial=0, currentFrameUploadedBytes=0;
};

// All GeometryScenes using one gfx::Device share a single reservation ledger.
// configureDeviceIndexBudget() must be consistent among concurrently live
// worlds; a mismatched nonzero cap fails rather than silently changing policy.
// A reservation is committed BEFORE a source's GPU arena allocation.
// Caller owns returned token; release it after the associated GPU resource.
bool configureDeviceIndexBudget(gfx::Device& device, uint64_t limitBytes);
bool reserveDeviceIndexBytes(gfx::Device& device, uint64_t bytes,
                             uint64_t& outToken);
void releaseDeviceIndexBytes(gfx::Device& device, uint64_t token);
// Call before gfx::Device destruction; never discards any active lease.
// Prevents an idle ledger from leaking its limit to a new device that
// happens to reuse the same C++ pointer address.
void clearIdleDeviceIndexBudget(gfx::Device& device);
DeviceIndexBudgetStats deviceIndexBudgetStats(gfx::Device& device);

// Stage 15.4: single GPU upload-byte limit shared by ALL scenes/worlds
// recording on a device. The caller's serialized RHI frame serial is the
// lifetime boundary. Serial 0 under a configured cap refuses new uploads;
// pinned roots loaded at scene initialization are outside this governor.
bool configureDeviceFrameUploadBudget(gfx::Device& device,uint64_t bytes);
uint64_t deviceFrameUploadAllowance(gfx::Device& device,uint64_t frameSerial);
void accountDeviceFrameUpload(gfx::Device& device,uint64_t frameSerial,
                              uint64_t checkedBytes);

struct SharedDiskIoStats {
    uint32_t workers=0;
    uint64_t queuedJobs=0, activeJobs=0, rejectedJobs=0;
    uint64_t readyBytes=0, readyLimitBytes=0;
};

// One process-wide bounded background executor, shared by independent disk
// archives and ECS worlds. Workers touch only immutable archive locations
// and ref-counted request state; never the GPU/renderer.
// Configure before the first archive opens, else only identical settings
// are accepted. Use 0 ready limit to retain the default 64 MiB cap.
bool configureSharedDiskIo(uint32_t workers, uint64_t readyLimitBytes);
SharedDiskIoStats sharedDiskIoStats();

// These internal entry points are public to the separate disk archive target.
// Submit one active job per archive (the archive queues requests internally).
// A rejected job never executes and must be rescheduled after pressure drops.
bool submitSharedDiskJob(std::function<void()> job);
// Blocks a worker, NOT a render thread, until global CPU-ready budget is
// available or the archive has been cancelled. Reservation MUST be released
// when the cached verified page is consumed or abandoned.
bool acquireSharedDiskReady(size_t bytes, const std::atomic<bool>& cancelled);
void releaseSharedDiskReady(size_t bytes);
void wakeSharedDiskIo();

} // namespace aiverse::render::strata
