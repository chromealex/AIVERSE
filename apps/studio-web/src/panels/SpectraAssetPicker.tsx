import { useMemo, useState } from 'react';
import { StudioButton, StudioInput } from '../ui';
import type { SpectraAssetReference } from './spectraAssetBinding';

/** Reusable VFX graph asset selector. Assets must be supplied by the Studio
 * GUID index; this component does not invent GUIDs or query a filesystem.
 */
export interface SpectraIndexedAsset extends SpectraAssetReference {
  name:string;
  kind:string;
}
interface Props {
  assets:readonly SpectraIndexedAsset[];
  value:SpectraAssetReference|null;
  onChange:(asset:SpectraAssetReference)=>void;
  locked?:boolean;
  onLockChange?:(locked:boolean)=>void;
  disabled?:boolean;
  kinds?:readonly string[];
}
const matches=(a:SpectraIndexedAsset,query:string):boolean=>{
  const needle=query.trim().toLocaleLowerCase();
  return !needle||[a.name,a.path??'',a.guid,a.kind]
    .some(text=>text.toLocaleLowerCase().includes(needle));
};
const isAsset=(v:unknown):v is SpectraAssetReference=>{
  if(!v||typeof v!=='object')return false;
  const x=v as Record<string,unknown>;
  return typeof x.guid==='string'&&
    (/^[a-fA-F0-9]{32}$/.test(x.guid)||
     /^[a-fA-F0-9]{8}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{12}$/.test(x.guid));
};
export default function SpectraAssetPicker({
  assets,value,onChange,locked=false,onLockChange,disabled=false,kinds,
}:Props){
  const [query,setQuery]=useState('');
  const [open,setOpen]=useState(false);
  const [error,setError]=useState('');
  const filtered=useMemo(()=>assets.filter(a=>
    (!kinds||kinds.includes(a.kind))&&matches(a,query)).slice(0,200),
    [assets,kinds,query]);
  const choose=(asset:SpectraIndexedAsset)=>{
    if(disabled)return;
    onChange({guid:asset.guid,...(asset.path?{path:asset.path}:{})});
    setError('');setOpen(false);
  };
  const drop=(event:React.DragEvent<HTMLDivElement>)=>{
    event.preventDefault();
    if(disabled)return;
    // Drag data is untrusted. Resolve GUID against the indexed assets,
    // never accept an arbitrary external file path or spoofed GUID.
    let candidate:unknown;
    try {candidate=JSON.parse(event.dataTransfer.getData('application/x-aiverse-asset')||'null');}
    catch {candidate=null;}
    if(!isAsset(candidate)){setError('Drop an indexed Studio asset.');return;}
    const resolved=assets.find(a=>a.guid.toLowerCase()===candidate.guid.toLowerCase()&&
      (!kinds||kinds.includes(a.kind)));
    if(!resolved){setError('Asset GUID is not in the current project index.');return;}
    choose(resolved);
  };
  const selected=assets.find(a=>a.guid.toLowerCase()===value?.guid.toLowerCase());
  return <div className="spectra-asset-picker" onDragOver={e=>e.preventDefault()} onDrop={drop}>
    <div style={{display:'flex',gap:6,alignItems:'center'}}>
      <StudioButton size="sm" disabled={disabled} onClick={()=>setOpen(v=>!v)}>
        {selected?.name??(value?.guid?'Missing asset: '+value.guid.slice(0,8):'Select asset')}
      </StudioButton>
      {onLockChange&&<StudioButton size="sm" variant="ghost" disabled={disabled}
        onClick={()=>onLockChange(!locked)}>{locked?'Locked':'Unlocked'}</StudioButton>}
    </div>
    {open&&<div role="dialog" aria-label="Select SpectraFX asset">
      <StudioInput value={query} onChange={e=>setQuery(e.target.value)}
        placeholder="Search name, path, type or GUID"/>
      <div style={{maxHeight:240,overflowY:'auto'}}>
        {filtered.map(a=><div key={a.guid}>
          <StudioButton size="sm" variant="ghost" disabled={disabled}
            onClick={()=>choose(a)}>{a.name} · {a.kind} · {a.path??a.guid}</StudioButton>
        </div>)}
        {filtered.length===0&&<small>No matching indexed assets</small>}
      </div>
    </div>}
    {error&&<small role="alert">{error}</small>}
  </div>;
}
