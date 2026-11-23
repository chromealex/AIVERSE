import {useEffect,useRef,useState} from 'react';
import {invoke} from '../bridge';
import {StudioAlert,StudioButton,StudioInput,StudioSection,StudioSelect} from '../ui';
import {object} from './nativeGraphAdapters';
import type {GraphObject} from './nativeGraphAdapters';

export default function GraphLiveSnapshotPanel({domain,projectPath}:{domain:'motionweave'|'spectrafx';projectPath:string}){
 const [entity,setEntity]=useState(''),[busy,setBusy]=useState(false),[error,setError]=useState('');
 const [profile,setProfile]=useState<GraphObject|null>(null);
 const [watch,setWatch]=useState<GraphObject|null>(null),[frame,setFrame]=useState(0);
 const [captureHistory,setCaptureHistory]=useState(false);
 const [snapshot,setSnapshot]=useState<GraphObject|null>(null);
 const epoch=useRef(0),lock=useRef(false);
 useEffect(()=>{epoch.current++;setSnapshot(null);setWatch(null);setProfile(null);setError('');setBusy(false);lock.current=false;
  return()=>{epoch.current++;lock.current=false};
 },[projectPath,domain,entity]);
 const read=async()=>{
  if(lock.current||!projectPath||domain==='motionweave'&&!/^[0-9]{1,20}$/.test(entity))return;
  lock.current=true;setBusy(true);setError('');setSnapshot(null);
  const token=epoch.current;
  try{
   const r=(await invoke('studio.op',{op:'graph_live_snapshot',params:{domain,...(domain==='motionweave'?{entity,captureHistory}:{})}})).result;
   if(token!==epoch.current)return;
   if(!object(r)||r.domain!==domain||typeof r.available!=='boolean')throw new Error('Invalid live snapshot response');
   setSnapshot(r);
  }catch(e){if(token===epoch.current)setError(e instanceof Error?e.message:String(e))}
  finally{if(token===epoch.current){lock.current=false;setBusy(false)}}
 };
 const watchAction=async(action:string)=>{
  if(lock.current||!projectPath)return;
  lock.current=true;setBusy(true);setError('');
  const token=epoch.current;
  try{
   const r=(await invoke('studio.op',{op:'graph_live_watch',params:{action,...(action==='start'?{domain,entity:domain==='spectrafx'?'preview':entity}:{})}})).result;
   if(token!==epoch.current)return;
   if(!object(r)||typeof r.available!=='boolean')throw new Error('Invalid watch response');
   setWatch(r);setFrame(Math.max(0,Array.isArray(r.frames)?r.frames.length-1:0));
  }catch(e){if(token===epoch.current)setError(e instanceof Error?e.message:String(e))}
  finally{if(token===epoch.current){lock.current=false;setBusy(false)}}
 };
 const profileAction=async(action:string)=>{
  if(lock.current||!projectPath||! /^[0-9]{1,20}$/.test(entity))return;
  lock.current=true;setBusy(true);setError('');const token=epoch.current;
  try{
   const r=(await invoke('studio.op',{op:'graph_live_profile',params:{action,entity}})).result;
   if(token!==epoch.current)return;
   if(!object(r)||typeof r.available!=='boolean')throw new Error('Invalid live profile response');
   setProfile(r);
  }catch(e){if(token===epoch.current)setError(e instanceof Error?e.message:String(e))}
  finally{if(token===epoch.current){lock.current=false;setBusy(false)}}
 };
 const frames=Array.isArray(watch?.frames)?watch.frames.filter(object):[];
 return <StudioSection title="Live owner snapshot">
  <StudioAlert>Read-only. The native host must publish the owner. A snapshot is not attached to the edited graph asset, and does not pause gameplay.</StudioAlert>
  {domain==='motionweave'&&<StudioInput aria-label="Packed entity ID" placeholder="Exact decimal packed entity ID" value={entity} disabled={busy} onChange={e=>setEntity(e.target.value)}/>}
  {domain==='motionweave'&&<label className="au-row"><input type="checkbox" className="au-checkbox__input" checked={captureHistory} disabled={busy} onChange={e=>setCaptureHistory(e.target.checked)}/> Capture read into native owner history</label>}
  <StudioButton size="sm" disabled={busy||!projectPath||domain==='motionweave'&&!/^[0-9]{1,20}$/.test(entity)} onClick={()=>void read()}>Read live snapshot</StudioButton>
  <StudioSection title="Native owner timeline">
   <small>One subscription per native editor. Captures published visible play-view frames only; replacement stops the watch. Refresh reads the bounded history.</small>
   <div className="au-row" style={{flexWrap:'wrap'}}><StudioButton size="sm" disabled={busy||!projectPath||domain==='motionweave'&&!/^[0-9]{1,20}$/.test(entity)} onClick={()=>void watchAction('start')}>Watch {domain==='spectrafx'?'CPU preview':'entity'}</StudioButton><StudioButton size="sm" disabled={busy} onClick={()=>void watchAction('status')}>Refresh timeline</StudioButton><StudioButton size="sm" disabled={busy} onClick={()=>void watchAction('stop')}>Stop watch</StudioButton></div>
   {watch&&<small>{String(watch.domain??'')} · {String(watch.reason??'')} · {frames.length} samples</small>}
   {frames.length>0&&<><StudioSelect aria-label="Timeline sample" value={String(frame)} onChange={e=>setFrame(Number(e.target.value))}>{frames.map((f,i)=><option key={i} value={i}>{String(f.sequence)} · {String(f.state??f.particles??'')}</option>)}</StudioSelect><pre>{JSON.stringify(frames[frame],null,2)}</pre></>}
  </StudioSection>
  {domain==='motionweave'&&<StudioSection title="Live CPU node profile">
   <small>One selected published common ECS instance. Timings include instrumentation overhead; no GPU or draft graph mapping.</small>
   <div className="au-row" style={{flexWrap:'wrap'}}>{(['enable','read','reset','disable'] as const).map(action=><StudioButton key={action} size="sm" disabled={busy||!projectPath||! /^[0-9]{1,20}$/.test(entity)} onClick={()=>void profileAction(action)}>{action}</StudioButton>)}</div>
   {profile&&<>{profile.available===false?<small>{String(profile.reason??'No matching active profile')}</small>:<pre style={{maxHeight:320,overflow:'auto'}}>{JSON.stringify(profile,null,2)}</pre>}</>}
  </StudioSection>}
  {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
  {snapshot&&<>{snapshot.available===false?<StudioAlert>{String(snapshot.reason??'Owner unavailable')}</StudioAlert>:<pre>{JSON.stringify(snapshot,null,2)}</pre>}</>}
 </StudioSection>;
}
