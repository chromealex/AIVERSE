import { useEffect, useState } from 'react';
import { invoke } from '../bridge';
import StaticGeometryDiagnostics from './StaticGeometryDiagnostics';
import {StudioSelect} from '../ui';
import './StaticGeometryPanel.css';
interface Instance { id:string; model:string; mode:'auto'|'strata'|'lod' }
interface Scene { file:string; instances:Instance[] }

export default function StaticGeometryPanel({projectPath}:{projectPath:string}) {
  const [scene,setScene]=useState<Scene|null>(null);
  const [error,setError]=useState('');
  const [busy,setBusy]=useState(true);
  const [revision,setRevision]=useState(0);
  useEffect(()=>{
    if(!projectPath)return;
    let live=true;
    setBusy(true);
    setError('');
    invoke('studio.op',{op:'geometry_instances_list',params:{}})
      .then(({result})=>{if(live)setScene(result as Scene);})
      .catch(e=>{if(live){setScene(null);setError(String(e));}})
      .finally(()=>{if(live)setBusy(false);});
    return ()=>{live=false;};
  },[projectPath,revision]);

  async function changeMode(id:string,mode:string){
    setBusy(true);
    setError('');
    try{
      await invoke('studio.op',{op:'geometry_strata_set',params:{id,mode}});
      setRevision(r=>r+1);
    }catch(e){setError(String(e));setBusy(false);}
  }
  if(!projectPath)return <p>Open a project to edit static geometry.</p>;
  return <section className="static-geometry">
    <header><strong>{scene?.file??'Static geometry'}</strong>
      <button type="button" disabled={busy} onClick={()=>setRevision(r=>r+1)}>Reload</button></header>
    {error&&<p role="alert">{error}</p>}
    {busy&&!scene&&<p role="status">Loading static geometry…</p>}
    {scene&&!scene.instances.length&&<p>No static objects in this scene.</p>}
    {scene?.instances.map(row=><label key={row.id}><span><b>{row.id}</b><small>{row.model}</small></span>
      <StudioSelect aria-label={'Rendering mode for '+row.id} disabled={busy||!row.id} value={row.mode}
        onChange={event=>void changeMode(row.id,event.target.value)}>
        <option value="auto">Auto</option><option value="strata">STRATA</option><option value="lod">LOD</option>
      </StudioSelect></label>)}
    <StaticGeometryDiagnostics project={projectPath}/>
  </section>;
}
