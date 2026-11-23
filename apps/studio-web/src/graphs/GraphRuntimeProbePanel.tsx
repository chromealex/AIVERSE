import {useEffect,useRef,useState} from 'react';
import {invoke} from '../bridge';
import {StudioAlert,StudioButton,StudioInput,StudioSection,StudioSelect} from '../ui';
import {object} from './nativeGraphAdapters';
import type {GraphObject} from './nativeGraphAdapters';

export default function GraphRuntimeProbePanel({domain,graph,disabled}:{domain:'motionweave'|'spectrafx';graph:GraphObject;disabled:boolean}){
 const [steps,setSteps]=useState(30),[busy,setBusy]=useState(false),[error,setError]=useState('');
 const [sample,setSample]=useState<{graph:GraphObject;domain:string;report:GraphObject}|null>(null);
 const [frame,setFrame]=useState(0),epoch=useRef(0),lock=useRef(false);
 useEffect(()=>()=>{epoch.current++;lock.current=false},[]);
 const run=async()=>{
  if(lock.current||disabled||!Number.isInteger(steps)||steps<1||steps>60)return;
  lock.current=true;setBusy(true);setError('');setSample(null);
  const token=epoch.current;
  try{
   const r=(await invoke('studio.op',{op:'graph_runtime_probe',params:{domain,graph,steps}})).result;
   if(token!==epoch.current)return;
   if(!object(r)||r.ok!==true||r.domain!==domain||r.liveAttached!==false||r.gpuMeasured!==false||!Array.isArray(r.frames)||r.frames.length>60||!r.frames.every(object))
    throw new Error(object(r)?JSON.stringify(r.errors??r.error??'Unexpected native probe response'):'Probe unavailable');
   setSample({graph,domain,report:r});setFrame(Math.max(0,r.frames.length-1));
  }catch(e){if(token===epoch.current)setError(e instanceof Error?e.message:String(e))}
  finally{if(token===epoch.current){lock.current=false;setBusy(false)}}
 };
 const stale=sample&&(sample.graph!==graph||sample.domain!==domain);
 const r=sample&&!stale?sample.report:null;
 const frames=r&&Array.isArray(r.frames)?r.frames.filter(object):[];
 const current=frames[frame];
 return <StudioSection title="Runtime CPU probe">
  <StudioAlert>Isolated test instance, fixed 1/60 s steps. Not live game telemetry or GPU profiling. No project files written.</StudioAlert>
  <div className="au-row">
   <StudioInput aria-label="Probe steps" type="number" min="1" max="60" value={steps} disabled={busy||disabled} onChange={e=>setSteps(Number(e.target.value))}/>
   <StudioButton size="sm" disabled={busy||disabled||!Number.isInteger(steps)||steps<1||steps>60} onClick={()=>void run()}>Run CPU probe</StudioButton>
  </div>
  {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
  {stale&&<StudioAlert tone="warning">Graph changed; run probe again.</StudioAlert>}
  {r&&<>
   <small>{String(r.completedSteps)} completed steps · Setup {String(r.setupMs)} ms · CPU updates {String(r.updateMs)} ms</small>
   {domain==='motionweave'&&<small>Pose evaluation: {String(r.evaluateMs)} ms · Healthy: {String(r.healthy)} · {String(r.errorMessage??'')}</small>}
   {frames.length>0&&<StudioSelect aria-label="Probe step" value={String(frame)} onChange={e=>setFrame(Number(e.target.value))}>{frames.map((f,i)=><option key={i} value={i}>Step {String(f.step)}</option>)}</StudioSelect>}
   {current&&<pre>{JSON.stringify(current,null,2)}</pre>}
   {Array.isArray(r.stateChanges)&&<><small>Observed state changes</small><pre>{JSON.stringify(r.stateChanges,null,2)}</pre></>}
   <small>{String(r.note??'')}</small>
  </>}
 </StudioSection>;
}
