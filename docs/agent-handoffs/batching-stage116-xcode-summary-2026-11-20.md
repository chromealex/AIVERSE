# AIVERSE — Stage 116: Xcode GPU capture overview inspected

**Date:** 2026-11-20  
**Repository:** `chromealex/AIVERSE`  
**Branch:** `perf/auto-batch-cost-metrics-20261119`  
**PR:** #39 (Draft)  

## Source

User screenshot `image(20261120-082344).png` shows two open Xcode Metal GPU Frame Capture windows for the 2048-primitive fixture. Left is `2048-direct`, right is `2048-mdi`. This is visual evidence of Xcode's Summary page only, **not** a Performance/profile trace. Prior Stage115 handoff verified native `.gputrace` files and raster parity.

## Observations from Xcode Summary

| Xcode metric | Direct | CPU MDI |
|---|---:|---:|
| Command Buffers | 1 | 1 |
| Render Encoders | 2 | 2 |
| Compute Encoders | 0 | 0 |
| Draw Calls | 4098 | 30 |
| Dispatch Calls | 0 | 0 |
| Memory reported by capture UI | 31.38 MiB | 48.78 MiB |
| Performance section | Performance data not available | Performance data not available |

The renderer still uses two render encoders and one command buffer in both capture modes. Xcode shows ~136.6x fewer **API draw calls** for MDI, reconcilable with the 2K fixture's 9 CPU MDI submissions + 5 direct fallback draws for each of the two model passes, plus two additional draws (floor or other surrounding scene drawing): `(9+5)*2 + 2 = 30`, vs Direct `2048*2+2 = 4098`. **Important:** indirect command buffers still contain 4086 individual indexed draw commands; low API draw count is not evidence that GPU processed only 30 primitives.

Memory shown is capture resource footprint / debugger accounting, not a proven in-game peak memory comparison. Xcode Summary shows "Performance data not available" for *both* recordings; there is **no Xcode GPU model-pass duration or per-command timing evidence yet**. Do not claim the Shadow or Opaque model group causes the earlier Stage109 GPU regression based on the Summary screen.

Stage109 previously measured from non-captured, completed Metal GPU CB timestamp data +37.29% GPU timeline for CPU MDI at 2K and +54.28% at 4K while CPU renderer wall times fell by ~35.6% and ~32.6%. The Summary screenshot does not overturn those results or quantify GPU occupancy or FPS.

## Performed / not performed

**Performed:** visually inspected exact owner-provided screenshot and compared UI counts to the grounded Stage107/115 MDI/Direct draw submission accounting; added this handoff. **Not performed:** Xcode "Profile" action, per-pass GPU profiling, GPU occupancy counters, detailed ICB command disassembly, Metal shader/resource analysis, native test or renderer changes.

## Concrete next step for owner / next agent

In **each** captured trace, click the **Profile** control in the Performance section (shown at top right of that section on the Xcode Summary page). If the GPU/OS/Xcode supports it, inspect GPU timeline/Performance and share shadow/opaque encoder/group timings, including labels `AIVERSE Shadow Models` and `AIVERSE Opaque Models`. If profiling remains unavailable, report this explicitly and inspect Xcode's command buffer render encoder tree and command resources. Expand Shadow and Opaque groups; share the command lists, the two ICB execute commands, and any GPU cost metrics Xcode displays. This must not be invented from API draw counts.

Avoid modifying Metal safety bindings, ICB residency, or AUTO thresholds until the observed GPU regression is localized. Keep CPU MDI off by default and PR #39 Draft. Continue with per-stage handoffs.
