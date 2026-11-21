# Lighting Stage 22 — native Luma scene host

2026-11-20, integration/lighting-main-stage1-20261120; code 6839eafb7fa0589a0336ab4004b234e8985ff05f.
Added apps/renderbench/LumaHost.h, Bench integration, recipe docs and real-glTF
acceptance. Additive optional luma JSON key: backend auto/gpu, quality
balanced/high/ultra; legacy settings unchanged. One direct owner, explicit
fallback for unsupported backend/capacity, rejection of duplicate static/baked
ownership. GI independent. Source colors converted back to linear. Static
geometry captured before upload. Native host synchronizes previously committed
GPU work so moving cameras never silently omit Luma. Each view requires actual
composition or reports failure. Synchronization cost is included, not a speedup.

Built aiverse_renderbench/aiverse_renderbench_gl on KINGSTON, PASS.
run-tests.sh --no-build --build-dir /Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120/build --filter '^lighting_(luma_host|native_host)':
4/4 PASS, report 20261120-150855-58200. Real glTF, source on/off, moving camera,
legacy conflict rejection, Metal GPU and desktop GL fallback; existing GI host
regression PASS. git diff --check PASS. No CI/push.

Release arm64 configured successfully in KINGSTON root/release-arm64; first
renderbench/hdr_scene build RUNNING, not yet validated. Native session72016,
logs/release-arm64-build.log. Preserve this build; do not spawn a duplicate.
No editor/client/mobile/Vulkan hardware or production performance PASS implied.
Current static native host excludes animated casters; next stage: generic
animated pose geometry snapshots and automatic host publication. Then editor
settings/UI, ICB and remaining platform/performance gates. User asked finish ALL,
not just this stage; continue. All build/temp/artifacts on KINGSTON.
