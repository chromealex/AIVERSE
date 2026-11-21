# VIVID motion review — 2026-11-20

User expectation: MetaHuman Creator-like idle, poses and transitions; inspect hair,
lashes and clothes in motion, deliver video/screenshots, all builds on KINGSTON.
Method: feature-qa; native Creator Metal captures, shared Studio animation helper,
CPU/session and real GPU tests. No Studio GUI interaction (user authorized testing
without locked desktop). Source baseline edbf019e0 plus this stage's code; exact
code SHA is in the separate handoff. No CI or cross-platform validation.

Artifacts root: `/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/motion-review/`.
- `body-poses.mp4`: 15s, 360 actual frames, 24fps; relaxed → A → T → portrait → relaxed.
- `face-idle.mp4`: 7s, 168 actual frames, 24fps; two blink events, gaze/head/breath loop.
- `relaxed.png`, `transition.png`, `a-pose.png`, `t-pose.png`, `relaxed-end.png`.
- `face-open.png`, `face-blink.png`, `face-second-blink.png`, `face-turn.png`.
- `body-final/`, `face/`: original numbered PPM frames; all frame hashes unique.
- `evidence.json`: frame counts and SHA256 of binary and tested model.
- `encode.swift`, `encode-{body,face}.log`: system AVFoundation H264 encoder and results.
- `body-final.log`, `face.log`: native captures, actor=MODEL_MORPH, pipelineFailures=0.
- `gallery.html`: video and screenshot index with explicit known failures.

These are offline fixed-step animation recordings, not real-time performance or
Studio panel evidence. Videos encode unchanged full framebuffer captures including
the existing Creator UI. Key decoded source frames were visually inspected at
both blink events, open/turn, relaxed endpoints, intermediate transition, A and T.
No claim of watching every frame or accepting temporal antialiasing is made.

| Case | Result | Evidence / limitation |
|---|---|---|
| Native render and frame sequence | PASS | 360 + 168 frames; all distinct; no pipeline failure |
| Phase-preserving pose transitions | PASS scoped | Native fixture checks no immediate rotation jump, midpoint, destination and paused switch; transition.png |
| Blink/gaze data | PASS | Named conjugate gaze fixture; both blink frames close eyelids; gray eye shader prevents judging gaze realism |
| Eyelash coverage | PASS technical | Source mask uses A, RGB all255; baker previously chose R; fixed and tested both layouts |
| Lash attachment during blinks | PARTIAL | Inspected open/two closure frames: opaque strips removed, fine edge artifacts remain |
| Hair attachment | PARTIAL | No gross separation in sampled head-motion frames; no physics/groom simulation acceptance |
| Hair silhouette | FAIL | Coarse card edges remain on light backdrop |
| Clothing | FAIL extreme pose | Relaxed/A/intermediate samples acceptable; T exposes skin at both shoulders |
| Eyes | FAIL visual | Gray iris/eye-shell artifacts remain; not resolved by lash alpha |
| Studio UI, real-time pacing, orbit, other rigs | NOT RUN | Native offline diagnostics only |

Executed builds: aiverse_studio, aiverse_vivid_creator,
aiverse_studio_human_appearance_session_tests, aiverse_studio_facial_gpu_tests.
Final build log `../build-motion-phase-fix.log`, all targets PASS.
Initial build PASS, first test run FAIL1/2: seek after play cancels Animator fade.
Replaced that with optional default-zero Play.startTime, preserving outgoing track.
Final `run-tests.sh --no-build --build-dir <KINGSTON>/native --filter
'^(studio_human_appearance_session|studio_facial_gpu)$' --ctest-jobs 1` PASS2/2;
report `native/test-reports/20261120-174042-42412`, log `../test-motion-phase-fix.log`.
Node `test_bake_materials.cjs <tooling> <KINGSTON>/tmp` PASS. No frontend changes,
so web tests/build NOT RUN. git diff --check PASS. Existing compiler warnings remain.

First `body/` recording used the flawed seek implementation and is NOT final
acceptance evidence; only body-final/ and body-poses.mp4 are the corrected result.
An attempted TERM of that first process found it had already exited successfully.
No unrelated process was stopped.

External asset changes: new `content/Characters/Ada-Motion-Review` clones the prior
face-edited character with only repaired eyelash coverage. Working source
`content/Ada/baseColor_8.png` also updated, original backed up under
source-texture-backup/ (source-texture-update.json records it). Older assembled
GLBs are retained unchanged and will still have their old coverage until rebaked.
No MetaHuman binary assets were added to Git. Tests/video do not claim original
Unreal animations, full RigLogic, anatomical correctives or collision-aware cloth.

Next: author proper shirt body-visibility coverage/corrective deformations for
shoulders, isolate eye-shell/iris rendering, then hair filtering/coverage and wider
orbit/motion QA. Generic masks must not be inferred by simply hiding the whole body.


Correction after user video review: user observed hair not bound to the head.
The earlier limited-frame hair attachment assessment is withdrawn; it must be
considered unverified/defective until an explicit head-rotation attachment test
passes. Stage91 adds a socket/SKM assembly mechanism but does not retroactively
fix or validate these recorded Ada frames.
