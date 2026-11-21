# Lighting Stage 20 — textured Shader Graph GI

2026-11-20, integration/lighting-main-stage1-20261120.
Code f32733dc5c7fe6906c25be5306d9627d002ffddb.

Model shaders now reserve tex13 for GI and move graph texture declarations AND
bindings to tex14+ in the GI permutation. Shader/pipeline cache keeps the existing
GI key; non-GI and shadow variants retain old graph slots. Metal slots >=16 use
constexpr samplers as before. No save format/default changes. ICB ABI still excluded.
Added actual pixel fixture with four sampled graph textures, High/Low, per-view
GI off, AO, light removal/restoration. This replaces the previous blanket graph
exclusion without overwriting the first graph texture with a GI atlas.

Built aiverse_hdr_scene/aiverse_hdr_gl on KINGSTON with its TMPDIR; PASS.
run-tests.sh --no-build --build-dir /Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120/build --filter '^lighting_graph_gi_':
2/2 PASS, six RGB comparisons on each real Metal/desktop GL backend.
Report 20261120-145548-39120. git diff --check PASS. CI/push N/A.
Mobile/Vulkan hardware NOT RUN; no performance claim.

User expanded scope to ALL remaining lighting. Next: Luma GPU direct owner,
animation snapshots, editor/settings integration, ICB and Release arm64/platform
acceptance. Do not stop after this intermediate stage. All builds on KINGSTON.
