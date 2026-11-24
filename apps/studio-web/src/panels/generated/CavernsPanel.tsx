import { useEffect, useRef, useState } from 'react';
import { Mountain, RefreshCw } from 'lucide-react';
import { invoke, onNativeEvent } from '../../bridge';
import type { StudioSnapshot } from '../../types';
import { StudioPanel, StudioSection, StudioField, StudioInput, StudioSelect, StudioButton, StudioIconButton, StudioAlert, StudioEmptyState, StudioBadge } from '../../ui';

type Stamp = { id: string; xCm: number; yCm: number; zCm: number; radiusCm: number; mode: 'carve' | 'fill' };
type Entrance = { id: string; xCm: number; zCm: number; caveYcm: number; surfaceYcm: number; radiusCm: number };
type Document = { revision: string; seed: string; generator: string; settings: Record<string, number>; stamps: Stamp[]; entrances: Entrance[] };
const name = 'terraweave_caverns_paint';
const decimal = (x: unknown): x is string => typeof x === 'string' && /^[0-9]+$/.test(x);
function decode(value: unknown): Document {
  const r = value as { revisionText?: unknown; seedText?: unknown; generatorVersionText?: unknown; stampIds?: unknown[]; entranceIds?: unknown[]; snapshot?: { settings?: Record<string, number>; stamps?: Omit<Stamp,'id'>[]; entrances?: Omit<Entrance,'id'>[] } };
  if (!decimal(r?.revisionText) || !decimal(r.seedText) || !decimal(r.generatorVersionText) ||
      !r.snapshot?.settings || !Array.isArray(r.snapshot.stamps) || !Array.isArray(r.snapshot.entrances) ||
      !Array.isArray(r.stampIds) || !Array.isArray(r.entranceIds) ||
      r.stampIds.length !== r.snapshot.stamps.length || r.entranceIds.length !== r.snapshot.entrances.length ||
      !r.stampIds.every(decimal) || !r.entranceIds.every(decimal)) throw new Error('Caverns native document contract unavailable. Integrate the Caverns branch.');
  const ids = r.stampIds as string[], entranceIds = r.entranceIds as string[];
  return { revision:r.revisionText, seed:r.seedText, generator:r.generatorVersionText, settings:r.snapshot.settings,
    stamps:r.snapshot.stamps.map((s,i)=>({...s,id:ids[i]})), entrances:r.snapshot.entrances.map((e,i)=>({...e,id:entranceIds[i]})) };
}
const message = (e: unknown) => e instanceof Error ? e.message : String(e);
const fields = ['xCm','yCm','zCm','radiusCm'] as const;
export default function CavernsPanel({ snapshot, commitSnapshot }: { snapshot: StudioSnapshot | null; commitSnapshot: (s: StudioSnapshot)=>void }) {
  const project = snapshot?.project.path ?? '';
  const [doc,setDoc] = useState<Document|null>(null);
  const [map,setMap] = useState('');
  const [error,setError] = useState('');
  const [status,setStatus] = useState('');
  const [busy,setBusy] = useState(false);
  const [mode,setMode] = useState<'carve'|'fill'>('carve');
  const [form,setForm] = useState({xCm:'0',yCm:'-500',zCm:'0',radiusCm:'400'});
  const [selected,setSelected] = useState('');
  const [pickMode,setPickMode] = useState<'surface'|'plane'>('surface');
  const [armed,setArmed] = useState(false);
  const [page,setPage] = useState(0);
  const epoch=useRef(0);
  const locked=useRef(false);
  const currentProject=useRef(project);
  currentProject.current=project;
  async function read(path: string, mapPath: string) {
    const result=await invoke('studio.op',{op:name,expectedProject:path,expectedMap:mapPath,params:{mode:'get'}});
    return decode(result.result);
  }
  async function refresh() {
    if (!project || locked.current) return;
    locked.current=true;
    const serial=++epoch.current;
    setBusy(true);setError('');setStatus('');setDoc(null);setSelected('');
    try {
      const available=await invoke('studio.ops',{panel:'caverns'});
      if (!available.ops.some(o=>o.name===name)) throw new Error('Caverns native backend not registered in this build.');
      const context=await invoke('studio.project',{});
      const next=await read(project,context.currentMap);
      if(serial===epoch.current && currentProject.current===project){setMap(context.currentMap);setDoc(next);setPage(0);}
    } catch(e){if(serial===epoch.current && currentProject.current===project)setError(message(e));}
    finally {locked.current=false;if(serial===epoch.current)setBusy(false);}
  }
  useEffect(()=>{
    ++epoch.current;setDoc(null);setSelected('');setError('');setStatus('');setBusy(false);
    // Explicit refresh avoids using an earlier map or unsaved document after a switch.
    return ()=>{++epoch.current;};
  },[project,snapshot?.scene.name,snapshot?.history.canUndo,snapshot?.history.canRedo]);
  useEffect(()=>onNativeEvent((event,payload)=>{
    if(event==='brush.changed' && payload && typeof payload==='object'){
      const b=payload as {armed?:boolean;kind?:string};
      setArmed(b.armed===true&&b.kind==='caverns');
    }
    if(event==='document.changed'){setDoc(null);setSelected('');}
  }),[]);
  const values=Object.fromEntries(fields.map(k=>[k,Number(form[k])])) as Record<typeof fields[number],number>;
  const valid=fields.every(k=>form[k].trim()!=='' && Number.isSafeInteger(values[k])) &&
    fields.slice(0,3).every(k=>Math.abs(values[k])<=1000000000) && values.radiusCm>=1 && values.radiusCm<=1000000;
  const editable=Boolean(doc && project && snapshot?.mode==='edit' && !busy);
  async function armScene(enable:boolean){
    if(locked.current||(!editable&&enable))return;
    locked.current=true;setBusy(true);setError('');
    try{
      const b=await invoke('studio.sceneBrush',enable?{kind:'caverns',armed:true,
        radius:values.radiusCm/100,caveMode:mode,caveY:values.yCm/100,caveSurface:pickMode==='surface'}:{armed:false});
      setArmed(b.armed&&b.kind==='caverns');
    }catch(e){setError(message(e));}
    finally{locked.current=false;setBusy(false);}
  }
  async function edit(params: Record<string,unknown>) {
    if(!editable || !doc || locked.current)return;
    locked.current=true;
    const serial=++epoch.current;
    setBusy(true);setError('');setStatus('');
    try {
      await invoke('studio.op',{op:name,expectedProject:project,expectedMap:map,params:{...params,expectedRevision:doc.revision}});
      const next=await read(project,map);
      const snap=await invoke('studio.snapshot',{});
      if(serial===epoch.current && currentProject.current===project && snap.project.path===project){
        setDoc(next);setSelected('');setStatus('Map edit committed. Native preview readiness is not reported.');commitSnapshot(snap);
      }
    }catch(e){if(serial===epoch.current && currentProject.current===project){setError(message(e));setDoc(null);}}
    finally{locked.current=false;if(serial===epoch.current)setBusy(false);}
  }
  return <StudioPanel title="Caverns" eyebrow="WORLD / TERRAWEAVE" subtitle={project ? snapshot?.project.name : 'No project open'}
    actions={<StudioIconButton aria-label="Refresh cave document" icon={<RefreshCw size={15}/>} disabled={!project||busy} onClick={()=>void refresh()}/>}
    footer={<StudioBadge tone={error?'danger':doc?'success':'neutral'}>{busy?'Working':error?'Unavailable':doc?'Document ready':'Not loaded'}</StudioBadge>}>
    <div className="au-stack">
      {!project && <StudioEmptyState icon={<Mountain size={22}/>} title="Open a project" description="Author signed-centimetre cave volume stamps."/>}
      {error && <StudioAlert tone="danger" title="Caverns">{error}</StudioAlert>}
      {status && <StudioAlert title="Authoring">{status}</StudioAlert>}
      {armed && <StudioButton variant="danger" disabled={busy} onClick={()=>void armScene(false)}>Disarm Scene brush</StudioButton>}
      {project && !doc && <StudioButton disabled={busy} onClick={()=>void refresh()}>Load cave document</StudioButton>}
      {doc && <>
        <StudioSection title="SDF brush">
          <StudioField label="Operation" htmlFor="cave-mode"><StudioSelect id="cave-mode" value={mode} disabled={!editable} onChange={e=>setMode(e.target.value as 'carve'|'fill')}><option value="carve">Carve — create void</option><option value="fill">Fill — solid material</option></StudioSelect></StudioField>
          {fields.map(k=><StudioField key={k} label={k==='radiusCm'?'Radius (cm)':k[0].toUpperCase()+' (cm)'} htmlFor={'cave-'+k}><StudioInput id={'cave-'+k} type="number" step={1} value={form[k]} disabled={!editable} onChange={e=>setForm(f=>({...f,[k]:e.target.value}))}/></StudioField>)}
          <StudioButton variant="primary" disabled={!editable||!valid} onClick={()=>void edit({mode,...values})}>Apply {mode} stamp</StudioButton>
          <StudioField label="Scene picking" htmlFor="cave-pick"><StudioSelect id="cave-pick" value={pickMode} disabled={busy} onChange={e=>setPickMode(e.target.value as 'surface'|'plane')}><option value="surface">SDF surface</option><option value="plane">Depth plane (Y above)</option></StudioSelect></StudioField>
          <StudioButton variant="primary" disabled={!editable||!valid||values.radiusCm<25||values.radiusCm>6400} onClick={()=>void armScene(true)}>{armed?'Update Scene brush':'Arm Scene brush'}</StudioButton>
          <StudioAlert title="Scene authoring">Plain left drag paints; Alt orbits, Esc disarms. Surface picks the native SDF; depth plane uses Y. A drag is one History step. Cave mesh preview is not yet connected to Studio.</StudioAlert>
        </StudioSection>
        <StudioSection title={'Stamps · '+doc.stamps.length}>
          {doc.stamps.slice(page*32,page*32+32).map(s=><StudioButton key={s.id} variant={selected===s.id?'primary':'secondary'} disabled={busy} onClick={()=>{setSelected(s.id);setForm({xCm:String(s.xCm),yCm:String(s.yCm),zCm:String(s.zCm),radiusCm:String(s.radiusCm)});setMode(s.mode);}}>{s.mode} #{s.id} · r {s.radiusCm} cm</StudioButton>)}
          <div className="au-row"><StudioButton disabled={page===0} onClick={()=>setPage(p=>p-1)}>Previous</StudioButton><StudioButton disabled={(page+1)*32>=doc.stamps.length} onClick={()=>setPage(p=>p+1)}>Next</StudioButton></div>
          <StudioButton variant="danger" disabled={!editable||!doc.stamps.some(s=>s.id===selected)} onClick={()=>void edit({mode:'erase',stampIdText:selected})}>Erase selected stamp</StudioButton>
        </StudioSection>
        <StudioSection title="Generator · read only">
          <div>Revision: {doc.revision}</div><div>Generator: {doc.generator}</div><div>Seed: {doc.seed}</div>
          {Object.entries(doc.settings).filter(([k])=>k!=='seed').map(([k,v])=><div key={k}>{k}: {v}</div>)}
        </StudioSection>
        <StudioSection title={'Entrances · '+doc.entrances.length}>
          {doc.entrances.slice(0,32).map(e=><div key={e.id}>#{e.id} · ({e.xCm}, {e.zCm}) · {e.caveYcm} → {e.surfaceYcm} cm</div>)}
          {doc.entrances.length>32 && <div>Showing first 32 entrances.</div>}
          <StudioAlert title="Native diagnostics">Viewport mesh, collision, streaming and paging telemetry are not exposed by this Studio build. Settings and entrances are read only here.</StudioAlert>
        </StudioSection>
      </>}
    </div>
  </StudioPanel>;
}
