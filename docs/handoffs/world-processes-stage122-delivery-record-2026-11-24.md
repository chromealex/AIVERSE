# Stage 122 — planetary EVOLVE climate routing

Base 45285f48. In progress. Extend existing read-only ecology sampling with a tick-scoped batch that binds real PHYSIS grid cells to explicitly owner-mapped canonical ground points, selects nearest eligible same-body source deterministically across seams, validates source membership and PHYSIS climate tick, and passes existing TerraWeave baseline through EVOLVE. No inferred XZ-to-planet weather, second forest or climate store, or automatic weather/vegetation simulation. Bounded query work, explicit missing-weather policy, source fixtures. Actual planetary rooted lifecycle still awaits TerraWeave owner. Tests deferred; no main merge.

Status: source 09e8cbc13b01ec5e478abccd21874850b6a2d473; fixture NOT RUN; rooted lifecycle and real host mapping remain owner dependencies.
