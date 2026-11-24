import { invoke } from './bridge';

/**
 * HTML overlays vs. native GPU views. In the default macOS host the Metal view
 * sits ABOVE the WebView, so any HTML dialog/menu/popover over the Scene or Game
 * would be hidden. Viewports call visibleViewport(): a menu only vacates the corner
 * it covers, and a dialog that covers most of the view hides the surface.
 * New overlays need no registration:
 * dialogs, menus and [data-native-overlay] elements are found automatically.
 */
export const NATIVE_OVERLAY_SELECTOR =
  '[data-native-overlay],[role="dialog"],[role="menu"],.nested-drop-zones,.nested-root-zones,.nested-panels-list[open] .nested-panels-popover,.studio-editor-settings';

export function overlayBlocks(rect: { left: number; top: number; right: number; bottom: number },
  root: ParentNode = document): boolean {
  for (const element of root.querySelectorAll<HTMLElement>(NATIVE_OVERLAY_SELECTOR)) {
    const r = element.getBoundingClientRect();
    if (r.width < 1 || r.height < 1) continue;
    if (r.right > rect.left && r.left < rect.right && r.bottom > rect.top && r.top < rect.bottom) return true;
  }
  return false;
}

type Box = { left: number; top: number; right: number; bottom: number };
const boxArea = (box: Box) => Math.max(0, box.right - box.left) * Math.max(0, box.bottom - box.top);
/** Largest piece of `box` that does not cover `block`. A menu in the corner must not blank the whole view. */
function boxWithout(box: Box, block: Box): Box {
  const hit: Box = {
    left: Math.max(box.left, block.left), top: Math.max(box.top, block.top),
    right: Math.min(box.right, block.right), bottom: Math.min(box.bottom, block.bottom),
  };
  if (hit.right <= hit.left || hit.bottom <= hit.top) return box;
  const options: Box[] = [
    { ...box, bottom: hit.top },
    { ...box, top: hit.bottom },
    { ...box, right: hit.left },
    { ...box, left: hit.right },
  ];
  return options.reduce((best, option) => boxArea(option) > boxArea(best) ? option : best);
}
/**
 * Where the native view may sit. A small menu (Layout) only trims the corner it covers.
 * A dialog that covers most of the view still hides the surface, or the GPU window would paint over it.
 */
export function visibleViewport(bounds: { left: number; top: number; right: number; bottom: number },
  root: ParentNode = document): { x: number; y: number; width: number; height: number; visible: boolean } {
  let box: Box = { left: bounds.left, top: bounds.top, right: bounds.right, bottom: bounds.bottom };
  const full = boxArea(box);
  for (const element of root.querySelectorAll<HTMLElement>(NATIVE_OVERLAY_SELECTOR)) {
    const r = element.getBoundingClientRect();
    if (r.width < 1 || r.height < 1) continue;
    box = boxWithout(box, { left: r.left, top: r.top, right: r.right, bottom: r.bottom });
  }
  const width = box.right - box.left, height = box.bottom - box.top;
  return {
    x: box.left, y: box.top, width: Math.max(0, width), height: Math.max(0, height),
    visible: full > 0 && width > 16 && height > 16 && boxArea(box) >= full * 0.45,
  };
}

/** While a tab is dragged, native Scene/Game windows must step aside or they swallow the drop. */
let dockDrag = false;
const dockDragListeners = new Set<() => void>();
export function dockDragActive(): boolean { return dockDrag; }
export function setDockDrag(active: boolean): void {
  if (dockDrag === active) return;
  dockDrag = active;
  for (const listener of dockDragListeners) listener();
}
export function watchDockDrag(listener: () => void): () => void {
  dockDragListeners.add(listener);
  return () => { dockDragListeners.delete(listener); };
}

/** Hide Scene and Game immediately. Waiting for the next React frame lets the cursor reach the native child, which then swallows the drag. */
export function concealNativeViews(): void {
  setDockDrag(true);
  for (const id of ['scene', 'game'] as const) {
    lastViewportRect.set(id, 'hidden');
    void invoke('studio.viewportRect', { id, x: 0, y: 0, width: 0, height: 0, dpr: 1, visible: false })
      .catch(() => { lastViewportRect.delete(id); });
  }
}

export function watchOverlays(changed: () => void): () => void {
  let frame = 0;
  const schedule = () => { if (!frame) frame = requestAnimationFrame(() => { frame = 0; changed(); }); };
  const observer = new MutationObserver(schedule);
  observer.observe(document.body, { subtree: true, childList: true, attributes: true,
    attributeFilter: ['open', 'class', 'style', 'role', 'hidden'] });
  document.addEventListener('toggle', schedule, true);
  return () => {
    observer.disconnect();
    document.removeEventListener('toggle', schedule, true);
    if (frame) cancelAnimationFrame(frame);
  };
}

/**
 * Viewport rectangles are re-measured on every DOM mutation (selection highlights,
 * hover…). Re-applying an unchanged rect makes the host move the Metal view and
 * reallocate its drawable, which repaints the whole window — so only changes go out.
 */
const lastViewportRect = new Map<string, string>();
export function viewportRectChanged(id: string, rect: { x: number; y: number; width: number; height: number; dpr: number; visible: boolean }): boolean {
  const r = (v: number) => Math.round(v * 2) / 2;
  const key = rect.visible ? [r(rect.x), r(rect.y), r(rect.width), r(rect.height), rect.dpr].join(',') : 'hidden';
  if (lastViewportRect.get(id) === key) return false;
  lastViewportRect.set(id, key);
  return true;
}
export function forgetViewportRect(id: string): void { lastViewportRect.delete(id); }
