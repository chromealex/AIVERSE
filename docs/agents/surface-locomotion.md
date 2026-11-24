# Agent rules: Procedural climbing, spider gaits and surface locomotion

Moved verbatim from AGENTS.md on 2026-11-21 (AGENTS.md keeps the cross-cutting rules; this file is
mandatory reading before working in this area). Newest entries are at the end.

## Procedural climbing, spider gaits and surface locomotion

Before implementing procedural footsteps, wall/ceiling crawling or multi-leg IK movement,
read [Surface locomotion](../../docs/surface-locomotion.md) and [Animation graphs](../../docs/animation-graphs.md).
Reuse `engine/render/SurfaceLocomotion` / the `surfaceLocomotion` rig unit; do not create a
second per-game gait planner. Use `examples/animation/spider_showcase.py` to generate an
original articulated model and the executable floor/wall/ceiling example. Do not assume
that a humanoid model or a single-bone leg can satisfy the required two-bone chains.

- Configure 3..16 independent hip/knee/foot chains, bind-relative home/pole positions,
  clearance/probes, step timing, minSupport/maxSwing and optional FullBodyIK capsules.
- Drive declared velocity/adhesion/reset parameters through C++ or view Lua. Keep state
  per instance; call graph update once per frame. Evaluate must not advance gait twice.
- Stance anchors must remain fixed relative to their surface until a step/lost contact/reset.
  Check reach and support budgets; missing geometry must not fabricate support. Preserve
  residual diagnostics when goals or collision constraints cannot be satisfied.
- `AnimationEnvironment` queries are read-only. Use stable surface IDs and real host
  collision queries for exact geometry; default per-primitive oriented bounds are approximate. Surface
  locomotion accepts wall/ceiling normals; ordinary full-body foot contacts retain slope limits.
- This controller moves the presentation root, not the RTS authoritative entity. Gameplay
  climbing/navigation, networking, saves and gravity remain with their existing owner.
  Never put floating-point rig state into deterministic simulation or bypass Commands.
- Inner-corner floor/wall/ceiling transitions and adhesive support are implemented. Do not
  advertise outer-edge wrapping, automatic navigation, physical adhesion forces, falling,
  deformable-surface transport, CCD or self-collision as implemented by this controller.
- Put the controller last in the pose graph. Subsequent blends/traits may invalidate its
  contacts and status. Use full weight for locked feet; reset after teleport or full detachment.
- Verify `aiverse_animation_tests`, `aiverse_tests animation_`, and the generated spider in
  the editor/client. Report actual tested platforms; retain the existing C++/Lua/JSON schema.

Dynamic scene support: provide `AnimationEnvironment::surfaceTransform` using the same
stable IDs as raycast hits. `surfaceLocomotion` stores local stance/swing-target anchors,
transports its body with the support, and releases removed surfaces. Use followSurface=false
when the authoritative host already carries the actor. GameView supplies per-primitive
oriented bounds and current rigid transforms (including entity_fx); custom physics queries
are required for exact triangles/deforming skins. Test translation, rotation, tilt, steps
on a moving support and removal. Use `spider_showcase.py ... --dynamic-tank` and the editor
`animation_runtime_status` / `rig_runtime_status` operations to verify the actual scene;
a static plane-only preview is not evidence of dynamic-object support.

