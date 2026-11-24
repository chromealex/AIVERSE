# Stage 38D — bounded priority queue

Branch: `feature/aiv20261124_texture_graph_gpu_slots`

Plan: `6d9294ab1da6d913ff7e313ada154a1241301d1b`
Implementation: `28a60f729797ceed0a8b1470ffee79c7f03c441c`
Typed enqueue/epoch: `2d7c2f842c5af745ed0aa6f78a0ffa71f6d2b894`
Final implementation: `8090faedcf45fc9b40f75112f9022b73c23e5f2a`

Stage 38D: the existing native TextureGraphGpuOwner now owns a CPU-only pending queue (8 waiting requests, 512 KiB per immutable serialized request, 2 MiB total queued payload, priority 0–3, highest first, FIFO within priority). No preemption, no second GPU owner, no waiting GPU allocation; one active generation keeps existing GPU caps. Native enqueue / enqueueResidentMaterial, queueStatus, cancelTicket and releaseTicket; MCP texture_graph_gpu_preview actions enqueue, queueRead, queueCancel, queueRelease. Ticket and promoted request IDs are distinct decimal strings. Poll queueRead for activeTicket/activeRequest, then use original read/publish/apply operations with the request ID. Ready result stays held until published and released or explicit discardResult=true; draining requires another release call after fence retirement. A direct ready unpublished result blocks promotion until published/cancelled; direct start is refused while queue owns scheduling. Pending cancellation removes only the exact ticket. Project change clears queue; device epoch invalidation drops pending work, reports error and abandons old-epoch handles for active queued requests. Execution timeout begins at promotion; no queued wait timeout, aging or parallel dispatch. Studio PBR queue controls and local draft matching preserve publication identity. Isolated priority storage test PASS, real-RHI fixture authored but NOT RUN; no native build, Studio/MCP live verification or performance claim.

## Changed paths

- engine/render/graph/TextureGraphRequestQueue.h
- engine/render/graph/TextureGraphGpuOwner.h
- apps/editor/core/GraphOps.h
- apps/editor/EditorPlayOps.cpp
- apps/studio-web/src/graphs/TextureGraphPanel.tsx
- tests/render/texture_graph_request_queue.cpp
- tests/render/texture_graph_gpu_acceptance.h
- CMakeLists.txt

## Actual evidence

Isolated Linux test, reconstructed exact Git blobs checked using git hash-object:
- TextureGraphRequestQueue.h: 4790e22bf6015b22421222c38839d6d1c20f07e8
- texture_graph_request_queue.cpp: 8ffaef1426f9b7bab069e6a2fd966d1710b7320c

```
g++ -std=c++20 -Wall -Wextra -Werror -I. tests/render/texture_graph_request_queue.cpp -o queue-gcc && ./queue-gcc
clang++ -std=c++20 -Wall -Wextra -Werror -I. tests/render/texture_graph_request_queue.cpp -o queue-clang && ./queue-clang
```

GCC 14.2.0 and Clang 17.0.0 both exit 0: PASS: priority, FIFO, cancellation, count/byte caps, nonrecycled tickets. Policy files unchanged in the subsequent owner-only fixes. This is not a full checkout or feature evidence report. Existing RHI fixture includes priority promotion and ready-result retention assertions, NOT RUN. Full native build, native queue lifecycle, Studio, MCP, actual GPU execution and Mac quick gate NOT RUN.

## Next

Stage 38E: design safe reclamation of generated material images without invalidating old appearance snapshots, using the existing Model and GPU retirement owners; no individual texture destruction while published indices or render packets can reference it. Before integration, exercise queue error, epoch expiry, release, backpressure and cancellation in actual native owner fixture. No merge to main until integration gates pass.
