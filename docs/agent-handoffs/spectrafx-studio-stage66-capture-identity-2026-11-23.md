# Stage 66 — capture identity safety

Branch feature/spectrafx-html5-data-interfaces-20261123; not merged. Commits 51c1807e733ab24b936daee0bbb584e94d793d65, e8924d1f04342403b1c3a11435a5784bec752bef, cb641e1e718ad48b94323404dd219bc545b2ce74.

StudioScene.h/cpp share frame provenance ring with callbacks rather than capture raw scene pointer. StudioHost.cpp validates project/generation and strict replay tick before accepting image. Skill updated. No tests/builds. Remaining: capture queue replacement/timeout handling, non-Metal tagging, native dispatch and prior feature compile audit. Next consolidate capture safety and deferred QA.
