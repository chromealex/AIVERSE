import {useMemo,useState} from 'react';
import {StudioAlert,StudioButton,StudioSection,StudioSelect,StudioTextarea} from '../ui';
import GraphCanvas from './GraphCanvas';
import {caveEntryId,inspectCaveReply,projectCavePage} from './caveGraphInspection';
import type {CaveFilter,CaveInspection} from './caveGraphInspection';

/** The future live owner passes an already checked immutable get response.
 * Remount on project/map/request identity changes; never label a draft as live. */
export function CaveStructureGraphView({data}:{data:CaveInspection}){
 const [filter,setFilter]=useState<CaveFilter>('all'),[page,setPage]=useState(0),[selected,setSelected]=useState('');
 const projection=useMemo(()=>projectCavePage(data,filter,page),[data,filter,page]);
 const active=projection.view.nodes.some(n=>n.id===selected)?selected:'';
 const entry=projection.visible.find(e=>caveEntryId(e)===active);
 const c=data.settings;
 const go=(next:number)=>{setPage(next);setSelected('');};
 return <StudioSection title="Caverns authored SDF graph">
  <small>Archive v1 · generator {data.generatorVersion} · revision {data.revision} · {data.stamps.length} stamps · {data.entrances.length} entrances</small>
  <div className="au-row" style={{flexWrap:'wrap'}}>
   <StudioSelect aria-label="Caverns entry filter" value={filter} onChange={e=>{const v=e.target.value;setFilter(v==='carve'||v==='fill'||v==='entrance'?v:'all');go(0)}}>
    <option value="all">All entries</option><option value="carve">Carve</option><option value="fill">Fill</option><option value="entrance">Entrances</option>
   </StudioSelect>
   <StudioButton size="sm" disabled={page===0} onClick={()=>go(page-1)}>Previous</StudioButton>
   <StudioButton size="sm" disabled={page+1>=projection.pages} onClick={()=>go(page+1)}>Next</StudioButton>
   <small>Page {page+1}/{projection.pages} · {projection.visible.length} of {projection.total} matching entries</small>
  </div>
  <div style={{height:340,display:'flex',flexDirection:'column',minWidth:0}}>
   <GraphCanvas view={projection.view} label="Caverns authored volume dependencies" readOnly selected={active} onSelect={setSelected} onMove={()=>{}}/>
  </div>
  {active==='settings'&&<div className="au-stack">
   <small>Seed {c.seed} · lattice {c.cellCm} cm</small><small>Tunnel radius {c.tunnelRadiusCm} cm · chance {c.tunnelChance}‰</small>
   <small>Chamber radius {c.chamberRadiusCm} cm · chance {c.chamberChance}‰</small><small>Depth band [{c.minYcm}, {c.maxYcm}) cm</small>
  </div>}
  {entry&&<div className="au-stack">
   <strong>{entry.kind==='stamp'?entry.mode:'Entrance'} #{entry.id}</strong>
   <small>Radius {entry.radiusCm} cm</small>
   {entry.kind==='stamp'?<small>Center: {entry.xCm}, {entry.yCm}, {entry.zCm} cm</small>:<small>XZ: {entry.xCm}, {entry.zCm} cm · surface Y {entry.surfaceYcm} cm · cave Y {entry.caveYcm} cm</small>}
  </div>}
  <StudioAlert>Authored input provenance only. Fill wins over overlapping carve in the native field. Edges do not represent connected caves, walkable paths, live chunks, mesh or collision.</StudioAlert>
 </StudioSection>;
}
export function CaveInspectionImport(){
 const [raw,setRaw]=useState(''),[data,setData]=useState<CaveInspection|null>(null),[error,setError]=useState('');
 const load=()=>{
  setData(null);setError('');
  try{
   if(raw.length>4194304)throw new Error('Report exceeds 4194304-character import limit');
   setData(inspectCaveReply(JSON.parse(raw) as unknown));
  }catch(e){setError(e instanceof Error?e.message:String(e));}
 };
 return <StudioSection title="Imported Caverns inspection">
  <small>Paste the complete native terraweave_caverns_paint result for mode=get. Imported data is unverified, not live. No project data is changed.</small>
  <StudioTextarea aria-label="Caverns get response JSON" placeholder="Caverns get response JSON" rows={5} value={raw} onChange={e=>{setRaw(e.target.value);setData(null);setError('')}}/>
  <StudioButton size="sm" disabled={!raw.trim()} onClick={load}>Inspect supplied data</StudioButton>
  {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
  {data&&<CaveStructureGraphView key={data.revision} data={data}/>}
 </StudioSection>;
}
