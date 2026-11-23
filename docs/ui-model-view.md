# 3D models in UI

`ui::UiModelView` (`engine/ui/UiModelView.h`) renders one model into a texture exposed to RmlUi as `<img src="#model:NAME"/>`. Create one view per image with `init(device, ui, name, width, height)`, render it outside any open graphics pass before `UiLayer::render()`, and call `shutdown()` before destroying the device or UI layer. Dimensions are 16–2048 pixels. The view owns its renderer and color/depth targets; the caller owns models, materials, animation and scene entities.

For a scene object, pass the host's already prepared `render::ModelDraw` and a camera to `renderDraw`. This preserves its model, materials, transform and current pose. For an independent animated object, keep a separate presentation `render::Animator`, advance it once per frame and call `renderAnimator(model, animator, camera)`. A host with a MotionWeave graph can instead pass its evaluated `render::Pose` to `renderPose`. No UI path mutates authoritative ECS state or advances a scene object's animation twice.

```cpp
ui::UiModelView icon;
icon.init(device, uiLayer, "selected", 256, 256);
// Before the main scene pass: draw is the scene entity's current render packet.
icon.renderDraw(draw, portraitCamera);
// RmlUi: <img src="#model:selected" />

render::Animator separateAnimation;
separateAnimation.setModel(&model);
// Configure a clip with Animator::play, then on each presentation frame:
separateAnimation.update(deltaSeconds);
icon.renderAnimator(model, separateAnimation, portraitCamera);
```

The source model and any borrowed draw pointers must remain valid through the synchronous render call. Rendering into the target and sampling it in RmlUi must happen in separate passes. Resource retirement still follows the device's GPU completion rules. The host chooses camera framing and any UI image orientation needed by its graphics backend.
