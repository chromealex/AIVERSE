import { useEffect, useId, useRef, useState } from 'react';
import { RefreshCw } from 'lucide-react';
import type { StudioSnapshot, StudioLiveWorldRegions, StudioWorldRegion } from '../../types';
import { invoke, nativeConnected } from '../../bridge';
import {
  StudioPanel, StudioSection, StudioIconButton, StudioBadge, StudioAlert,
  StudioEmptyState, StudioField, StudioSelect, StudioListRow, StudioProgress
} from '../../ui';
import './WorldExplorerPanel.css';

const cellKey=(r:StudioWorldRegion)=>r.cell.join(',');
type Available=Extract<StudioLiveWorldRegions,{available:true}>;

function RegionSlice({data}:{data:Available}){
  const uid=useId();
  const [layer,setLayer]=useState<string>(''),[selection,setSelection]=useState<string>('');
  const layers=[...new Set(data.regions.map(r=>r.cell[1]))].sort((a,b)=>a-b);
  const y=layers.find(v=>String(v)===layer)??layers[0];
  const rows=data.regions.filter(r=>r.cell[1]===y);
  const selected=rows.find(r=>cellKey(r)===selection);
  if(!rows.length)return <StudioEmptyState title="No occupied cells in the returned snapshot"/>;
  const minX=Math.min(...rows.map(r=>r.cell[0])),maxX=Math.max(...rows.map(r=>r.cell[0]));
  const minZ=Math.min(...rows.map(r=>r.cell[2])),maxZ=Math.max(...rows.map(r=>r.cell[2]));
  const scale=180/Math.max(1,maxX-minX+1,maxZ-minZ+1);
  return <>
    <StudioField label="Y layer" htmlFor={uid}>
      <StudioSelect id={uid} value={String(y)} onChange={e=>{setLayer(e.target.value);setSelection('')}}>
        {layers.map(v=><option key={v} value={v}>{v}</option>)}
      </StudioSelect>
    </StudioField>
    <svg className="world-explorer-map" viewBox="0 0 200 200" role="img" aria-label="Occupied ownership cells, X right and Z up, selected Y layer">
      {rows.map(r=><rect key={cellKey(r)} x={10+(r.cell[0]-minX)*scale} y={10+(maxZ-r.cell[2])*scale}
        width={scale*.9} height={scale*.9} className={cellKey(r)===selection?'world-explorer-cell is-selected':'world-explorer-cell'}
        opacity={cellKey(r)===selection?1:.25+.75*r.materialized/Math.max(1,r.live)}
        onClick={()=>setSelection(cellKey(r))}>
        <title>{cellKey(r)} · {r.materialized}/{r.live} materialized</title>
      </rect>)}
    </svg>
    <small>Brightness = materialized share; cells are not collision or navigation tiles. Select a row for keyboard access.</small>
    <div className="world-explorer-rows" aria-label="Ownership cells in this layer">
      {rows.map(r=><StudioListRow key={cellKey(r)} label={cellKey(r)}
        meta={r.materialized+'/'+r.live+' materialized'} selected={cellKey(r)===selection}
        onClick={()=>setSelection(cellKey(r))}/>)}
    </div>
    {selected&&<StudioSection title={'Cell '+cellKey(selected)}>
      <div>Live identities: {selected.live}</div><div>Materialized: {selected.materialized}</div>
      <div>Unmaterialized: {selected.live-selected.materialized}</div>
    </StudioSection>}
  </>;
}

function LiveExplorer({projectPath}:{projectPath:string}){
  const [data,setData]=useState<StudioLiveWorldRegions|null>(null);
  const [pending,setPending]=useState(false),[error,setError]=useState('');
  const request=useRef(0),lock=useRef(false);
  useEffect(()=>()=>{++request.current;lock.current=false;},[]);
  const connected=nativeConnected();
  async function refresh(){
    if(!connected||!projectPath||lock.current)return;
    lock.current=true;const id=++request.current;
    setPending(true);setError('');setData(null);
    try{
      const next=await invoke('studio.gameRegions',{expectedProject:projectPath});
      if(id===request.current)setData(next);
    }catch(e){if(id===request.current)setError(e instanceof Error?e.message:String(e));}
    finally{if(id===request.current){lock.current=false;setPending(false);}}
  }
  return <StudioPanel title="World Explorer" eyebrow="WORLD / DIAGNOSTICS" subtitle="Local Game · read-only snapshot"
    actions={<StudioIconButton aria-label="Capture current region snapshot" icon={<RefreshCw size={15}/>}
      disabled={!projectPath||!connected||pending} onClick={()=>void refresh()}/>}
    footer={<StudioBadge>{pending?'Reading…':data?.available?'Snapshot tick '+data.tick:'No snapshot'}</StudioBadge>}>
    <div className="au-stack world-explorer">
      {!projectPath?<StudioEmptyState title="Open a project"/>:!connected?<StudioEmptyState title="Native Studio required"/>:
        <StudioAlert>Start a local match, then refresh. This is a point-in-time ownership snapshot, not a streaming or disk-save report. Refresh explicitly after play state changes.</StudioAlert>}
      {pending&&<StudioBadge>Reading actual ECS ledger…</StudioBadge>}
      {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
      {data&&!data.available&&<StudioEmptyState title={data.reason}/>}
      {data?.available&&<>
        <StudioSection title="Ownership">
          <dl className="world-explorer-stats">
            {([['Tick',data.tick],['Epoch',data.epoch],['Revision',data.revision],['Regions',data.regionCount],
              ['Live identities',data.live],['Materialized',data.materialized],['Tombstones',data.tombstones],
              ['Ledger bytes',data.ledgerArchiveBytes]] as const).map(([k,v])=><div key={k}><dt>{k}</dt><dd>{v}</dd></div>)}
          </dl>
          <StudioProgress label="Ledger record capacity" value={data.live+data.tombstones} max={data.capacity}/>
        </StudioSection>
        <StudioSection title="Region map">
          {data.regionsTruncated&&<StudioAlert tone="warning">Native result limited to {data.regions.length} of {data.regionCount} occupied regions. Missing cells are unknown, not empty.</StudioAlert>}
          <RegionSlice key={data.epoch+':'+data.tick+':'+data.revision} data={data}/>
        </StudioSection>
        <StudioSection title="Decision cadence">
          {data.simulationLod.available?<dl className="world-explorer-stats">
            {([['Sample tick',data.simulationLod.tick],['Hot',data.simulationLod.hot],['Warm',data.simulationLod.warm],
              ['Cold',data.simulationLod.cold],['Executed',data.simulationLod.executed],['Deferred',data.simulationLod.deferred]] as const)
              .map(([k,v])=><div key={k}><dt>{k}</dt><dd>{v}</dd></div>)}
          </dl>:<StudioEmptyState title="No Simulation LOD diagnostics in this match"/>}
          <small>Last reported decision counts; not CPU timings or per-region LOD classification.</small>
        </StudioSection>
      </>}
    </div>
  </StudioPanel>;
}
export default function WorldExplorerPanel({snapshot}:{snapshot:StudioSnapshot|null}){
  const projectPath=snapshot?.project.path??'';
  return <LiveExplorer key={projectPath} projectPath={projectPath}/>;
}
