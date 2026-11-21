import { useEffect, useState } from 'react';
import type { StudioSnapshot, StudioRuntimePage, StudioRuntimeEntity } from '../../types';
import {invoke,nativeConnected} from '../../bridge';
import {StudioPanel,StudioSection,StudioBadge,StudioAlert,StudioEmptyState,StudioField,StudioInput,StudioButton,StudioListRow} from '../../ui';
import './RuntimePanel.css';

function Value({value,depth=0,follow}:{value:unknown;depth?:number;follow:(ref:string)=>void}) {
  if(value===null || typeof value!=='object')return <span>{String(value??'—')}</span>;
  if(depth>10)return <span>Nested data exceeds display depth</span>;
  const record=value as Record<string,unknown>;
  if(typeof record.$entity==='number' && Number.isSafeInteger(record.$entity) && record.$entity>0)
    return <StudioButton onClick={()=>follow(String(record.$entity))}>{String(record.label||`Entity ${record.$entity}`)}</StudioButton>;
  const entries=Object.entries(value);
  if(!entries.length)return <span>{Array.isArray(value)?'[]':'{}'}</span>;
  return <dl className="studio-runtime__fields">{entries.slice(0,200).map(([key,item])=><div key={key}>
    <dt>{key}</dt><dd>{item && typeof item==='object'
      ?<details><summary>{Array.isArray(item)?`${item.length} items`:'Fields'}</summary><Value value={item} depth={depth+1} follow={follow}/></details>
      :<Value value={item} depth={depth+1} follow={follow}/>}</dd>
  </div>)}{entries.length>200 && <div><dt>More</dt><dd>{entries.length-200} additional fields</dd></div>}</dl>;
}

export default function RuntimePanel({snapshot}:{snapshot:StudioSnapshot|null}) {
  const [page,setPage]=useState<StudioRuntimePage|null>(null);
  const [selected,setSelected]=useState<{epoch:string;ref:string}|null>(null);
  const [detail,setDetail]=useState<StudioRuntimeEntity|null>(null);
  const [error,setError]=useState('');
  const [query,setQuery]=useState(''),[offset,setOffset]=useState(0);
  const connected=nativeConnected();
  useEffect(()=>{
    let live=true,inFlight=false;
    const refresh=async()=>{
      if(!connected || inFlight)return;inFlight=true;
      try {
        const next=await invoke('studio.gameEntities',{offset,limit:50,query});
        if(!live)return;setPage(next);setError('');
        if(selected && next.active && selected.epoch===next.epoch) {
          try{const value=await invoke('studio.gameInspect',selected);if(live)setDetail(value);}
          catch(e){if(live){setError(String(e));setSelected(null);setDetail(null);}}
        }else {setSelected(null);setDetail(null);}
      }catch(e){if(live)setError(String(e));}
      finally{inFlight=false;}
    };
    void refresh();const timer=window.setInterval(()=>void refresh(),750);
    return ()=>{live=false;window.clearInterval(timer);};
  },[connected,offset,query,selected?.epoch,selected?.ref,snapshot?.project.path]);
  const choose=(ref:string)=>{if(page){setSelected({epoch:page.epoch,ref});setDetail(null);}};
  return <StudioPanel title="Runtime" className="studio-panel-runtime" subtitle="Live entities · read only"
    actions={<StudioBadge>{page?.active?`Tick ${page.tick}`:'Stopped'}</StudioBadge>}>
    <div className="au-stack">
      {error && <StudioAlert tone="danger">{error}</StudioAlert>}
      {!page?.active?<StudioEmptyState title="No running match" description="Start a local game in Game to inspect its live entities and components."/>:<>
        <StudioField label="Search live entities" htmlFor="runtime-search" hint="Search name or use t:type, p:owner, c:component, #id">
          <StudioInput id="runtime-search" type="search" value={query} maxLength={256} onChange={e=>{setQuery(e.target.value);setOffset(0);}}/>
        </StudioField>
        <div className="studio-runtime__list" role="list" aria-label="Live entities">
          {page.rows.map(row=><StudioListRow key={row.ref} label={row.name} meta={`${row.kind} · player ${row.owner} · ${row.components.length} components`}
            selected={selected?.ref===row.ref && selected.epoch===page.epoch} onClick={()=>choose(row.ref)}/>)}
        </div>
        {!page.rows.length && <StudioEmptyState title="No matching entities" description="Change the filter or return to an earlier page."/>}
        <div className="studio-runtime__paging"><StudioButton disabled={offset===0} onClick={()=>setOffset(Math.max(0,offset-50))}>Previous</StudioButton>
          <span>{page.total?offset+1:0}–{Math.min(offset+50,page.total)} / {page.total}</span>
          <StudioButton disabled={offset+50>=page.total} onClick={()=>setOffset(offset+50)}>Next</StudioButton></div>
        {selected && !detail && <StudioBadge>Reading entity…</StudioBadge>}
        {detail && <>
          <StudioSection title={`Entity ${detail.ref}`} actions={<StudioBadge>Tick {detail.tick}</StudioBadge>}>
            {Object.entries(detail.components).map(([name,value])=><details key={name} className="studio-runtime__component" open>
              <summary>{name}</summary><Value value={value} follow={choose}/>
            </details>)}
          </StudioSection>
          {detail.stats && <StudioSection title="Effective stats"><Value value={detail.stats} follow={choose}/></StudioSection>}
        </>}
      </>}
    </div>
  </StudioPanel>;
}
