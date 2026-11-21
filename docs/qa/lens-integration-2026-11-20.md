# LENS integration acceptance — 2026-11-20

Source branch: `codex/lens-completion`. Integrated main: `291908eb95054b1a331b1b7c54c8147d0330ef2f`.
Latest production repair: `2387b3996`; integration merge: `2fd08e44abcd0b407df79211d3892753dfc7cd81`.
Final verified tree: `783c2bcba193f99960d58fd538bc7641e5b41d7b` (subsequent changes are reports only).

All native builds, compiler caches, test temporary files, logs and screenshots
are under `/Volumes/KINGSTON/AIVERSE-builds/lens-completion`.
Configuration: macOS Debug, x86_64, client + editor + tests enabled.

| Check | Actual result |
|---|---|
| Full configured build, including native client/editor | PASS, `main-integration-build-9.log` |
| Full CTest, 326 registered cases | 323 PASS, 2 failures, 1 explicit GPU opt-in skip; 1079.99 s |
| Repaired/affected cases + explicit Metal rerun | 9/9 PASS, 0 skipped; `final-rechecks.log/xml` |
| Main core executable in full CTest | 274371 checks, 0 failed |
| Late Overlay allocation failure, topology rollback and retry | 18 checks, 0 failed |
| Native Metal primary/prepared replay | 0 mismatched channels (tolerance 2), 12146 changed channels for a distinct camera |
| Editor launch and automatic screenshot | PASS; project scene + LENS camera row loaded, 73 pipelines, 0 failures |
| Native client with `lens.enabled=true` and authored texture camera | PASS, 120 frames, exit 0; 76 pipelines, 0 failures |
| Diff whitespace check against current origin/main | PASS |

The full run's two failures were `animation_ragdoll_asset` and
`animation_model_dependencies`. Their merge incompatibilities were repaired:
neckless ragdolls retain the existing torso fallback, and literal texture
paths no longer become material-library dependencies. Both pass in the final
CTest rerun. The opt-in Metal case also passes there. Thus every registered
case has a passing execution across the full run and targeted rechecks;
this is not a claim that the first full invocation returned success.

Final recheck selection:

```
AIVERSE_LENS_METAL_VALIDATE=1 ctest --test-dir <KINGSTON>/debug --parallel 4   -R '^(animation_ragdoll_asset|animation_model_dependencies|ktx2_transcode|strata_lod_benchmark|lens_metal_frame|lens_sprite_gpu_faults|camera_model_snapshot|view_scene_packets|animation_graphs)$'   --output-on-failure --output-junit <KINGSTON>/final-rechecks.xml
```

Evidence: `full-ctest.log/xml`, `final-rechecks.log/xml`,
`lens-late-overlay-final.log`, `lens-metal-validated.log`,
`lens-editor.png`, `lens-client-enabled.png`, and matching native logs.
The test project is an isolated `qa-project` copy on KINGSTON. The first
client attempt did not enable LENS because strict Python JSON rejected a
commented manifest; only the corrected `lens-client-enabled` run is accepted.

Limitations: expanded interactive editor inspection was blocked by the locked
Mac; automatic screenshots and native rendering did run. ARM64-native,
Vulkan/GLES runtime parity, large camera-count performance, complete P2 camera
features and GPU device-loss recovery were not established here. Legacy
material-paint/field caches remain outside the captured surface transaction.
No performance improvement or universal production parity is claimed.
CI was not manually dispatched; local results above are not CI results.

The implementation preserves save/asset schemas and ordinary noncapture host
behavior. Final publication to main is authorized by the user; the live
handoff records the publication checkpoint.
