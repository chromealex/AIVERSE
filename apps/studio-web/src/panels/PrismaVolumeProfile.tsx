import {useState} from 'react';
import {StudioButton, StudioInput, StudioSelect} from '../ui';
import type {CatalogEffect, CatalogParam} from './PostFxPanel';

type Effect = Record<string,unknown> & {type:string};
interface Profile {effects?:Effect[]}

function ProfileValue({param,value,set}: {param:CatalogParam;value:unknown;set:(value:unknown)=>void}) {
  const {name,range,kind}=param;
  if (kind==='number') return <div className="pfx-profile-slider">
    <input type="range" aria-label={name} min={range?.[0]} max={range?.[1]} step="any"
      value={Number(value)} onChange={e=>set(Number(e.target.value))}/><output>{Number(Number(value).toFixed(3))}</output>
  </div>;
  if (kind==='bool') return <input type="checkbox" aria-label={name} checked={value===true} onChange={e=>set(e.target.checked)}/>;
  if (kind.startsWith('color')) {
    const rgb=Array.isArray(value)?value:[1,1,1];
    return <div className="pfx-profile-rgb">{['R','G','B'].map((c,i)=>
      <label key={c}>{c} <output>{Number(Number(rgb[i]).toFixed(3))}</output><input aria-label={name+' '+c} type="range" step="any" min={range?.[0]} max={range?.[1]}
        value={rgb[i]} onChange={e=>set(rgb.map((v,j)=>j===i?e.target.valueAsNumber:v))}/></label>)}</div>;
  }
  return <StudioInput aria-label={name} value={String(value??'')} onChange={e=>set(e.target.value)}/>;
}

export default function PrismaVolumeProfile({initial,catalog}: {initial?:Record<string,unknown>;catalog:CatalogEffect[]}) {
  const [raw,setRaw]=useState(()=>JSON.stringify(initial??{effects:[]},null,2));
  let profile:Profile|null;
  try {profile=JSON.parse(raw);} catch {profile=null;}
  const editable=profile && typeof profile==='object' && !Array.isArray(profile) &&
    (profile.effects===undefined || (Array.isArray(profile.effects) && profile.effects.every(e=>e && typeof e.type==='string')));
  const effects=editable?profile?.effects??[]:[];
  const replace=(next:Effect[])=>setRaw(JSON.stringify({...profile,effects:next},null,2));
  function update(index:number,key:string,value:unknown) {
    const next={...effects[index]};
    if (value===undefined) delete next[key]; else next[key]=value;
    replace(effects.map((e,i)=>i===index?next:e));
  }
  return <section className="pfx-profile"><h4>Volume effects</h4>
    {editable && <>
      <StudioSelect aria-label="Add volume effect" value="" onChange={e=>replace([...effects,{type:e.target.value}])}>
        <option value="" disabled>Add effect…</option>
        {catalog.filter(d=>!effects.some(e=>e.type===d.type)).map(d=><option key={d.type} value={d.type}>{d.type}</option>)}
      </StudioSelect>
      {effects.map((effect,i)=><details key={effect.type+i} className="pfx-profile-effect"><summary>{effect.type}</summary>
        <label>Activation <StudioSelect aria-label={effect.type+' activation'} value={effect.enabled===undefined?'inherit':String(effect.enabled)}
          onChange={e=>update(i,'enabled',e.target.value==='inherit'?undefined:e.target.value==='true')}>
          <option value="inherit">Inherit</option><option value="true">Enabled</option><option value="false">Disabled</option>
        </StudioSelect></label>
        {catalog.find(d=>d.type===effect.type)?.params.map(p=>{
          const local=Object.hasOwn(effect,p.name);
          const fallback=p.default??'';
          return <div key={p.name} className="pfx-profile-param" title={p.help}>
            <label><input type="checkbox" checked={local} aria-label={'Override '+p.name}
              onChange={e=>update(i,p.name,e.target.checked?fallback:undefined)}/>{p.name}</label>
            {local && <ProfileValue param={p} value={effect[p.name]} set={v=>update(i,p.name,v)}/>}
          </div>;
        })}
        <StudioButton size="sm" onClick={()=>replace(effects.filter((_,j)=>j!==i))}>Remove effect</StudioButton>
      </details>)}
    </>}
    <details><summary>Profile JSON</summary><textarea className="au-input" aria-label="Effect profile JSON" name="profile"
      rows={6} value={raw} onChange={e=>setRaw(e.target.value)}/></details>
  </section>;
}
