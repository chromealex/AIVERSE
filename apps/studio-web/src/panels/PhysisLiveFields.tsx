import {useEffect,useRef,useState} from 'react';
import {invoke} from '../bridge';
import {StudioButton} from '../ui';

type Fields=Awaited<ReturnType<typeof read>>;
function read(project:string,epoch:string){
  return invoke('studio.physisFieldEdit',{action:'status',expectedProject:project,expectedDocumentEpoch:epoch});
}

export default function PhysisLiveFields({project,epoch}:{project:string;epoch:string}){
  const [open,setOpen]=useState(false),[fields,setFields]=useState<Fields>(),[error,setError]=useState('');
  const applying=useRef(false), version=useRef(0);
  useEffect(()=>{
    if(!open)return;
    let active=true,reading=false;
    async function refresh(){
      if(reading||applying.current)return;
      reading=true;const started=version.current;
      try{const next=await read(project,epoch);if(active&&version.current===started)setFields(next);}
      catch(e){if(active&&version.current===started)setError(String(e));}
      finally{reading=false;}
    }
    void refresh();
    const timer=window.setInterval(()=>void refresh(),700);
    return ()=>{active=false;window.clearInterval(timer);};
  },[open,project,epoch]);
  async function apply(){
    if(!fields?.available||applying.current)return;
    applying.current=true;++version.current;setError('');
    try{setFields(await invoke('studio.physisFieldEdit',{action:'apply',expectedProject:project,expectedDocumentEpoch:epoch,
      runtimeEpoch:fields.runtimeEpoch,mapRevision:fields.mapRevision,fieldRevision:fields.fieldRevision}));}
    catch(e){setError(String(e));}
    finally{applying.current=false;}
  }
  return <details className="physis-zones" onToggle={event=>setOpen(event.currentTarget.open)}>
    <summary>Apply zones to local play-test</summary>
    {open&&<><p role="status">{fields?.message||fields?.status||'Reading play-test…'}</p>
      {fields?.fieldRevision&&<p>Revision {fields.fieldRevision} · Tick {fields.tick}
        {fields.scheduledTick!==undefined&&' · Scheduled for tick '+fields.scheduledTick}</p>}
      <StudioButton disabled={!fields?.available} onClick={()=>void apply()}>Apply map zones</StudioButton>
      {error&&<p role="alert">{error}</p>}</>}
  </details>;
}
