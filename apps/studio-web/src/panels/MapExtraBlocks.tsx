import { useEffect, useRef, useState } from 'react';
import { invoke } from '../bridge';
import type { StudioGraphDocumentScope, StudioSnapshot } from '../types';
import { StudioButton, StudioSelect } from '../ui';

export function MapExtraBlocks({keys,scope,busy,onApply,requested}:{requested?:{key:string;index?:number};keys:string[];scope:StudioGraphDocumentScope;busy:boolean;onApply:(s:StudioSnapshot)=>void}){
  const [open,setOpen]=useState(false);
  const [selection,setSelection]=useState('');
  const [index,setIndex]=useState<number>();
  const [loaded,setLoaded]=useState<{key:string;revision:string;text:string;index?:number;entries:string[]}>();
  const [draft,setDraft]=useState<typeof loaded>();
  const [pending,setPending]=useState(false);
  const [error,setError]=useState('');
  const section=useRef<HTMLDetailsElement>(null);
  const lastRequest=useRef<typeof requested>(undefined);
  useEffect(()=>{
    if(!requested || lastRequest.current===requested || draft || !keys.includes(requested.key))return;
    lastRequest.current=requested;
    setSelection(requested.key);setIndex(requested.index);
    if(section.current){section.current.open=true;section.current.scrollIntoView({block:'nearest'});}
  },[requested,keys,draft]);
  const key=draft?.key ?? (keys.includes(selection)?selection:keys[0]);
  useEffect(()=>{
    if(!key || !open)return;
    let active=true;
    invoke('studio.mapProperties',{scope,blockKey:key,blockIndex:index}).then(data=>{
      if(active){
        setLoaded({key,revision:scope.revision,text:data.blockText??'',index:data.blockIndex,entries:data.blockEntries??[]});
        setIndex(data.blockIndex);setError('');
      }
    }).catch(e=>{if(active)setError(String(e));});
    return ()=>{active=false;};
  },[open,key,index,scope.epoch,scope.revision,scope.projectPath,scope.mapPath]);
  const current=loaded?.key===key && loaded?.revision===scope.revision && loaded?.index===index ? loaded : undefined;
  async function apply(remove:boolean){
    if(!key || !current || (draft && draft.revision!==scope.revision))return;
    setPending(true);setError('');
    try{
      const extraBlock=remove ? {key,index,remove:true as const} : {key,index,text:draft!.text};
      onApply(await invoke('studio.mapPropertiesSet',{scope,changes:{extraBlock}}));
      if(remove)setIndex(undefined);
      setDraft(undefined);
    }catch(e){setError(String(e));}finally{setPending(false);}
  }
  if(!keys.length && !draft)return null;
  return <details ref={section} onToggle={e=>setOpen(e.currentTarget.open)}><summary>Additional map blocks</summary>
    <fieldset disabled={busy||pending}>
      <label>Block<StudioSelect disabled={!!draft} value={key} onChange={e=>{setSelection(e.target.value);setIndex(undefined);}}>
        {keys.map(k=><option key={k}>{k}</option>)}</StudioSelect></label>
      {!!loaded?.entries.length && loaded.key===key && <label>Entry<StudioSelect value={index??-1} disabled={!!draft}
        onChange={e=>setIndex(e.target.value==='-1'?undefined:Number(e.target.value))}>
        <option value={-1}>Entire block</option>{loaded.entries.map((name,i)=><option key={i} value={i}>{i+1} · {name}</option>)}
      </StudioSelect></label>}
      {(current||draft) && <label>{index===undefined?'Block JSON':'Entry JSON'}<textarea className="au-textarea" rows={9} value={(draft??current)!.text}
        onChange={e=>setDraft({...((draft??current)!),text:e.target.value})}/></label>}
      {draft ? <><StudioButton type="button" disabled={!current || draft.revision!==scope.revision} onClick={()=>void apply(false)}>{index===undefined?'Apply block':'Apply entry'}</StudioButton>
        <StudioButton type="button" onClick={()=>setDraft(undefined)}>Discard edits</StudioButton></> :
        <StudioButton type="button" disabled={!current} onClick={()=>void apply(true)}>{index===undefined?'Delete block':'Delete entry'}</StudioButton>}
      {draft && draft.revision!==scope.revision && <p role="alert">Map changed. Discard edits to reload this block.</p>}
      {error && <p role="alert">{error}</p>}
    </fieldset></details>;
}
