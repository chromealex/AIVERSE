import { useEffect, useState } from 'react';
import { Map as MapIcon } from 'lucide-react';
import { MapAreas } from './MapAreas';
import { MapPlayers } from './MapPlayers';
import { invoke } from '../bridge';
import type { StudioGraphDocumentScope, StudioMapProperties, StudioMapChanges, StudioSnapshot } from '../types';
import { StudioButton, StudioInput } from '../ui';

function form(data:StudioMapProperties){
  return {name:data.name,width:String(data.width),height:String(data.height),roadsOnly:data.roadsOnly,
    victory:data.victory==null?'':JSON.stringify(data.victory,null,2)};
}

interface Props {
  scope:StudioGraphDocumentScope;
  onApply:(snapshot:StudioSnapshot)=>void;
  onSelect:(id:string)=>void;
  busy:boolean;
  view:"object"|"map"|"players"|"areas";
}

export function MapInspector({scope,onApply,onSelect,busy,view}:Props){
  const [data,setData]=useState<StudioMapProperties>();
  const [draft,setDraft]=useState<{base:StudioMapProperties;fields:ReturnType<typeof form>}>();
  const [error,setError]=useState('');
  const [pending,setPending]=useState(false);
  const [reload,setReload]=useState(0);
  useEffect(()=>{
    let live=true;
    setError('');
    invoke('studio.mapProperties',{scope}).then(result=>{if(live)setData(result);})
      .catch(e=>{if(live)setError(String(e));});
    return ()=>{live=false;};
  },[scope.projectPath,scope.mapPath,scope.epoch,scope.revision,reload]);
  if(!data)return <div className="insp-map" role="status">{error||'Reading map…'}
    {error && <StudioButton onClick={()=>setReload(v=>v+1)}>Reload</StudioButton>}</div>;
  const base=draft?.base??data, fields=draft?.fields??form(data);
  const dirty=JSON.stringify(fields)!==JSON.stringify(form(base));
  const stale=base.scope.revision!==scope.revision;
  const edit=(change:Partial<typeof fields>)=>{setDraft({base,fields:{...fields,...change}});setError('');};
  const apply=async()=>{
    setPending(true);setError('');
    try{
      const changes:StudioMapChanges={};
      if(fields.name!==base.name)changes.name=fields.name;
      if(fields.roadsOnly!==base.roadsOnly)changes.roadsOnly=fields.roadsOnly;
      if(fields.width!==String(base.width)||fields.height!==String(base.height))changes.size=[Number(fields.width),Number(fields.height)];
      if(fields.victory!==form(base).victory){
        const victory=fields.victory.trim()?JSON.parse(fields.victory):null;
        if(victory!==null && (Array.isArray(victory)||typeof victory!=='object'))throw Error('Use a JSON object for the victory override.');
        changes.victory=victory;
      }
      onApply(await invoke('studio.mapPropertiesSet',{scope:base.scope,changes}));
      setDraft(undefined);
    }catch(e){setError(e instanceof Error?e.message:String(e));}
    finally{setPending(false);}
  };
  const validate=async()=>{
    setPending(true);setError('');
    try{setData(await invoke('studio.mapProperties',{scope,validate:true}));}
    catch(e){setError(String(e));}
    finally{setPending(false);}
  };
  if(view==="areas")return <MapAreas data={data} revision={scope.revision} busy={busy} onApply={onApply}/>;
  if(view==="players")return <MapPlayers data={data} revision={scope.revision} busy={busy} onApply={onApply}/>;
  return <form className="insp-map" aria-label={data.worldSpace?"Scene properties":"Map properties"} onSubmit={e=>{e.preventDefault();void apply();}}>
    <header><MapIcon size={19}/><div><h2>{data.name || 'Untitled map'}</h2>
      <small>{data.worldSpace
        ? `${data.worldSpace.colliders} colliders · ${data.worldSpace.spawns} spawn points`
        : `${data.entities} objects · ${data.players} players`}</small></div></header>
    <fieldset disabled={busy||pending}>
      <label>{data.worldSpace?"Scene name":"Map name"}<StudioInput value={fields.name} required onChange={e=>edit({name:e.target.value})}/></label>
      {data.grid && <div className="insp-map__size">{(['width','height'] as const).map(key=>
        <label key={key}>{key==='width'?'Width':'Height'} (cells)<StudioInput type="number" min={Math.min(8,base[key])} max={data.maxSize}
          step={1} required value={fields[key]} onChange={e=>edit({[key]:e.target.value})}/></label>)}</div>}
      {(fields.width!==String(base.width)||fields.height!==String(base.height)) &&
        <small>Resizing keeps the top-left terrain and clears visual terrain detail.</small>}
      {!data.worldSpace && <><label className="insp-map__toggle"><input type="checkbox" checked={fields.roadsOnly} onChange={e=>edit({roadsOnly:e.target.checked})}/>Movement only on roads</label>
      <details><summary>Victory override</summary>
        <label>Map victory JSON<textarea className="au-textarea" value={fields.victory} placeholder="Use project rules"
          onChange={e=>edit({victory:e.target.value})}/></label></details></>}
      {error && <p role="alert">{error}</p>}
      {stale && dirty && <p role="alert">The map has changed. Reload its properties before applying.</p>}
      <div className="insp-map__actions"><StudioButton type="submit" variant="primary" disabled={!dirty||stale}>Apply</StudioButton>
        {draft && <StudioButton onClick={()=>{setDraft(undefined);setReload(v=>v+1);}}>{stale?'Reload':'Discard'}</StudioButton>}
        {!data.worldSpace && <StudioButton disabled={dirty||stale} onClick={()=>void validate()}>Validate map</StudioButton>}</div>
    </fieldset>
    {!dirty && data.scope.revision===scope.revision && data.issues && <section aria-label="Map validation"><h3>Validation</h3>
      {data.issues.length===0?<p>No issues found.</p>:<ul>{data.issues.map((issue,i)=><li key={i}>
        <strong>{issue.error?'Error':'Warning'}</strong> {issue.text}
        {issue.entityId && <StudioButton onClick={()=>onSelect(issue.entityId!)}>Inspect object</StudioButton>}
      </li>)}</ul>}</section>}
  </form>;
}
