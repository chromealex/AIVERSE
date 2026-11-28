import {useEffect,useRef,useState} from 'react';
import {invoke} from '../bridge';
import {StudioButton} from '../ui';

type Fields=Awaited<ReturnType<typeof read>>;
function read(project:string,epoch:string,remote:boolean){
  if(remote)return invoke('studio.physisRemoteFields',{action:'status',expectedProject:project,expectedDocumentEpoch:epoch});
  return invoke('studio.physisFieldEdit',{action:'status',expectedProject:project,expectedDocumentEpoch:epoch});
}

export default function PhysisLiveFields({project,epoch,remote=false}:{project:string;epoch:string;remote?:boolean}){
  const [open,setOpen]=useState(false),[fields,setFields]=useState<Fields>(),[error,setError]=useState('');
  const applying=useRef(false), version=useRef(0);
  useEffect(()=>{
    if(!open)return;
    let active=true,reading=false;
    async function refresh(){
      if(reading||applying.current)return;
      reading=true;const started=version.current;
      try{const next=await read(project,epoch,remote);if(active&&version.current===started)setFields(next);}
      catch(e){if(active&&version.current===started)setError(String(e));}
      finally{reading=false;}
    }
    void refresh();
    const timer=window.setInterval(()=>void refresh(),700);
    return ()=>{active=false;window.clearInterval(timer);};
  },[open,project,epoch,remote]);
  const server=fields&&'connectionVersion' in fields?fields:undefined;
  const local=fields&&!('connectionVersion' in fields)?fields:undefined;
  async function act(action:'apply'|'retry'|'refresh'){
    if(!fields||applying.current)return;
    applying.current=true;++version.current;setError('');
    try{
      if(server)setFields(await invoke('studio.physisRemoteFields',{action,expectedProject:project,expectedDocumentEpoch:epoch,
        remoteRevision:server.remoteRevision,mapRevision:server.mapRevision,connectionVersion:server.connectionVersion,requestId:server.requestId}));
      else if(local&&action==='apply')setFields(await invoke('studio.physisFieldEdit',{action:'apply',expectedProject:project,expectedDocumentEpoch:epoch,
      runtimeEpoch:local.runtimeEpoch,mapRevision:local.mapRevision,fieldRevision:local.fieldRevision}));}
    catch(e){setError(String(e));}
    finally{applying.current=false;}
  }
  return <details className="physis-zones" onToggle={event=>setOpen(event.currentTarget.open)}>
    <summary>{remote?'Apply zones to server':'Apply zones to local play-test'}</summary>
    {open&&<><p role="status">{server?server.status:fields?.message||fields?.status||'Reading status…'}</p>
      {server&&<><p>{server.receipt||server.message}</p>
        {server.remoteRevision&&<p>Server revision {server.remoteRevision}</p>}
        {server.requestId&&<p>Request {server.requestId}</p>}
        <StudioButton disabled={!server.canRefresh} onClick={()=>void act('refresh')}>Refresh revision</StudioButton>
        {server.canRetry&&<StudioButton onClick={()=>void act('retry')}>Retry request {server.requestId}</StudioButton>}</>}
      {local?.fieldRevision&&<p>Revision {local.fieldRevision} · Tick {local.tick}
        {local.scheduledTick!==undefined&&' · Scheduled for tick '+local.scheduledTick}</p>}
      <StudioButton disabled={!fields?.available} onClick={()=>void act('apply')}>{remote?'Submit zones':'Apply map zones'}</StudioButton>
      {error&&<p role="alert">{error}</p>}</>}
  </details>;
}
