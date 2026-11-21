# Lighting Stage 34 — Light entity and component Inspector

2026-11-20; integration/lighting-main-stage1-20261120.
Implementation: d6ddd951b521f7a10aff5ddace7ebd76032eb8c7.
User required lights to be entities, all objects to follow that rule, and then
explicitly rejected the intermediate flat Properties Inspector. Final Inspector
uses actual native Transform/Light descriptors and component-targeted writes;
name belongs to the entity. See docs/studio-light-entities.md.

Changed paths: editor EditorEntityAdapter/EditorSceneNodes, core MapDoc/LightOps
and new DocumentLightIdentity; Studio document IDs/visuals/lighting geometry,
Session and MetalScene; native render-lighting test; web App/types/LightingPanel,
StudioPanelRenderer/lightingSchema. No new ImGui UI or simulation ECS float state.

Compatibility and ownership:
- lights[] is the sole canonical legacy payload; additive entityId metadata.
  Old files load with deterministic legacy IDs; new additions get unique IDs;
  duplicate IDs are repaired. IDs persist through save/reopen and deletion of
  preceding elements. No parallel mutable light state.
- Generic document Transform/Light read/write, native selection and viewport
  pick handles. Handles excluded from GI geometry and opaque draws.
- Inspector snapshot components + entityProperties, optional componentId on
  setProperty; reject ownership mismatch. Light update/delete optionally resolve
  entityId before legacy index. Old callers retain compatibility.
- Lighting/Hierarchy/Inspector/viewport share entity selection. FPS mutations
  validate whole document through fps_set; RTS uses existing MapDoc history.
- Global all-object mandate committed in Stage33 and copied to root AGENTS.md;
  root copy remains uncommitted on its unrelated branch, other changes preserved.

Actually executed (all builds/temp/artifacts on KINGSTON root
/Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120):
- Debug and Release arm64 aiverse_studio, aiverse_studio_render_lighting_tests,
  aiverse_studio_metal_lighting_tests builds PASS. Final component changes rebuilt
  Studio/session/render-lighting targets. Legacy aiverse_editor final build PASS.
- Entity phase Debug studio_render_lighting + studio_metal_lighting 2/2 PASS;
  final stable-ID regression selection render_lighting/session/fps_lighting_document/
  document_visuals/scene_nodes 5/5 PASS. Release render_lighting + Metal 2/2 PASS.
- After component correction: Debug render_lighting/session 2/2 PASS and Release
  render_lighting 1/1 PASS. Tests exercise stable identity, deletion/reindexing,
  stale ordinal resolution by ID, deleted-ID rejection, canonical writes,
  validation, Undo/Redo, save/reopen, shared transform and component ownership.
- npm --prefix apps/studio-web run build -- --outDir <root>/studio-web-dist PASS
  (includes tsc --noEmit); final test:web 57/57 PASS.
- AIVERSE_STUDIO_WEB_DIST=<root>/studio-web-dist python3 -m unittest discover
  -s tools/studio -p 'test_*.py' -v: final 19/19 PASS. First invocation without
  external bundle env skipped one check; corrected invocation has no skips.
- Logs stage34-*.log, particularly components-{web,build,tests,release-build,
  release-tests,ui-contracts,web-tests}, legacy-editor-build, final-tests,
  release-tests. git diff --check PASS; no CI/push.

Actual CUA Release WKWebView verification on unlocked Mac:
- Light appears in Hierarchy and Inspector with persisted entity ID.
- Rename propagated to Hierarchy and Lighting; shared selected source tracked
  selecting/deselecting by native viewport pick. Highlight follows source.
- User flagged flat intermediate UI; final relaunched app shows Name outside
  components, TRANSFORM and LIGHT headings, no flat Properties block for light.
- Component Light intensity 7→6 and Transform X 4→6 edited successfully; Undo
  restored X4, Redo restored X6; Save disabled after native commit. Native
  viewport source moved and lighting followed. Disk JSON confirmed X6/intensity6
  with unchanged entityId. Fixture remains saved on KINGSTON, no user map edited.
- Actual screenshots inspected in CUA conversation, not exported files.

Limits: this is the authoring document compatibility adapter, not a claim that
all legacy objects/gameplay ECS or old ImGui lighting selection are migrated.
Generic component attach/remove is separate. No production performance claim;
Android/Vulkan acceptance remains unavailable per user. Current opt-in lighting
settings/defaults unchanged. Next: use this component/identity path for subsequent
scene object migrations; maintain one canonical owner and tests for each type.
