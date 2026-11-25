import { useEffect, useState } from 'react';
import { invoke } from '../bridge';
import { StudioButton, StudioSection } from '../ui';

const simulationFields = [
  { key:'tickRate', label:'Simulation tick rate', unit:'Hz', min:20, max:120 },
  { key:'snapshotRate', label:'Network snapshot rate', unit:'Hz', min:1, max:120 },
  { key:'maxPlayers', label:'Player limit', unit:'', min:1, max:200 },
] as const;
const movementFields = [
  { key:'speed', label:'Walk speed', unit:'m/s', min:1, max:30 },
  { key:'sprintSpeed', label:'Sprint speed', unit:'m/s', min:1, max:50 },
] as const;
type Settings = Record<string,number>;

export function ActorSimulation({project,page}:{project:string;page:'Simulation'|'Movement'}) {
  const movement=page==='Movement';
  const section=movement?'actorMovement':'actorSimulation';
  const fields=movement?movementFields:simulationFields;
  const [original,setOriginal]=useState('');
  const [values,setValues]=useState<Settings>();
  const [busy,setBusy]=useState(false);
  const [message,setMessage]=useState('');
  const [error,setError]=useState('');
  const [reload,setReload]=useState(0);
  useEffect(()=>{
    let live=true;
    setBusy(true);setError('');setMessage('');
    invoke('studio.rulesDocument',{section}).then(({text})=>{
      if(live){setValues(JSON.parse(text));setOriginal(text);}
    }).catch(e=>{if(live)setError(String(e));}).finally(()=>{if(live)setBusy(false);});
    return ()=>{live=false;};
  },[project,reload,section]);
  const saved:Settings|undefined=original?JSON.parse(original):undefined;
  const dirty=values && saved && fields.some(({key})=>values[key]!==saved[key]);
  async function save(){
    if(!dirty || busy)return;
    setBusy(true);setError('');
    try{
      const {text}=await invoke('studio.rulesDocumentSave',{section,projectPath:project,original,value:Object.fromEntries(fields.filter(({key})=>values[key]!==saved[key]).map(({key})=>[key,values[key]]))});
      setOriginal(text);setValues(JSON.parse(text));setMessage(page+' settings saved.');
    }catch(e){setError(e instanceof Error?e.message:String(e));}
    finally{setBusy(false);}
  }
  return <form className="actor-simulation" aria-label={'Actor '+page.toLowerCase()+' settings'} onSubmit={e=>{e.preventDefault();void save();}}>
    <StudioSection title={page}>
      {values && fields.map(({key,label,unit,min,max})=><label key={key}>
        <span>{label}<output>{values[key]} {unit}</output></span>
        <input type="range" aria-label={label} min={key==='sprintSpeed'?values.speed:min} max={key==='snapshotRate'?values.tickRate:max} step={movement?0.01:1}
          value={values[key]} disabled={busy} onChange={e=>{
            const value=e.target.valueAsNumber;
            const next={...values,[key]:value};
            if(key==='tickRate')next.snapshotRate=Math.min(values.snapshotRate,value);
            if(key==='speed')next.sprintSpeed=Math.max(values.sprintSpeed,value);
            setValues(next);
            setMessage('');setError('');
          }}/>
      </label>)}
      {busy && !values && <p role="status">Reading project settings…</p>}
      {error && <p role="alert">{error}</p>}
      {message && <p role="status">{message}</p>}
      <div className="insp-map__actions">
        <StudioButton type="submit" variant="primary" disabled={!dirty||busy}>Save settings</StudioButton>
        <StudioButton disabled={busy} onClick={()=>setReload(v=>v+1)}>{dirty?'Discard and reload':'Reload'}</StudioButton>
      </div>
    </StudioSection>
  </form>;
}
