/**
 * Floating panel windows: dock tabs can be torn out into their own OS windows
 * (for example on a second monitor). Each floating window has its own small
 * dock (several tabs, splits). The main window remembers which windows float,
 * what they hold and where, so they come back on the next launch.
 */
import {
  STUDIO_PANELS, closeTab, dockStacks, moveTab, normalizeDockTree, type DockTree, type StudioPanel
} from './nestedDock.ts';

export const FLOATING_KEY = 'aiverse.studio.floating.v2';
const TREE_PREFIX = 'aiverse.studio.float.';
/** AppKit screen frame of a floating window (origin bottom-left of the main screen). */
export interface FloatingWindow { key: string; panels: StudioPanel[]; x?: number; y?: number; width?: number; height?: number }
type Storage = Pick<globalThis.Storage, 'getItem' | 'setItem'> & Partial<Pick<globalThis.Storage, 'removeItem'>>;
function storage(): Storage | null {
  try { return typeof window === 'undefined' ? null : window.localStorage; } catch { return null; }
}
export const knownPanel = (value: unknown): value is StudioPanel =>
  typeof value === 'string' && (STUDIO_PANELS as readonly string[]).includes(value);
export const validFloatKey = (value: unknown): value is string =>
  typeof value === 'string' && /^[a-z0-9-]{1,40}$/.test(value);
const finite = (value: unknown): number | undefined =>
  typeof value === 'number' && Number.isFinite(value) ? value : undefined;
const panelList = (value: unknown): StudioPanel[] =>
  Array.isArray(value) ? [...new Set(value.filter(knownPanel))] : [];

export function newFloatKey(): string {
  return 'w' + Date.now().toString(36) + Math.floor(Math.random() * 1e6).toString(36);
}
/** `?window=<key>&panels=a,b` marks a WebView that is a floating window. */
export function floatingParams(search: string = typeof window === 'undefined' ? '' : window.location.search):
    { key: string; panels: StudioPanel[] } | null {
  try {
    const query = new URLSearchParams(search);
    const key = query.get('window');
    if (!validFloatKey(key)) return null;
    return { key, panels: panelList((query.get('panels') ?? '').split(',')) };
  } catch { return null; }
}
export function readFloating(store: Storage | null = storage()): FloatingWindow[] {
  try {
    const raw = JSON.parse(store?.getItem(FLOATING_KEY) ?? '[]');
    if (!Array.isArray(raw)) return [];
    const keys = new Set<string>(), seen = new Set<StudioPanel>();
    const out: FloatingWindow[] = [];
    for (const item of raw) {
      if (!item || !validFloatKey(item.key) || keys.has(item.key)) continue;
      const panels = panelList(item.panels).filter(panel => !seen.has(panel));
      if (!panels.length) continue;
      keys.add(item.key); panels.forEach(panel => seen.add(panel));
      const entry: FloatingWindow = { key: item.key, panels };
      const x = finite(item.x), y = finite(item.y), width = finite(item.width), height = finite(item.height);
      if (x !== undefined && y !== undefined && width !== undefined && height !== undefined && width > 0 && height > 0)
        Object.assign(entry, { x, y, width, height });
      out.push(entry);
    }
    return out;
  } catch { return []; }
}
export function saveFloating(list: FloatingWindow[], store: Storage | null = storage()): void {
  try { store?.setItem(FLOATING_KEY, JSON.stringify(list.filter(item => item.panels.length))); } catch { /* storage full or disabled */ }
}
export const floatingPanels = (list: FloatingWindow[]): StudioPanel[] => list.flatMap(item => item.panels);
export const floatOwner = (list: FloatingWindow[], panel: StudioPanel): string | null =>
  list.find(item => item.panels.includes(panel))?.key ?? null;
/** A floating window reported its panels: they now belong to it and to no other window. */
export function setFloatPanels(list: FloatingWindow[], key: string, panels: unknown): FloatingWindow[] {
  if (!validFloatKey(key)) return list;
  const mine = panelList(panels);
  const others = list.map(item => item.key === key ? item : { ...item, panels: item.panels.filter(panel => !mine.includes(panel)) });
  return others.some(item => item.key === key)
    ? others.map(item => item.key === key ? { ...item, panels: mine } : item)
    : [...others, { key, panels: mine }];
}
export const removeFloatWindow = (list: FloatingWindow[], key: string): FloatingWindow[] =>
  list.some(item => item.key === key) ? list.filter(item => item.key !== key) : list;
/** A panel docked back into the main window leaves every floating window. */
export function releasePanel(list: FloatingWindow[], panel: StudioPanel): FloatingWindow[] {
  return list.some(item => item.panels.includes(panel))
    ? list.map(item => ({ ...item, panels: item.panels.filter(p => p !== panel) })) : list;
}
/** Native `window.floatFrame` payload → stored frame (ignored for unknown windows). */
export function updateFloatingFrame(list: FloatingWindow[], payload: unknown): FloatingWindow[] {
  if (!payload || typeof payload !== 'object') return list;
  const p = payload as Record<string, unknown>;
  const x = finite(p.x), y = finite(p.y), width = finite(p.width), height = finite(p.height);
  if (!validFloatKey(p.window) || !list.some(item => item.key === p.window) || x === undefined || y === undefined ||
      width === undefined || height === undefined || width <= 0 || height <= 0) return list;
  return list.map(item => item.key === p.window ? { ...item, x, y, width, height } : item);
}

export const treePanels = (tree: DockTree): StudioPanel[] => dockStacks(tree).flatMap(stack => stack.tabs);
/** The dock of a floating window: its saved arrangement, holding exactly `panels`. */
export function floatTreeFor(stored: unknown, panels: StudioPanel[]): DockTree {
  const single = (): DockTree => normalizeDockTree({ version: 3, root: { kind: 'stack', id: 'float', tabs: panels, active: panels[0] ?? null } });
  if (!stored || typeof stored !== 'object' || !('root' in stored)) return single();
  let tree = normalizeDockTree(stored);
  for (const panel of treePanels(tree)) if (!panels.includes(panel)) tree = closeTab(tree, panel);
  const first = dockStacks(tree)[0];
  if (!first) return single();
  for (const panel of panels) if (!treePanels(tree).includes(panel)) tree = moveTab(tree, panel, first.id);
  return treePanels(tree).length ? tree : single();
}
export function readFloatTree(key: string, panels: StudioPanel[], store: Storage | null = storage()): DockTree {
  try { return floatTreeFor(JSON.parse(store?.getItem(TREE_PREFIX + key) ?? 'null'), panels); }
  catch { return floatTreeFor(null, panels); }
}
export function saveFloatTree(key: string, tree: DockTree, store: Storage | null = storage()): void {
  try { store?.setItem(TREE_PREFIX + key, JSON.stringify(tree)); } catch { /* storage full or disabled */ }
}
export function forgetFloatTree(key: string, store: Storage | null = storage()): void {
  try { store?.removeItem?.(TREE_PREFIX + key); } catch { /* storage disabled */ }
}
