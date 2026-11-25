# Stage 41 — procedural material cavity AO

Continues Stage 40 from `3703752e` on `integration/building-graph-stage36b-20261124`. Tests remain deferred per previous handoff.

- Portable deterministic fourth PBR channel: material-space ambient occlusion approximation from positive neighbouring height differences at two periodic radii. Resolution-independent sampling distance, grayscale linear channel, bounded intensity. Not baked scene or mesh AO.
- Native Model and GLB bind the same generated map when no authored AO GUID exists; existing imported AO takes precedence and authored occlusion strength remains intact.
- Studio surface preview exposes actual AO PNG and accounts for four generated maps in the existing 128 MiB decoded pixel budget. External emissive assets remain supported; no generated emissive added.
- Extend existing CPU, GLB and Studio preview regressions. No compile/CTest/Metal/visual checks executed for this stage. Existing Stage 39/40 checks still pending.

Remaining: standalone reusable texture Assets, unique atlas, functional footprint and interior furnishing, visual attachment acceptance, measured preview responsiveness and full integration. No main merge.
