import { useCallback, useEffect, useLayoutEffect, useRef, useState, type ReactNode } from 'react';
import { Bookmark, Expand, Globe2, Hand, Magnet, Maximize, Mountain, Move3d, Rotate3d, Shapes, Layers, Box } from 'lucide-react';
import { CAMERA_SLOTS, importBookmarks, readBookmarks, removeBookmark, saveBookmark, type CameraBookmark } from './sceneBookmarks';
import { invoke, onNativeEvent } from '../bridge';
import ErosionZoneHighlight from './ErosionZoneHighlight';
import { StudioInput, StudioSelect } from '../ui';
import { EcologyLayerPicker } from '../panels/SceneBrushBar';
import type { WindDebugView, StudioGraphDocumentScope } from '../types';
import './SceneToolbar.css';

export type SceneTool = 'view' | 'move' | 'rotate' | 'scale';
export interface SceneToolState { tool: SceneTool; local: boolean; snap: boolean; snapMove: number; snapRotate: number; snapScale: number; gizmos: boolean; modelDebugMode:number; strataColor:number; strataBounds:boolean; strataWire:boolean; strataFill:boolean; strataRoutes:boolean; natureRadius:number; natureSpacing:number; mapRoutes:boolean; playerStarts:boolean; resourceFields:boolean; rampTiles:boolean; regionBorders:boolean; triggerAreas:boolean; erosionZones:boolean; terraTreeWind:boolean; terraSlope:boolean; terraSlopeAtTarget:boolean; nereidFlow:boolean; nereidAllWater:boolean; natureWind:boolean; natureFire:boolean; worldAxes:boolean; objectBounds:boolean; selectionBox:boolean; surface: boolean }
const KEY = 'aiverse.studio.sceneTools';
const DEFAULTS: SceneToolState = { tool: 'move', local: false, snap: false, snapMove: 1, snapRotate: 15, snapScale: 0.1, gizmos: true, modelDebugMode:0, strataColor:0, strataBounds:false, strataWire:false, strataFill:false, strataRoutes:false, natureRadius:48, natureSpacing:6, mapRoutes:true, playerStarts:true, resourceFields:true, rampTiles:false, regionBorders:false, triggerAreas:true, erosionZones:true, terraTreeWind:true, terraSlope:true, terraSlopeAtTarget:false, nereidFlow:true, nereidAllWater:false, natureWind:false, natureFire:false, worldAxes:false, objectBounds:false, selectionBox:true, surface: false };
const TOOLS: Array<{ id: SceneTool; key: string; label: string; icon: typeof Hand }> = [
  { id: 'view', key: 'Q', label: 'Hand — drag to pan', icon: Hand },
  { id: 'move', key: 'W', label: 'Move', icon: Move3d },
  { id: 'rotate', key: 'E', label: 'Rotate', icon: Rotate3d },
  { id: 'scale', key: 'R', label: 'Scale', icon: Expand },
];
export const SCENE_TOOL_KEYS: Record<string, SceneTool> = { q: 'view', w: 'move', e: 'rotate', r: 'scale' };
const MODEL_VIEWS = ['Shaded','Draw calls','Normals','UV','Depth','Albedo','Roughness','Metallic','Emissive','World position'];
const isModelView = (value:unknown):value is number => typeof value==='number' && Number.isInteger(value) && value>=0 && value<MODEL_VIEWS.length;
const MOVE_STEPS = [0.25, 0.5, 1, 2, 5];
const ROTATE_STEPS = [5, 15, 45, 90];

export function readSceneTools(): SceneToolState {
  try {
    const v = JSON.parse(window.localStorage.getItem(KEY) ?? '{}') as Partial<SceneToolState>;
    const tool = TOOLS.some(t => t.id === v.tool) ? v.tool as SceneTool : DEFAULTS.tool;
    const num = (x: unknown, d: number) => typeof x === 'number' && Number.isFinite(x) && x > 0 ? x : d;
    return { tool, local: v.local === true, snap: v.snap === true, gizmos: v.gizmos !== false, modelDebugMode:isModelView(v.modelDebugMode)?v.modelDebugMode:0, strataColor:[0,1,2,3].includes(v.strataColor??-1)?v.strataColor!:0, strataBounds:v.strataBounds===true, strataWire:v.strataWire===true, strataFill:v.strataFill===true, strataRoutes:v.strataRoutes===true, natureRadius:Math.min(200,Math.max(8,num(v.natureRadius,48))), natureSpacing:Math.min(32,Math.max(2,num(v.natureSpacing,6))), mapRoutes:v.mapRoutes!==false, playerStarts:v.playerStarts!==false, resourceFields:v.resourceFields!==false, rampTiles:v.rampTiles===true, regionBorders:v.regionBorders===true, triggerAreas:v.triggerAreas!==false, erosionZones:v.erosionZones!==false, terraTreeWind:v.terraTreeWind!==false, terraSlope:v.terraSlope!==false, terraSlopeAtTarget:v.terraSlopeAtTarget===true, nereidFlow:v.nereidFlow!==false, nereidAllWater:v.nereidAllWater===true, natureWind:v.natureWind===true, natureFire:v.natureFire===true, worldAxes:v.worldAxes===true, objectBounds:v.objectBounds===true, selectionBox:v.selectionBox!==false, surface: v.surface === true,
      snapMove: num(v.snapMove, 1), snapRotate: num(v.snapRotate, 15), snapScale: num(v.snapScale, 0.1) };
  } catch { return DEFAULTS; }
}

function DebugLayersMenu({children}:{children:ReactNode}) {
  const [query,setQuery]=useState('');
  const [empty,setEmpty]=useState(false);
  const list=useRef<HTMLDivElement>(null);
  useLayoutEffect(()=>{
    const groups=list.current;
    if(!groups)return;
    const terms=query.trim().toLowerCase().split(/\s+/);
    const filter=()=>{
      let matches=0;
      for(const group of groups.querySelectorAll(':scope > fieldset')){
        const text=(group.textContent??'').toLowerCase();
        const visible=terms.every(term=>text.includes(term));
        group.toggleAttribute('hidden',!visible);
        if(visible)matches++;
      }
      setEmpty(matches===0);
    };
    filter();
    // Some project-specific options arrive after the menu opens.
    const observer=new MutationObserver(filter);
    observer.observe(groups,{childList:true,subtree:true,characterData:true});
    return ()=>observer.disconnect();
  },[query]);
  return <div className="scene-toolbar__marks-menu scene-toolbar__layers" data-native-overlay role="group" aria-label="Scene overlays">
    <StudioInput type="search" autoFocus aria-label="Search debug views" placeholder="Search debug views…" value={query}
      onChange={event=>setQuery(event.currentTarget.value)}/>
    <div ref={list} className="scene-toolbar__layer-groups">{children}</div>
    {empty&&<p role="status">No matching debug views.</p>}
  </div>;
}

/** Scene header: Q/W/E/R tools, Global/Local, snapping (Ctrl/Cmd inverts while dragging), gizmos. */
export default function SceneToolbar({ enabled, projectPath = '', documentScope }: { enabled: boolean; projectPath?: string; documentScope?:StudioGraphDocumentScope }) {
  const [state, setState] = useState<SceneToolState>(readSceneTools);
  const [marks, setMarks] = useState<CameraBookmark[]>(() => readBookmarks(projectPath));
  const [layersOpen,setLayersOpen]=useState(false);
  const [windDebug,setWindDebug]=useState<WindDebugView>('off');
  const [windError,setWindError]=useState('');
  useEffect(()=>{
    if (!layersOpen || !enabled) return;
    let active=true;
    const refresh=()=>invoke('studio.wind',{}).then(result=>{if(active){setWindDebug(result.settings.debug);setWindError('');}})
      .catch(error=>{if(active)setWindError(String(error));});
    void refresh();
    const off=onNativeEvent(event=>{if(event==='wind.changed')void refresh();});
    return ()=>{active=false;off();};
  },[layersOpen,enabled,projectPath]);
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
      if (event === 'viewport.natureLayers' && payload && typeof payload === 'object') {
        const patch:Partial<SceneToolState>={};
        for (const key of ['strataBounds','strataWire','strataFill','strataRoutes','natureWind','natureFire','nereidFlow','nereidAllWater','terraSlope','terraSlopeAtTarget','terraTreeWind','erosionZones','triggerAreas','regionBorders','rampTiles','resourceFields','playerStarts','mapRoutes'] as const) {
          const value=(payload as Record<string,unknown>)[key];
          if(typeof value==='boolean')patch[key]=value;
        }
        const mode=(payload as Record<string,unknown>).modelDebugMode;
        if(isModelView(mode))patch.modelDebugMode=mode;
        const color=(payload as Record<string,unknown>).strataColor;
        if(typeof color==='number' && [0,1,2,3].includes(color))patch.strataColor=color;
        for(const key of ['natureRadius','natureSpacing'] as const){
          const value=(payload as Record<string,unknown>)[key];
          if(typeof value==='number' && Number.isFinite(value))patch[key]=value;
        }
        setState(current=>{
          const next={...current,...patch};
          try { window.localStorage.setItem(KEY,JSON.stringify(next)); } catch { /* optional */ }
          return next;
        });
        return;
      }
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
        disabled={!enabled} aria-expanded={layersOpen} onClick={()=>{setMarksOpen(false);setLayersOpen(v=>!v);}}><Layers size={13}/> Debug</button>
      {layersOpen&&<DebugLayersMenu>
       <fieldset><legend>Models</legend><label>Render view
         <StudioSelect value={state.modelDebugMode} onChange={e=>set({modelDebugMode:Number(e.target.value)})}>
           {MODEL_VIEWS.map((name,value)=><option key={name} value={value}>{name}</option>)}
         </StudioSelect></label></fieldset>
       {([
         ['Scene',[['worldAxes','World axes'],['objectBounds','Object bounds'],['selectionBox','Selection box']]],
         ['Map',[['mapRoutes','Map routes'],['playerStarts','Player starts'],['resourceFields','Resource fields'],['rampTiles','Ramp tiles'],['regionBorders','Region borders'],['triggerAreas','Trigger areas']]],
         ['STRATA',[['strataBounds','Cluster bounds'],['strataWire','Cluster wireframe'],['strataFill','Cluster fill'],['strataRoutes','Instance routes']]],
         ['PHYSIS · play-test',[['natureWind','Wind field'],['natureFire','Fire spread']]],
         ['TerraWeave',[['terraTreeWind','Wind at selected tree'],['erosionZones','Erosion zones'],['terraSlope','Slope at selected tile'],['terraSlopeAtTarget','Slope at camera target']]],
       ] as const).map(([name,items])=><fieldset key={name}><legend>{name}</legend>{items.map(([key,label])=>
         <label key={key}><input type="checkbox" checked={state[key]} onChange={e=>set({[key]:e.target.checked})}/>{label}</label>)}{name==='STRATA'&&<label>Color by
           <StudioSelect value={state.strataColor} onChange={e=>set({strataColor:Number(e.currentTarget.value)})}>
             {['Cluster','LOD depth','Geometric error','Material'].map((label,value)=><option key={label} value={value}>{label}</option>)}
           </StudioSelect></label>}{name==='TerraWeave' && state.erosionZones && documentScope && <ErosionZoneHighlight key={JSON.stringify([documentScope.projectPath,documentScope.mapPath,documentScope.epoch,documentScope.revision])} scope={documentScope}/>}</fieldset>)}
       <fieldset><legend>Wind rig</legend><label>Display
        <StudioSelect value={windDebug} onChange={e=>{
          const debug=e.currentTarget.value as WindDebugView;
          void invoke('studio.windSet',{debug}).then(result=>{setWindDebug(result.settings.debug);setWindError('');})
            .catch(error=>setWindError(String(error)));
        }}>
         {(['off','parts','depth','weight','skeleton'] as const).map((value,i)=><option key={value} value={value}>{['Off','Parts','Hierarchy depth','Sway weight','Skeleton'][i]}</option>)}
        </StudioSelect></label>{windError&&<small role="alert">{windError}</small>}</fieldset>

       <fieldset><legend>World processes</legend><EcologyLayerPicker key={projectPath} showDiagnostics={false}/></fieldset>
       <fieldset><legend>NEREID</legend><label><input type="checkbox" checked={state.nereidFlow} onChange={e=>set({nereidFlow:e.target.checked})}/>River flow</label>
       {state.nereidFlow&&<label><input type="checkbox" checked={state.nereidAllWater} onChange={e=>set({nereidAllWater:e.target.checked})}/>Show all rivers</label>}</fieldset>
       {(state.natureWind||state.natureFire||state.terraSlope||state.terraSlopeAtTarget)&&<fieldset className="scene-toolbar__sampling"><legend>Field sampling</legend>
        {([['natureRadius','Radius',8,200],['natureSpacing','Spacing',2,32]] as const).map(([key,label,min,max])=>
         <label key={key}>{label}<output>{state[key]} m</output>
          <input aria-label={label+' in metres'} type="range" min={min} max={max} value={state[key]} onChange={e=>set({[key]:e.currentTarget.valueAsNumber})}/>
         </label>)}
       </fieldset>}
      </DebugLayersMenu>}
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
