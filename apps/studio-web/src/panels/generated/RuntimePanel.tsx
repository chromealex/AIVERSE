import { useEffect, useId, useState } from 'react';
import type { StudioSnapshot, StudioRuntimePage, StudioRuntimeEntity, StudioRuntimeOwnership } from '../../types';
import {invoke,nativeConnected} from '../../bridge';
import {StudioPanel,StudioSection,StudioBadge,StudioAlert,StudioEmptyState,StudioField,StudioInput,StudioButton,StudioListRow} from '../../ui';
import './RuntimePanel.css';

function Value({value,depth=0,follow}:{value:unknown;depth?:number;follow:(ref:string)=>void}) {
  if(value===null || typeof value!=='object')return <span>{String(value??'—')}</span>;
  if(depth>10)return <span>Nested data exceeds display depth</span>;
  const record=value as Record<string,unknown>;
  const exact=typeof record.$entityRef==='string'&&/^[1-9][0-9]{0,19}$/.test(record.$entityRef)?record.$entityRef:
    typeof record.$entity==='number'&&Number.isSafeInteger(record.$entity)&&record.$entity>0?String(record.$entity):null;
  if(exact)return <StudioButton onClick={()=>follow(exact)}>{String(record.label||('Entity '+exact))}</StudioButton>;
  if('$entity' in record||'$entityRef' in record)return <span>Reference unavailable (invalid or unsafe precision)</span>;
  const entries=Object.entries(value);
  if(!entries.length)return <span>{Array.isArray(value)?'[]':'{}'}</span>;
  return <dl className="studio-runtime__fields">{entries.slice(0,200).map(([key,item])=><div key={key}>
    <dt>{key}</dt><dd>{item && typeof item==='object'
      ?<details><summary>{Array.isArray(item)?`${item.length} items`:'Fields'}</summary><Value value={item} depth={depth+1} follow={follow}/></details>
      :<Value value={item} depth={depth+1} follow={follow}/>}</dd>
  </div>)}{entries.length>200 && <div><dt>More</dt><dd>{entries.length-200} additional fields</dd></div>}</dl>;
}

type Selection={epoch:string;ref:string};
function Ownership({value}:{value:StudioRuntimeOwnership|undefined}){
  if(!value)return <StudioEmptyState title="Ownership diagnostics unavailable in this native build"/>;
  return <dl className="studio-runtime__fields">
    <div><dt>Stable ID</dt><dd>{value.stableId??'Not assigned'}</dd></div>
    <div><dt>Ledger status</dt><dd>{value.state}</dd></div>
    <div><dt>Ownership cell XYZ</dt><dd>{value.cell?.join(', ')??'—'}</dd></div>
    <div><dt>Ledger revision</dt><dd>{value.revision??'—'}</dd></div>
    <div><dt>Dormant marker</dt><dd>{value.dormantMarker?'Present':'Absent'}</dd></div>
  </dl>;
}
export default function RuntimePanel({snapshot}:{snapshot:StudioSnapshot|null}){
  const projectPath=snapshot?.project.path??'';
  return <RuntimeBody key={projectPath} projectPath={projectPath}/>;
}
function RuntimeBody({projectPath}:{projectPath:string}) {
  const uid=useId();
  const [page,setPage]=useState<StudioRuntimePage|null>(null);
  const [selected,setSelected]=useState<Selection|null>(null);
  const [back,setBack]=useState<Selection[]>([]),[componentQuery,setComponentQuery]=useState('');
  const [detail,setDetail]=useState<StudioRuntimeEntity|null>(null);
  const [error,setError]=useState('');
  const [query,setQuery]=useState(''),[offset,setOffset]=useState(0);
  const connected=nativeConnected();
  useEffect(()=>{
    let live=true,inFlight=false;
    const refresh=async()=>{
      if(!connected || !projectPath || inFlight)return;inFlight=true;
      try {
        const next=await invoke('studio.gameEntities',{offset,limit:50,query,expectedProject:projectPath});
        if(!live)return;setPage(next);
        setBack(old=>next.active?old.filter(item=>item.epoch===next.epoch):[]);
        if(selected && next.active && selected.epoch===next.epoch) {
          try{const value=await invoke('studio.gameInspect',{...selected,expectedProject:projectPath});if(live){setDetail(value);setError('');}}
          catch(e){if(live){setError(String(e));setSelected(null);setDetail(null);}}
        }else {setSelected(null);setDetail(null);if(!selected)setError('');}
      }catch(e){if(live){setError(String(e));setPage(null);setDetail(null);}}
      finally{inFlight=false;}
    };
    void refresh();const timer=window.setInterval(()=>void refresh(),750);
    return ()=>{live=false;window.clearInterval(timer);};
  },[connected,offset,query,selected?.epoch,selected?.ref,projectPath]);
  const choose=(ref:string)=>{
    if(!page?.active||selected?.ref===ref&&selected.epoch===page.epoch)return;
    if(selected?.epoch===page.epoch)setBack(old=>[...old,selected].slice(-32));
    setSelected({epoch:page.epoch,ref});setDetail(null);setComponentQuery('');setError('');
  };
  const goBack=()=>{
    const prior=back[back.length-1];
    if(prior&&prior.epoch===page?.epoch){
      setBack(old=>old.slice(0,-1));setSelected(prior);setDetail(null);setComponentQuery('');setError('');
    }
  };
  const components=Object.entries(detail?.components??{}).filter(([name])=>name.toLowerCase().includes(componentQuery.toLowerCase()));
  const selectedRow=page?.rows.find(row=>row.ref===selected?.ref);
  return <StudioPanel title="Runtime" className="studio-panel-runtime" subtitle="Entity Debugger · live read-only"
    actions={<StudioBadge>{page?.active?`Tick ${page.tick}`:'Stopped'}</StudioBadge>}>
    <div className="au-stack">
      {error && <StudioAlert tone="danger">{error}</StudioAlert>}
      {!projectPath?<StudioEmptyState title="Open a project"/>:!connected?<StudioEmptyState title="Native Studio required"/>:!page?.active?<StudioEmptyState title={page?"No running match":"No runtime snapshot"} description="Start a local game in Game to inspect its live entities and components."/>:<>
        <StudioField label="Search live entities" htmlFor={uid+"-search"} hint="t:type, p:owner, c:component, #index, ref:runtime-ref, sid:stable-id, cell:x,y,z (ownership)">
          <StudioInput id={uid+"-search"} type="search" value={query} maxLength={256} onChange={e=>{setQuery(e.target.value);setOffset(0);}}/>
        </StudioField>
        <div className="studio-runtime__list" role="list" aria-label="Live entities">
          {page.rows.map(row=><StudioListRow key={row.ref} label={row.name} meta={`${row.kind} · player ${row.owner} · ${row.components.length} catalogued components${row.ownership?.stableId?" · SID "+row.ownership.stableId:""}` }
            selected={selected?.ref===row.ref && selected.epoch===page.epoch} onClick={()=>choose(row.ref)}/>)}
        </div>
        {!page.rows.length && <StudioEmptyState title="No matching entities" description="Change the filter or return to an earlier page. Only entities in the runtime model are searched; absent stable IDs are not classified here."/>}
        <div className="studio-runtime__paging"><StudioButton disabled={offset===0} onClick={()=>setOffset(Math.max(0,offset-50))}>Previous</StudioButton>
          <span>{page.total?offset+1:0}–{Math.min(offset+50,page.total)} / {page.total}</span>
          <StudioButton disabled={offset+50>=page.total} onClick={()=>setOffset(offset+50)}>Next</StudioButton></div>
        <StudioButton disabled={!back.length||back[back.length-1].epoch!==page.epoch} onClick={goBack}>Back to previous entity</StudioButton>
        {selected && !detail && <StudioBadge>Reading entity…</StudioBadge>}
        {detail && <>
          <StudioSection title={`Runtime ref ${detail.ref}`} actions={<StudioBadge>Tick {detail.tick}</StudioBadge>}>
            <small>Session epoch {detail.epoch}. Runtime references are not persistent IDs.</small>
            <Ownership value={detail.ownership}/>
            {selectedRow?.parent&&<StudioButton onClick={()=>choose(selectedRow.parent)}>Inspect parent</StudioButton>}
            {detail.ownership?.stableId&&<StudioButton onClick={()=>{setQuery('sid:'+detail.ownership!.stableId);setOffset(0)}}>Find by stable ID</StudioButton>}
            {detail.ownership?.cell&&detail.ownership.state==='live-record'&&<StudioButton onClick={()=>{setQuery('cell:'+detail.ownership!.cell!.join(','));setOffset(0)}}>Find entities in ownership cell</StudioButton>}
          </StudioSection>
          <StudioSection title={`Components (${components.length}/${Object.keys(detail.components).length})`}>
            <StudioField label="Filter inspected components" htmlFor={uid+"-component"}>
              <StudioInput id={uid+"-component"} value={componentQuery} type="search" onChange={e=>setComponentQuery(e.target.value)}/>
            </StudioField>
            {!components.length&&<StudioEmptyState title="No matching inspected components"/>}
            {components.map(([name,value])=><details key={detail.epoch+':'+detail.ref+':'+name} className="studio-runtime__component" open>
              <summary>{name}</summary><Value value={value} follow={choose}/>
              {detail.componentSchemas?.[name]!==undefined&&<details><summary>Native schema</summary><Value value={detail.componentSchemas[name]} follow={choose}/></details>}
            </details>)}
          </StudioSection>
          {detail.stats && <StudioSection title="Effective stats"><Value value={detail.stats} follow={choose}/></StudioSection>}
        </>}
      </>}
    </div>
  </StudioPanel>;
}
