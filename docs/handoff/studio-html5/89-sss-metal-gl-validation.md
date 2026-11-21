# Stage 89 — Metal and GL SSS visual validation (2026-11-21)

Branch: `feature/studio-html5`. Implementation commit:
`3946b0dfb049d76612528f2a7b8333c79f4a637b`.
Previous SSS implementation: `8dac0a25645bef3f543807d5d503c6f4e7697efc`.

## What changed

- `tests/render/HdrScene.h` accepts `AIVERSE_SSS_TEST` to set the five PBR
  fixture spheres to identical dielectric skin-like materials. The value is
  `sssStrength`; tint is linear RGB [1, 0.35, 0.22], spread 0.8. The usual
  HDR fixture remains unchanged when the variable is absent.
- `tests/render/verify_sss.py` compares four PPM captures (Metal/GL, SSS
  off/on), checks a substantial change within the model/reflection area and
  checks close Metal/GL images. The numerical thresholds are scoped to this
  960×640 HDR fixture, not a general SSS quality metric.
- Durable visual evidence: [Metal off](assets/sss-metal-off.jpg) and
  [Metal on](assets/sss-metal-on.jpg). These are actual Apple M3 Ultra GPU
  captures, compressed from the raw PPM files.

No public API, material JSON or GPU uniform layout changed in this stage.
The active renderer remains the built-in Model PBR shader, not PRISMA or
SpectraFX. No other agent's staged files were included in the code commit.

## Verification actually run

- `cmake --build /private/tmp/aiverse-shadow-noise-20261120 --target
  aiverse_hdr_scene aiverse_hdr_gl -j 4`: PASS.
- Four runs with `AIVERSE_SSS_TEST=0` or `0.85`,
  `AIVERSE_HDR_CAPTURE=/private/tmp/aiverse-sss-{metal,gl}-{off,on}.ppm`,
  and the matching `aiverse_hdr_scene` (Metal) or `aiverse_hdr_gl`:
  PASS. Both report HDR, 11 post passes, SSR 1, reflection 1 and capture
  success. Metal reported Apple M3 Ultra with zero pipeline failures; GL
  reported GL33 Apple M3 Ultra / 4.1 Metal - 89.4.
- `python3 tests/render/verify_sss.py
  /private/tmp/aiverse-sss-metal-off.ppm
  /private/tmp/aiverse-sss-metal-on.ppm
  /private/tmp/aiverse-sss-gl-off.ppm
  /private/tmp/aiverse-sss-gl-on.ppm`: PASS.
  Metal: 12,499 changed pixels, mean channel delta 0.355, bounds
  (302,180)-(705,344). GL: 12,433 changed pixels, mean 0.355, bounds
  (302,180)-(706,342). Metal/GL mean channel deltas are 0.279 (off)
  and 0.283 (on).
- Manually inspected the two Metal images: the reddish diffuse response
  appears on shaded sphere edges and their reflection. The 0.85 setting
  makes thick spheres glow too strongly for realistic skin.
- `git diff --cached --check` on this stage's paths: PASS.

CI URL/status: N/A; no CI dispatched. NOT RUN: Vulkan, GLES/WebGL, target
Studio UI interaction, real thin imported asset, GPU timing A/B/A, whole native
suite. The captures demonstrate a visual change and cross-backend similarity
on this synthetic scene; they do not validate physical transmission or
performance.

## Next action

For production skin/foliage, add a separate opt-in thickness/coverage texture
or material mask and test it on actual thin and thick imported geometry.
That mask should attenuate backlighting on thick parts without altering
specular or existing materials. Keep SSS disabled by default and preserve the
current U2/ICB compatibility until a GPU binding design is reviewed. Also
compare at practical strengths (about 0.1–0.35) in the native Studio viewport.
