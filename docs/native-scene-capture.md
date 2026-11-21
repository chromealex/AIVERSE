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

NativeEditor Metal and shared native Studio compilation were checked; this does
not claim screenshot-operation parity in the separate HTML5 Studio renderer.
Mouse/keyboard interaction remains a separate acceptance check when the Mac is
unlocked. Preserve the Studio native Metal viewport and bridge allowlist.
