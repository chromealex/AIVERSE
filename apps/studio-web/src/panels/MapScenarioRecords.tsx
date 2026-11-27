import { useEffect, useRef, useState } from 'react';
import { invoke } from '../bridge';
import { StudioButton, StudioSelect } from '../ui';
import type { StudioGraphDocumentScope, StudioSnapshot } from '../types';

type Group = 'waves' | 'triggers';
type Entry = Record<string,unknown>;
export function MapScenarioRecords({scope,busy,onApply,requested}:{requested?:{kind:'wave'|'trigger';index:number};scope:StudioGraphDocumentScope;busy:boolean;onApply:(s:StudioSnapshot)=>void}){
  const [open,setOpen]=useState(false);
  const [group,setGroup]=useState<Group>('waves');
  const [entries,setEntries]=useState<{revision:string;group:Group;rows:Entry[]}>();
  const [index,setIndex]=useState(0);
  const [draft,setDraft]=useState<{revision:string;text:string}>();
  const [pending,setPending]=useState(false);
  const [error,setError]=useState('');
  const section=useRef<HTMLDetailsElement>(null);
  const lastRequest=useRef<typeof requested>(undefined);
  useEffect(()=>{
    if(!requested || draft || lastRequest.current===requested)return;
    lastRequest.current=requested;
    setGroup(requested.kind==='wave'?'waves':'triggers');
    setIndex(requested.index);
    if(section.current){section.current.open=true;section.current.scrollIntoView({block:'nearest'});}
  },[requested,draft]);
  useEffect(()=>{
    if(!open)return;
    let current=true;
    async function load(){
      const {scenarioRecords:rows=[]}=await invoke('studio.mapProperties',{scope,scenarioGroup:group});
      if(current){
        setEntries({revision:scope.revision,group,rows});
        setIndex(i=>Math.min(i,Math.max(0,rows.length-1)));
        setError('');
      }
    }
    void load().catch(e=>{if(current)setError(String(e));});
    return ()=>{current=false;};
  },[scope.projectPath,scope.mapPath,scope.revision,scope.epoch,group,open]);
  const rows=entries?.revision===scope.revision && entries.group===group ? entries.rows : undefined;
  const selected=rows?.[index];
  async function remove(){
    if(!selected || draft || pending)return;
    setPending(true);setError('');
    try{
      onApply(await invoke('studio.mapPropertiesSet',{scope,changes:{scenarioRecord:{group,index,remove:true}}}));
    }catch(e){setError(String(e));}finally{setPending(false);}
  }
  async function apply(){
    if(!draft || draft.revision!==scope.revision || !selected)return;
    setPending(true);setError('');
    try{
      const value=JSON.parse(draft.text);
      if(!value || typeof value!=='object' || Array.isArray(value))throw Error('Enter a JSON object.');
      const snapshot=await invoke('studio.mapPropertiesSet',{scope,changes:{scenarioRecord:{group,index,value}}});
      setDraft(undefined);onApply(snapshot);
    }catch(e){setError(String(e));}finally{setPending(false);}
  }
  return <details ref={section} onToggle={e=>setOpen(e.currentTarget.open)}><summary>Waves and triggers</summary>
    <fieldset disabled={busy||pending}>
      <label>Record type<StudioSelect value={group} disabled={!!draft} onChange={e=>{setGroup(e.target.value as Group);setIndex(0);}}>
        <option value="waves">Waves</option><option value="triggers">Triggers</option></StudioSelect></label>
      {!!rows?.length && <label>Record<StudioSelect value={index} disabled={!!draft} onChange={e=>setIndex(Number(e.target.value))}>
        {rows.map((row,i)=><option key={i} value={i}>{group==='waves'?'Wave '+(i+1)+(typeof row?.time==='number'?' · '+row.time+' s':''):String(row?.id??'Trigger '+(i+1))}</option>)}
      </StudioSelect></label>}
      {(selected || draft) && <><label>Record JSON<textarea className="au-textarea" rows={8} value={draft?.text??JSON.stringify(selected,null,2)}
        onChange={e=>setDraft({revision:draft?.revision??scope.revision,text:e.target.value})}/></label>
        <StudioButton type="button" disabled={!draft || draft.revision!==scope.revision} onClick={()=>void apply()}>Apply record</StudioButton>
        {!draft && <StudioButton type="button" onClick={()=>void remove()}>Delete {group==='waves'?'wave':'trigger'}</StudioButton>}
        {draft && <StudioButton type="button" onClick={()=>setDraft(undefined)}>Discard record edits</StudioButton>}</>}
      {draft && draft.revision!==scope.revision && <p role="alert">Map changed. Discard record edits to reload.</p>}
      {rows?.length===0 && <p>No {group} on this map.</p>}
      {error && <p role="alert">{error}</p>}
    </fieldset></details>;
}
