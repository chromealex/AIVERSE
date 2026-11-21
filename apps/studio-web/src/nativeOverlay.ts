/**
 * HTML overlays vs. native GPU views. In the default macOS host the Metal view
 * sits ABOVE the WebView, so any HTML dialog/menu/popover over the Scene or Game
 * would be hidden. Viewports call overlayBlocks() and hide their native surface
 * while a visible overlay intersects them. New overlays need no registration:
 * dialogs, menus and [data-native-overlay] elements are found automatically.
 */
export const NATIVE_OVERLAY_SELECTOR =
  '[data-native-overlay],[role="dialog"],[role="menu"],.nested-panels-list[open] .nested-panels-popover,.studio-editor-settings';

export function overlayBlocks(rect: { left: number; top: number; right: number; bottom: number },
  root: ParentNode = document): boolean {
  for (const element of root.querySelectorAll<HTMLElement>(NATIVE_OVERLAY_SELECTOR)) {
    const r = element.getBoundingClientRect();
    if (r.width < 1 || r.height < 1) continue;
    if (r.right > rect.left && r.left < rect.right && r.bottom > rect.top && r.top < rect.bottom) return true;
  }
  return false;
}

/** Calls `changed` (rAF-debounced) whenever overlays may have appeared, moved or closed. */
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
