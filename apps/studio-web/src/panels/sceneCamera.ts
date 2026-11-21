// Editor-only Scene camera navigation math (Unity-style scheme). Pure functions:
// no DOM, no bridge; ViewportPlaceholder and SceneAxisGizmo consume them and the
// native StudioMetalScene owns the actual camera.
//
// Camera convention matches engine/render/Math3D.h Camera:
//   eye = target + (sin(yaw)cos(pitch), sin(pitch), cos(yaw)cos(pitch)) * distance

export type SceneAxis = 'x' | 'y' | 'z';
export type SceneNavigation = 'orbit' | 'pan' | 'look' | 'zoom' | 'select';
export interface SceneCameraState { yaw: number; pitch: number; distance: number; ortho: boolean }
export interface GizmoArm { axis: SceneAxis; sign: 1 | -1; x: number; y: number; depth: number }
type Vec3 = [number, number, number];

export const SCENE_PITCH_LIMIT = Math.PI / 2;
export const FLY_SPEED_MIN = 0.05;
export const FLY_SPEED_MAX = 20;
export const DEFAULT_SCENE_CAMERA: SceneCameraState = { yaw: 0.55, pitch: 0.85, distance: 30, ortho: false };

const dot = (a: Vec3, b: Vec3) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a: Vec3, b: Vec3): Vec3 =>
  [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];

/** Camera basis (world space): right, screen up and forward (eye -> target). */
export function cameraBasis(yaw: number, pitch: number): { right: Vec3; up: Vec3; forward: Vec3 } {
  const cp = Math.cos(pitch), sp = Math.sin(pitch);
  const forward: Vec3 = [-Math.sin(yaw) * cp, -sp, -Math.cos(yaw) * cp];
  // Same right vector as the native camera: stays defined when looking straight down.
  const right: Vec3 = [Math.cos(yaw), 0, -Math.sin(yaw)];
  const up = cross(right, forward);
  return { right, up, forward };
}

/** Screen-space arms of the corner gizmo, sorted back to front (draw order). */
export function gizmoArms(yaw: number, pitch: number, radius: number): GizmoArm[] {
  const { right, up, forward } = cameraBasis(yaw, pitch);
  const arms: GizmoArm[] = [];
  (['x', 'y', 'z'] as SceneAxis[]).forEach((axis, index) => {
    for (const sign of [1, -1] as const) {
      const v: Vec3 = [0, 0, 0];
      v[index] = sign;
      arms.push({ axis, sign, x: dot(v, right) * radius, y: -dot(v, up) * radius, depth: dot(v, forward) });
    }
  });
  // Larger depth = farther from the viewer, drawn first.
  return arms.sort((a, b) => b.depth - a.depth);
}

/** Orientation that places the eye on the clicked arm's side, looking at the pivot. */
export function viewForAxis(axis: SceneAxis, sign: 1 | -1): { yaw: number; pitch: number } {
  if (axis === 'y') return { yaw: 0, pitch: sign * SCENE_PITCH_LIMIT };
  if (axis === 'x') return { yaw: sign * Math.PI / 2, pitch: 0 };
  return { yaw: sign > 0 ? 0 : Math.PI, pitch: 0 };
}

const VIEW_NAMES: Record<string, string> = {
  'x1': 'Right', 'x-1': 'Left', 'y1': 'Top', 'y-1': 'Bottom', 'z1': 'Front', 'z-1': 'Back'
};

/** Name of an axis-aligned view ("Top", "Right"...) or null for a free view. */
export function alignedViewName(yaw: number, pitch: number, tolerance = 0.002): string | null {
  const { forward } = cameraBasis(yaw, pitch);
  for (let i = 0; i < 3; ++i) {
    const value = -forward[i]; // eye side
    if (Math.abs(Math.abs(value) - 1) <= tolerance)
      return VIEW_NAMES[('xyz'[i]) + (value > 0 ? '1' : '-1')];
  }
  return null;
}

/** Unity Scene view mouse mapping (macOS: Alt+Cmd+LMB pans without a middle button). */
export function navigationFor(button: number, mods: { alt: boolean; meta: boolean }): SceneNavigation | null {
  if (button === 1) return 'pan';
  if (button === 2) return mods.alt ? 'zoom' : 'look';
  if (button === 0) {
    if (mods.alt && mods.meta) return 'pan';
    if (mods.alt) return 'orbit';
    return 'select';
  }
  return null;
}

/** Wheel while flying changes fly speed geometrically, like Unity's camera speed. */
export function stepFlySpeed(speed: number, wheelDeltaY: number): number {
  if (!Number.isFinite(speed) || speed <= 0) speed = 1;
  const steps = Math.max(-10, Math.min(10, -wheelDeltaY / 100));
  const next = speed * Math.pow(1.25, steps);
  return Math.min(FLY_SPEED_MAX, Math.max(FLY_SPEED_MIN, Math.round(next * 1000) / 1000));
}

/** Fly acceleration while movement keys stay held (1x -> 3x over two seconds). */
export function flyAcceleration(heldMs: number): number {
  return 1 + Math.min(2, Math.max(0, heldMs) / 1000);
}

/**
 * One movement step for held WASD/QE keys. Units are the native `move` command's
 * (scaled by camera distance natively); clamped to the native validator's ±50.
 */
export function flyStep(keys: ReadonlySet<string>, opts: { elapsedMs: number; heldMs: number; speed: number; boost: boolean }) {
  const scale = Math.min(3, Math.max(0, opts.elapsedMs) / 33.333) *
    opts.speed * flyAcceleration(opts.heldMs) * (opts.boost ? 3 : 1);
  const axis = (positive: string, negative: string) =>
    (Number(keys.has(positive)) - Number(keys.has(negative))) * scale;
  const clamp = (value: number) => Math.max(-50, Math.min(50, value));
  return { right: clamp(axis('d', 'a')), up: clamp(axis('e', 'q')), forward: clamp(axis('w', 's')) };
}

/** Alt+RMB drag zoom: dragging right/down zooms in, as in Unity. */
export function dragZoomAmount(dx: number, dy: number): number {
  return (dx + dy) * 0.6;
}

export function isSceneCameraState(value: unknown): value is SceneCameraState {
  if (typeof value !== 'object' || value === null) return false;
  const v = value as Record<string, unknown>;
  return typeof v.yaw === 'number' && Number.isFinite(v.yaw) &&
    typeof v.pitch === 'number' && Number.isFinite(v.pitch) &&
    typeof v.distance === 'number' && Number.isFinite(v.distance) &&
    typeof v.ortho === 'boolean';
}
