# STRATA Stage 18.5 — lossless/lossy visual acceptance matrix

This is an acceptance **plan and offline raw-capture comparator**, not a
claim that Metal, Vulkan or GLES3 have passed hardware raster parity.

## Preconditions

Use the exact Git commit SHA and record OS, GPU, driver, backend, image
resolution, sample count, color space and full Strata feature gate values.
Do not turn on Vulkan GPU production selection/raster/temporal parity
flags simply to get screenshots: only run separately opted-in diagnostic
render paths with the existing single Renderer3D draw owner.

Capture outputs from the actual hardware render target (no screenshot
substitution): main RGBA8, sun shadow Depth32Float, spot shadow
Depth32Float, planar reflection RTT RGBA8, with both original/Legacy
and Strata aliases; use equal camera transforms and lighting inputs.
Ensure a nonempty, nonconstant depth target and a nonblank reflection
difference versus the empty scene. Do not compare blank captures.

## Required scenario matrix

| Scene | Camera trace | LOD mode | Main RGBA | Sun depth | Spot depth | Reflection RTT |
| --- | --- | --- | --- | --- | --- | --- |
| Two textured opaque materials, duplicated UV seam | two independent views | Lossless fine | Exact parity | Exact | Exact | Exact |
| Two textured opaque materials, duplicated UV seam | two independent views | Lossless coarse | Exact parity | Exact | Exact | Exact |
| One-material disconnected UV islands | orbit crossing seam | Lossy mixed LOD | seam ROI quality | seam ROI quality | seam ROI quality | seam ROI quality |
| Cross-mesh transformed source seam | orbit and nonuniform transform | Lossy mixed LOD | seam ROI quality | seam ROI quality | seam ROI quality | seam ROI quality |
| T-junction / noncoincident material border | static | any | must decline STRATA | must decline | must decline | must decline |
| Residency pressure while orbiting | static+moving cameras | root fallback | compare to valid root | compare | compare | compare |
| Oscillation around LOD threshold | 240 deterministic frames | h=0, h=0.2 | popping statistics | flicker assessment | flicker assessment | flicker assessment |

Repeat supported scenarios on Metal, Vulkan, and GLES3 fallback.
The lossless tests should use tolerance=0 for raw byte parity.
Lossy scenes require reviewed, **explicit** pixel/depth tolerances,
an ROI covering the seam, and reference images: never select a
tolerance merely to pass. Separate valid visual discontinuity
investigation from numeric image-diff pass/fail.

Existing Stage13 Metal reflection fixtures verify lossless paths only.
Stage17 seam audits are CPU certification, not a proof of visual GPU
raster correctness. GPU temporal masks and CPU masks still require
their own cold/warm frame-by-frame parity acceptance.

## Offline raw-capture comparison

The script `tools/strata-stage18-image-audit.py` supports:

```sh
python3 tools/strata-stage18-image-audit.py --self-test
python3 tools/strata-stage18-image-audit.py \
  --reference captures/legacy-main.rgba \
  --candidate captures/strata-main.rgba \
  --format rgba8 --width 800 --height 600 --tolerance 0 \
  --roi 360 100 440 500 --report captures/main-seam.json
python3 tools/strata-stage18-image-audit.py \
  --reference captures/legacy-sun.depth32 \
  --candidate captures/strata-sun.depth32 \
  --format depth32 --width 512 --height 512 --tolerance 0
```

Raw RGBA8 means tightly packed R,G,B,A bytes (NOT BGRA), top-to-bottom
rows, exactly width×height×4 bytes. Depth32Float means little-endian
IEEE754 float32, tightly packed, same row convention. The capture owner
must remove backend row padding and perform any needed BGRA conversion
**before** writing raw files. A size mismatch, invalid ROI, nonfinite
depth, missing mandatory capture or failed pixel threshold returns
nonzero rather than claiming parity.

The optional `--manifest path.json` verifies **all required entries**.
Manifest case keys are `backend`, `pass`, `view`, `lod`; capture
entries additionally provide `format`, `width`, `height`,
`reference`, `candidate`, optional `tolerance`, and optional
`roi` [x0,y0,x1,y1]. Paths are relative to the manifest directory.
Example shape (not a claim of actual captures):

```json
{
  "required": [
    {"backend":"Metal","pass":"main","view":"A","lod":"lossless-fine","format":"rgba8"},
    {"backend":"Metal","pass":"sun","view":"A","lod":"lossless-fine","format":"depth32"}
  ],
  "captures": [
    {"backend":"Metal","pass":"main","view":"A","lod":"lossless-fine","format":"rgba8",
     "width":800,"height":600,"tolerance":0,
     "reference":"legacy-main.rgba","candidate":"strata-main.rgba"},
    {"backend":"Metal","pass":"sun","view":"A","lod":"lossless-fine","format":"depth32",
     "width":512,"height":512,"tolerance":0,
     "reference":"legacy-sun.depth32","candidate":"strata-sun.depth32"}
  ]
}
```

This is a deliberately **partial example**. The final release manifest
must include the full approved matrix and all required backend variants,
including both cameras and applicable passes. Do not use a minimal
manifest to claim complete STRATA acceptance.

## Known limits

The offline tool does not inspect material semantics, prove that a
hardware render path ran, certify quality by itself, detect blank scene
equivalence, compute frame time/FPS, or detect dynamic popping unless
the caller supplies frame-by-frame captures and statistics.
Visual frame evidence must be reviewed by a human after numeric checks.

## Stage 19

Once feature work is done: update an isolated integration worktree from
the latest main, reconcile batching/Renderer3D changes, build the full
Strata suite and run focused CPU/recording tests. On each actual
backend, execute native main/depth/reflection parity fixtures, create
a full manifest with capture provenance, and retain the reports.
Keep all parity gates default OFF until results are independently
validated and bound to the tested commit.
