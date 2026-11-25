# Caverns Stage54c — isolated Studio integration and native visual preview

Date: 2026-11-24. Branch: integration/caverns-studio-stage54c-20261124. Neither main nor the two source feature branches were changed.

## Integration

Studio parent 8f173a81849299a98ec6164ff76b9135e990668d; Caverns parent 6cd16e60bd903f44c9b8e8ec3a949804a170f000; common ancestor 71aa5c93afa5993c4e45ba39f1bf5454343389ea. PR #139 initially reported a conflict. Reconstructed a merge tree from Studio with all 193 Caverns changed paths; four shared paths (CMakeLists.txt, GameClient.h, HostedClient.cpp, HostedClient.h) resolved by applying Caverns deltas to Studio. Both parent histories preserved by merge commit 2262e5e4f89720c59649e89ffb7dc623c4a9cd36, pushed with expected old HEAD. GitHub compare confirms ahead of both parents, 193 changed paths versus Studio, no removed paths. This is source-level integration, not proof of successful compilation.

## Preview commits

- 9dcf3fe78bd4baab5a8a276ce6db515ef56c0a67: shared CaveClientVisualHost presentation policy and actual owner statistics.
- 62add042249994148eed7af12b3abd4b9a114daf: StudioCavernsPreview adapter, bounded camera-local streaming and session options.
- e7349690b8b41321bea89c851b82a2f8fdc2e2f3: split document sync/idle from active-frame staging.
- 84165bfe6dff742b1771a25680d4bb21a0296a7d and d07a4f526f74bc7668d402c72aeaf276badf2466: native StudioScene lifecycle, pre-pass update and real Renderer3D draw; underground isolation.
- 05108217941e0ae54086d9a98ea9905de4f56a67: scoped studio.cavernsPreview bridge.
- aa3785636779e82973a7f5d1f9cc1e4b2f1e90c2 and 87d837038b9bf010716fdafe5e4a73c44212cc4f: typed panel controls and actual telemetry.
- 3f4216eae735ae6f713d0daa1013e34b036faad7: failed shutdown retains owner rather than destructing unverified GPU-live models.
- 03aa1acfd2555f62742f4d7b4dcec0b202305f18 and 32cc31778fccf1ba0a35476be3379b6c1b219a91: skill updates.

## Usage and scope

Open the actual native Scene and Caverns panel, load document, author a stamp and refresh preview diagnostics. Preview enabled by default for an authored flat-world cave document. The existing visual host streams a bounded 27-chunk neighborhood about the brush/camera target, not the whole world. Isolation hides the terrain and composite lighting model; it is not a surface cut or actual terrain subtraction. Other authored models remain. Preview/paging toggles are session-only; paging restarts the owner at an explicit idle point. STRATA uses existing budgets and fallback. Metrics are owner payload/arena estimates and last-observed page counters, not physical VRAM or gameplay physics/nav readiness. Normal retirement is fence-based; a failed final idle proof can quarantine/leak the owner rather than destroy GPU-live storage. Native build, TS typecheck/build, browser tests, screenshots, GPU/physics acceptance NOT RUN.

## Next

Stage54d: editable generator/entrance settings via shared MapDoc operations, configurable paging limits with exact device policy validation, diagnostics/focus controls and document identity/brush isolation fixes. Continue on the integration branch, review any new upstream Studio changes before merging. Close outstanding native gameplay ownership and deterministic replay issues; mandatory KINGSTON build and runtime QA before merge to main.
