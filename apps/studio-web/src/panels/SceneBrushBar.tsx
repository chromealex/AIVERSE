import { useEffect, useState } from 'react';
import { Brush, Spline } from 'lucide-react';
import { invoke, onNativeEvent } from '../bridge';
import { WorldProcessDiagnostics } from '../components/WorldProcessDiagnostics';
import type { StudioSceneBrush, EvolveLayer, EvolveLiveState, AetherLayer } from '../types';

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
  const [historyRegion,setHistoryRegion]=useState('all');
  const [history,setHistory]=useState<{tick:number;fertility:number;pollution:number;succession:number;plants:number}[]>([]);
  useEffect(()=>{
    if(!state?.active){setHistory([]);return;}
    const tick=state.tick;
    if(tick===undefined||!state.regions.length)return;
    const selected=historyRegion==='all'?state.regions:state.regions.filter(r=>r.id===historyRegion);
    if(!selected.length)return;
    const mean=(key:'fertility'|'pollution'|'succession')=>selected.reduce((sum,r)=>sum+r[key],0)/selected.length/1e7;
    const point={tick,fertility:mean('fertility'),pollution:mean('pollution'),succession:mean('succession'),plants:state.plantCount??0};
    setHistory(old=>{
      const last=old[old.length-1];
      if(last&&tick===last.tick)return old;
      return [...(last&&tick<last.tick?[]:old),point].slice(-240);
    });
  },[state,historyRegion]);
  const exportHistory=(format:'csv'|'json')=>{
    const body=format==='json'?JSON.stringify({region:historyRegion,samples:history},null,2):['tick,fertility,pollution,succession,plants',...history.map(p=>[p.tick,p.fertility,p.pollution,p.succession,p.plants].join(','))].join('\n');
    const url=URL.createObjectURL(new Blob([body],{type:format==='json'?'application/json':'text/csv'}));
    const a=document.createElement('a');a.href=url;a.download='evolve-history.'+format;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
  };
  const [vegetation,setVegetation]=useState(true);
  const [climate,setClimate]=useState(false);
  const [nutrients,setNutrients]=useState(false);
  const [controlBusy,setControlBusy]=useState(false);
  const [controlError,setControlError]=useState('');
  const control=async(action:'pause'|'resume'|'step'|'checkpointSave'|'checkpointRestore'|'speed1'|'speed2'|'speed5'|'speed10'|'speed25'|'speed50'|'speed100')=>{if(controlBusy)return;setControlBusy(true);setControlError('');try{await invoke('studio.gameControl',{action});}catch(e){setControlError(message(e));}finally{setControlBusy(false);}};
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
  if(state.topology==='planet')return <div><small>Planetary live snapshot · tick {state.tick}</small>
    {state.morph&&<div><small>MORPH · {state.morph.nodes.length}/{state.morph.nodeCount} nodes · {state.morph.springs.length}/{state.morph.springCount} springs{state.morph.available?'':' · unavailable'}{state.morph.truncated?' · capped':''}</small>
      <small>Unprojected {state.morph.unprojected} · frame mismatch {state.morph.frameMismatch} · cross-body {state.morph.crossBody} · omitted {state.morph.omittedSprings} · rejected {state.morph.rejected}{state.morph.gravityCapped?' · gravity budget exceeded':''}</small>
      {state.morph.nodes.slice(0,8).map(n=><div key={n.id}><small>{n.body??'no spatial binding'} · {n.id} · Δ [{n.displacement.join(', ')}] · velocity [{n.velocity.join(', ')}] · configured force [{n.configuredForce.join(', ')}]{n.currentGravityLoad?' · current PHYSIS load ['+n.currentGravityLoad.join(', ')+']':''}{n.pendingImpact?' · pending impact ['+n.pendingImpact.impulse.join(', ')+'] @ '+n.pendingImpact.tick:''}</small></div>)}
      {state.morph.springs.slice(0,8).map(s=><div key={s.id}><small>{s.first} ↔ {s.second} · residual [{s.residual.join(', ')}] · stiffness {s.stiffness}{s.projected?'':' · no common spatial frame'}</small></div>)}
      <small>Quantized node units. Amber rest-to-current displacement; purple springs. Only explicit frame bindings are drawn. Current gravity load is not a recorded historical impulse.</small></div>}
    {state.fracture&&<div><small>FRACTURE · {state.fracture.members.length}/{state.fracture.memberCount} members · {state.fracture.bonds.length}/{state.fracture.bondCount} bonds{state.fracture.available?'':' · unavailable'}{state.fracture.truncated?' · capped':''}</small>
      <small>Anchors {state.fracture.anchored} · broken {state.fracture.broken} · detached {state.fracture.detached} · pending support events {state.fracture.pending} · cross-body {state.fracture.crossBody} · omitted {state.fracture.omittedBonds} · rejected {state.fracture.rejected}</small>
      {state.fracture.members.slice(0,8).map(m=><div key={m.id}><small>{m.body} · {m.id} · {m.detached?'detached':m.broken?'broken':m.anchored?'anchor':'member'}{m.collapse?' · support '+m.collapse.delivery+' @ '+m.collapse.tick:''}{m.natureFracture?' · PHYSIS '+m.natureFracture.delivery:''}{m.stress?' · load '+m.stress.normalizedLoad.toFixed(2)+' · damage '+(m.stress.accumulatedDamage*100).toFixed(1)+'% · climate tick '+m.stress.climateTick:''}</small></div>)}
      {state.fracture.bonds.slice(0,8).map(b=><div key={b.id}><small>{b.first} ↔ {b.second} · damage {b.damage}/{b.strength} · {b.activeSupportEdge?'active':b.enabled?'inactive':'disabled'}</small></div>)}
      <small>Cyan anchor · green member · orange broken · red detached · yellow pending event. Bond damage is not a mechanical stress calculation.</small></div>}
    {state.aether&&<div><small>AETHER · {state.aether.policy} · {state.aether.cells.length}/{state.aether.cellCount} cells · {state.aether.ducts.length}/{state.aether.ductCount} ducts{state.aether.truncated?' · capped':''}{state.aether.available?'':' · unavailable'}</small>
      <small>Cross-body {state.aether.crossBody} · omitted ducts {state.aether.omittedDucts} · rejected {state.aether.rejected}{state.aether.gravityCapped?' · gravity budget exceeded':''}</small>
      {state.aether.cells.slice(0,8).map(c=><div key={c.id}><small>{c.body} · {c.id} · smoke {c.mass[0]} · toxin {c.mass[1]} · oxygen {c.mass[2]} · configured source [{c.sourceRate.join(', ')}]</small></div>)}
      {state.aether.ducts.slice(0,8).map(d=><div key={d.id}><small>{d.from} → {d.to} · {d.oneWay?'one-way':'two-way'} · cap {d.cap} · {d.potentialAvailable?'potential ['+d.potential?.join(', ')+'] · bias ['+d.bias?.join(', ')+']':'potential unavailable'}</small></div>)}
      <small>Mass units are game-quantized, not physical concentrations. Potential is isolated-duct tendency, not measured previous-tick flow. Values ordered smoke/toxin/oxygen.</small></div>}
    {state.gravity&&<div><small>PHYSIS effective gravity · {state.gravity.fieldCount} fields{state.gravity.usesDefaultSettings?" · default baseline":""}{state.gravity.available?"": " · unavailable"}</small>
      {state.gravity.bodies.map(b=><div key={b.body}><small>{b.body}: {b.minMagnitude.toFixed(2)}–{b.maxMagnitude.toFixed(2)} m/s² · inward {b.inward} · outward {b.outward} · tangent {b.tangent} · zero {b.zero}</small></div>)}
      <small>Cyan inward · orange outward · violet tangent · grey zero. Sampled surface points only.</small></div>}
    {state.evolve&&<div><small>EVOLVE: {state.evolve.regions.length} / {state.evolve.regionCount} regions · {layer}{state.evolve.truncated?' · capped':''} · clipped {state.evolve.clipped} · rejected {state.evolve.rejected}</small>
      {state.evolve.regions.slice(0,12).map(r=><div key={r.id}><small>{r.body} · {r.id}{r.chartClipped?' · chart clipped':''} · F {(r.fertility/1e7).toFixed(1)}% · P {(r.pollution/1e7).toFixed(1)}% · S {(r.succession/1e7).toFixed(1)}% · D {(r.durability/1e7).toFixed(1)}%</small></div>)}
      {state.evolve.regions.length>12&&<small>Showing first 12 sampled regions</small>}</div>}
    {state.imprint&&<small>IMPRINT: {state.imprint.cells.length} traces{state.imprint.truncated?' · capped':''}</small>}
    {state.granular&&<small>GRANULAR: {state.granular.cells.length} tops / {state.granular.fieldCount} fields · {state.granular.cellSizeCm} cm diagnostic scale{state.granular.truncated?' · capped':''} · rejected {state.granular.rejected}</small>}
    <div aria-label="Simulation time controls">{(['pause','resume','step'] as const).map(action=><button key={action} type="button" disabled={controlBusy} onClick={()=>void control(action)}>{action==='step'?'Single tick':action==='pause'?'Pause':'Resume'}</button>)}{controlError&&<span role="alert">{controlError}</span>}</div>
    <small>Read-only Scene gizmos. Planar ecology map unavailable on planets.</small></div>;
  const regions=state.regions.slice(0,8192);
  if(!regions.length)return <small>No active EVOLVE regions</small>;
  const x=Math.min(...regions.map(r=>r.minXcm)),z=Math.min(...regions.map(r=>r.minZcm));
  const w=Math.max(1,Math.max(...regions.map(r=>r.maxXcm))-x);
  const h=Math.max(1,Math.max(...regions.map(r=>r.maxZcm))-z);
  const u=(n:number)=>Math.round(255*Math.max(0,Math.min(1,n/1e9)));
  return <div><small>Live ecology · tick {state.tick}</small>
    <div aria-label="Simulation time controls">{(['pause','resume','step'] as const).map(action=><button key={action} type="button" disabled={controlBusy} onClick={()=>void control(action)}>{action==='step'?'Single tick':action==='pause'?'Pause':'Resume'}</button>)}<label>Speed <select disabled={controlBusy} defaultValue="" onChange={e=>void control(e.target.value as 'speed1'|'speed2'|'speed5'|'speed10'|'speed25'|'speed50'|'speed100')}><option value="" disabled>Set speed</option>{[1,2,5,10,25,50,100].map(n=><option key={n} value={'speed'+n}>{n}×</option>)}</select></label><button type="button" disabled={controlBusy} onClick={()=>void control('checkpointSave')}>Save checkpoint</button><button type="button" disabled={controlBusy} onClick={()=>void control('checkpointRestore')}>Restore checkpoint</button><small>Controls the entire local simulation. Checkpoints require Pause.</small>{controlError&&<span role="alert">{controlError}</span>}</div>
    <div><label><input type="checkbox" checked={vegetation} onChange={e=>setVegetation(e.target.checked)}/>Vegetation</label>
      <label><input type="checkbox" checked={climate} onChange={e=>setClimate(e.target.checked)}/>Climate</label>
      <label><input type="checkbox" checked={nutrients} onChange={e=>setNutrients(e.target.checked)}/>Deadwood / nutrients</label>
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
      {nutrients&&(state.soil??[]).map(c=><rect key={`${c.x}:${c.z}`} x={c.x} y={c.z} width={3200} height={3200} fill="#bfa34c" fillOpacity={opacity*.3}><title>Soil nutrients: {c.units}</title></rect>)}
      {nutrients&&(state.logs??[]).map((c,i)=><circle key={i} cx={c.x} cy={c.z} r={Math.max(w,h)/120} fill="#956b43" fillOpacity={Math.max(.1,c.ratio)*opacity}><title>Deadwood remaining {Math.round(c.ratio*100)}%</title></circle>)}
      {vegetation&&(state.plants??[]).map((p,i)=><circle key={p.id??i} cx={p.x} cy={p.z} r={Math.max(w,h)/180} fill={p.dead?'#a36c55':p.fallen?'orange':p.mature===false?'#8ee9ef':'lightgreen'} fillOpacity={opacity}><title>{p.dead?'Dead':p.fallen?'Fallen':p.mature===false?'Juvenile':'Mature'} · age {p.ageTicks??'unknown'} · shade {p.shadeStress??'—'} · flood {p.floodStress??'—'} · drought {p.droughtStress??'—'}</title></circle>)}
    </svg>
    <label>History region <select value={historyRegion} onChange={e=>{setHistory([]);setHistoryRegion(e.target.value);}}><option value="all">All regions</option>{regions.map(r=><option key={r.id} value={r.id}>{r.id}</option>)}</select></label>
    {history.length>1&&<div><small>Ecology history · last {history.length} samples</small>
      <svg viewBox="0 0 320 100" width="100%" height="100" aria-label="Ecology history">
        {([['fertility','#68c978'],['pollution','#ed7777'],['succession','#80aaff']] as const).map(([key,color])=>
          <polyline key={key} fill="none" stroke={color} strokeWidth="2" points={history.map(p=>{
            const x=320*(p.tick-history[0].tick)/Math.max(1,history[history.length-1].tick-history[0].tick);
            return `${x},${100-p[key]}`;
          }).join(' ')}/>)}
      </svg><small>Green: fertility · red: pollution · blue: succession</small>
      <small>Plants: {history[0].plants} → {history[history.length-1].plants}</small>
      <button type="button" onClick={()=>setHistory([])}>Clear history</button><button type="button" onClick={()=>exportHistory('csv')}>CSV</button><button type="button" onClick={()=>exportHistory('json')}>JSON</button>
    </div>}{nutrients&&<small>Absorbed: {state.absorbedUnits??"—"} · respired: {state.respiredUnits??"—"}</small>}{vegetation&&<small>Rooted plants: {state.plantCount??0} · displayed {state.plants?.length??0}</small>}</div>;
}
export function EcologyLayerPicker(){
  const [layer,setLayer]=useState<EvolveLayer>('combined');
  const [live,setLive]=useState(false);
  const [granularCellCm,setGranularCellCm]=useState(0);
  const [liveGravity,setLiveGravity]=useState(false);
  const [aetherLayer,setAetherLayer]=useState<AetherLayer>('off');
  const [liveFracture,setLiveFracture]=useState(false);
  const [liveMorph,setLiveMorph]=useState(false);
  const [pending,setPending]=useState(true);
  const [error,setError]=useState('');
  useEffect(()=>{let active=true;void invoke('studio.evolveLayer',{}).then(r=>{if(active){setLayer(r.layer);setLive(r.live);setGranularCellCm(r.granularCellCm??0);setLiveGravity(r.liveGravity??false);setAetherLayer(r.aetherLayer??'off');setLiveFracture(r.liveFracture??false);setLiveMorph(r.liveMorph??false);}}).catch(e=>{if(active)setError(message(e));}).finally(()=>{if(active)setPending(false);});return()=>{active=false;};},[]);
  return <div><label className="ptp__row"><span>EVOLVE (authored)</span><select className="au-input" value={layer} disabled={pending} onChange={e=>{
    const next=e.target.value as EvolveLayer;
    setPending(true);
    void invoke('studio.evolveLayer',{layer:next}).then(r=>{setLayer(r.layer);setError('');}).catch(e=>setError(message(e))).finally(()=>setPending(false));
  }}>{(['combined','fertility','pollution','succession','durability','off'] as EvolveLayer[]).map(v=><option key={v} value={v}>{v}</option>)}</select>{error&&<span role="alert">{error}</span>}</label><label><input type="checkbox" checked={live} disabled={pending} onChange={e=>{setPending(true);void invoke('studio.evolveLayer',{live:e.target.checked}).then(r=>{setLive(r.live);setError('');}).catch(e=>setError(message(e))).finally(()=>setPending(false));}}/>Live 3D overlay</label><label><input type="checkbox" checked={liveGravity} disabled={pending||!live} onChange={e=>{setPending(true);void invoke('studio.evolveLayer',{liveGravity:e.target.checked}).then(r=>{setLiveGravity(r.liveGravity??false);setError('');}).catch(e=>setError(message(e))).finally(()=>setPending(false));}}/>Effective PHYSIS gravity (planet)</label><label><input type="checkbox" checked={liveFracture} disabled={pending||!live} onChange={e=>{setPending(true);void invoke('studio.evolveLayer',{liveFracture:e.target.checked}).then(r=>{setLiveFracture(r.liveFracture??false);setError('');}).catch(e=>setError(message(e))).finally(()=>setPending(false));}}/>FRACTURE structural state (planet)</label><label><input type="checkbox" checked={liveMorph} disabled={pending||!live} onChange={e=>{setPending(true);void invoke('studio.evolveLayer',{liveMorph:e.target.checked}).then(r=>{setLiveMorph(r.liveMorph??false);setError('');}).catch(e=>setError(message(e))).finally(()=>setPending(false));}}/>MORPH node displacement (planet)</label><label className="ptp__row"><span>AETHER gas mass / potential (planet)</span><select className="au-input" value={aetherLayer} disabled={pending||!live} onChange={e=>{setPending(true);void invoke('studio.evolveLayer',{aetherLayer:e.target.value as AetherLayer}).then(r=>{setAetherLayer(r.aetherLayer??'off');setError('');}).catch(e=>setError(message(e))).finally(()=>setPending(false));}}>{(['off','smoke','toxin','oxygen'] as const).map(v=><option key={v} value={v}>{v}</option>)}</select></label><label className="ptp__row"><span>GRANULAR planet diagnostic cell/depth (cm)</span><select className="au-input" disabled={pending||!live} value={granularCellCm} onChange={e=>{
    setPending(true);void invoke('studio.evolveLayer',{granularCellCm:Number(e.target.value)}).then(r=>{setGranularCellCm(r.granularCellCm??0);setError('');}).catch(e=>setError(message(e))).finally(()=>setPending(false));
  }}>{Array.from(new Set([0,10,25,50,100,200,500,1000,2000,5000,10000,granularCellCm])).sort((a,b)=>a-b).map(n=><option key={n} value={n}>{n===0?'Off':n}</option>)}</select></label><small>Display scale only; ground-relative tops, no physical geometry changes.</small><EcologyLiveMap layer={layer}/><WorldProcessDiagnostics/></div>;
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
