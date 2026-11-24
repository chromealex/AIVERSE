/**
 * Editor workspaces: a named dock layout per kind of work (Unity/Blender style).
 * Hierarchy, Inspector, Assets and Console are shared; each workspace adds the
 * panels of its area and remembers its own arrangement in localStorage.
 * Legacy (not yet migrated) ImGui windows are grouped by the same areas so the
 * command palette and Window menu can show where they will live.
 */
import { STUDIO_PANEL_HOME, normalizeDockTree, panelStack, reopenTab, type DockNode, type DockTree, type StudioPanel } from './nestedDock.ts';
import { LEGACY_WINDOWS_NOT_MIGRATED } from './legacyWindows.ts';

export const WORKSPACES = ['scene', 'world', 'look', 'animate', 'character', 'logic', 'publish'] as const;
export type StudioWorkspace = typeof WORKSPACES[number];

export interface WorkspaceInfo {
  id: StudioWorkspace; title: string; purpose: string; key: string;
  left: StudioPanel[]; center: StudioPanel[]; right: StudioPanel[]; bottom: StudioPanel[];
  /** Optional share of the left column (default .19) and of the body over the bottom row (default .74). */
  leftRatio?: number; bodyRatio?: number;
  legacy: string[];
}

export const WORKSPACE_INFO: Record<StudioWorkspace, WorkspaceInfo> = {
  scene: { id: 'scene', title: 'Scene', purpose: 'Place and test', key: '1',
    left: ['hierarchy', 'project'], center: ['scene', 'game'], right: ['inspector', 'history'], bottom: ['assets', 'console', 'agent'],
    legacy: [] },
  world: { id: 'world', title: 'World', purpose: 'Terrain, water, roads', key: '2',
    left: ['hierarchy'], center: ['scene', 'game'], right: ['inspector', 'terrain', 'water', 'roads', 'tiles', 'fog', 'generate', 'fields', 'physis', 'nereid_flow'], bottom: ['assets', 'console'],
    legacy: [] },
  look: { id: 'look', title: 'Look', purpose: 'Materials, light, FX', key: '3',
    left: ['hierarchy'], center: ['scene', 'materials', 'shader_graph'], right: ['inspector', 'lighting', 'postfx', 'effects', 'lightbake', 'models', 'modeling', 'fx_graph', 'fx_functions', 'wind'], bottom: ['assets', 'console', 'texture_resources'],
    legacy: [] },
  animate: { id: 'animate', title: 'Animate', purpose: 'Rigs, clips, graphs', key: '4',
    left: ['hierarchy'], center: ['animation', 'facial_rig', 'scene'], right: ['inspector', 'anim_assets', 'anim_graph', 'anim_rig', 'model_anim', 'attachments'], bottom: ['assets', 'console'],
    legacy: [] },
  character: { id: 'character', title: 'Character', purpose: 'Digital humans', key: '7',
    left: ['human_creator'], center: ['scene', 'facial_rig'], right: ['inspector', 'human_appearance'], bottom: ['assets', 'console'],
    leftRatio: .34, bodyRatio: .86, legacy: [] },
  logic: { id: 'logic', title: 'Logic', purpose: 'Rules, scripts, runtime', key: '5',
    left: ['hierarchy'], center: ['rules', 'scripts', 'game', 'scene'], right: ['runtime', 'inspector', 'input', 'overlays', 'actor', 'raid', 'balance', 'stats'], bottom: ['console', 'assets', 'agent'],
    legacy: [] },
  publish: { id: 'publish', title: 'Publish', purpose: 'Build and audit', key: '6',
    left: ['project', 'hierarchy'], center: ['build', 'game', 'scene', 'snapshot_diff'], right: ['inspector', 'branding', 'asset_audit', 'project_import', 'extensions'], bottom: ['console', 'assets', 'frame_capture'],
    legacy: [] },
};

export function isWorkspace(value: unknown): value is StudioWorkspace {
  return typeof value === 'string' && (WORKSPACES as readonly string[]).includes(value);
}

function stack(id: string, tabs: StudioPanel[]): DockNode { return { kind: 'stack', id, tabs, active: tabs[0] ?? null }; }

/** Default arrangement: left | center | right over a bottom strip. Panels appear once. */
export function workspaceDockTree(id: StudioWorkspace): DockTree {
  const w = WORKSPACE_INFO[id];
  const seen = new Set<StudioPanel>();
  const take = (panels: StudioPanel[]) => panels.filter(p => !seen.has(p) && (seen.add(p), true));
  const left = take(w.left), center = take(w.center), right = take(w.right), bottom = take(w.bottom);
  return normalizeDockTree({ version: 3, root: {
    kind: 'split', id: 'root', axis: 'vertical', ratio: w.bodyRatio ?? .74,
    first: { kind: 'split', id: 'split-body-left', axis: 'horizontal', ratio: w.leftRatio ?? .19,
      first: stack('dock-left', left),
      second: { kind: 'split', id: 'split-body-right', axis: 'horizontal', ratio: .74,
        first: stack('dock-center', center), second: stack('dock-right', right) } },
    second: stack('dock-bottom', bottom) } });
}

/** Area of a legacy window id, or null when it is not grouped. */
export function legacyWorkspace(windowId: string): StudioWorkspace | null {
  for (const id of WORKSPACES) if (WORKSPACE_INFO[id].legacy.includes(windowId)) return id;
  return null;
}
export function legacyWindowsFor(id: StudioWorkspace) {
  return LEGACY_WINDOWS_NOT_MIGRATED.filter(w => WORKSPACE_INFO[id].legacy.includes(w.id));
}

const ACTIVE_KEY = 'aiverse.studio.workspace';
const LAYOUT_KEY = (id: StudioWorkspace) => 'aiverse.studio.dock.v3.' + id;
const LEGACY_SCENE_KEY = 'aiverse.studio.dock.v3';

type Storage = Pick<globalThis.Storage, 'getItem' | 'setItem'>;
function storage(): Storage | null {
  try { return typeof window === 'undefined' ? null : window.localStorage; } catch { return null; }
}
export function readActiveWorkspace(store: Storage | null = storage()): StudioWorkspace {
  try { const v = store?.getItem(ACTIVE_KEY); return isWorkspace(v) ? v : 'scene'; } catch { return 'scene'; }
}
export function saveActiveWorkspace(id: StudioWorkspace, store: Storage | null = storage()): void {
  try { store?.setItem(ACTIVE_KEY, id); } catch { /* private mode */ }
}
const WORKSPACE_ADDED: Partial<Record<StudioWorkspace, StudioPanel[]>> = {
  publish: ['build', 'branding', 'asset_audit', 'project_import', 'catalog', 'snapshot_diff', 'frame_capture', 'extensions'], look: ['wind', 'postfx', 'effects', 'lightbake', 'models', 'modeling', 'fx_graph', 'fx_functions', 'texture_resources', 'catalog'], scene: ['history', 'agent'],
  world: ['terrain', 'water', 'roads', 'tiles', 'fog', 'generate', 'fields', 'physis'], logic: ['scripts', 'input', 'overlays', 'agent', 'actor', 'raid', 'balance', 'stats'],
  animate: ['anim_assets', 'anim_graph', 'anim_rig', 'model_anim', 'attachments'],
};
/** Saved layout of a workspace; the Scene workspace inherits the pre-workspace layout once. */
export function readWorkspaceDock(id: StudioWorkspace, store: Storage | null = storage()): DockTree {
  try {
    const saved = store?.getItem(LAYOUT_KEY(id)) ?? (id === 'scene' ? store?.getItem(LEGACY_SCENE_KEY) : null);
    if (!saved) return workspaceDockTree(id);
    let tree = normalizeDockTree(JSON.parse(saved) as unknown);
    // Panels a workspace gained after its layout was saved appear once (closing them later sticks).
    for (const panel of WORKSPACE_ADDED[id] ?? []) {
      const flag = 'aiverse.studio.ws.added.' + id + '.' + panel;
      if (store?.getItem(flag)) continue;
      if (panelStack(tree, panel) === null) tree = reopenTab(tree, panel, STUDIO_PANEL_HOME[panel]);
      store?.setItem(flag, '1');
    }
    return tree;
  } catch { return workspaceDockTree(id); }
}
export function saveWorkspaceDock(id: StudioWorkspace, tree: DockTree, store: Storage | null = storage()): void {
  try { store?.setItem(LAYOUT_KEY(id), JSON.stringify(normalizeDockTree(tree))); } catch { /* quota */ }
}
