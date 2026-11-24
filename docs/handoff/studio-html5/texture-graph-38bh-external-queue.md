# Stage 38BH — external texture queue and incremental execution

Branch: `feature/aiv20261124_texture_graph_gpu_slots`. Implementation SHA: `ba7ac117280ea98621c78b617cb07a7a25624474`. Previous QA HEAD: 029912de51b3522effb789d809f987c1e0768b79. No main merge.

## Implemented
- Queue captures the validated native Program at admission, including immutable decoded external RGB snapshots. Promotion consumes that Program and does not invoke a resolver or reread a project file. No GPU handles or borrowed callbacks held in pending queue.
- Separate pending source storage cap 32 MiB, existing 16 MiB per graph, and existing JSON queue limits retained. Accounting on admission, cancellation, promotion, scope/epoch invalidation and shutdown. CPU snapshots moved rather than recopied at promotion.
- External image restriction removed from incremental GPU generation, live-target output reuse and CPU evaluation cache. Identities use pinned asset and SHA-256 revision in resolved properties, decoder ABI version and dimensions. The project resolver checks encoded SHA-256. Trusted native resolvers must return identical decoded data for identical pinned revisions in project scope.
- Studio and legacy editor enqueue provide the native project resolver; native resident convenience methods accept explicit resolver. Updated Inspector description and texture RHI skill.
- Authored real GPU fixture for unchanged revision reuse, changed revision invalidation, queue snapshot survival when resolver stops working, and pending byte release after cancellation/promotion.

## Evidence
GitHub accepted source commits 1ef431f6 and ba7ac117. Compared changed paths; no compilation, CPU execution, actual Metal/Vulkan, frontend or live Studio verification performed for this stage. Earlier 38BG QA is not certification of these changes. Native publication of a set still revalidates pinned source file; deleted/changed sources can prevent disk publication even if generation from queued snapshot succeeds.

## Remaining feature work
External alpha/HDR/mip/compressed texture support, nested functions, dependent asset updates, full GGX environment filtering/BRDF LUT/EXR, general custom shader resource ABI. Then consolidated 38BG rerun on final feature HEAD and hardware/GUI/MCP acceptance. Keep branch separate from main.
