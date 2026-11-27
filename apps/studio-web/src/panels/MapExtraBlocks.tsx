import { useEffect, useState } from 'react';
import { invoke } from '../bridge';
import type { StudioGraphDocumentScope, StudioSnapshot } from '../types';
import { StudioButton, StudioSelect } from '../ui';

export function MapExtraBlocks({keys,scope,busy,onApply}:{keys:string[];scope:StudioGraphDocumentScope;busy:boolean;onApply:(s:StudioSnapshot)=>void}){
  const [open,setOpen]=useState(false);
  const [selection,setSelection]=useState('');
  const [loaded,setLoaded]=useState<{key:string;revision:string;text:string}>();
  const [draft,setDraft]=useState<typeof loaded>();
  const [pending,setPending]=useState(false);
  const [error,setError]=useState('');
  const key=draft?.key ?? (keys.includes(selection)?selection:keys[0]);
  useEffect(()=>{
    if(!key || !open)return;
    let active=true;
    invoke('studio.mapProperties',{scope,blockKey:key}).then(data=>{
      if(active){setLoaded({key,revision:scope.revision,text:data.blockText??''});setError('');}
    }).catch(e=>{if(active)setError(String(e));});
    return ()=>{active=false;};
  },[open,key,scope.epoch,scope.revision,scope.projectPath,scope.mapPath]);
  const current=loaded?.key===key && loaded?.revision===scope.revision ? loaded : undefined;
  async function apply(remove:boolean){
    if(!key || !current || (draft && draft.revision!==scope.revision))return;
    setPending(true);setError('');
    try{
      const extraBlock=remove ? {key,remove:true as const} : {key,text:draft!.text};
      onApply(await invoke('studio.mapPropertiesSet',{scope,changes:{extraBlock}}));
      setDraft(undefined);
    }catch(e){setError(String(e));}finally{setPending(false);}
  }
  if(!keys.length && !draft)return null;
  return <details onToggle={e=>setOpen(e.currentTarget.open)}><summary>Additional map blocks</summary>
    <fieldset disabled={busy||pending}>
      <label>Block<StudioSelect disabled={!!draft} value={key} onChange={e=>setSelection(e.target.value)}>
        {keys.map(k=><option key={k}>{k}</option>)}</StudioSelect></label>
      {(current||draft) && <label>Block JSON<textarea className="au-textarea" rows={9} value={(draft??current)!.text}
        onChange={e=>setDraft({...((draft??current)!),text:e.target.value})}/></label>}
      {draft ? <><StudioButton type="button" disabled={!current || draft.revision!==scope.revision} onClick={()=>void apply(false)}>Apply block</StudioButton>
        <StudioButton type="button" onClick={()=>setDraft(undefined)}>Discard edits</StudioButton></> :
        <StudioButton type="button" disabled={!current} onClick={()=>void apply(true)}>Delete block</StudioButton>}
      {draft && draft.revision!==scope.revision && <p role="alert">Map changed. Discard edits to reload this block.</p>}
      {error && <p role="alert">{error}</p>}
    </fieldset></details>;
}
