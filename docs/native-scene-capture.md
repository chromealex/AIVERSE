# Native clean Scene capture

`view_screenshot` with `window:"scene"` defaults to a clean render-texture PNG.
It captures the existing native Renderer3D/PRISMA output without editor UI,
notifications, selection/hover outlines or transform gizmos. The document and
selection remain unchanged. `clean:false` retains the earlier viewport crop
with editor overlays; `window:"all"` and `window:"game"` retain their semantics.
This is tonemapped RGB8 output, not linear-HDR export or an AI-generated scene.

Example arguments:

```json
{"window":"scene","width":2560,"height":1440,
 "x":256,"y":80,"z":385,"distance":300,"pitch":-14.5,"yaw":182.4}
```

The camera target is x/y/z, pitch/yaw are degrees. Scene orbit now allows pitch
-89..89, including eye-level and upward views, in both native FPS/RTS controls
and agent inspection. Camera changes are not map edits. X/Z must appear together;
all numeric camera fields validate before any camera update.

Width and height are optional and must appear together, integer256..4096 with
at most8388608 pixels. Requested dimensions require a clean Scene capture.
Without dimensions, current viewport resolution is retained within the same
readback bounds. Returned `width/height` describe the PNG; `render_width/height`
report actual internal rendering before PRISMA resolve, so a quality policy's
upscaling is not misrepresented. Native2560x1440 output and internal rendering
were both checked on Metal. No image upscaling is added by the capture encoder.

The Scene target temporarily renders at export size for one requested frame,
then returns to its ordinary UI dimensions. Its normal scoped LOD/culling/material
policies still apply. Hit-testing is skipped for that single frame if export
aspect differs; the next regular Scene draw refreshes the usual pick cache.
There is no second simulation/render pass or duplicate water/lens clock update.

Only one screenshot can be pending; overlapping requests return an error before
altering the camera. Hidden/unavailable Scene or unsupported texture readback
returns an explicit error, never a full-editor-frame fallback. The readback uses
Device.captureTexture after the final scene pass; ordinary frames add no readback.
Existing ViewportGpuRetirement and capture lifetime token own GPU/shutdown safety.
Metal is asynchronous; GL's explicit captureTexture path can synchronously read
pixels. GPU/backend parity must be tested separately.

HTML Studio exports the active map with the Scene renderer on its render thread.
The next scheduled map frame draws into a separate color and depth target.
Camera arguments apply to the export projection. The interactive orbit still
belongs to the Scene panel. A changed document, closed panel or expired
capture returns an error. A later ordinary frame resumes the drawable.

## Capture live PHYSIS fields in Scene

Open Scene and start a local play-test of the same flat map with PHYSIS enabled.
Pause the simulation to compare captures at the same tick. In Scene overlays,
enable **Field heatmap** and choose the field, or enable **Field outlines** to
see wind zones, active storms and gravity fields. The PHYSIS preview window has
its own display settings.

For linked Studio sessions, call `view_gizmos` with an empty object and keep
the returned `layers` settings before changing the view. For example, these
arguments show wind speed with a radius of nine cells and one sample per cell.

```json
{"physisHeatmap":true,"physisLayer":8,"physisRadius":9,"physisStride":1,"physisOutlines":true}
```

Call `view_screenshot` with `{"window":"scene","clean":false}` to capture the
overlays. Restore the saved settings with `view_gizmos` after the comparison.
`physisRadius` accepts 6 to 24 cells and `physisStride` accepts 1 to 4 cells.
