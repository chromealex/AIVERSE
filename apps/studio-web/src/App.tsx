import { useCallback, useEffect, useMemo, useState } from 'react';
import { shortcutKey } from './keyboard';
import { ChevronDown, Search, Settings2 } from 'lucide-react';
import { invoke, nativeConnected, nativeWindowAction, nativeWindowResize, onNativeEvent } from './bridge';
import {
  STUDIO_PANELS, STUDIO_PANEL_TITLES, STUDIO_PANEL_HOME, DOCK_PRESET_EVENT, closeTab, dockStacks, panelStack, reopenTab
} from './nestedDock';
import {
  WORKSPACES, WORKSPACE_INFO, legacyWorkspace, readActiveWorkspace, readWorkspaceDock, saveActiveWorkspace,
  saveWorkspaceDock, workspaceDockTree, type StudioWorkspace
} from './workspaces';
import type { CommandEntry } from './commandSearch';
import CommandPalette from './components/CommandPalette';
import PanelErrorBoundary from './components/PanelErrorBoundary';
import { ProjectLoadingDialog, SceneLoadingStrip, sceneLoadingText, useProjectLoading } from './components/ProjectLoading';
import StudioStatusBar from './components/StudioStatusBar';
import WorkspaceTabs from './components/WorkspaceTabs';
import type { StudioPanel } from './nestedDock';
import type { StudioAsset, StudioEntity, StudioGameState, StudioInspector, StudioSnapshot } from './types';
import HeaderGameControls from './components/HeaderGameControls';
import LayoutMenu from './components/LayoutMenu';
import engineLogo from '../../../engine/assets/branding/mark.png';
import NestedDockWorkspace from './components/NestedDockWorkspace';
import StudioMenuBar, { type StudioMenu } from './components/StudioMenuBar';
import { LEGACY_WINDOWS_NOT_MIGRATED } from './legacyWindows';
import WelcomeTour, { tourSeen } from './components/WelcomeTour';
import ProjectHub from './components/ProjectHub';
import ProjectSplash from './components/ProjectSplash';
import { renderStudioPanel } from './panels/StudioPanelRenderer';
import { resetLive } from './panels/livePlay';
import {
  floatOwner, floatingPanels, floatingParams, forgetFloatTree, newFloatKey, readFloatTree, readFloating, releasePanel,
  removeFloatWindow, saveFloatTree, saveFloating, setFloatPanels, treePanels, updateFloatingFrame, validFloatKey,
  type FloatingWindow
} from './floating';
import './components/FloatingShell.css';

/** Menu shortcut glyph. Windows shows Ctrl; macOS keeps the command mark. */
const windowsHost = typeof navigator !== 'undefined' && /Win/i.test(navigator.platform + navigator.userAgent);
const menuShortcut = (key: string) => (windowsHost ? 'Ctrl+' : '⌘') + key;

/** Set when this WebView is a floating window (`?window=…&panels=…`), null in the main window. */
const FLOAT = floatingParams();
const WINDOW_KEY = FLOAT?.key ?? 'main';
const field = (payload: unknown, key: string): unknown =>
  payload && typeof payload === 'object' && key in payload ? (payload as Record<string, unknown>)[key] : undefined;
const broadcast = (event: string, payload: unknown) =>
  void invoke('studio.windowBroadcast', { event, payload }).catch(console.warn);
/** Tell every other Studio window that this one now shows `panel`. */
const announceTaken = (panel: StudioPanel) => broadcast('ui.panelTaken', { panel, by: WINDOW_KEY });
const knownPanel = (value: unknown): value is StudioPanel =>
  typeof value === 'string' && (STUDIO_PANELS as readonly string[]).includes(value);
const showPanelEvent = (panel: StudioPanel) =>
  window.dispatchEvent(new CustomEvent('studio:show-panel', { detail: panel }));

function hierarchyRows(nodes: StudioEntity[], parentId?: string, depth = 0):
  Array<{ node: StudioEntity; depth: number }> {
  return nodes.filter(node => node.parentId === parentId)
    .flatMap(node => [{ node, depth }, ...hierarchyRows(nodes, node.id, depth + 1)]);
}

export default function App() {
  const [snapshot, setSnapshot] = useState<StudioSnapshot | null>(null);
  const [search, setSearch] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [workspace, setWorkspace] = useState<StudioWorkspace>(readActiveWorkspace);
  const [dock, setDock] = useState(() => FLOAT ? readFloatTree(FLOAT.key, FLOAT.panels) : readWorkspaceDock(readActiveWorkspace()));
  const [floating, setFloating] = useState<FloatingWindow[]>(() => FLOAT ? [] : readFloating());
  const [palette, setPalette] = useState<string | null>(null); // null = closed, else initial query
  const [paletteAssets, setPaletteAssets] = useState<StudioAsset[]>([]);
  const [game, setGame] = useState<StudioGameState | null>(null);
  const [sceneAction, setSceneAction] = useState('');
  const [hoverId, setHoverId] = useState('');
  const [dockHover, setDockHover] = useState(false); // a floating window is dragged over the main window
  const [loading, clearLoading] = useProjectLoading();
  // Requests answered while the native side opens a project are expected, not errors.
  useEffect(() => { if (error === 'Project is loading') setError(''); }, [error]);
  useEffect(() => { resetLive(); }, [game?.active,game?.epoch]); // live refs belong to one session
  const [connected] = useState(nativeConnected);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [tourOpen, setTourOpen] = useState(() => !tourSeen());
  const [hubOpen, setHubOpen] = useState(false);
  const [uiScale, setUiScale] = useState<number>(() => {
    const stored = Number(window.localStorage.getItem('aiverse.studio.uiScale'));
    return Number.isFinite(stored) && stored >= 0.75 && stored <= 2 ? stored : 1;
  });
  const [inspectorPin, setInspectorPin] = useState<{
    entity: StudioEntity; inspector: StudioInspector | null;
  } | null>(null);
  useEffect(() => {
    // Scale type, not the page. CSS zoom grew the layout past the monitor and
    // moved the native scene view, which is placed from unzoomed CSS pixels.
    document.documentElement.style.zoom = '';
    document.documentElement.style.setProperty('--au-ui-scale', String(uiScale));
    window.localStorage.setItem('aiverse.studio.uiScale', String(uiScale));
    return () => { document.documentElement.style.removeProperty('--au-ui-scale'); };
  }, [uiScale]);

  // The web view covers the frame, so Windows never sees the edges. The outer pixels start the system resize.
  useEffect(() => {
    if (!windowsHost || FLOAT) return;
    const border = 8;
    type ResizeEdge = 'left' | 'right' | 'top' | 'bottom' | 'top-left' | 'top-right' | 'bottom-left' | 'bottom-right';
    const edgeAt = (x: number, y: number): ResizeEdge | '' => {
      const left = x <= border;
      const right = x >= window.innerWidth - border;
      const top = y <= border;
      const bottom = y >= window.innerHeight - border;
      if (top && left) return 'top-left';
      if (top && right) return 'top-right';
      if (bottom && left) return 'bottom-left';
      if (bottom && right) return 'bottom-right';
      if (left) return 'left';
      if (right) return 'right';
      if (top) return 'top';
      if (bottom) return 'bottom';
      return '';
    };
    const cursorClass = (edge: ResizeEdge) => edge === 'left' || edge === 'right' ? 'studio-edge-ew'
      : edge === 'top' || edge === 'bottom' ? 'studio-edge-ns'
      : edge === 'top-left' || edge === 'bottom-right' ? 'studio-edge-nwse' : 'studio-edge-nesw';
    const paint = (edge: ResizeEdge | '') => {
      const root = document.documentElement;
      root.classList.remove('studio-edge-ew', 'studio-edge-ns', 'studio-edge-nwse', 'studio-edge-nesw');
      if (edge) root.classList.add(cursorClass(edge));
    };
    const move = (event: PointerEvent) => { if (event.buttons === 0) paint(edgeAt(event.clientX, event.clientY)); };
    const down = (event: PointerEvent) => {
      if (event.button !== 0) return;
      const edge = edgeAt(event.clientX, event.clientY);
      if (!edge) return;
      const target = event.target instanceof Element ? event.target : null;
      if (target?.closest('button,a,input,select,textarea')) return;
      event.preventDefault();
      nativeWindowResize(edge);
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerdown', down, true);
    return () => {
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerdown', down, true);
      paint('');
    };
  }, []);

  // Store browser-only layout (not engine state) with bounded write frequency.
  useEffect(() => {
    if (FLOAT) return; // a floating window never owns the main dock layout
    const handle = window.setTimeout(() => saveWorkspaceDock(workspace, dock), 180);
    return () => window.clearTimeout(handle);
  }, [dock, workspace]);
  // Each workspace keeps its own arrangement; switching never loses the current one.
  const switchWorkspace = useCallback((next: StudioWorkspace) => {
    if (next === workspace) return;
    saveWorkspaceDock(workspace, dock);
    saveActiveWorkspace(next);
    setWorkspace(next);
    setDock(readWorkspaceDock(next));
  }, [workspace, dock]);
  // Character Creator "studio layout" button: open the Character workspace.
  useEffect(() => {
    const preset = (event: Event) => {
      if ((event as CustomEvent<string>).detail !== 'character') return;
      // The layout button restores the default Character arrangement.
      switchWorkspace('character'); setDock(workspaceDockTree('character'));
    };
    window.addEventListener(DOCK_PRESET_EVENT, preset);
    return () => window.removeEventListener(DOCK_PRESET_EVENT, preset);
  }, [switchWorkspace]);

  const refresh = useCallback(async () => {
    try {
      setSnapshot(await invoke('studio.snapshot', {}));
      setError('');
    } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
  }, []);
  useEffect(() => {
    void refresh();
    return onNativeEvent((event,payload) => {
      if (event === 'material.preview') {
        if (typeof payload === 'object' && payload !== null &&
            'message' in payload && String(payload.message)) {
          showPanelEvent('scene');
        }
        void refresh();
      } else if (event === 'viewport.hover') {
        setHoverId(payload && typeof payload === 'object' && 'message' in payload ? String(payload.message) : '');
      } else if (event === 'viewport.action') {
        setSceneAction(payload && typeof payload === 'object' && 'message' in payload ? String(payload.message) : '');
      } else if (event === 'ui.panelTaken') {
        // Another window now shows this panel (a tab dragged across windows).
        const panel = field(payload, 'panel');
        if (knownPanel(panel) && field(payload, 'by') !== WINDOW_KEY) {
          setDock(t => panelStack(t, panel) !== null ? closeTab(t, panel) : t);
          if (!FLOAT && validFloatKey(field(payload, 'by'))) setFloating(list => setFloatPanels(list, String(field(payload, 'by')),
            [...(list.find(item => item.key === field(payload, 'by'))?.panels ?? []), panel]));
        }
      } else if (FLOAT && event === 'ui.activatePanel') {
        const panel = field(payload, 'panel');
        if (knownPanel(panel)) setDock(t => panelStack(t, panel) !== null ? reopenTab(t, panel) : t);
      } else if (!FLOAT && event === 'window.floatClosed') {
        const key = field(payload, 'message');
        if (validFloatKey(key)) window.dispatchEvent(new CustomEvent('studio:float-closed', { detail: key }));
      } else if (!FLOAT && event === 'window.dockHover') {
        setDockHover(field(payload, 'over') === true);
      } else if (!FLOAT && event === 'window.floatFrame') {
        setFloating(list => updateFloatingFrame(list, payload));
      } else if (!FLOAT && event === 'ui.floatPanels') {
        const key = field(payload, 'window');
        if (validFloatKey(key)) setFloating(list => setFloatPanels(list, key, field(payload, 'panels')));
      } else if (!FLOAT && (event === 'ui.showPanel' || event === 'ui.dockPanel')) {
        const panel = field(payload, 'panel');
        if (knownPanel(panel)) window.dispatchEvent(new CustomEvent(event === 'ui.dockPanel' ? 'studio:dock-panel' : 'studio:show-panel', { detail: panel }));
      } else if (event === 'workspace.changed' || event === 'selection.changed' ||
                 event === 'document.changed') void refresh();
    });
  }, [refresh]);

  const historyAction = useCallback(async (method: 'studio.save' | 'studio.undo' | 'studio.redo') => {
    if (method !== 'studio.save') setInspectorPin(null); // positional entity IDs may be reassigned by history
    setBusy(true);
    try { setSnapshot(await invoke(method, {})); setError(''); }
    catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    finally { setBusy(false); }
  }, []);

  const openProject = async () => {
    if (!connected || busy) return;
    setBusy(true);
    try {
      const result = await invoke('studio.openProject', {});
      if (result.snapshot) { setInspectorPin(null); setSnapshot(result.snapshot); }
      setError('');
    } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    finally { setBusy(false); }
  };

  useEffect(() => {
    // Editor chrome: no browser context menu (Reload/Inspect…) except in text fields.
    const menu = (event: MouseEvent) => {
      if (!(event.target instanceof HTMLElement && event.target.closest('input,textarea,[contenteditable="true"]')))
        event.preventDefault();
    };
    window.addEventListener('contextmenu', menu);
    return () => window.removeEventListener('contextmenu', menu);
  }, []);
  useEffect(() => {
    const shortcut = (event: KeyboardEvent) => {
      if (!(event.metaKey || event.ctrlKey)) return;
      const key = shortcutKey(event);
      // Global navigation works even while a request is in flight.
      if (key === 'k' && !event.altKey && !event.shiftKey) { event.preventDefault(); setPalette(current => current === null ? '' : null); return; }
      if (key === 'p') {
        event.preventDefault();
        window.dispatchEvent(new CustomEvent('studio:play-control', { detail: event.altKey ? 'step' : event.shiftKey ? 'pause' : 'toggle' }));
        return;
      }
      const ws = FLOAT ? undefined : WORKSPACES.find(id => WORKSPACE_INFO[id].key === key);
      if (ws && !event.altKey && !event.shiftKey) { event.preventDefault(); switchWorkspace(ws); return; }
      if (event.altKey || busy) return;
      const target = event.target;
      const editable = target instanceof HTMLElement &&
        !!target.closest('input,textarea,[contenteditable="true"]');
      if (key === 'o' && connected) { event.preventDefault(); void openProject(); }
      else if (key === 's' && event.shiftKey && connected && snapshot?.project.path) {
        event.preventDefault();
        void invoke('studio.saveAs', {}).then(setSnapshot).catch(e => setError(e instanceof Error ? e.message : String(e)));
      } else if (key === 's' && connected && snapshot?.project.path) {
        event.preventDefault();
        if (snapshot.project.dirty) void historyAction('studio.save');
      } else if (key === 'd' && !editable && !event.shiftKey) {
        event.preventDefault();
        window.dispatchEvent(new CustomEvent('studio:entity-action', { detail: 'duplicate' }));
      } else if (key === 'z' && !editable && !event.shiftKey && snapshot?.history.canUndo) {
        event.preventDefault();
        void historyAction('studio.undo');
      } else if (((key === 'z' && event.shiftKey) || key === 'y') &&
                 !editable && snapshot?.history.canRedo) {
        event.preventDefault();
        void historyAction('studio.redo');
      }
    };
    window.addEventListener('keydown', shortcut);
    return () => window.removeEventListener('keydown', shortcut);
  }, [busy, connected, snapshot, historyAction, switchWorkspace]);

  // Selection is cheap and must not flash every control as disabled (no global busy).
  const select = async (id: string | null, mode: 'replace' | 'add' | 'toggle' = 'replace') => {
    try { setSnapshot(await invoke('studio.select', { id, mode })); setError(''); }
    catch (e) { setError(e instanceof Error ? e.message : String(e)); }
  };
  const selectIds = async (ids: string[], mode: 'replace' | 'add' | 'toggle' = 'replace') => {
    try { setSnapshot(await invoke('studio.select', { ids, mode })); setError(''); }
    catch (e) { setError(e instanceof Error ? e.message : String(e)); }
  };
  const changeProperty = async (key: string, value: string | number | boolean, componentId?: string) => {
    const entityId = inspectorPin?.entity.id ?? snapshot?.selectedId;
    if (!entityId) return;
    setBusy(true);
    try {
      // The pinned Inspector re-reads its entity after every snapshot change.
      setSnapshot(await invoke('studio.setProperty', { entityId, componentId, key, value }));
      setError('');
    } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    finally { setBusy(false); }
  };
  // Refresh the pinned object from native by ID without changing global selection.
  // This also reflects undo/redo and edits made from other panels.
  useEffect(() => {
    const entityId = inspectorPin?.entity.id;
    if (!entityId || !snapshot?.project.path) return;
    let active = true;
    void invoke('studio.inspect', {entityId}).then(inspector => {
      if (active) setInspectorPin(current => current?.entity.id === entityId
        ? {...current, inspector} : current);
    }).catch(() => {
      // The pinned object may have been removed by undo or another editor.
      if (active) setInspectorPin(current => current?.entity.id === entityId ? null : current);
    });
    return () => { active = false; };
  }, [inspectorPin?.entity.id, snapshot]);

  // Pins are project-local; never retain a target after switching projects.
  useEffect(() => { setInspectorPin(null); }, [snapshot?.project.path]);
  const rows = useMemo(() => hierarchyRows(snapshot?.scene.entities ?? []), [snapshot]);
  const selected = inspectorPin?.entity ??
    snapshot?.scene.entities.find(entity => entity.id === snapshot.selectedId);
  const inspector = inspectorPin ? inspectorPin.inspector : snapshot?.inspector ?? null;
  const toggleInspectorPin = () => setInspectorPin(current => current ? null :
    selected ? {entity: {...selected}, inspector: snapshot?.inspector ? structuredClone(snapshot.inspector) : null} : null);
  const renderPanel = (panel: StudioPanel) => <PanelErrorBoundary key={panel} name={STUDIO_PANEL_TITLES[panel]}>{renderStudioPanel(panel, {
    snapshot, busy, connected, search, setSearch, rows, selected,
    inspector, inspectorPinned: !!inspectorPin, toggleInspectorPin,
    select, selectIds, hoverId, changeProperty, openProject, updateSnapshot: value => {
      setInspectorPin(null); // create/delete/duplicate or map replacement invalidates positional pins
      setSnapshot(value);
    }, setBusy, commitSnapshot: setSnapshot, showPanel: panel => showPanel(panel), reportError: setError,
    playing: !!game?.active
  })}</PanelErrorBoundary>;

  const project = connected && !!snapshot?.project.path;
  const selectedEntity = snapshot?.selectedId ?? null;
  const selection = (snapshot?.selectedIds?.length ? snapshot.selectedIds : selectedEntity ? [selectedEntity] : [])
    .filter(id => id.startsWith('entity:'));
  const entityAction = (method: 'studio.entityDuplicate' | 'studio.entityDelete') => {
    if (!selection.length || busy) return;
    setBusy(true);
    void invoke(method === 'studio.entityDelete' ? 'studio.entitiesDelete' : 'studio.entitiesDuplicate', { entityIds: selection })
      .then(next => { setInspectorPin(null); setSnapshot(next); setError(''); })
      .catch(e => setError(e instanceof Error ? e.message : String(e))).finally(() => setBusy(false));
  };
  useEffect(() => {
    const run = (event: Event) => entityAction((event as CustomEvent<string>).detail === 'delete' ? 'studio.entityDelete' : 'studio.entityDuplicate');
    window.addEventListener('studio:entity-action', run);
    return () => window.removeEventListener('studio:entity-action', run);
  });
  // Floating windows: remembered by the main window, reopened on launch; a panel
  // shown by a floating window is never also in the main dock (any workspace).
  useEffect(() => { if (!FLOAT) saveFloating(floating); }, [floating]);
  useEffect(() => {
    if (FLOAT) return;
    const docked = floatingPanels(floating).filter(panel => panelStack(dock, panel) !== null);
    if (docked.length) setDock(t => docked.reduce((tree, panel) => closeTab(tree, panel), t));
  }, [dock, floating]);
  // A floating window saves its own dock, reports its panels and names itself.
  useEffect(() => {
    if (!FLOAT || !connected) return;
    saveFloatTree(FLOAT.key, dock);
    const panels = treePanels(dock);
    broadcast('ui.floatPanels', { window: FLOAT.key, panels });
    if (!panels.length) { void invoke('studio.windowClose', { window: FLOAT.key }).catch(console.warn); return; }
    const active = dockStacks(dock).map(stack => stack.active).filter((panel): panel is StudioPanel => !!panel);
    void invoke('studio.windowTitle', { title: active.map(panel => STUDIO_PANEL_TITLES[panel]).join(' · ').slice(0, 64) })
      .catch(console.warn);
  }, [dock, connected]);
  /** Main window: show `panel` in its own dock, taking it from any floating window. */
  const dockHere = (panel: StudioPanel) => {
    setFloating(list => releasePanel(list, panel));
    setDock(t => reopenTab(t, panel, STUDIO_PANEL_HOME[panel]));
    announceTaken(panel);
  };
  const adopt = (panel: StudioPanel) => {
    if (!FLOAT) setFloating(list => releasePanel(list, panel));
    announceTaken(panel);
  };
  useEffect(() => {
    if (FLOAT || !connected) return;
    for (const item of readFloating()) {
      const restore = () => {
        setFloating(list => removeFloatWindow(list, item.key));
        setDock(t => item.panels.reduce((tree, panel) => reopenTab(tree, panel, STUDIO_PANEL_HOME[panel]), t));
      };
      void invoke('studio.windowOpen', { window: item.key, panels: item.panels,
        title: item.panels.map(panel => STUDIO_PANEL_TITLES[panel]).join(' · ').slice(0, 64),
        x: item.x, y: item.y, width: item.width, height: item.height })
        .then(result => { if (!result.opened) restore(); }).catch(restore);
    }
    // once per launch
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [connected]);
  useEffect(() => { // a closed floating window gives its panels back to the main dock
    const run = (event: Event) => {
      const key = String((event as CustomEvent<unknown>).detail);
      const panels = floating.find(item => item.key === key)?.panels ?? [];
      setFloating(list => removeFloatWindow(list, key));
      setDock(t => panels.reduce((tree, panel) => reopenTab(tree, panel, STUDIO_PANEL_HOME[panel]), t));
      forgetFloatTree(key);
    };
    const dockPanel = (event: Event) => { const panel = (event as CustomEvent<unknown>).detail; if (knownPanel(panel)) dockHere(panel); };
    window.addEventListener('studio:float-closed', run);
    window.addEventListener('studio:dock-panel', dockPanel);
    return () => { window.removeEventListener('studio:float-closed', run); window.removeEventListener('studio:dock-panel', dockPanel); };
  });
  const floatOut = (panel: StudioPanel, ifOutside: boolean) => {
    if (!connected) return;
    const key = newFloatKey();
    void invoke('studio.windowOpen', { window: key, panels: [panel], title: STUDIO_PANEL_TITLES[panel], ifOutside }).then(result => {
      if (!result.opened) return;
      if (!FLOAT) setFloating(list => setFloatPanels(list, key, [panel]));
      setDock(t => closeTab(t, panel));
    }).catch(e => setError(e instanceof Error ? e.message : String(e)));
  };
  const showPanel = (panel: StudioPanel) => {
    if (FLOAT) {
      if (panelStack(dock, panel) !== null) setDock(t => reopenTab(t, panel));
      else broadcast('ui.showPanel', { panel }); // the main window owns everything else
      return;
    }
    const owner = floatOwner(floating, panel);
    if (owner) { // bring its window forward instead of docking a copy
      void invoke('studio.windowOpen', { window: owner }).catch(console.warn);
      broadcast('ui.activatePanel', { panel });
      return;
    }
    setDock(t => reopenTab(t, panel, STUDIO_PANEL_HOME[panel]));
  };
  useEffect(() => {
    const run = (event: Event) => { const panel = (event as CustomEvent<unknown>).detail; if (knownPanel(panel)) showPanel(panel); };
    window.addEventListener('studio:show-panel', run);
    return () => window.removeEventListener('studio:show-panel', run);
  });
  const menus: StudioMenu[] = [
    { id: 'file', label: 'File', items: () => [
      { id: 'hub', label: 'Project Hub…', disabled: !connected || busy, onSelect: () => setHubOpen(true) },
      { id: 'open', label: 'Open Project…', shortcut: '⌘O', disabled: !connected || busy, onSelect: () => void openProject() },
      { id: 'maps', label: 'New / Open Map…', separator: true, disabled: !project, onSelect: () => showPanel('project') },
      { id: 'save', label: 'Save', shortcut: '⌘S', disabled: !project || busy || !snapshot?.project.dirty,
        onSelect: () => void historyAction('studio.save') },
      { id: 'saveas', label: 'Save As…', shortcut: '⇧⌘S', disabled: !project || busy,
        onSelect: () => { setBusy(true); void invoke('studio.saveAs', {}).then(next => { if (next && 'project' in next) setSnapshot(next); })
          .catch(e => setError(e instanceof Error ? e.message : String(e))).finally(() => setBusy(false)); } },
    ] },
    { id: 'edit', label: 'Edit', items: () => [
      { id: 'undo', label: 'Undo', shortcut: '⌘Z', disabled: busy || !snapshot?.history.canUndo, onSelect: () => void historyAction('studio.undo') },
      { id: 'redo', label: 'Redo', shortcut: '⇧⌘Z', disabled: busy || !snapshot?.history.canRedo, onSelect: () => void historyAction('studio.redo') },
      { id: 'dup', label: selection.length > 1 ? 'Duplicate ' + selection.length + ' Objects' : 'Duplicate', shortcut: '⌘D', separator: true, disabled: busy || !selection.length,
        onSelect: () => entityAction('studio.entityDuplicate') },
      { id: 'del', label: selection.length > 1 ? 'Delete ' + selection.length + ' Objects' : 'Delete', shortcut: '⌫', disabled: busy || !selection.length,
        onSelect: () => entityAction('studio.entityDelete') },
      { id: 'frame', label: 'Frame Selected', shortcut: 'F', disabled: !selectedEntity || !connected,
        onSelect: () => void invoke('studio.viewportInput', { kind: 'focus' }).catch(console.warn) },
      { id: 'frameAll', label: 'Frame All', shortcut: '⇧F', disabled: !connected || !project,
        onSelect: () => void invoke('studio.viewportInput', { kind: 'frameAll' }).catch(console.warn) },
      { id: 'align', label: 'Align', caption: true, separator: true, onSelect: () => {} },
      ...([['minX', 'Align Left (min X)', 2], ['centerX', 'Align Centre X', 2], ['maxX', 'Align Right (max X)', 2],
           ['minZ', 'Align Back (min Z)', 2], ['centerZ', 'Align Centre Z', 2], ['maxZ', 'Align Front (max Z)', 2],
           ['distributeX', 'Distribute Along X', 3], ['distributeZ', 'Distribute Along Z', 3], ['ground', 'Drop to Ground', 1]] as const)
        .map(([mode, label, min]) => ({ id: 'align-' + mode, label, disabled: busy || selection.length < min,
          onSelect: () => void invoke('studio.entitiesAlign', { entityIds: selection, mode })
            .then(next => { setInspectorPin(null); setSnapshot(next); setError(''); })
            .catch(e => setError(e instanceof Error ? e.message : String(e))) })),
      { id: 'prefs', label: 'Preferences…', separator: true, onSelect: () => setSettingsOpen(true) },
    ] },
    { id: 'window', label: 'Window', items: () => {
      const byTitle = (a: StudioPanel, b: StudioPanel) => STUDIO_PANEL_TITLES[a].localeCompare(STUDIO_PANEL_TITLES[b]);
      const isOpen = (panel: StudioPanel) => panelStack(dock, panel) !== null || !!floatOwner(floating, panel);
      const toggle = (panel: StudioPanel) => floatOwner(floating, panel) ? dockHere(panel)
        : setDock(t => panelStack(t, panel) !== null ? closeTab(t, panel) : reopenTab(t, panel, STUDIO_PANEL_HOME[panel]));
      const openPanels = STUDIO_PANELS.filter(isOpen).sort(byTitle);
      const otherPanels = STUDIO_PANELS.filter(panel => !isOpen(panel)).sort(byTitle);
      return [
      { id: 'ws', label: 'Workspaces', caption: true, onSelect: () => {} },
      ...WORKSPACES.map(id => ({ id: 'ws-' + id, label: WORKSPACE_INFO[id].title, shortcut: menuShortcut(WORKSPACE_INFO[id].key),
        checked: workspace === id, onSelect: () => switchWorkspace(id) })),
      { id: 'reset', label: 'Reset ' + WORKSPACE_INFO[workspace].title + ' Layout', separator: true, onSelect: () => setDock(workspaceDockTree(workspace)) },
      ...(!FLOAT && floating.length ? [{ id: 'dock-floating', label: 'Dock All Floating Windows (' + floating.length + ')',
        onSelect: () => { for (const item of floating) void invoke('studio.windowClose', { window: item.key }).catch(console.warn); } }] : []),
      { id: 'palette', label: 'Command Palette…', shortcut: menuShortcut('K'), onSelect: () => setPalette('') },
      { id: 'tour', label: 'Welcome Tour…', onSelect: () => setTourOpen(true) },
      { id: 'open-cap', label: 'Open', caption: true, separator: true, onSelect: () => {} },
      ...openPanels.map(panel => ({ id: 'panel-' + panel, label: STUDIO_PANEL_TITLES[panel], checked: true, onSelect: () => toggle(panel) })),
      { id: 'more-cap', label: 'Panels', caption: true, separator: true, onSelect: () => {} },
      ...otherPanels.map(panel => ({ id: 'panel-' + panel, label: STUDIO_PANEL_TITLES[panel], onSelect: () => toggle(panel) })),
      ...(LEGACY_WINDOWS_NOT_MIGRATED.length ? [{ id: 'legacy', label: 'Legacy editor — not migrated yet', caption: true, separator: true, onSelect: () => {} }] : []),
      ...LEGACY_WINDOWS_NOT_MIGRATED.map(w => {
        const area = legacyWorkspace(w.id);
        return { id: 'legacy-' + w.id, label: w.title + (area ? '  ·  ' + WORKSPACE_INFO[area].title : ''), disabled: true, onSelect: () => {} };
      }),
    ]; } },
  ];

  useEffect(() => {
    if (palette === null || !project) return;
    let live = true;
    void invoke('studio.assets', {}).then(list => { if (live) setPaletteAssets(list.files); }).catch(() => {});
    return () => { live = false; };
  }, [palette === null, project]);
  const paletteEntries = useMemo<CommandEntry[]>(() => {
    const out: CommandEntry[] = [];
    for (const menu of menus) for (const item of menu.items()) {
      if (item.caption || item.id.startsWith('legacy-') || item.id.startsWith('panel-') || item.id.startsWith('ws-') || item.id === 'palette') continue;
      out.push({ id: 'menu:' + menu.id + ':' + item.id, kind: 'command', title: item.label, hint: menu.label,
        shortcut: item.shortcut, disabled: item.disabled, run: item.onSelect });
    }
    out.push({ id: 'cmd:refresh', kind: 'command', title: 'Refresh from engine', hint: 'Re-read the native document', run: () => void refresh() });
    out.push({ id: 'cmd:console', kind: 'command', title: 'Show Console', run: () => showPanel('console') });
    for (const id of WORKSPACES) out.push({ id: 'ws:' + id, kind: 'workspace', title: WORKSPACE_INFO[id].title + ' workspace',
      hint: WORKSPACE_INFO[id].purpose, shortcut: '⌘' + WORKSPACE_INFO[id].key, run: () => switchWorkspace(id) });
    for (const panel of STUDIO_PANELS) out.push({ id: 'panel:' + panel, kind: 'window', title: STUDIO_PANEL_TITLES[panel],
      hint: floatOwner(floating, panel) ? 'Floating window · focus' : panelStack(dock, panel) ? 'Open · focus' : 'Open in ' + WORKSPACE_INFO[workspace].title, run: () => showPanel(panel) });
    if (connected) for (const stack of dockStacks(dock)) if (stack.active) {
      const panel = stack.active;
      out.push({ id: 'float:' + panel, kind: 'window', title: 'Open ' + STUDIO_PANEL_TITLES[panel] + ' in New Window',
        hint: 'Undock into its own window (second monitor)', run: () => floatOut(panel, false) });
    }
    for (const w of LEGACY_WINDOWS_NOT_MIGRATED) {
      const area = legacyWorkspace(w.id);
      out.push({ id: 'legacy:' + w.id, kind: 'window', title: w.title, legacy: true, disabled: true,
        hint: (area ? WORKSPACE_INFO[area].title + ' workspace · ' : '') + 'not migrated to HTML5 yet — use the legacy editor', run: () => {} });
    }
    for (const entity of snapshot?.scene.entities ?? []) out.push({ id: 'entity:' + entity.id, kind: 'entity', title: entity.name,
      hint: entity.kind + ' · select and frame (F)', run: () => { void select(entity.id).then(() => invoke('studio.viewportInput', { kind: 'focus' })).catch(console.warn); } });
    for (const asset of paletteAssets) out.push({ id: 'asset:' + asset.path, kind: 'asset', title: asset.path.split('/').pop() ?? asset.path,
      hint: asset.path, keywords: asset.path, run: () => {
        showPanel('assets');
        window.setTimeout(() => window.dispatchEvent(new CustomEvent('studio:reveal-asset', { detail: { path: asset.path } })), 0);
      } });
    return out;
    // menus are rebuilt each render from the same state listed here
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [snapshot, paletteAssets, dock, floating, workspace, busy, connected, switchWorkspace]);

  // Floating window: its own small dock, no header, menus or workspaces.
  if (FLOAT) return <div className={'studio compact-shell floating-shell'+(connected?' compact-shell--native':'')}>
    {error && <div role="alert" className="error-banner">{error}
      <button onClick={() => setError('')} aria-label="Dismiss">×</button></div>}
    <NestedDockWorkspace compact tree={dock} setTree={setDock} renderPanel={renderPanel}
      onFloat={connected ? floatOut : undefined} onAdopt={adopt}
      onDockBack={panel => broadcast('ui.dockPanel', { panel })}/>
    {palette !== null && <CommandPalette entries={paletteEntries} initial={palette} onClose={() => setPalette(null)}/>}
  </div>;

  return <div className={'studio compact-shell'+(windowsHost?' compact-shell--windows':connected?' compact-shell--native':'')+(game?.active?' is-playing':'')}>
    <header className="titlebar"
      onMouseDown={event => {
        // Empty title-bar areas move the native window. The outer pixels resize it instead.
        const target = event.target instanceof Element ? event.target : (event.target as Node).parentElement;
        if (event.button !== 0 || !target || target.closest('button,input,select,textarea,a,[role=button],[role=menu]')) return;
        if (windowsHost && (event.clientY <= 8 || event.clientX <= 8 || event.clientX >= window.innerWidth - 8)) return;
        if (event.detail === 2) nativeWindowAction('studio.windowZoom');
        else nativeWindowAction('studio.windowDrag');
      }}>
      <div className="titlebar__side titlebar__side--left">
      <div className="wordmark"><img className="engine-mark" src={engineLogo} alt=""/>
        <strong>AIVERSE</strong></div>
      <StudioMenuBar menus={menus}/>
      <button className="project-breadcrumb tool-button" onClick={() => void openProject()}
        disabled={!connected || busy} title={snapshot?.project.dirty ? 'Open project · unsaved changes' : 'Open project'}>
        <b>{snapshot?.project.name ?? 'Loading…'}</b>{snapshot?.project.dirty && <span className="dirty-dot active" aria-label="Unsaved changes"/>}<ChevronDown size={13}/>
      </button>
      <div className="header-drag-space"/>
      </div>
      <HeaderGameControls enabled={connected && !!snapshot?.project.path} onState={setGame}
        onPlay={()=>showPanel('game')} onError={setError}/>
      <div className="titlebar__side titlebar__side--right">
      <div className="header-drag-space"/>
      <WorkspaceTabs value={workspace} onChange={switchWorkspace}/>
      <button type="button" className="command-search-button" onClick={() => setPalette('')} aria-label="Search or run a command (⌘K)">
        <Search size={13}/><span>Search or run…</span><kbd>⌘K</kbd>
      </button>
      <SceneLoadingStrip state={loading}/>
      <div className="title-actions">
        <LayoutMenu workspace={workspace} dock={dock} onApply={setDock} onError={setError}/>
        <button className="icon-button" aria-label="Editor settings" title="Editor settings"
          aria-expanded={settingsOpen} onClick={() => setSettingsOpen(v => !v)}><Settings2 size={17}/></button>
      {windowsHost && <div className="window-controls">
        <button type="button" aria-label="Minimize" onClick={() => nativeWindowAction('studio.windowMinimize')}>–</button>
        <button type="button" aria-label="Maximize" onClick={() => nativeWindowAction('studio.windowZoom')}>□</button>
        <button type="button" className="is-close" aria-label="Close" onClick={() => nativeWindowAction('studio.windowClose')}>×</button>
      </div>}
      </div>
      </div>
    </header>
    {settingsOpen && <section className="studio-editor-settings" aria-label="Editor settings">
      <strong>Editor settings</strong>
      <label>UI scale <output>{Math.round(uiScale * 100)}%</output>
        <input type="range" min="75" max="200" step="5" value={Math.round(uiScale * 100)}
          onChange={e => setUiScale(Number(e.target.value) / 100)}/>
      </label>
      <button type="button" onClick={() => setUiScale(1)}>Reset scale to 100%</button>
    </section>}
    {!connected && <div className="demo-banner"><span>Preview mode:</span> interface and edits
      are demonstrated locally. Native C++ Workspace and GPU renderer are not connected yet.</div>}
    {error && <div role="alert" className="error-banner">{error}
      <button onClick={() => setError('')} aria-label="Dismiss">×</button></div>}
    <NestedDockWorkspace tree={dock} setTree={setDock} renderPanel={renderPanel}
      onFloat={connected ? floatOut : undefined} onAdopt={adopt}/>
    {FLOAT ? <ProjectLoadingDialog state={loading} onClose={clearLoading}/> : <ProjectSplash state={loading} onClose={clearLoading} onHub={() => setHubOpen(true)}/>}
    {dockHover && <div className="dock-drop-hint" data-native-overlay role="status">Release to dock the window's tabs here</div>}
    {tourOpen && !FLOAT && <WelcomeTour onClose={() => setTourOpen(false)}/>}
    {hubOpen && !FLOAT && <div className="hub-modal" role="dialog" aria-modal="true" aria-label="Project hub" data-native-overlay onMouseDown={e => { if (e.target === e.currentTarget) setHubOpen(false); }}>
      <ProjectHub onOpened={next => { setHubOpen(false); setInspectorPin(null); setSnapshot(next); }} openOther={() => { setHubOpen(false); void openProject(); }}/></div>}
    <StudioStatusBar snapshot={snapshot} connected={connected} game={game} action={sceneAction || sceneLoadingText(loading)}
      onOpenConsole={() => { showPanel('console');
        window.setTimeout(() => window.dispatchEvent(new CustomEvent('studio:console-filter', { detail: 'error' })), 0); }}/>
    {palette !== null && <CommandPalette entries={paletteEntries} initial={palette} onClose={() => setPalette(null)}/>}
  </div>;
}
