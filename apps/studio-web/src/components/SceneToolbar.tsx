import { useCallback, useEffect, useState } from 'react';
import { Expand, Globe2, Hand, Magnet, Move3d, Rotate3d, Shapes, Box } from 'lucide-react';
import { invoke, onNativeEvent } from '../bridge';
import './SceneToolbar.css';

export type SceneTool = 'view' | 'move' | 'rotate' | 'scale';
export interface SceneToolState { tool: SceneTool; local: boolean; snap: boolean; snapMove: number; snapRotate: number; snapScale: number; gizmos: boolean }
const KEY = 'aiverse.studio.sceneTools';
const DEFAULTS: SceneToolState = { tool: 'move', local: false, snap: false, snapMove: 1, snapRotate: 15, snapScale: 0.1, gizmos: true };
const TOOLS: Array<{ id: SceneTool; key: string; label: string; icon: typeof Hand }> = [
  { id: 'view', key: 'Q', label: 'Hand — drag to pan', icon: Hand },
  { id: 'move', key: 'W', label: 'Move', icon: Move3d },
  { id: 'rotate', key: 'E', label: 'Rotate', icon: Rotate3d },
  { id: 'scale', key: 'R', label: 'Scale', icon: Expand },
];
export const SCENE_TOOL_KEYS: Record<string, SceneTool> = { q: 'view', w: 'move', e: 'rotate', r: 'scale' };
const MOVE_STEPS = [0.25, 0.5, 1, 2, 5];
const ROTATE_STEPS = [5, 15, 45, 90];

export function readSceneTools(): SceneToolState {
  try {
    const v = JSON.parse(window.localStorage.getItem(KEY) ?? '{}') as Partial<SceneToolState>;
    const tool = TOOLS.some(t => t.id === v.tool) ? v.tool as SceneTool : DEFAULTS.tool;
    const num = (x: unknown, d: number) => typeof x === 'number' && Number.isFinite(x) && x > 0 ? x : d;
    return { tool, local: v.local === true, snap: v.snap === true, gizmos: v.gizmos !== false,
      snapMove: num(v.snapMove, 1), snapRotate: num(v.snapRotate, 15), snapScale: num(v.snapScale, 0.1) };
  } catch { return DEFAULTS; }
}

/** Scene header: Q/W/E/R tools, Global/Local, snapping (Ctrl/Cmd inverts while dragging), gizmos. */
export default function SceneToolbar({ enabled }: { enabled: boolean }) {
  const [state, setState] = useState<SceneToolState>(readSceneTools);
  const apply = useCallback((next: SceneToolState) => {
    setState(next);
    try { window.localStorage.setItem(KEY, JSON.stringify(next)); } catch { /* optional */ }
    if (enabled) void invoke('studio.viewportInput', { kind: 'tool', ...next }).catch(() => {});
  }, [enabled]);
  // Push the saved state once the native Scene exists. The viewport accepts input
  // only after its first layout, so retry until it answers (≤ 10 s).
  useEffect(() => {
    if (!enabled) return;
    let live = true, tries = 0, timer = 0;
    const push = () => {
      void invoke('studio.viewportInput', { kind: 'tool', ...readSceneTools() })
        .catch(() => { if (live && ++tries < 20) timer = window.setTimeout(push, 500); });
    };
    push();
    return () => { live = false; window.clearTimeout(timer); };
  }, [enabled]);
  // Q/W/E/R pressed in the native view, or in the HTML Scene (studio:scene-tool).
  useEffect(() => {
    const fromWeb = (event: Event) => {
      const tool = (event as CustomEvent<SceneTool>).detail;
      if (TOOLS.some(t => t.id === tool)) setState(current => { const next = { ...current, tool }; apply(next); return next; });
    };
    window.addEventListener('studio:scene-tool', fromWeb);
    const off = onNativeEvent((event, payload) => {
      if (event !== 'viewport.tool') return;
      const tool = payload && typeof payload === 'object' && 'message' in payload ? String(payload.message) : '';
      if (TOOLS.some(t => t.id === tool)) setState(current => {
        const next = { ...current, tool: tool as SceneTool };
        try { window.localStorage.setItem(KEY, JSON.stringify(next)); } catch { /* optional */ }
        return next;
      });
    });
    return () => { window.removeEventListener('studio:scene-tool', fromWeb); off(); };
  }, [apply]);
  const set = (patch: Partial<SceneToolState>) => apply({ ...state, ...patch });
  return <div className="scene-toolbar" role="toolbar" aria-label="Scene tools">
    <div className="scene-toolbar__group" role="radiogroup" aria-label="Transform tool">
      {TOOLS.map(t => { const Icon = t.icon; return <button key={t.id} type="button" role="radio" aria-checked={state.tool === t.id}
        className={'scene-toolbar__tool' + (state.tool === t.id ? ' is-active' : '')} disabled={!enabled}
        title={t.label + ' (' + t.key + ')'} aria-label={t.label} onClick={() => set({ tool: t.id })}>
        <Icon size={14}/><kbd>{t.key}</kbd></button>; })}
    </div>
    <button type="button" className="scene-toolbar__toggle" disabled={!enabled} aria-pressed={state.local}
      title={state.local ? 'Handles follow the object (Local)' : 'Handles follow the world axes (Global)'}
      onClick={() => set({ local: !state.local })}>
      {state.local ? <Box size={13}/> : <Globe2 size={13}/>}{state.local ? 'Local' : 'Global'}</button>
    <div className="scene-toolbar__group">
      <button type="button" className={'scene-toolbar__toggle' + (state.snap ? ' is-on' : '')} disabled={!enabled} aria-pressed={state.snap}
        title="Snap while dragging (hold Ctrl/⌘ to invert)" onClick={() => set({ snap: !state.snap })}><Magnet size={13}/>Snap</button>
      <select className="scene-toolbar__select" aria-label="Move snap step" disabled={!enabled} value={state.snapMove}
        onChange={e => set({ snapMove: Number(e.target.value) })}>
        {MOVE_STEPS.map(v => <option key={v} value={v}>{v} u</option>)}</select>
      <select className="scene-toolbar__select" aria-label="Rotate snap step" disabled={!enabled} value={state.snapRotate}
        onChange={e => set({ snapRotate: Number(e.target.value) })}>
        {ROTATE_STEPS.map(v => <option key={v} value={v}>{v}°</option>)}</select>
    </div>
    <button type="button" className={'scene-toolbar__toggle' + (state.gizmos ? ' is-on' : '')} disabled={!enabled} aria-pressed={state.gizmos}
      title="Show handles, selection box and the orientation gizmo" onClick={() => set({ gizmos: !state.gizmos })}><Shapes size={13}/>Gizmos</button>
  </div>;
}
