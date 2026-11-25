import { useEffect, useRef, useState } from 'react';
import { Brush, Map as MapIcon } from 'lucide-react';
import { invoke, onNativeEvent } from '../bridge';
import type { StudioGraphDocumentScope, StudioMapRegions, StudioSceneBrush, StudioSnapshot } from '../types';
import { StudioButton, StudioInput } from '../ui';

function territoryPath(rows:string[],stride:number){
  const runs:string[]=[];
  rows.forEach((row,y)=>{
    for(const segment of row.matchAll(/#+/g)){
      const width=segment[0].length*stride;
      runs.push(`M${segment.index*stride} ${y*stride}h${width}v${stride}h-${width}z`);
    }
  });
  return runs.join('');
}

export function MapRegions({scope,busy,onApply}:{
  scope:StudioGraphDocumentScope;busy:boolean;onApply:(snapshot:StudioSnapshot)=>void;
}){
  const [letter,setLetter]=useState('A');
  const initialSelection=useRef(true);
  const [loaded,setLoaded]=useState<{letter:string;revision:string;map:StudioMapRegions}>();
  const [draft,setDraft]=useState<{name:string;scope:StudioGraphDocumentScope}>();
  const [brush,setBrush]=useState<StudioSceneBrush>();
  const [error,setError]=useState('');
  const [pending,setPending]=useState(false);
  const [retry,setRetry]=useState(0);
  useEffect(()=>{
    let live=true;
    setError('');
    invoke('studio.mapRegions',{scope,letter}).then(map=>{
      if(!live)return;
      if(initialSelection.current){
        initialSelection.current=false;
        const first=map.regions.find(r=>r.cells>0 || r.name);
        if(first && first.letter!==letter){setLetter(first.letter);return;}
      }
      setLoaded({map,letter,revision:scope.revision});
    }).catch(e=>{if(live)setError(String(e));});
    return ()=>{live=false;};
  },[scope.projectPath,scope.mapPath,scope.epoch,scope.revision,letter,retry]);
  useEffect(()=>{
    let live=true;
    const off=onNativeEvent((event,payload)=>{if(event==='brush.changed')setBrush(payload as StudioSceneBrush);});
    invoke('studio.sceneBrush',{}).then(b=>{if(live)setBrush(b);}).catch(()=>{});
    return ()=>{live=false;off();};
  },[]);
  const map=loaded?.map;
  const region=map?.regions.find(r=>r.letter===letter);
  const current=loaded?.revision===scope.revision && loaded?.letter===letter;
  const armed=brush?.armed && brush.kind==='region' && brush.regionLetter===letter;
  const stale=draft && scope.revision!==draft.scope.revision;
  const locked=busy||pending||!current;
  async function rename(){
    if(!draft)return;
    setPending(true);setError('');
    try{
      onApply(await invoke('studio.mapRegionName',{scope:draft.scope,letter,name:draft.name}));
      setDraft(undefined);
    }catch(e){setError(String(e));}finally{setPending(false);}
  }
  async function paint(){
    setPending(true);setError('');
    try{setBrush(await invoke('studio.sceneBrush',{scope,kind:'region',regionLetter:letter,armed:!armed}));}
    catch(e){setError(String(e));}finally{setPending(false);}
  }
  return <section className="insp-map insp-regions" aria-label="Map regions">
    <header><MapIcon size={19}/><div><h2>Map regions</h2>{map && <small>{map.width} × {map.height} cells</small>}</div></header>
    {map && region && <fieldset disabled={busy||pending}>
      <label>Region<select value={letter} disabled={!!draft || armed} onChange={e=>setLetter(e.target.value)}>
        {map.regions.map(r=><option value={r.letter} key={r.letter}>{r.letter}{r.name?' · '+r.name:''} ({r.cells})</option>)}
      </select></label>
      {loaded?.letter===letter && <figure>
        <svg role="img" aria-label={`Region ${letter}, ${region.cells} cells`} viewBox={`0 0 ${map.width} ${map.height}`}>
          <rect width={map.width} height={map.height} fill="var(--au-field)"/>
          <path d={territoryPath(map.coverage,map.stride)} fill="var(--au-accent)"/>
        </svg>
        <figcaption>{region.cells.toLocaleString()} painted cells{map.stride>1 && ` · Overview grouped in ${map.stride} × ${map.stride} cells`}</figcaption>
      </figure>}
      <label>Display name<StudioInput value={draft?.name??region.name} placeholder={`Region ${letter}`}
        disabled={!current} onChange={e=>setDraft({name:e.target.value,scope:draft?.scope??scope})}/></label>
      {stale && <p role="alert">The map changed. Discard the draft to load the current name.</p>}
      <div className="insp-map__actions">
        <StudioButton variant="primary" disabled={locked||!!stale||!draft||draft.name===region.name} onClick={()=>void rename()}>Save name</StudioButton>
        {draft && <StudioButton onClick={()=>setDraft(undefined)}>Discard</StudioButton>}
        <StudioButton disabled={locked||!!draft} aria-pressed={!!armed} onClick={()=>void paint()}><Brush size={14}/>{armed?'Stop painting':'Paint in Scene'}</StudioButton>
      </div>
    </fieldset>}
    {!map && !error && <p role="status">Reading regions…</p>}
    {error && <div><p role="alert">{error}</p><StudioButton onClick={()=>setRetry(v=>v+1)}>Reload</StudioButton></div>}
  </section>;
}
