/** Genre-independent, JSON-serializable docking state. No engine or React dependency. */
export const DOCK_AREAS = ['left', 'center', 'right', 'bottom'] as const;
export const DOCK_PANELS = ['hierarchy', 'assets', 'scene', 'game', 'materials', 'animation', 'inspector', 'console'] as const;

export type DockArea = typeof DOCK_AREAS[number];
export type DockPanel = typeof DOCK_PANELS[number];
export type DockAreas = Record<DockArea, DockPanel[]>;
export type DockActive = Record<DockArea, DockPanel | null>;
export interface DockLayout {
  areas: DockAreas;
  active: DockActive;
  leftSize: number;
  rightSize: number;
  bottomSize: number;
}
export const PANEL_TITLES: Record<DockPanel, string> = {
  hierarchy: 'Hierarchy', assets: 'Assets', scene: 'Scene', game: 'Game',
  materials: 'Materials', animation: 'Animation', inspector: 'Inspector', console: 'Console'
};
export const DEFAULT_AREAS: Record<DockPanel, DockArea> = {
  hierarchy: 'left', assets: 'left', scene: 'center', game: 'center',
  materials: 'center', animation: 'center', inspector: 'right', console: 'bottom'
};
const KEY = 'aiverse.studio.dock.v2';
const OLD_KEY = 'aiverse.studio.layout.v1';

export function defaultDockLayout(): DockLayout {
  return {
    areas: {
      left: ['hierarchy', 'assets'],
      center: ['scene', 'game', 'materials', 'animation'],
      right: ['inspector'],
      bottom: ['console']
    },
    active: { left: 'hierarchy', center: 'scene', right: 'inspector', bottom: 'console' },
    leftSize: 260, rightSize: 300, bottomSize: 180
  };
}
function record(input: unknown): Record<string, unknown> | null {
  return input !== null && typeof input === 'object' && !Array.isArray(input)
    ? input as Record<string, unknown> : null;
}
function knownPanel(value: unknown): value is DockPanel {
  return typeof value === 'string' && (DOCK_PANELS as readonly string[]).includes(value);
}
function clampWidth(input: unknown, fallback: number, min: number, max: number): number {
  return typeof input === 'number' && Number.isFinite(input)
    ? Math.min(max, Math.max(min, Math.round(input))) : fallback;
}

/** Deduplicate panel IDs across all slots and ignore corrupt/localStorage input. */
export function normalizeDockLayout(input: unknown): DockLayout {
  const obj = record(input);
  if (!obj) return defaultDockLayout();
  const sourceAreas = record(obj.areas);
  if (!sourceAreas) return defaultDockLayout();
  const sourceActive = record(obj.active);
  const areas = { left: [], center: [], right: [], bottom: [] } as DockAreas;
  const active = { left: null, center: null, right: null, bottom: null } as DockActive;
  const seen = new Set<DockPanel>();
  for (const area of DOCK_AREAS) {
    const candidates = sourceAreas[area];
    if (!Array.isArray(candidates)) continue;
    for (const candidate of candidates) {
      if (knownPanel(candidate) && !seen.has(candidate)) {
        areas[area].push(candidate);
        seen.add(candidate);
      }
    }
    const preferred = sourceActive?.[area];
    active[area] = knownPanel(preferred) && areas[area].includes(preferred)
      ? preferred : areas[area][0] ?? null;
  }
  return {
    areas, active,
    leftSize: clampWidth(obj.leftSize, 260, 180, 600),
    rightSize: clampWidth(obj.rightSize, 300, 180, 600),
    bottomSize: clampWidth(obj.bottomSize, 180, 110, 480)
  };
}
export function dockAreaOf(layout: DockLayout, panel: DockPanel): DockArea | null {
  for (const area of DOCK_AREAS) if (layout.areas[area].includes(panel)) return area;
  return null;
}
export function moveDockPanel(layout: DockLayout, panel: DockPanel, target: DockArea, before?: DockPanel): DockLayout {
  const next = normalizeDockLayout(layout);
  const previous = dockAreaOf(next, panel);
  if (previous === null) return next;
  const previousTabs = next.areas[previous].filter(id => id !== panel);
  next.areas[previous] = previousTabs;
  if (next.active[previous] === panel) next.active[previous] = previousTabs[0] ?? null;
  const targetTabs = next.areas[target];
  const index = before ? targetTabs.indexOf(before) : -1;
  if (index >= 0) targetTabs.splice(index, 0, panel);
  else targetTabs.push(panel);
  next.active[target] = panel;
  return next;
}
export function setActiveDockPanel(layout: DockLayout, area: DockArea, panel: DockPanel): DockLayout {
  const next = normalizeDockLayout(layout);
  if (next.areas[area].includes(panel)) next.active[area] = panel;
  return next;
}
export function closeDockPanel(layout: DockLayout, panel: DockPanel): DockLayout {
  const next = normalizeDockLayout(layout);
  const area = dockAreaOf(next, panel);
  if (area === null) return next;
  next.areas[area] = next.areas[area].filter(id => id !== panel);
  if (next.active[area] === panel) next.active[area] = next.areas[area][0] ?? null;
  return next;
}
export function openDockPanel(layout: DockLayout, panel: DockPanel, preferred = DEFAULT_AREAS[panel]): DockLayout {
  const next = normalizeDockLayout(layout);
  const present = dockAreaOf(next, panel);
  const area = present ?? preferred;
  if (present === null) next.areas[area].push(panel);
  next.active[area] = panel;
  return next;
}

export function migrateLegacyLayout(input: unknown): DockLayout {
  const obj = record(input);
  const next = defaultDockLayout();
  if (!obj) return next;
  next.leftSize = clampWidth(obj.leftSize, next.leftSize, 180, 600);
  next.rightSize = clampWidth(obj.rightSize, next.rightSize, 180, 600);
  if (obj.leftTab === 'assets' || obj.leftTab === 'hierarchy') next.active.left = obj.leftTab;
  if (obj.selectedTab === 'scene' || obj.selectedTab === 'game' ||
      obj.selectedTab === 'materials' || obj.selectedTab === 'animation') {
    next.active.center = obj.selectedTab;
  }
  if (obj.showConsole === false) {
    next.areas.bottom = [];
    next.active.bottom = null;
  }
  return next;
}
export function readDockLayout(): DockLayout {
  if (typeof window === 'undefined') return defaultDockLayout();
  try {
    const latest = window.localStorage.getItem(KEY);
    if (latest !== null) return normalizeDockLayout(JSON.parse(latest) as unknown);
    const legacy = window.localStorage.getItem(OLD_KEY);
    return legacy ? migrateLegacyLayout(JSON.parse(legacy) as unknown) : defaultDockLayout();
  } catch { return defaultDockLayout(); }
}
export function saveDockLayout(layout: DockLayout): void {
  if (typeof window === 'undefined') return;
  try { window.localStorage.setItem(KEY, JSON.stringify(normalizeDockLayout(layout))); }
  catch { /* Private browsing, restricted WebView or quota errors must not break the editor. */ }
}
