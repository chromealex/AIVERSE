# Stage 38BT — strict raw HDR GPU readback and EXR publication

Branch: `feature/aiv20261124_texture_graph_gpu_slots`. Parent: `60f71950126acde5bc361528767c7f75d8710df0`. No main merge.

- Single radiance bake packs float4, reserves 16 bytes/texel, dispatches exact pixel count and reads through existing bounded callbacks. Strict GPU only, finite signed float CPU-reference validation (absolute/relative 1e-4), status resultFormat and byte count. Raw native getter checks exact scope/request and retirement.
- Shared exclusive HALF EXR publisher uses actual GPU readback, not reference pixels; Studio GPU output bake publishes via existing adapter. Native explicit upload of accepted bytes to caller-owned RGBA16F/RGBA32F texture via live RHI lease; no implicit material adoption.
- Authored strict GPU signed values/readback count/stale identity fixture. No compilation, execution, hardware, GUI or timing verification performed. Native HDR texture upload is CPU-staged, not zero-copy; direct float texture transfer, mixed output sets, HDR alpha, compressed formats and other remaining work still open.
