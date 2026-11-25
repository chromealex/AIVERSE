import { useEffect, useState } from 'react';
import { invoke } from '../bridge';
import { StudioButton, StudioSection } from '../ui';

const fields = [
  { key:'tickRate', label:'Simulation tick rate', unit:'Hz', min:20, max:120 },
  { key:'snapshotRate', label:'Network snapshot rate', unit:'Hz', min:1, max:120 },
  { key:'maxPlayers', label:'Player limit', unit:'', min:1, max:200 },
] as const;
type Settings = Record<typeof fields[number]['key'], number>;

export function ActorSimulation({project}:{project:string}) {
  const [original,setOriginal]=useState('');
  const [values,setValues]=useState<Settings>();
  const [busy,setBusy]=useState(false);
  const [message,setMessage]=useState('');
  const [error,setError]=useState('');
  const [reload,setReload]=useState(0);
  useEffect(()=>{
    let live=true;
    setBusy(true);setError('');setMessage('');
    invoke('studio.rulesDocument',{section:'actorSimulation'}).then(({text})=>{
      if(live){setValues(JSON.parse(text));setOriginal(text);}
    }).catch(e=>{if(live)setError(String(e));}).finally(()=>{if(live)setBusy(false);});
    return ()=>{live=false;};
  },[project,reload]);
  const saved:Settings|undefined=original?JSON.parse(original):undefined;
  const dirty=values && saved && fields.some(({key})=>values[key]!==saved[key]);
  async function save(){
    if(!dirty || busy)return;
    setBusy(true);setError('');
    try{
      const {text}=await invoke('studio.rulesDocumentSave',{section:'actorSimulation',projectPath:project,original,value:values});
      setOriginal(text);setValues(JSON.parse(text));setMessage('Simulation settings saved.');
    }catch(e){setError(e instanceof Error?e.message:String(e));}
    finally{setBusy(false);}
  }
  return <form className="actor-simulation" aria-label="Actor simulation settings" onSubmit={e=>{e.preventDefault();void save();}}>
    <StudioSection title="Simulation">
      {values && fields.map(({key,label,unit,min,max})=><label key={key}>
        <span>{label}<output>{values[key]} {unit}</output></span>
        <input type="range" aria-label={label} min={min} max={key==='snapshotRate'?values.tickRate:max}
          value={values[key]} disabled={busy} onChange={e=>{
            const value=e.target.valueAsNumber;
            setValues({...values,[key]:value,...(key==='tickRate'?{snapshotRate:Math.min(values.snapshotRate,value)}:{})});
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
