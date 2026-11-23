import {useEffect,useRef,useState} from 'react';
import {invoke} from '../bridge';
import {StudioAlert,StudioButton,StudioInput,StudioSection} from '../ui';
import {object} from './nativeGraphAdapters';
import type {GraphObject} from './nativeGraphAdapters';

export default function GraphLiveSnapshotPanel({domain,projectPath}:{domain:'motionweave'|'spectrafx';projectPath:string}){
 const [entity,setEntity]=useState(''),[busy,setBusy]=useState(false),[error,setError]=useState('');
 const [snapshot,setSnapshot]=useState<GraphObject|null>(null);
 const epoch=useRef(0),lock=useRef(false);
 useEffect(()=>{epoch.current++;setSnapshot(null);setError('');setBusy(false);lock.current=false;
  return()=>{epoch.current++;lock.current=false};
 },[projectPath,domain,entity]);
 const read=async()=>{
  if(lock.current||!projectPath||domain==='motionweave'&&!/^[0-9]{1,20}$/.test(entity))return;
  lock.current=true;setBusy(true);setError('');setSnapshot(null);
  const token=epoch.current;
  try{
   const r=(await invoke('studio.op',{op:'graph_live_snapshot',params:{domain,...(domain==='motionweave'?{entity}:{})}})).result;
   if(token!==epoch.current)return;
   if(!object(r)||r.domain!==domain||typeof r.available!=='boolean')throw new Error('Invalid live snapshot response');
   setSnapshot(r);
  }catch(e){if(token===epoch.current)setError(e instanceof Error?e.message:String(e))}
  finally{if(token===epoch.current){lock.current=false;setBusy(false)}}
 };
 return <StudioSection title="Live owner snapshot">
  <StudioAlert>Read-only. The native host must publish the owner. A snapshot is not attached to the edited graph asset, and does not pause gameplay.</StudioAlert>
  {domain==='motionweave'&&<StudioInput aria-label="Packed entity ID" placeholder="Exact decimal packed entity ID" value={entity} disabled={busy} onChange={e=>setEntity(e.target.value)}/>}
  <StudioButton size="sm" disabled={busy||!projectPath||domain==='motionweave'&&!/^[0-9]{1,20}$/.test(entity)} onClick={()=>void read()}>Read live snapshot</StudioButton>
  {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
  {snapshot&&<>{snapshot.available===false?<StudioAlert>{String(snapshot.reason??'Owner unavailable')}</StudioAlert>:<pre>{JSON.stringify(snapshot,null,2)}</pre>}</>}
 </StudioSection>;
}
