import {useEffect,useId,useState} from 'react';
import {invoke,onNativeEvent} from '../bridge';
import {StudioAlert,StudioButton,StudioInput,StudioSection,StudioSelect} from '../ui';
import SpectraDockViewport from './SpectraDockViewport';

export default function SourceModelPreview({project}:{project:string}){
  const list=useId();
  const [files,setFiles]=useState<string[]>([]);
  const [sources,setSources]=useState<{type:string;path:string}[]>([]);
  const [sourceType,setSourceType]=useState('');
  const [path,setPath]=useState('');
  const [active,setActive]=useState('');
  const [configured,setConfigured]=useState(false);
  const [owner,setOwner]=useState(0),[playing,setPlaying]=useState(true);
  const [busy,setBusy]=useState(false);
  const [error,setError]=useState('');
  const [yaw,setYaw]=useState(.6),[pitch,setPitch]=useState(.4);
  useEffect(()=>{
    let live=true;
    invoke('studio.assets',{}).then(r=>{
      if(live)setFiles(r.files.filter(f=>/\.(gltf|glb|fbx|obj)$/i.test(f.path)).map(f=>'content/'+f.path));
    }).catch(e=>{if(live)setError(String(e));});
    invoke('studio.rules',{}).then(r=>{
      const entries=Object.entries(r.resolved).flatMap(([type,entry])=>{
        const view=entry&&typeof entry==='object'&&'view' in entry?entry.view:null;
        return view&&typeof view==='object'&&'model' in view&&typeof view.model==='string'&&view.model&&!view.model.startsWith('shape:')
          ?[{type,path:view.model}]:[{type,path:''}];
      });
      if(live)setSources(entries);
    }).catch(()=>{/* Actor projects can still inspect a file directly. */});
    const off=onNativeEvent((event,reason)=>{
      if(event==='sourceModel.previewError'){setActive('');setError(String(reason));}
    });
    return ()=>{live=false;off();void invoke('studio.sourceModelPreview',{project,close:true}).catch(()=>{});};
  },[project]);
  async function open(asType=false){
    const model=path.trim();
    setError('');setBusy(true);
    try{
      await invoke('studio.sourceModelPreview',{project,...(asType?{type:sourceType,owner,playing:true}:{model}),yaw:.6,pitch:.4});
      setActive(asType?sourceType:model);setConfigured(asType);setPlaying(true);setYaw(.6);setPitch(.4);
    }catch(e){setError(e instanceof Error?e.message:String(e));}
    finally{setBusy(false);}
  }
  function orbit(axis:'yaw'|'pitch',value:number){
    if(axis==='yaw')setYaw(value);else setPitch(value);
    void invoke('studio.sourceModelPreview',{project,[axis]:value}).catch(e=>setError(String(e)));
  }
  return <StudioSection title="Model preview">
    <form className="au-stack" onSubmit={e=>{e.preventDefault();if(path.trim()&&!busy)void open();}}>
      {!!sources.length&&<label>Game type<StudioSelect aria-label="Original model game type" value={sourceType}
        onChange={e=>{const type=e.target.value;setSourceType(type);setPath(sources.find(row=>row.type===type)?.path??'');}}>
        <option value="">Choose a type…</option>{sources.map(row=><option key={row.type}>{row.type}</option>)}
      </StudioSelect></label>}
      <label>Source file<StudioInput aria-label="Source model file" list={list} placeholder="content/models/stone.glb"
        value={path} onChange={e=>{setSourceType('');setPath(e.target.value);}}/></label>
      <datalist id={list}>{files.map(file=><option key={file} value={file}/>)}</datalist>
      <div className="au-row">
        <StudioButton type="button" disabled={!sourceType||busy} onClick={()=>void open(true)}>Preview game type</StudioButton>
        <StudioButton type="submit" disabled={!path.trim()||busy}>Preview original</StudioButton>
      </div>
    </form>
    {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
    {active&&<div className="au-stack">
      <strong>{active}</strong>
      <SpectraDockViewport id="sourceModel" label={configured?'Configured game type':'Original imported model'}/>
      {configured&&<div className="au-row">
        <label>Player<StudioSelect aria-label="Preview player" value={owner} onChange={e=>{
          const value=Number(e.target.value);setOwner(value);
          void invoke('studio.sourceModelPreview',{project,owner:value}).catch(e=>setError(String(e)));
        }}>{Array.from({length:9},(_,i)=><option key={i} value={i}>{i?'Player '+i:'Neutral'}</option>)}</StudioSelect></label>
        <StudioButton size="sm" onClick={()=>{
          setPlaying(!playing);void invoke('studio.sourceModelPreview',{project,playing:!playing}).catch(e=>setError(String(e)));
        }}>{playing?'Pause animation':'Resume animation'}</StudioButton>
      </div>}
      <label>Orbit<StudioInput aria-label="Original model orbit" type="range" min={-3.14} max={3.14} step={.01}
        value={yaw} onChange={e=>orbit('yaw',e.target.valueAsNumber)}/></label>
      <label>Elevation<StudioInput aria-label="Original model elevation" type="range" min={-.9} max={1.4} step={.01}
        value={pitch} onChange={e=>orbit('pitch',e.target.valueAsNumber)}/></label>
    </div>}
  </StudioSection>;
}
