// Shared state of the Character Creator sculpt brush. The Sculpt tab owns it;
// the Scene viewport reads it to paint strokes while dragging and to draw the
// brush circle under the cursor.
export interface SculptBrushView { armed: boolean; radius: number }

let state: SculptBrushView = { armed: false, radius: .12 };
const listeners = new Set<(s: SculptBrushView) => void>();

export function sculptBrush(): SculptBrushView { return state; }
export function setSculptBrush(next: SculptBrushView): void {
  if (next.armed === state.armed && next.radius === state.radius) return;
  state = next;
  for (const l of listeners) l(state);
}
export function onSculptBrush(listener: (s: SculptBrushView) => void): () => void {
  listeners.add(listener);
  return () => { listeners.delete(listener); };
}
/** Approximate brush circle in viewport pixels. The face camera frames the
 *  head at roughly 60% of the viewport height; radius is a share of the head. */
export function brushPixels(radius: number, viewportHeight: number): number {
  return Math.max(6, radius * viewportHeight * .6);
}
/** Minimum cursor travel between two drag strokes. */
export function strokeSpacing(brushPx: number): number { return Math.max(6, brushPx * .35); }
