# 3D model views in UI — 2026-11-23

`engine/ui/UiModelView` adds a reusable RmlUi dynamic image source backed by an offscreen `Renderer3D` color/depth pair. `renderDraw` accepts the current scene entity's already prepared `ModelDraw`; `renderPose` consumes a published presentation pose; `renderAnimator` reads a separately owned `Animator` without advancing it. The class does not own a second scene object or write simulation ECS state. The target is registered as `#model:NAME` and invalidated before GPU teardown. Added to `aiverse_ui` without linking the source directly into the minimal scene client.

Verification run on macOS: strict C++20 compilation of `UiModelView.cpp` to an object file passed (`-Wall -Wextra -Werror`, output and compiler temporary files on KINGSTON). `python3.12 tools/site/build.py check` reported 37 entries, 0 errors. No native UI screenshot, end-to-end GPU render, backend orientation check or live animation playback was run. Do not mark the visible feature complete or publish a devlog entry until an actual client frame is captured. The roadmap stays `in-progress`.

Next step: connect a concrete UI document and scene/independent animation source in a native host, verify GPU lifetime and orientation on Metal and GL, capture a real frame, then add the development entry and mark the roadmap item done after live verification. No save or replay format changed.

Main integration check: the component compiled from the current `origin/main` with `-Wall -Wextra -Werror`; the site and library catalog checks passed as recorded in the construction-grid integration note. GPU rendering remains unverified after integration.
