# AIVERSE GPU Timing Router — shared RHI diagnostic service

## Responsibility and ownership

AIVERSE's cross-backend `gfx::Device` owns **one**
`gfx::GpuTimingRouter` (`engine/gfx/GpuTimingRouter.h`) accessible
through `device.gpuTimingRouter()`. It is a shared per-device service,
**not** a TerraWeave module or RTS/FPS-specific implementation. It
supports consumers such as Horizon, Prisma, SpectraFX/VFX, editor
profilers, and command-line benchmarks with independent result
mailboxes. All calls are on the **gfx Device owner thread**.

Backends continue exposing `timeGpuFrame(rawTag)` and
`takeGpuTimings()`; **only the shared router** should call those
methods inside a process that uses routed timing. In particular, calling
`takeGpuTimings()` directly in another subsystem destructively drains
**all** completed GPU timings, steals or loses other subscribers'
samples, and breaks frame-tag pairing. This service fixes the fan-out
problem without changing the Metal/GL/Vulkan backend implementations.

## Native API

```cpp
#include "engine/gfx/Gfx.h"

gfx::Device& device = ...;
auto& timing = device.gpuTimingRouter();
const auto subscriber = timing.subscribe(); // 0 means resource limit

// During an actual render frame, after beginFrame():
constexpr uint64_t ownTag = 42; // caller-defined frame/work identifier
bool accepted = subscriber && timing.request(device,subscriber,ownTag);
// On GL, another simultaneous timer query may already be active.
// Unsupported backend or router capacity can also reject the request.

// In later frames, nonblocking and independent of other subscribers:
timing.poll(device);
for(const auto& sample : timing.take(subscriber)) {
    // sample.tag is the ORIGINAL ownTag (not the router's raw GPU tag)
    // sample.valid determines whether sample.milliseconds is meaningful
}

timing.unsubscribe(subscriber); // before the consumer's lifetime ends
```

**Semantics:** up to 32 active subscribers, up to 64 total pending
backend requests, and up to 64 completed samples per subscriber
mailbox. The router assigns each GPU request a unique device-private
raw tag beginning with the reserved 0xD17A prefix; a pending map
remembers the subscriber and original client tag. The backend's
asynchronous result returns with the private tag and is routed to
**only** the matching subscriber mailbox. Subscriber sample queues
are separate and bounded; a subscriber's slow consumer cannot
overwrite another subscriber's results. `take(subscriber)` drains
only that consumer. `unsubscribe(subscriber)` removes its mailbox
and pending mappings. `cancel(subscriber,clientTag)` drops matching
still-pending requests without touching another owner, for timeout
handling (a later backend completion is safely ignored).

Router diagnostics: `subscribers()`, `pendingRequests()`,
`unownedResults()`, `droppedResults()`. These counters are bookkeeping
only, not verified GPU performance measurements.

No GPU wait, fence, synchronous readback, new GPU allocation or
thread callback is introduced. GPU request rejection does **not**
guarantee that a backend can perform multiple overlapping timing
queries. For example, the current GL query implementation allows one
active `GL_TIME_ELAPSED` measurement, while Metal can handle
multiple tags for a command buffer but measures its entire GPU
command-buffer interval. A call returning false should trigger
that subsystem's CPU-only or delayed fallback rather than introducing
a blocking GPU wait.

## Migrated consumers (Stage 42)

- `engine/world/TerraWeaveHorizonFrameTimingBridge.h`: Horizon Stage41
  now subscribes through the Device router, preserving original frame
  IDs and its bounded 8-frame GPU latency fallback. It cancels timed-out
  mappings and unsubscribes when adaptation is disabled or when a native
  streamed-world session ends. The RTS GameClient and native FPS game
  keep their existing frame ownership.
- `apps/editor/windows/SceneWindow.cpp`: Metal batching benchmark
  requests/receives its own `A17E` original tags through an independent
  mailbox, including device-recreation detection.
- `apps/renderbench/Bench.h`: the benchmark's tagged GPU readings use
  a dedicated mailbox, unsubscribed on shutdown.
- `tools/geometrybench/main.cpp` and
  `tests/geometry/instance_preview.cpp`: diagnostic timing samples
  are also routed, rather than calling `takeGpuTimings()` directly.

The underlying editor/Game windows may share one gfx Device. The
router preserves tag fan-out when both consumers are active and
returns only owned samples. Existing unsupported backends retain
the previous behavior of failing GPU timing requests; routing alone
does not create GPU timer support.

## Scope, limitations and QA

This routes **completed GPU timing information only**. It does not
provide GPU busy percentage, power draw, exclusive render-pass timing
or a globally comparable GPU interval. Backend scope differs:
Metal measures the completed command buffer, and current GL timers
measure the interval from their start until `endFrame`; those values
can include UI/other systems or idle gaps. The owner must interpret
and label samples accordingly.

The router is intentionally not thread safe. It has no global
singleton, no cross-device distribution and no new gameplay
component. It has no PHYSIS, Unity, Unreal or genre dependency.
At capacity it returns false or drops the oldest completed result
from the *same* subscriber queue (with `droppedResults` updated).
Polling after a canceled/unsubscribed GPU result increments
`unownedResults`. A broken backend that never completes a query
may keep its own backend queue occupied even after a router
cancellation; backend device reset/recovery is a separate contract.

**Validation status: NOT RUN at user request.** Stage42 source was
committed and read through GitHub, but Clang/GCC builds, CTest,
native RTS/FPS/editor/renderbench execution, Metal/GL/Vulkan timing
completion and measured frame-time performance have not been
verified. A separate testing agent must cover two owners requesting
out-of-order results, simultaneous polling, cancellation/unsubscribe,
unknown tags, per-mailbox overflow, 32 subscriber/64 pending
limits, unsupported device behavior, GL exclusive query behavior,
Metal full-command-buffer semantics, Device recreation, and
FPS/RTS stream teardown. Keep PR #24 Draft until compiled and
tested; see `docs/terrain/AGENT_HANDOFF.md` Stage42.


## Stage 43 — hardware-free GPU timing router regression fixture

A dedicated native C++20 fixture `tests/gpu_timing_router_test.cpp` constructs an isolated
`gfx::GpuTimingRouter` with a fake tagged GPU backend. It checks same client tag
across subscribers, out-of-order completion, consumer mailbox isolation,
invalid GPU samples, cancel/unsubscribe with late completions, unsupported and
busy backend rejection, 32 subscribers, 64 in-flight queries, 64 queued
completed samples and dropped-result diagnostics, and independent Device/router
lifetime. There is no real graphics driver in this fixture.

CMake target `aiverse_gpu_timing_router_test`, CTest name `gpu_timing_router`.
The TerraWeave isolated GCC/Clang workflow builds this target as well as the
nine TerraWeave CPU tests. This checks the shared Stage42 infrastructure,
not the quality of actual Metal/GL/Vulkan timing or the integration of
real RTS/FPS/Editor frame callbacks. Those still require independent QA.
