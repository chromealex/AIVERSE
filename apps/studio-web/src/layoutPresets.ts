/** Named editor layouts. One factory set per workspace, plus layouts the user saves in this browser. */
import { normalizeDockTree, type DockTree } from './nestedDock.ts';
import { WORKSPACE_INFO, WORKSPACES, isWorkspace, saveWorkspaceDock, workspaceDockTree, type StudioWorkspace } from './workspaces.ts';

export interface LayoutPreset {
  id: string;
  title: string;
  workspace: StudioWorkspace;
  factory: boolean;
  tree: DockTree;
}

/** Scene with Assets as a full-height column on the left. The bottom strip no longer owns Assets. */
export function sceneAssetsLeft(): DockTree {
  return normalizeDockTree({
    version: 3,
    root: {
      kind: 'split', id: 'root', axis: 'horizontal', ratio: .22,
      first: { kind: 'stack', id: 'dock-assets', tabs: ['assets'], active: 'assets' },
      second: {
        kind: 'split', id: 'split-main', axis: 'vertical', ratio: .74,
        first: {
          kind: 'split', id: 'split-body-left', axis: 'horizontal', ratio: .22,
          first: { kind: 'stack', id: 'dock-left', tabs: ['hierarchy', 'project'], active: 'hierarchy' },
          second: {
            kind: 'split', id: 'split-body-right', axis: 'horizontal', ratio: .74,
            first: { kind: 'stack', id: 'dock-center', tabs: ['scene', 'game'], active: 'scene' },
            second: { kind: 'stack', id: 'dock-right', tabs: ['inspector', 'history'], active: 'inspector' },
          },
        },
        second: { kind: 'stack', id: 'dock-bottom', tabs: ['console', 'agent'], active: 'console' },
      },
    },
  });
}

export function factoryPresets(workspace: StudioWorkspace): LayoutPreset[] {
  const presets: LayoutPreset[] = [{
    id: workspace + ':default', title: WORKSPACE_INFO[workspace].title + ' default',
    workspace, factory: true, tree: workspaceDockTree(workspace),
  }];
  if (workspace === 'scene') presets.push({
    id: 'scene:assets-left', title: 'Assets on the left', workspace, factory: true, tree: sceneAssetsLeft(),
  });
  return presets;
}

const KEY = 'aiverse.studio.presets.v1';
type Store = Pick<Storage, 'getItem' | 'setItem'>;
function browserStore(): Store | null {
  try { return typeof window === 'undefined' ? null : window.localStorage; } catch { return null; }
}

export function readSavedPresets(workspace: StudioWorkspace | null = null, store: Store | null = browserStore()): LayoutPreset[] {
  try {
    const raw = store?.getItem(KEY);
    const parsed: unknown = raw ? JSON.parse(raw) : [];
    if (!Array.isArray(parsed)) return [];
    const out: LayoutPreset[] = [];
    for (const item of parsed) {
      if (!item || typeof item !== 'object') continue;
      const row = item as { id?: unknown; title?: unknown; workspace?: unknown; tree?: unknown };
      if (typeof row.id !== 'string' || typeof row.title !== 'string' || !isWorkspace(row.workspace)) continue;
      if (workspace && row.workspace !== workspace) continue;
      out.push({ id: row.id, title: row.title.slice(0, 40), workspace: row.workspace, factory: false, tree: normalizeDockTree(row.tree) });
    }
    return out.slice(0, 24);
  } catch { return []; }
}

function writeAll(list: LayoutPreset[], store: Store | null): void {
  const saved = list.filter(preset => !preset.factory).map(preset => ({
    id: preset.id, title: preset.title, workspace: preset.workspace, tree: preset.tree,
  }));
  try { store?.setItem(KEY, JSON.stringify(saved)); } catch { /* private mode or quota */ }
}

export function saveLayoutPreset(title: string, workspace: StudioWorkspace, tree: DockTree, store: Store | null = browserStore()): LayoutPreset[] {
  const name = title.trim().slice(0, 40) || 'Layout';
  const preset: LayoutPreset = {
    id: workspace + ':' + Date.now().toString(36), title: name, workspace, factory: false, tree: normalizeDockTree(tree),
  };
  const all = readSavedPresets(null, store);
  const next = [...all, preset].slice(-24);
  writeAll(next, store);
  return next;
}

export function deleteLayoutPreset(id: string, store: Store | null = browserStore()): LayoutPreset[] {
  const next = readSavedPresets(null, store).filter(preset => preset.id !== id);
  writeAll(next, store);
  return next;
}

/** Put every workspace back on its factory arrangement. Named presets stay. */
export function resetWorkspaceLayouts(store: Store | null = browserStore()): void {
  for (const id of WORKSPACES) saveWorkspaceDock(id, workspaceDockTree(id), store);
}

export const LAYOUT_FILE_KIND = 'aiverse.studio.layout';

export function layoutDownloadName(title: string): string {
  const clean = title.trim().replace(/[^\w.-]+/g, '-').replace(/^-+|-+$/g, '').slice(0, 40);
  return (clean || 'layout') + '.layout.json';
}

/** A layout file is a download the user asked for. It is not a project write. */
export function serializeLayoutFile(title: string, workspace: StudioWorkspace, tree: DockTree): string {
  return JSON.stringify({
    kind: LAYOUT_FILE_KIND, version: 1,
    title: title.trim().slice(0, 40) || 'Layout',
    workspace, tree: normalizeDockTree(tree),
  });
}

export function parseLayoutFile(text: string): { title: string; workspace: StudioWorkspace; tree: DockTree } | null {
  try {
    const value = JSON.parse(text) as { kind?: unknown; version?: unknown; title?: unknown; workspace?: unknown; tree?: unknown };
    if (!value || value.kind !== LAYOUT_FILE_KIND || value.version !== 1 || !isWorkspace(value.workspace)) return null;
    const title = typeof value.title === 'string' && value.title.trim() ? value.title.trim().slice(0, 40) : 'Layout';
    return { title, workspace: value.workspace, tree: normalizeDockTree(value.tree) };
  } catch { return null; }
}
