import { useCallback, useEffect, useState } from 'react';
import { Bookmark, Expand, Globe2, Hand, Magnet, Maximize, Mountain, Move3d, Rotate3d, Shapes, Layers, Box } from 'lucide-react';
import { CAMERA_SLOTS, importBookmarks, readBookmarks, removeBookmark, saveBookmark, type CameraBookmark } from './sceneBookmarks';
import { invoke, onNativeEvent } from '../bridge';
import ErosionZoneHighlight from './ErosionZoneHighlight';
import type { StudioGraphDocumentScope } from '../types';
import './SceneToolbar.css';

export type SceneTool = 'view' | 'move' | 'rotate' | 'scale';
export interface SceneToolState { tool: SceneTool; local: boolean; snap: boolean; snapMove: number; snapRotate: number; snapScale: number; gizmos: boolean; natureRadius:number; natureSpacing:number; erosionZones:boolean; terraTreeWind:boolean; terraSlope:boolean; terraSlopeAtTarget:boolean; nereidFlow:boolean; nereidAllWater:boolean; natureWind:boolean; natureFire:boolean; worldAxes:boolean; objectBounds:boolean; selectionBox:boolean; surface: boolean }
const KEY = 'aiverse.studio.sceneTools';
const DEFAULTS: SceneToolState = { tool: 'move', local: false, snap: false, snapMove: 1, snapRotate: 15, snapScale: 0.1, gizmos: true, natureRadius:48, natureSpacing:6, erosionZones:true, terraTreeWind:true, terraSlope:true, terraSlopeAtTarget:false, nereidFlow:true, nereidAllWater:false, natureWind:false, natureFire:false, worldAxes:false, objectBounds:false, selectionBox:true, surface: false };
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
    return { tool, local: v.local === true, snap: v.snap === true, gizmos: v.gizmos !== false, natureRadius:Math.min(200,Math.max(8,num(v.natureRadius,48))), natureSpacing:Math.min(32,Math.max(2,num(v.natureSpacing,6))), erosionZones:v.erosionZones!==false, terraTreeWind:v.terraTreeWind!==false, terraSlope:v.terraSlope!==false, terraSlopeAtTarget:v.terraSlopeAtTarget===true, nereidFlow:v.nereidFlow!==false, nereidAllWater:v.nereidAllWater===true, natureWind:v.natureWind===true, natureFire:v.natureFire===true, worldAxes:v.worldAxes===true, objectBounds:v.objectBounds===true, selectionBox:v.selectionBox!==false, surface: v.surface === true,
      snapMove: num(v.snapMove, 1), snapRotate: num(v.snapRotate, 15), snapScale: num(v.snapScale, 0.1) };
  } catch { return DEFAULTS; }
}

/** Scene header: Q/W/E/R tools, Global/Local, snapping (Ctrl/Cmd inverts while dragging), gizmos. */
export default function SceneToolbar({ enabled, projectPath = '', documentScope }: { enabled: boolean; projectPath?: string; documentScope?:StudioGraphDocumentScope }) {
  const [state, setState] = useState<SceneToolState>(readSceneTools);
  const [marks, setMarks] = useState<CameraBookmark[]>(() => readBookmarks(projectPath));
  const [layersOpen,setLayersOpen]=useState(false);
  const [marksOpen, setMarksOpen] = useState(false);
  const [legacyViews, setLegacyViews] = useState<CameraBookmark[]>([]);
  useEffect(() => {
    let current = true;
    setMarks(readBookmarks(projectPath));
    setLegacyViews([]);
    if (projectPath) void invoke('studio.legacyCameraBookmarks', { project: projectPath }).then(views => {
      if (current) {
        setLegacyViews(views);
        setMarks(importBookmarks(projectPath, views));
      }
    }).catch(() => {});
    return () => { current = false; };
  }, [projectPath]);
  // Camera bookmarks: ⌥1…9 go to a slot, ⌥⇧1…9 store the current view in it.
  const storeSlot = useCallback(async (slot: number) => {
    try {
      const cam = await invoke('studio.viewportInput', { kind: 'camera' }) as unknown as CameraBookmark;
      setMarks(saveBookmark(projectPath, { ...cam, slot, name: 'View ' + slot }));
    } catch { /* Scene not ready */ }
  }, [projectPath]);
  const goSlot = useCallback((mark: CameraBookmark) => {
    void invoke('studio.viewportInput', { kind: 'camera', set: mark }).catch(() => {});
  }, []);
  useEffect(() => {
    if (!enabled) return;
    const key = (e: KeyboardEvent) => {
      if (!e.altKey || e.metaKey || e.ctrlKey) return;
      const slot = CAMERA_SLOTS.find(s => e.code === 'Digit' + s);
      if (!slot) return;
      if ((e.target as HTMLElement)?.closest?.('input,textarea,select,[contenteditable="true"]')) return;
      e.preventDefault();
      if (e.shiftKey) void storeSlot(slot);
      else { const m = marks.find(x => x.slot === slot); if (m) goSlot(m); }
    };
    window.addEventListener('keydown', key);
    return () => window.removeEventListener('keydown', key);
  }, [enabled, marks, storeSlot, goSlot]);
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
    <button type="button" className={'scene-toolbar__toggle' + (state.surface ? ' is-on' : '')} disabled={!enabled} aria-pressed={state.surface}
      title="Surface snap: moved objects rest on the terrain (hold V while moving to snap to another object's pivot)"
      onClick={() => set({ surface: !state.surface })}><Mountain size={13}/></button>
    <button type="button" className={'scene-toolbar__toggle' + (state.gizmos ? ' is-on' : '')} disabled={!enabled} aria-pressed={state.gizmos}
      title="Show handles, selection box and the orientation gizmo" onClick={() => set({ gizmos: !state.gizmos })}><Shapes size={13}/></button>
    <div className="scene-toolbar__marks" onBlur={e=>{if(!e.currentTarget.contains(e.relatedTarget))setLayersOpen(false);}}
      onKeyDown={e=>{if(e.key==='Escape'){e.currentTarget.querySelector('button')?.focus();setLayersOpen(false);}}}>
      <button type="button" className="scene-toolbar__toggle" aria-label="Scene overlays" title="Scene overlays"
        disabled={!enabled} aria-expanded={layersOpen} onClick={()=>{setMarksOpen(false);setLayersOpen(v=>!v);}}><Layers size={13}/></button>
      {layersOpen&&<div className="scene-toolbar__marks-menu scene-toolbar__layers" data-native-overlay role="group" aria-label="Scene overlays">
       {([['worldAxes','World axes'],['objectBounds','Object bounds'],['selectionBox','Selection box'],['natureWind','Play-test wind'],['natureFire','Play-test fire spread'],['terraTreeWind','Wind at selected tree'],['erosionZones','Erosion zones']] as const).map(([key,label])=>
        <label key={key}><input type="checkbox" checked={state[key]} onChange={e=>set({[key]:e.target.checked})}/>{label}</label>)}
       {state.erosionZones && documentScope && <ErosionZoneHighlight key={JSON.stringify([documentScope.projectPath,documentScope.mapPath,documentScope.epoch,documentScope.revision])} scope={documentScope}/>}
       <label><input type="checkbox" checked={state.nereidFlow} onChange={e=>set({nereidFlow:e.target.checked})}/>River flow</label>
       {state.nereidFlow&&<label><input type="checkbox" checked={state.nereidAllWater} onChange={e=>set({nereidAllWater:e.target.checked})}/>Show all rivers</label>}
       <label><input type="checkbox" checked={state.terraSlope} onChange={e=>set({terraSlope:e.target.checked})}/>Slope at selected tile</label>
       <label><input type="checkbox" checked={state.terraSlopeAtTarget} onChange={e=>set({terraSlopeAtTarget:e.target.checked})}/>Slope at camera target</label>
       {(state.natureWind||state.natureFire||state.terraSlope||state.terraSlopeAtTarget)&&<fieldset className="scene-toolbar__sampling"><legend>Field sampling</legend>
        {([['natureRadius','Radius',8,200],['natureSpacing','Spacing',2,32]] as const).map(([key,label,min,max])=>
         <label key={key}>{label}<output>{state[key]} m</output>
          <input aria-label={label+' in metres'} type="range" min={min} max={max} value={state[key]} onChange={e=>set({[key]:e.currentTarget.valueAsNumber})}/>
         </label>)}
       </fieldset>}
      </div>}
    </div>
    <button type="button" className="scene-toolbar__toggle" disabled={!enabled} title="Frame all objects (⇧F)"
      onClick={() => void invoke('studio.viewportInput', { kind: 'frameAll' }).catch(() => {})}><Maximize size={13}/></button>
    <div className="scene-toolbar__marks">
      <button type="button" className={'scene-toolbar__toggle' + (marksOpen ? ' is-on' : '')} disabled={!enabled} aria-expanded={marksOpen}
        title="Camera bookmarks (⌥1…9 go, ⌥⇧1…9 store)" onClick={() => setMarksOpen(o => !o)}><Bookmark size={13}/>{marks.length || ''}</button>
      {marksOpen && <div className="scene-toolbar__marks-menu" role="menu" data-native-overlay>
        {CAMERA_SLOTS.map(slot => { const m = marks.find(x => x.slot === slot);
          return <div key={slot} className="scene-toolbar__mark" role="none">
            <button type="button" role="menuitem" disabled={!m} onClick={() => { if (m) { goSlot(m); setMarksOpen(false); } }}>
              <kbd>⌥{slot}</kbd>{m ? m.name : <i>empty</i>}</button>
            <button type="button" role="menuitem" title={'Store the current view in ' + slot} onClick={() => void storeSlot(slot)}>Store</button>
            {m && <button type="button" role="menuitem" title="Remove" onClick={() => setMarks(removeBookmark(projectPath, slot))}>×</button>}
          </div>; })}
        {legacyViews.length > 0 && <details>
          <summary>Views from the previous editor</summary>
          {legacyViews.map(view => <div key={view.slot} className="scene-toolbar__mark">
            <button type="button" role="menuitem" onClick={() => { goSlot(view); setMarksOpen(false); }}>{view.name}</button>
          </div>)}
        </details>}
      </div>}
    </div>
  </div>;
}
