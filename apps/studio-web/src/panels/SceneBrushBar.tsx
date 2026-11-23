import { useEffect, useState } from 'react';
import { Brush, Spline } from 'lucide-react';
import { invoke, onNativeEvent } from '../bridge';
import type { StudioSceneBrush, EvolveLayer, EvolveLiveState } from '../types';

export type BrushKind = StudioSceneBrush['kind'];
const KIND_LABEL: Record<BrushKind, string> = {
  terrain: 'Sculpt', paint: 'Paint', objects: 'Objects', tiles: 'Tiles', field: 'Field', road: 'Draw road', river: 'Draw river',
};
const OPS: Array<[StudioSceneBrush['op'], string]> = [['raise', 'Raise'], ['lower', 'Lower'], ['smooth', 'Smooth'], ['flatten', 'Flatten'], ['noise', 'Noise'], ['erode', 'Erode']];
const message = (e: unknown) => e instanceof Error ? e.message : String(e);
const op = async (name: string, params: Record<string, unknown> = {}) => (await invoke('studio.op', { op: name, params })).result as Record<string, any>;

/** Choices as chips (filterable when there are many): clickable without a native pop-up menu. */
function Choice({ label, value, options, onChange }: { label: string; value: string; options: string[]; onChange: (v: string) => void }) {
  const [filter, setFilter] = useState('');
  const shown = options.filter(o => !filter || o.toLowerCase().includes(filter.toLowerCase())).slice(0, 60);
  return <div className="tbrush__choice"><span>{label}</span>
    {options.length > 12 && <input className="au-input tbrush__filter" placeholder="Filter" value={filter} onChange={e => setFilter(e.target.value)}/>}
    <div className="tbrush__chips" role="radiogroup" aria-label={label}>
      {shown.map(o => <button key={o} type="button" role="radio" aria-checked={value === o} className={value === o ? 'is-on' : ''} onClick={() => onChange(o)}>{o}</button>)}
      {!options.length && <em>none in this project</em>}
    </div></div>;
}
function Slider({ label, value, min, max, step, onCommit }: { label: string; value: number; min: number; max: number; step: number; onCommit: (v: number) => void }) {
  const [v, setV] = useState(value);
  useEffect(() => setV(value), [value]);
  return <label className="ptp__row"><span>{label}</span>
    <input type="range" min={min} max={max} step={step} value={v} onChange={e => setV(Number(e.target.value))}
      onPointerUp={() => v !== value && onCommit(v)} onKeyUp={() => v !== value && onCommit(v)}/><output>{v}</output></label>;
}

/**
 * Scene brush for a World panel: arm it, then drag in the Scene. Dab tools (Sculpt, Paint, Objects, Tiles, Field) apply live;
 * Draw road / Draw river collect the path and build it on release. Each stroke is one History step. [ ] resize, Esc puts it down.
 */
function EcologyLiveMap({layer}:{layer:EvolveLayer}){
  const [state,setState]=useState<EvolveLiveState|null>(null);
  const [vegetation,setVegetation]=useState(true);
  const [climate,setClimate]=useState(false);
  const [opacity,setOpacity]=useState(.65);
  const [error,setError]=useState('');
  useEffect(()=>{
    let alive=true,busy=false;
    const refresh=async()=>{
      if(busy||document.hidden)return;
      busy=true;
      try{const result=await invoke('studio.evolveState',{});if(alive){setState(result);setError('');}}
      catch(e){if(alive){setState(null);setError(message(e));}}
      finally{busy=false;}
    };
    void refresh();const timer=window.setInterval(refresh,500);
    return()=>{alive=false;window.clearInterval(timer);};
  },[]);
  if(error)return <span role="alert">{error}</span>;
  if(!state?.active)return <small>Live ecology: start Play mode</small>;
  const regions=state.regions.slice(0,8192);
  if(!regions.length)return <small>No active EVOLVE regions</small>;
  const x=Math.min(...regions.map(r=>r.minXcm)),z=Math.min(...regions.map(r=>r.minZcm));
  const w=Math.max(1,Math.max(...regions.map(r=>r.maxXcm))-x);
  const h=Math.max(1,Math.max(...regions.map(r=>r.maxZcm))-z);
  const u=(n:number)=>Math.round(255*Math.max(0,Math.min(1,n/1e9)));
  return <div><small>Live ecology · tick {state.tick}</small>
    <div><label><input type="checkbox" checked={vegetation} onChange={e=>setVegetation(e.target.checked)}/>Vegetation</label>
      <label><input type="checkbox" checked={climate} onChange={e=>setClimate(e.target.checked)}/>Climate</label>
      <label>Opacity <input type="range" min="0.1" max="1" step="0.05" value={opacity} onChange={e=>setOpacity(Number(e.target.value))}/></label></div>
    <svg viewBox={`${x} ${z} ${w} ${h}`} width="100%" height="160" aria-label="Live ecological regions">
      {regions.map(r=>{
        let red=u(r.pollution),green=u(r.fertility),blue=90;
        if(layer==='fertility')red=40;
        if(layer==='pollution')green=40;
        if(layer==='succession'){red=40;green=u(r.succession);blue=220;}
        if(layer==='durability'){green=u(r.durability);red=255-green;}
        if(layer==='off'){red=138;green=190;blue=255;}
        return <rect key={r.id} x={r.minXcm} y={r.minZcm} width={r.maxXcm-r.minXcm} height={r.maxZcm-r.minZcm}
          fill={`rgb(${red},${green},${blue})`} fillOpacity={opacity} stroke="currentColor" vectorEffect="non-scaling-stroke"><title>{r.id}: fertility {r.fertility/1e7}%, pollution {r.pollution/1e7}%, succession {r.succession/1e7}%</title></rect>;
      })}
      {climate&&(state.weather??[]).map(c=><rect key={`${c.x}:${c.z}`} x={c.x} y={c.z} width={c.size} height={c.size}
        fill="royalblue" fillOpacity={Math.min(.8,Math.max(0,c.wetness))*opacity}><title>Rain {c.rain}; temperature {c.temperature} °C; wetness {c.wetness}</title></rect>)}
      {vegetation&&(state.plants??[]).map((p,i)=><circle key={p.id??i} cx={p.x} cy={p.z} r={Math.max(w,h)/180} fill={p.dead?'#a36c55':p.fallen?'orange':p.mature===false?'#8ee9ef':'lightgreen'} fillOpacity={opacity}><title>{p.dead?'Dead':p.fallen?'Fallen':p.mature===false?'Juvenile':'Mature'} · age {p.ageTicks??'unknown'} · shade {p.shadeStress??'—'} · flood {p.floodStress??'—'} · drought {p.droughtStress??'—'}</title></circle>)}
    </svg>{vegetation&&<small>Rooted plants: {state.plantCount??0} · displayed {state.plants?.length??0}</small>}</div>;
}
export function EcologyLayerPicker(){
  const [layer,setLayer]=useState<EvolveLayer>('combined');
  const [live,setLive]=useState(false);
  const [pending,setPending]=useState(true);
  const [error,setError]=useState('');
  useEffect(()=>{let active=true;void invoke('studio.evolveLayer',{}).then(r=>{if(active){setLayer(r.layer);setLive(r.live);}}).catch(e=>{if(active)setError(message(e));}).finally(()=>{if(active)setPending(false);});return()=>{active=false;};},[]);
  return <div><label className="ptp__row"><span>EVOLVE (authored)</span><select className="au-input" value={layer} disabled={pending} onChange={e=>{
    const next=e.target.value as EvolveLayer;
    setPending(true);
    void invoke('studio.evolveLayer',{layer:next}).then(r=>{setLayer(r.layer);setError('');}).catch(e=>setError(message(e))).finally(()=>setPending(false));
  }}>{(['combined','fertility','pollution','succession','durability','off'] as EvolveLayer[]).map(v=><option key={v} value={v}>{v}</option>)}</select>{error&&<span role="alert">{error}</span>}</label><label><input type="checkbox" checked={live} disabled={pending} onChange={e=>{setPending(true);void invoke('studio.evolveLayer',{live:e.target.checked}).then(r=>{setLive(r.live);setError('');}).catch(e=>setError(message(e))).finally(()=>setPending(false));}}/>Live 3D overlay</label><EcologyLiveMap layer={layer}/></div>;
}
export default function SceneBrushBar({ kinds }: { kinds: BrushKind[] }) {
  const [brush, setBrush] = useState<StudioSceneBrush | null>(null);
  const [error, setError] = useState('');
  const [editing, setEditing] = useState(false);
  const [options, setOptions] = useState<{ tiles: string[]; fields: string[]; types: string[]; roads: string[] }>({ tiles: [], fields: [], types: [], roads: [] });
  const set = (patch: Partial<StudioSceneBrush>) =>
    void invoke('studio.sceneBrush', patch).then(b => { setBrush(b); setError(''); }).catch(e => setError(message(e)));
  useEffect(() => {
    void invoke('studio.sceneBrush', kinds.includes('terrain') ? {} : { kind: kinds[0] }).then(b =>
      kinds.includes(b.kind) ? setBrush(b) : invoke('studio.sceneBrush', { kind: kinds[0], armed: false }).then(setBrush)).catch(() => {});
    const off = onNativeEvent((event, payload) => { if (event === 'brush.changed' && payload) setBrush(payload as StudioSceneBrush); });
    // Leaving the panel puts the brush down: Scene clicks select again.
    return () => { off(); void invoke('studio.sceneBrush', { armed: false }).catch(() => {}); void invoke('studio.splineEdit', { kind: '' }).catch(() => {}); };
  }, [kinds.join()]); // eslint-disable-line react-hooks/exhaustive-deps
  const kind = brush && kinds.includes(brush.kind) ? brush.kind : kinds[0];
  useEffect(() => {
    // Choices for the active tool, from the same ops the panels use.
    if (kind === 'tiles') void op('tileset_list').then(r => setOptions(o => ({ ...o,
      tiles: (r.tilesets ?? []).filter((t: any) => !r.mapTileset || t.name === r.mapTileset || t.path?.includes(r.mapTileset)).flatMap((t: any) => (t.terrains ?? []).map((x: any) => x.id)) }))).catch(() => {});
    if (kind === 'field') void op('field_list').then(r => setOptions(o => ({ ...o, fields: (r.fields ?? []).map((f: any) => f.id) }))).catch(() => {});
    if (kind === 'road') void op('road_types').then(r => setOptions(o => ({ ...o, roads: (r.types ?? []).map((t: any) => t.id ?? t.name).filter(Boolean) }))).catch(() => {});
    if (kind === 'objects') void invoke('studio.rules', {}).then((r: any) => setOptions(o => ({ ...o, types: Object.keys(r.types ?? {}).sort() }))).catch(() => {});
  }, [kind]);
  // A tool with nothing chosen yet takes the first choice, so a stroke never fails on an empty setting.
  useEffect(() => {
    if (!brush) return;
    if (kind === 'tiles' && !brush.tile && options.tiles[0]) set({ tile: options.tiles[0] });
    if (kind === 'field' && !brush.field && options.fields[0]) set({ field: options.fields[0] });
    if (kind === 'road' && !brush.roadType && options.roads[0]) set({ roadType: options.roads[0] });
    if (kind === 'objects' && !brush.type && options.types[0]) set({ type: options.types.find(t => /tree|rock|stone/i.test(t)) ?? options.types[0] });
  }, [kind, options, brush?.tile, brush?.field, brush?.roadType, brush?.type]); // eslint-disable-line react-hooks/exhaustive-deps
  if (!brush) return null;
  const continuous = kind === 'terrain' || kind === 'paint' || kind === 'tiles' || kind === 'field' || kind === 'objects';
  return <div className={'tbrush' + (brush.armed ? ' is-armed' : '')}>
    <EcologyLayerPicker/>
    <div className="tbrush__row">
      <button type="button" className={'au-button au-button--sm' + (brush.armed ? ' au-button--primary' : '')} aria-pressed={brush.armed}
        onClick={() => { set({ armed: !brush.armed, kind }); if (!brush.armed) setEditing(false); }} title="Drag in the Scene (Alt+drag orbits, [ ] size, Esc stops)">
        <Brush size={13}/>{brush.armed ? 'Brush on' : 'Brush'}</button>
      {(kind === 'road' || kind === 'river') && <button type="button" className={'au-button au-button--sm' + (editing ? ' au-button--primary' : '')}
        aria-pressed={editing} title="Show the nodes in the Scene and drag them (each move is one History step)"
        onClick={() => void invoke('studio.splineEdit', { kind: editing ? '' : kind }).then(r => { setEditing(!!r.kind); if (r.kind) setBrush(b => b && { ...b, armed: false }); setError(''); })
          .catch(e => setError(message(e)))}><Spline size={13}/>{editing ? 'Editing nodes' : 'Edit nodes'}</button>}
      {kinds.length > 1 && <div className="ptp__seg" role="radiogroup" aria-label="Brush tool">
        {kinds.map(k => <button key={k} type="button" role="radio" aria-checked={kind === k} className={kind === k ? 'is-on' : ''}
          onClick={() => set({ kind: k })}>{KIND_LABEL[k]}</button>)}</div>}
    </div>
    {kind === 'terrain' && <div className="ptp__seg" role="radiogroup" aria-label="Sculpt operation">
      {OPS.map(([id, label]) => <button key={id} type="button" role="radio" aria-checked={brush.op === id}
        className={brush.op === id ? 'is-on' : ''} onClick={() => set({ op: id })}>{label}</button>)}</div>}
    {kind === 'paint' && <Slider label="Layer" value={brush.layer} min={0} max={3} step={1} onCommit={v => set({ layer: v })}/>}
    {kind === 'tiles' && <Choice label="Terrain" value={brush.tile} options={options.tiles} onChange={v => set({ tile: v })}/>}
    {kind === 'field' && <>
      <Choice label="Field" value={brush.field} options={options.fields} onChange={v => set({ field: v })}/>
      <Choice label="Mode" value={brush.fieldMode} options={['set', 'add', 'max', 'erase']} onChange={v => set({ fieldMode: v as StudioSceneBrush['fieldMode'] })}/>
      <Slider label="Value" value={brush.value} min={0} max={100} step={1} onCommit={v => set({ value: v })}/></>}
    {kind === 'objects' && <>
      <Choice label="Type" value={brush.type} options={options.types} onChange={v => set({ type: v })}/>
      <Slider label="Owner" value={brush.owner} min={-1} max={8} step={1} onCommit={v => set({ owner: v })}/>
      <Slider label="Per dab" value={brush.count} min={1} max={10} step={1} onCommit={v => set({ count: v })}/>
      <Slider label="Spread" value={brush.spread} min={0} max={8} step={0.5} onCommit={v => set({ spread: v })}/></>}
    {kind === 'road' && <Choice label="Road type" value={brush.roadType} options={options.roads} onChange={v => set({ roadType: v })}/>}
    {kind === 'river' && <Slider label="Width, m" value={brush.width} min={2} max={80} step={1} onCommit={v => set({ width: v })}/>}
    {continuous && <Slider label={kind === 'objects' ? 'Spacing' : 'Radius'} value={brush.radius} min={0.5} max={24} step={0.5} onCommit={v => set({ radius: v })}/>}
    {(kind === 'terrain' || kind === 'paint') && <Slider label="Strength" value={brush.strength} min={0.05}
      max={kind === 'paint' || brush.op === 'smooth' || brush.op === 'flatten' || brush.op === 'erode' ? 1 : 6} step={0.05} onCommit={v => set({ strength: v })}/>}
    {editing && <p className="ptp__hint">Drag a {kind === 'road' ? 'road node' : 'river point'} in the Scene to move it</p>}
    {brush.armed && <p className="ptp__hint">{continuous ? 'Drag in the Scene to paint' : 'Drag the path in the Scene; it is built on release'} · one History step per stroke · [ ] size · Esc stops</p>}
    {error && <div className="tool__error" role="alert">{error}</div>}
  </div>;
}
