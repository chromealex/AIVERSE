import test from 'node:test';
import assert from 'node:assert/strict';
import {
  alignedViewName, cameraBasis, dragZoomAmount, flyStep, gizmoArms, isSceneCameraState,
  navigationFor, stepFlySpeed, viewForAxis, FLY_SPEED_MAX, FLY_SPEED_MIN
} from '../src/panels/sceneCamera.ts';

const close = (a, b, eps = 1e-6) => assert.ok(Math.abs(a - b) < eps, `${a} != ${b}`);

test('camera basis matches the native eye/right convention', () => {
  const { right, forward, up } = cameraBasis(0, 0);
  // yaw 0, pitch 0: eye on +Z looking toward -Z, X to the right, Y up.
  close(forward[2], -1); close(right[0], 1); close(up[1], 1);
});

test('gizmo arms project world axes and draw back to front', () => {
  const arms = gizmoArms(0, 0, 10);
  const x = arms.find(a => a.axis === 'x' && a.sign === 1);
  const y = arms.find(a => a.axis === 'y' && a.sign === 1);
  const z = arms.find(a => a.axis === 'z' && a.sign === 1);
  close(x.x, 10); close(x.y, 0);
  close(y.x, 0); close(y.y, -10); // SVG y grows downwards
  close(z.x, 0); close(z.depth, -1); // +Z points at the viewer -> drawn last
  assert.equal(arms.at(-1).axis, 'z'); assert.equal(arms.at(-1).sign, 1);
  for (let i = 1; i < arms.length; ++i) assert.ok(arms[i - 1].depth >= arms[i].depth);
});

test('clicking an arm puts the eye on that side and names the view', () => {
  for (const [axis, sign, name] of [['x', 1, 'Right'], ['x', -1, 'Left'], ['y', 1, 'Top'],
    ['y', -1, 'Bottom'], ['z', 1, 'Front'], ['z', -1, 'Back']]) {
    const view = viewForAxis(axis, sign);
    assert.equal(alignedViewName(view.yaw, view.pitch), name);
    const { forward } = cameraBasis(view.yaw, view.pitch);
    const index = 'xyz'.indexOf(axis);
    close(-forward[index], sign, 1e-6);
  }
  assert.equal(alignedViewName(0.55, 0.85), null);
});

test('Unity mouse mapping: LMB selects, Alt orbits/zooms, RMB flies, MMB pans', () => {
  const none = { alt: false, meta: false };
  assert.equal(navigationFor(0, none), 'select');
  assert.equal(navigationFor(0, { alt: true, meta: false }), 'orbit');
  assert.equal(navigationFor(0, { alt: true, meta: true }), 'pan');
  assert.equal(navigationFor(1, none), 'pan');
  assert.equal(navigationFor(2, none), 'look');
  assert.equal(navigationFor(2, { alt: true, meta: false }), 'zoom');
  assert.equal(navigationFor(3, none), null);
  assert.ok(dragZoomAmount(10, 0) > 0 && dragZoomAmount(0, -10) < 0);
});

test('fly speed steps with the wheel and stays bounded', () => {
  assert.ok(stepFlySpeed(1, -100) > 1);
  assert.ok(stepFlySpeed(1, 100) < 1);
  assert.equal(stepFlySpeed(FLY_SPEED_MAX, -10000), FLY_SPEED_MAX);
  assert.equal(stepFlySpeed(FLY_SPEED_MIN, 10000), FLY_SPEED_MIN);
  assert.equal(stepFlySpeed(Number.NaN, 0), 1);
});

test('held fly keys accelerate, boost and clamp to the native limit', () => {
  const keys = new Set(['w', 'd', 'q']);
  const base = flyStep(keys, { elapsedMs: 33.333, heldMs: 0, speed: 1, boost: false });
  close(base.forward, 1, 1e-3); close(base.right, 1, 1e-3); close(base.up, -1, 1e-3);
  const later = flyStep(keys, { elapsedMs: 33.333, heldMs: 5000, speed: 1, boost: true });
  close(later.forward, 9, 1e-3);
  const huge = flyStep(new Set(['s']), { elapsedMs: 500, heldMs: 5000, speed: 20, boost: true });
  assert.equal(huge.forward, -50);
  const idle = flyStep(new Set(), { elapsedMs: 33, heldMs: 0, speed: 1, boost: false });
  assert.deepEqual([idle.right, idle.up, idle.forward].map(Math.abs), [0, 0, 0]);
});

test('camera events are validated before use', () => {
  assert.ok(isSceneCameraState({ yaw: 0, pitch: 0, distance: 3, ortho: false }));
  assert.ok(!isSceneCameraState({ yaw: 'a', pitch: 0, distance: 3, ortho: false }));
  assert.ok(!isSceneCameraState({ yaw: 0, pitch: Number.NaN, distance: 3, ortho: true }));
  assert.ok(!isSceneCameraState(null));
});
