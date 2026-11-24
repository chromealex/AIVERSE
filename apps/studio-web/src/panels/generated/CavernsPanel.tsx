import { useEffect, useRef, useState } from 'react';
import { Mountain, RefreshCw } from 'lucide-react';
import { invoke, onNativeEvent } from '../../bridge';
import type { StudioSnapshot, StudioCavernsPreview, StudioCavernsBudgets } from '../../types';
import { StudioPanel, StudioSection, StudioField, StudioInput, StudioSelect, StudioButton, StudioIconButton, StudioAlert, StudioEmptyState, StudioBadge, StudioCheckbox } from '../../ui';

type Stamp = { id: string; xCm: number; yCm: number; zCm: number; radiusCm: number; mode: 'carve' | 'fill' };
type Entrance = { id: string; xCm: number; zCm: number; caveYcm: number; surfaceYcm: number; radiusCm: number };
type Document = { mapRevision: string; revision: string; seed: string; generator: string; settings: Record<string, number>; stamps: Stamp[]; entrances: Entrance[] };
const name = 'terraweave_caverns_paint';
const decimal = (x: unknown): x is string => typeof x === 'string' && /^[0-9]+$/.test(x);
function decode(value: unknown): Document {
  const r = value as { mapRevisionText?: unknown; revisionText?: unknown; seedText?: unknown; generatorVersionText?: unknown; stampIds?: unknown[]; entranceIds?: unknown[]; snapshot?: { settings?: Record<string, number>; stamps?: Omit<Stamp,'id'>[]; entrances?: Omit<Entrance,'id'>[] } };
  if (!decimal(r?.mapRevisionText) || !decimal(r.revisionText) || !decimal(r.seedText) || !decimal(r.generatorVersionText) ||
      !r.snapshot?.settings || !Array.isArray(r.snapshot.stamps) || !Array.isArray(r.snapshot.entrances) ||
      !Array.isArray(r.stampIds) || !Array.isArray(r.entranceIds) ||
      r.stampIds.length !== r.snapshot.stamps.length || r.entranceIds.length !== r.snapshot.entrances.length ||
      !r.stampIds.every(decimal) || !r.entranceIds.every(decimal)) throw new Error('Caverns native document contract unavailable. Integrate the Caverns branch.');
  const ids = r.stampIds as string[], entranceIds = r.entranceIds as string[];
  return { mapRevision:r.mapRevisionText, revision:r.revisionText, seed:r.seedText, generator:r.generatorVersionText, settings:r.snapshot.settings,
    stamps:r.snapshot.stamps.map((s,i)=>({...s,id:ids[i]})), entrances:r.snapshot.entrances.map((e,i)=>({...e,id:entranceIds[i]})) };
}
const message = (e: unknown) => e instanceof Error ? e.message : String(e);
const fields = ['xCm','yCm','zCm','radiusCm'] as const;
const recipeFields = ['cellCm','tunnelRadiusCm','chamberRadiusCm','tunnelChance','chamberChance','minYcm','maxYcm'] as const;
const budgetFields = ['perSourceBytes','perShardBytes','deviceBytes','perSourceUploadBytes','perShardUploadBytes','deviceUploadBytes'] as const;
const budgetLabels = ['Source index MiB','Shard index MiB','Device index MiB','Source upload MiB/frame','Shard upload MiB/frame','Device upload MiB/frame'];
const entranceFields = ['xCm','zCm','surfaceYcm','caveYcm','radiusCm'] as const;
const int = (s:string) => s.trim()!=='' && Number.isSafeInteger(Number(s));
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
  const [recipe,setRecipe] = useState<Record<string,string>>({});
  const [entrance,setEntrance] = useState({xCm:'0',zCm:'0',surfaceYcm:'0',caveYcm:'-500',radiusCm:'400'});
  const [entranceId,setEntranceId] = useState('');
  const [entrancePage,setEntrancePage] = useState(0);
  const [preview,setPreview] = useState<StudioCavernsPreview|null>(null);
  const [previewError,setPreviewError] = useState('');
  const [budgets,setBudgets] = useState<Record<string,string>>({});
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
    ++epoch.current;setDoc(null);setSelected('');setError('');setStatus('');setBusy(false);setPreview(null);setPreviewError('');
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
  useEffect(()=>{if(doc){setRecipe({seedText:doc.seed,...Object.fromEntries(recipeFields.map(k=>[k,String(doc.settings[k])]))});setEntranceId('');setEntrancePage(0);}},[doc]);
  const recipeValid=decimal(recipe.seedText) && recipeFields.every(k=>int(recipe[k]??'')) && Number(recipe.cellCm)>=100 && Number(recipe.cellCm)<=10000 && Number(recipe.tunnelRadiusCm)>=1 && Number(recipe.tunnelRadiusCm)<=Math.floor(Number(recipe.cellCm)/3) && Number(recipe.chamberRadiusCm)>=1 && Number(recipe.chamberRadiusCm)<=Math.floor(Number(recipe.cellCm)/2) && ['tunnelChance','chamberChance'].every(k=>Number(recipe[k])>=0&&Number(recipe[k])<=1000) && Math.abs(Number(recipe.minYcm))<=1000000000 && Math.abs(Number(recipe.maxYcm))<=1000000000 && Number(recipe.minYcm)<Number(recipe.maxYcm);
  const entranceValid=entranceFields.every(k=>int(entrance[k])) && entranceFields.slice(0,4).every(k=>Math.abs(Number(entrance[k]))<=1000000000) && Number(entrance.surfaceYcm)>Number(entrance.caveYcm) && Number(entrance.radiusCm)>=1 && Number(entrance.radiusCm)<=1000000;
  const values=Object.fromEntries(fields.map(k=>[k,Number(form[k])])) as Record<typeof fields[number],number>;
  const valid=fields.every(k=>form[k].trim()!=='' && Number.isSafeInteger(values[k])) &&
    fields.slice(0,3).every(k=>Math.abs(values[k])<=1000000000) && values.radiusCm>=1 && values.radiusCm<=1000000;
  const editable=Boolean(doc && project && snapshot?.mode==='edit' && !busy);
  const budgetBytes=Object.fromEntries(budgetFields.map(k=>[k,Number(budgets[k])*1048576])) as unknown as StudioCavernsBudgets;
  const budgetValid=budgetFields.every(k=>Number.isSafeInteger(budgetBytes[k])&&budgetBytes[k]>=4&&budgetBytes[k]<=2147483648&&budgetBytes[k]%4===0) && budgetBytes.perSourceBytes<=budgetBytes.perShardBytes && budgetBytes.perShardBytes<=budgetBytes.deviceBytes && budgetBytes.perSourceUploadBytes<=budgetBytes.perShardUploadBytes && budgetBytes.perShardUploadBytes<=budgetBytes.deviceUploadBytes;
  async function updatePreview(patch: {enabled?:boolean;isolate?:boolean;paging?:boolean;budgets?:StudioCavernsBudgets} = {}){
    if(!doc||!project||locked.current)return;
    locked.current=true;setBusy(true);setPreviewError('');
    const serial=epoch.current;
    try{
      const next=await invoke('studio.cavernsPreview',{expectedProject:project,expectedMap:map,...patch});
      if(serial===epoch.current&&currentProject.current===project){setPreview(next);if(next.budgets)setBudgets(Object.fromEntries(budgetFields.map(k=>[k,String(next.budgets[k]/1048576)])));}
    }catch(e){if(serial===epoch.current&&currentProject.current===project)setPreviewError(message(e));}
    finally{locked.current=false;if(serial===epoch.current)setBusy(false);}
  }
  async function armScene(enable:boolean){
    if(locked.current||(!editable&&enable))return;
    locked.current=true;setBusy(true);setError('');
    try{
      const b=await invoke('studio.sceneBrush',enable?{kind:'caverns',armed:true,expectedProject:project,expectedMap:map,
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
      await invoke('studio.op',{op:name,expectedProject:project,expectedMap:map,params:{...params,expectedRevision:doc.revision,expectedMapRevision:doc.mapRevision}});
      const next=await read(project,map);
      const snap=await invoke('studio.snapshot',{});
      if(serial===epoch.current && currentProject.current===project && snap.project.path===project){
        setDoc(next);setSelected('');setStatus('Map edit committed. The native Scene rebuilds asynchronously.');commitSnapshot(snap);
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
          <StudioAlert title="Scene authoring">Plain left drag paints; Alt orbits, Esc disarms. Surface picks the native SDF; depth plane uses Y. A drag is one History step. Native Scene streams nearby cave meshes.</StudioAlert>
        </StudioSection>
        <StudioSection title={'Stamps · '+doc.stamps.length}>
          {doc.stamps.slice(page*32,page*32+32).map(s=><StudioButton key={s.id} variant={selected===s.id?'primary':'secondary'} disabled={busy} onClick={()=>{setSelected(s.id);setForm({xCm:String(s.xCm),yCm:String(s.yCm),zCm:String(s.zCm),radiusCm:String(s.radiusCm)});setMode(s.mode);}}>{s.mode} #{s.id} · r {s.radiusCm} cm</StudioButton>)}
          <div className="au-row"><StudioButton disabled={page===0} onClick={()=>setPage(p=>p-1)}>Previous</StudioButton><StudioButton disabled={(page+1)*32>=doc.stamps.length} onClick={()=>setPage(p=>p+1)}>Next</StudioButton></div>
          <StudioButton variant="danger" disabled={!editable||!doc.stamps.some(s=>s.id===selected)} onClick={()=>void edit({mode:'erase',stampIdText:selected})}>Erase selected stamp</StudioButton>
        </StudioSection>
        <StudioSection title="Native Scene preview">
          <StudioButton disabled={busy} onClick={()=>void updatePreview()}>Refresh preview diagnostics</StudioButton>
          {previewError && <StudioAlert tone="warning" title="Scene unavailable">{previewError}</StudioAlert>}
          {preview && <>
            <StudioCheckbox label="Preview cave geometry" checked={preview.enabled} onChange={e=>void updatePreview({enabled:e.target.checked})}/>
            <StudioCheckbox label="Isolate underground geometry" checked={preview.isolate} onChange={e=>void updatePreview({isolate:e.target.checked})}/>
            <StudioCheckbox label="STRATA index paging (restart preview)" checked={preview.paging} onChange={e=>void updatePreview({paging:e.target.checked})}/>
            {preview.paging && <>
              {budgetFields.map((k,i)=><StudioField key={k} label={budgetLabels[i]} htmlFor={'cave-budget-'+k}><StudioInput id={'cave-budget-'+k} type="number" min={0.00390625} max={2048} step="any" value={budgets[k]??''} disabled={busy} onChange={e=>setBudgets(v=>({...v,[k]:e.target.value}))}/></StudioField>)}
              <StudioButton disabled={busy||!budgetValid} onClick={()=>void updatePreview({budgets:budgetBytes})}>Apply budgets and restart preview</StudioButton>
              <StudioAlert title="Shared device budget">All live STRATA owners must agree on the device limits. Initial root uploads are outside the incremental page upload budget.</StudioAlert>
            </>}
            {preview.error && <StudioAlert tone="danger" title="Native preview">{preview.error}</StudioAlert>}
            <StudioBadge tone={preview.visible?'accent':'neutral'}>{preview.visible?'Visual owner open':'No active preview source'}</StudioBadge>
            <div>Terrain openings: {preview.terrainCutEntrances ?? '—'} · rejected: {preview.terrainRejectedEntrances ?? '—'}</div>
            <div>Terrain triangles: {preview.terrainTriangles ?? '—'}</div>
            <div>Sparse terrain: {preview.sparseTerrainResident ?? '—'} resident / {preview.sparseTerrainAuthored ?? '—'} authored</div>
            {preview.sparseTerrainDiagnostic && <StudioAlert tone="warning" title="Sparse terrain">{preview.sparseTerrainDiagnostic}</StudioAlert>}
            {preview.terrainCutSupported===false && <StudioAlert title="Terrain source">Legacy heightfield cuts are unavailable. Explicit sparse terrain can cut shafts strictly inside one tile; arbitrary entity mesh assets are unchanged.</StudioAlert>}
            {preview.terrainCutDiagnostic && <StudioAlert tone="warning" title="Surface cut">{preview.terrainCutDiagnostic}</StudioAlert>}
            <div>Ready chunks: {preview.ready ?? '—'} / {preview.tracked ?? '—'}</div>
            <div>CPU jobs: {preview.pendingCpu ?? '—'}</div>
            <div>GPU shards: {preview.activeShards ?? '—'} active / {preview.stagedShards ?? '—'} staged</div>
            <div>Retired scenes: {preview.retiredScenes ?? '—'}</div>
            <div>Source bytes (active / staged / retired): {preview.activeSourceBytes ?? '—'} / {preview.stagedSourceBytes ?? '—'} / {preview.retiredSourceBytes ?? '—'}</div>
            <div>STRATA arena bytes (active / staged / retired): {preview.activeStrataArenaBytes ?? '—'} / {preview.stagedStrataArenaBytes ?? '—'} / {preview.retiredStrataArenaBytes ?? '—'}</div>
            <div>Page transfer bytes / deferrals: {preview.pageTransfers ?? '—'} / {preview.pageDeferrals ?? '—'}</div>
          </>}
          <StudioAlert title="Visual preview only">The same clipped heightfield supplies ordinary and PBR terrain geometry. Isolation hides terrain and the composite lighting mesh; other authored models remain. Counters describe native visual residency and estimates, not gameplay collision, NavMesh or measured driver VRAM.</StudioAlert>
        </StudioSection>
        <StudioSection title="Generator">
          <div>Revision: {doc.revision} · Generator: {doc.generator}</div>
          <StudioField label="Seed (decimal)" htmlFor="cave-seed"><StudioInput id="cave-seed" value={recipe.seedText??''} disabled={!editable} onChange={e=>setRecipe(v=>({...v,seedText:e.target.value}))}/></StudioField>
          {recipeFields.map(k=><StudioField key={k} label={k} htmlFor={'cave-recipe-'+k}><StudioInput id={'cave-recipe-'+k} type="number" step={1} value={recipe[k]??''} disabled={!editable} onChange={e=>setRecipe(v=>({...v,[k]:e.target.value}))}/></StudioField>)}
          <StudioButton variant="primary" disabled={!editable||!recipeValid} onClick={()=>void edit({mode:'settings',settings:{seedText:recipe.seedText,...Object.fromEntries(recipeFields.map(k=>[k,Number(recipe[k])]))}})}>Apply generator settings</StudioButton>
          <StudioAlert title="Regeneration">Changes the procedural base, preserves stamps and shafts, and requires regeneration of the loaded region. Undo restores the previous recipe.</StudioAlert>
        </StudioSection>
        <StudioSection title={'Entrances · '+doc.entrances.length}>
          {doc.entrances.slice(entrancePage*32,entrancePage*32+32).map(e=><StudioButton key={e.id} variant={entranceId===e.id?'primary':'secondary'} disabled={busy} onClick={()=>{setEntranceId(e.id);setEntrance({xCm:String(e.xCm),zCm:String(e.zCm),surfaceYcm:String(e.surfaceYcm),caveYcm:String(e.caveYcm),radiusCm:String(e.radiusCm)});}}>#{e.id} · ({e.xCm}, {e.zCm})</StudioButton>)}
          <div className="au-row"><StudioButton disabled={entrancePage===0} onClick={()=>setEntrancePage(p=>p-1)}>Previous</StudioButton><StudioButton disabled={(entrancePage+1)*32>=doc.entrances.length} onClick={()=>setEntrancePage(p=>p+1)}>Next</StudioButton></div>
          {entranceFields.map(k=><StudioField key={k} label={k} htmlFor={'cave-entrance-'+k}><StudioInput id={'cave-entrance-'+k} type="number" step={1} value={entrance[k]} disabled={!editable} onChange={e=>setEntrance(v=>({...v,[k]:e.target.value}))}/></StudioField>)}
          <StudioButton disabled={!editable||!entranceValid} onClick={()=>void edit({mode:'entrance_add',...Object.fromEntries(entranceFields.map(k=>[k,Number(entrance[k])]))})}>Add entrance</StudioButton>
          <StudioButton disabled={!editable||!entranceValid||!entranceId} onClick={()=>void edit({mode:'entrance_update',entranceIdText:entranceId,...Object.fromEntries(entranceFields.map(k=>[k,Number(entrance[k])]))})}>Update selected entrance</StudioButton>
          <StudioButton variant="danger" disabled={!editable||!entranceId} onClick={()=>void edit({mode:'entrance_erase',entranceIdText:entranceId})}>Remove selected entrance</StudioButton>
          <StudioAlert title="Shaft authoring">Surface Y and cave Y are explicit centimetre coordinates. Valid shafts automatically subtract openings from the legacy Studio heightfield. Fill, mismatched elevation or unsupported terrain can prevent a cut; check diagnostics. The SDF shaft mesh is approximate and gameplay collision is separate.</StudioAlert>
        </StudioSection>
      </>}
    </div>
  </StudioPanel>;
}
