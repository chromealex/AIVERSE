import {useEffect,useId,useRef,useState} from 'react';
import {invoke,onNativeEvent} from '../bridge';
import {StudioAlert,StudioButton,StudioInput,StudioSection,StudioSelect} from '../ui';
import SpectraDockViewport from './SpectraDockViewport';
import './SourceModelPreview.css';

export default function ImportedClipPreview({project}:{project:string}){
  const list=useId();
  const [files,setFiles]=useState<string[]>([]);
  const [path,setPath]=useState(''),[active,setActive]=useState('');
  const [clips,setClips]=useState<{name:string;duration:number}[]>([]);
  const [clip,setClip]=useState(0),[playing,setPlaying]=useState(false),[time,setTime]=useState(0);
  const [busy,setBusy]=useState(false),[error,setError]=useState('');
  const intent=useRef(0);
  const duration=clips[clip]?.duration??0;
  useEffect(()=>{
    let live=true;
    invoke('studio.assets',{}).then(r=>{
      if(live)setFiles(r.files.filter(f=>/\.(gltf|glb|fbx|dae|mdx|mdl)$/i.test(f.path)).map(f=>'content/'+f.path));
    }).catch(e=>{if(live)setError(String(e));});
    const off=onNativeEvent((event,reason)=>{
      if(event==='importedClip.previewError'){++intent.current;setActive('');setPlaying(false);setError(String(reason));}
    });
    return ()=>{live=false;++intent.current;off();void invoke('studio.importedClipPreview',{project,close:true}).catch(()=>{});};
  },[project]);
  useEffect(()=>{
    if(!active||!playing)return;
    let live=true,timer=0;
    async function poll(){
      const generation=intent.current;
      try{
        const r=await invoke('studio.importedClipPreview',{project,status:true});
        if(live&&generation===intent.current&&typeof r.time==='number')setTime(r.time);
      }catch(e){if(live)setError(String(e));}
      if(live)timer=window.setTimeout(poll,180);
    }
    void poll();
    return ()=>{live=false;window.clearTimeout(timer);};
  },[active,playing,project]);
  async function open(){
    const generation=++intent.current,model=path.trim();
    setBusy(true);setError('');
    try{
      const r=await invoke('studio.importedClipPreview',{project,model,playing:false});
      if(generation!==intent.current)return;
      setActive(model);setClips(r.clips??[]);setClip(0);setTime(0);setPlaying(false);
    }catch(e){if(generation===intent.current)setError(String(e));}
    finally{setBusy(false);}
  }
  function control(settings:{clip?:number;time?:number;playing:boolean}){
    const generation=++intent.current;
    setError('');setPlaying(settings.playing);
    if(settings.clip!==undefined){setClip(settings.clip);setTime(0);}
    if(settings.time!==undefined)setTime(settings.time);
    void invoke('studio.importedClipPreview',{project,...settings}).then(async()=>{
      if(generation!==intent.current||settings.playing||settings.clip!==undefined||settings.time!==undefined)return;
      const r=await invoke('studio.importedClipPreview',{project,status:true});
      if(generation===intent.current&&typeof r.time==='number')setTime(r.time);
    }).catch(e=>{if(generation===intent.current)setError(String(e));});
  }
  return <StudioSection title="Imported animation">
    <div className={'source-model'+(active?' has-preview':'')}>
      <form onSubmit={e=>{e.preventDefault();if(path.trim()&&!busy)void open();}}>
        <label>Model file<StudioInput list={list} aria-label="Animation model file" value={path}
          placeholder="content/models/character.glb" onChange={e=>setPath(e.target.value)}/></label>
        <datalist id={list}>{files.map(file=><option key={file} value={file}/>)}</datalist>
        <StudioButton type="submit" disabled={busy||!path.trim()}>Open model</StudioButton>
      </form>
      {active&&<div className="source-model__viewer">
        <strong>{active}</strong><SpectraDockViewport id="importedClip" label="Imported clip preview"/>
        {clips.length?<fieldset disabled={busy} className="source-model__controls">
          <label>Clip<StudioSelect aria-label="Imported clip" value={clip}
            onChange={e=>control({clip:Number(e.target.value),playing:false})}>
            {clips.map((c,i)=><option key={i} value={i}>{c.name||'Clip '+(i+1)}</option>)}
          </StudioSelect></label>
          <label>Time · {time.toFixed(2)} / {duration.toFixed(2)} s<StudioInput type="range" aria-label="Imported clip time"
            min={0} max={duration} step="any" disabled={!duration} value={time} onChange={e=>control({time:e.target.valueAsNumber,playing:false})}/></label>
          <StudioButton disabled={!duration} onClick={()=>control({playing:!playing})}>{playing?'Pause clip':'Play clip'}</StudioButton>
        </fieldset>:<p>This model contains no animation clips.</p>}
      </div>}
    </div>
    {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
  </StudioSection>;
}
