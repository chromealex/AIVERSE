# Stage 49 — build-aware model format discovery

Branch feature/spectrafx-html5-data-interfaces-20261123; not merged. Commits 392b5df4aea5865e6e87f228bdb488f0c74a4b4e, 5ce3ea4298e683032d9d003d8ac43849d090e51d, 604d4f622c773a394b60558f595693e3e960833c.

## Architecture and changes
SpectraFxSceneHost sets the CPU mesh loader to meshFromModelFile, which calls render::loadModel. Model.cpp registers formats according to build flags and falls back to path+.glb when no native loader exists. StudioSession.cpp adds modelLoadableFormat to studio.assets model rows using isNativeModelFormat and indexed converted sibling. spectraStudioAssetIndex.ts filters explicitly unsupported model formats, preserving compatibility with older hosts. Skill updated.

## Tests
None; deferred by user. No compilation or runtime verification.

## Limits and next
Format availability is not decode or GPU success. Audit native linking, malformed model diagnostics, asset refresh debounce and selection lock lifecycle. GPU shader RNG and exact capture provenance remain outstanding. Continue feature work before QA.
