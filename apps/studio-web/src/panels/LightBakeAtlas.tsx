import { useEffect, useState } from 'react';
import { invoke } from '../bridge';
import { StudioAlert, StudioButton, StudioField, StudioSelect } from '../ui';

export interface AtlasPage { width:number; height:number; mask:boolean }
export function LightBakeAtlas({pages,hash,epoch}:{pages:AtlasPage[];hash:string;epoch:string}) {
  const [open,setOpen]=useState(false);
  const [page,setPage]=useState(0);
  const [channel,setChannel]=useState<'lightmap'|'mask'>('lightmap');
  const [image,setImage]=useState('');
  const [error,setError]=useState('');
  const [busy,setBusy]=useState(false);
  const [retry,setRetry]=useState(0);
  useEffect(()=>{
    if(!open)return;
    let current=true;
    setImage('');setError('');setBusy(true);
    void invoke('studio.lightBakeAtlas',{expectedDocumentEpoch:epoch,hash,page,channel})
      .then(result=>{if(current)setImage('data:image/png;base64,'+result.png);})
      .catch(reason=>{if(current)setError(String(reason));})
      .finally(()=>{if(current)setBusy(false);});
    return ()=>{current=false;};
  },[open,hash,epoch,page,channel,retry]);
  return <details className="bake-atlas" onToggle={e=>setOpen(e.currentTarget.open)}>
    <summary>Atlas preview</summary>
    {open && <>
      <StudioField label="Atlas page" htmlFor="bake-page"><StudioSelect id="bake-page" value={page} disabled={busy}
        onChange={e=>{const n=Number(e.target.value);setPage(n);if(!pages[n].mask)setChannel('lightmap');}}>
        {pages.map((p,i)=><option key={i} value={i}>Page {i+1} · {p.width} × {p.height}</option>)}
      </StudioSelect></StudioField>
      <StudioField label="Channel" htmlFor="bake-channel"><StudioSelect id="bake-channel" value={channel} disabled={busy}
        onChange={e=>setChannel(e.target.value==='mask'?'mask':'lightmap')}>
        <option value="lightmap">Baked irradiance</option>
        {pages[page]?.mask && <option value="mask">Sun visibility</option>}
      </StudioSelect></StudioField>
      {busy && <p role="status">Loading atlas page…</p>}
      {error && <StudioAlert tone="danger">{error} <StudioButton onClick={()=>setRetry(n=>n+1)}>Retry</StudioButton></StudioAlert>}
      {image && <img alt={(channel==='mask'?'Sun visibility':'Baked irradiance')+' · page '+(page+1)} src={image}/>}
    </>}
  </details>;
}
