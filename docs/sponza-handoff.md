# Sponza rendering handoff — 2026-11-12

Branch: `codex/sponza-full`. Worktree on the author's machine:
`/Users/aleksandrfeer/Projects/RTSEngine/.worktrees/linear-hdr`.
The user requested that all work be pushed for another agent to continue.
This is a continuation checkpoint, **not a claim that the visual defects are resolved**.

## Latest request / blocker

The user spotted a sharp band/diagonal on the right arch beside green/blue fabric.
The 116x140 crop maps to x=1103, y=245 in the 1280x720 reference camera.
It is present in both AO-off and AO-on captures. An earlier diagnostic with probe
visibility planes disabled removes it. Therefore it is the existing probe visibility
artifact, not the newly baked local AO. The earlier black wedges on left columns
also remain. Do not mask these with extra AO, a global light floor, exposure changes,
or disabling visibility in the production cache.

See [before local AO](benchmarks/sponza-2026-11-12/stripe-before.png),
[after local AO](benchmarks/sponza-2026-11-12/stripe-after.png), and
[diagnostic unoccluded probes](benchmarks/sponza-2026-11-12/stripe-unoccluded.png).
The last image is an older diagnostic, not a production solution or exact AO A/B.

## Implemented and verified in this checkpoint

- Fixed TAA jitter coordinates: current/history samples now resolve to an unjittered
  output grid. Stationary centroid drift is < .052 pixels on GL/Metal; two-pixel edge
  transitions in the fixture, versus prior visible camera shaking.
- Mobile GI: four bilinear visibility taps (three-tap filtering was rejected),
  adaptive four-to-eight probe evaluation in one loop for low coverage, fixed-radiance
  sky/sun precombination on the upload copy. Original cache remains independently
  relightable; the precombined runtime copy requires reupload for relighting.
- Correct signed-plane probe visibility comparison: removed the erroneous 2x radial
  depth clamp. This fixes the synthetic coplanar regression, not all real scene bands.
- Material AO now attenuates baked GI and ambient rim, not direct sun/point lights.
  Sponza columns have no imported AO maps, so this alone did not improve their cavities.
- Added opt-in `MeshOcclusion.h` bake + `MeshOcclusionCache.h`. Static opaque mesh-local
  ray-traced visibility goes in unused vertex color alpha. Material flag enables AO
  independently of vertex RGB; no added vertex stride, sampler, render pass or history.
  GL and Metal, including Low material quality, use the same AO. Existing models default off.
- AO cache matches source geometry, topology, normals, colors, relevant material flags,
  radius/sample settings, version, plus payload checksum. Refuses occupied alpha and
  incompatible/shared vertex use. Transparent/double-sided/deformed primitives are skipped.
  Mesh-local AO does not include neighboring meshes/instances or normal-map-only relief.
- Renderbench optional `meshOcclusion` block supports offline `write` / runtime `read`.
  `reference_refinement.py --local-occlusion` generates an authoring configuration.

See `docs/probe-gi.md` for API, limitations and cost; `docs/sponza-full.md` for observations.

## Artifacts committed with this handoff

`docs/benchmarks/sponza-2026-11-12/`:

- Actual before/after Mobile, High and GL screenshots (no generated imagery).
- Per-frame benchmark JSON for Metal Mobile AO off/on, Metal High AO on,
  GL Mobile AO off/on. Same camera, lights and dense GI field.
- CPU/GPU/TAA verification logs, cropped stripe evidence, and final scene recipes.
- The recipes must be copied beside the external scene assets to resolve relative paths.

M3 Ultra, 1280x720, full desktop profile, 60 warmup + 120 measured frames:
Metal Mobile off/on median 20.10/19.21 ms; GL Mobile off/on 19.16/18.76 ms;
Metal High on 28.38 ms. Consider GPU-clock/presentation variability; do not claim AO
speeds up rendering. These are **not Android results**, nor proof of 30 FPS on a phone.

The local AO bake traces 2,027,431 vertices in ~44 s; cache size 2,035,098 bytes.
Cached validation/application was ~132 ms on the Mac. Preview does not rebake.
Fixed screenshot-region means: foreground capital -7.5%, right capital -10.4%,
arch interior -.3%. Remaining GI bands are still visible in final screenshots.

## External files (not committed)

Assets: `/Volumes/KINGSTON/AIVERSE/Sponza`.
Build: `/Volumes/KINGSTON/AIVERSE/Builds/.temp/sponza-arm64`.

- `day-mobile-dense.avgic`: 10,710 probes, spacing 1.25 m, 128 rays, three bounces;
  17.66 MiB atlas. Used for both High and Mobile, identical lighting.
- `day-local-ao.avao`: final AO cache, radius .75 mesh units, 64 sphere directions.
- `scene-day-mobile-local-ao.json`, `scene-day-high-local-ao.json`: final cached recipes.
- `scene-day-high-bake-ao.json`: one-time AO writer (refuses overwrite).
- `scene-day-mobile-dense.json`: AO-off control.
- `diagnostic-unoccluded.avgic` / `diagnostic-dc-only.avgic`: diagnostic only, never ship.
- Full imported glTF/textures and licenses remain on KINGSTON, not in Git.

Ignored local preview launchers now use cached AO:
`build/preview-sponza-probe-gi.command` (Mobile GI / moving preview),
`build/preview-sponza-day-lit.command` (High / fixed reference camera).

## Reproduce

From the worktree above (explicit working directory):

```sh
cmake --build /Volumes/KINGSTON/AIVERSE/Builds/.temp/sponza-arm64 \
  --target aiverse_renderbench aiverse_renderbench_gl aiverse_hdr_gl aiverse_hdr_scene aiverse_static_light_grid_test -j 6
AIVERSE_HDR_VERIFY=1 /Volumes/KINGSTON/AIVERSE/Builds/.temp/sponza-arm64/aiverse_hdr_gl
AIVERSE_HDR_VERIFY=1 /Volumes/KINGSTON/AIVERSE/Builds/.temp/sponza-arm64/aiverse_hdr_scene
AIVERSE_TAA_VERIFY=1 /Volumes/KINGSTON/AIVERSE/Builds/.temp/sponza-arm64/aiverse_hdr_gl
AIVERSE_TAA_VERIFY=1 /Volumes/KINGSTON/AIVERSE/Builds/.temp/sponza-arm64/aiverse_hdr_scene
/Volumes/KINGSTON/AIVERSE/Builds/.temp/sponza-arm64/aiverse_static_light_grid_test
/Volumes/KINGSTON/AIVERSE/Builds/.temp/sponza-arm64/aiverse_renderbench \
  --scene /Volumes/KINGSTON/AIVERSE/Sponza/scene-day-mobile-local-ao.json \
  --read-light-cache day-mobile-dense.avgic --profile full --camera 0 \
  --warmup 60 --samples 120 --capture /tmp/continue.ppm --output /tmp/continue.json
```

Use `_gl` executable for GL, High recipe for High. Run GPU benchmarks sequentially.
Wait for process completion: Metal capture may appear after timing JSON.
To recreate GI when absent, omit `--read-light-cache` and use
`--write-light-cache /absolute/new-file.avgic`. To recreate AO, remove its `read`
key and set `write` to a new absolute output. Preserve recipe metadata for GI cache
key compatibility. Do not overwrite imported original assets.

Builds passed without compiler warnings. CPU tests and 52 HDR GPU probes passed
on GL/Metal. TAA checks passed both. Android/web and actual mobile FPS are unverified.
No simulation changes or simulation/headless validation in this checkpoint.

## Suggested next investigation

1. Reproduce the right-band crop plus left-column wedges with current source/cache.
2. Obtain exact receiver world position/normal with the existing `SunStudy.h` camera
   ray diagnostic and BVH. Compare exact rays to the eight surrounding probes with
   coarse 4x4-per-cube-face plane visibility. Determine whether planes overextend
   finite blockers or probes are inside geometry; don't guess from the image alone.
3. A DC-only SH diagnostic kept the wedges; disabling visibility removed them.
   A .125 m receiver-normal bias flattened the lighting and did not solve them.
   A 3–15 cm smooth plane threshold brightened the scene without removing bands.
   Both experiments were reverted. Adaptive four-to-eight sampling fixed the large
   foreground-capital hole but not all bands. Consider relocation/validity or more
   accurate visibility only after the exact-ray comparison; preserve mobile budgets.
4. Keep the reference's capital cavities, arch detail, crisp texture and stable camera.
   Local AO is complete as a mesh-local feature, not a full Unreal reference match.
