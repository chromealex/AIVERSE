import {useEffect,useRef,useState} from 'react';
import {invoke} from '../bridge';
import {StudioAlert,StudioButton,StudioInput,StudioSection,StudioSelect} from '../ui';
import GraphCanvas from './GraphCanvas';
import GraphExecutionProfilePanel from './GraphExecutionProfilePanel';
import {animationProjection,object} from './nativeGraphAdapters';
import type {GraphObject} from './nativeGraphAdapters';

export default function GraphDebugSessionPanel({domain,graph,projectPath,disabled}:{domain:'motionweave'|'spectrafx';graph:GraphObject;projectPath:string;disabled:boolean}){
 const [report,setReport]=useState<GraphObject|null>(null),[source,setSource]=useState<GraphObject|null>(null);
 const [busy,setBusy]=useState(false),[error,setError]=useState(''),[state,setState]=useState(''),[node,setNode]=useState(''),[frame,setFrame]=useState(0);
 const epoch=useRef(0),lock=useRef(false);
 useEffect(()=>{epoch.current++;setReport(null);setSource(null);setError('');setBusy(false);lock.current=false;
  return()=>{epoch.current++;lock.current=false};
 },[projectPath,domain]);
 const run=async(action:string,extra:GraphObject={})=>{
  if(lock.current||disabled||!projectPath)return;
  lock.current=true;setBusy(true);setError('');
  const token=epoch.current;
  try{
   const params={action,...(action==='start'?{domain,graph}:action==='status'?{}:{session:report?.session}),...extra};
   const r=(await invoke('studio.op',{op:'graph_debug_session',params})).result;
   if(token!==epoch.current)return;
   if(!object(r)||r.ok!==true||typeof r.active!=='boolean'||r.liveAttached!==false||r.gpuMeasured!==false)throw new Error(object(r)?String(r.error??'Invalid debugger response'):'Debugger unavailable');
   setReport(r);if(action==='start')setSource(graph);else if(!r.active)setSource(null);
   else if(r.domain!==domain||r.session!==report?.session)setSource(null);
   setFrame(Math.max(0,Array.isArray(r.frames)?r.frames.length-1:0));
  }catch(e){if(token===epoch.current)setError(e instanceof Error?e.message:String(e))}
  finally{if(token===epoch.current){lock.current=false;setBusy(false)}}
 };
 const active=report?.active===true;
 const own=active&&report?.domain===domain&&source!==null;
 const frames=Array.isArray(report?.frames)?report.frames.filter(object):[];
 const current=frames[frame];
 return <StudioSection title="Isolated CPU debug session">
  <StudioAlert>One session per native Workspace. Explicit advancement only; gameplay is not attached or paused. Breakpoints stop after a matching update, not before a node executes.</StudioAlert>
  <div className="au-row" style={{flexWrap:'wrap'}}>
   <StudioButton size="sm" disabled={disabled||busy||active} onClick={()=>void run('start')}>Start</StudioButton>
   <StudioButton size="sm" disabled={disabled||busy} onClick={()=>void run('status')}>Read session</StudioButton>
   <StudioButton size="sm" disabled={disabled||busy||!own} onClick={()=>void run('step')}>Step</StudioButton>
   <StudioButton size="sm" disabled={disabled||busy||!own} onClick={()=>void run('continue',{steps:30})}>Continue 30 steps</StudioButton>
   <StudioButton size="sm" disabled={disabled||busy||!active} onClick={()=>void run('stop')}>Stop</StudioButton>
  </div>
  {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
  {active&&source!==graph&&<StudioAlert tone="warning">Session is an immutable graph snapshot. Stop and restart to use the current draft. A session read from another panel cannot be mapped to this draft.</StudioAlert>}
  {own&&domain==='motionweave'&&<>
   <StudioInput aria-label="State breakpoint" placeholder="State ID (optional)" value={state} onChange={e=>setState(e.target.value)}/>
   <StudioInput aria-label="Program breakpoint" placeholder="Pose node ID (optional)" value={node} onChange={e=>setNode(e.target.value)}/>
   <StudioButton size="sm" disabled={disabled||busy} onClick={()=>void run('breakpoints',{states:state?[state]:[],nodes:node?[node]:[]})}>Set / clear breakpoints</StudioButton>
  </>}
  {report&&<small>Session {String(report.session??'—')} · Step {String(report.step??0)} · {String(report.reason??'')}</small>}
  {frames.length>0&&<StudioSelect aria-label="Debug history frame" value={String(frame)} onChange={e=>setFrame(Number(e.target.value))}>{frames.map((f,i)=><option key={i} value={i}>Step {String(f.sequence)}</option>)}</StudioSelect>}
  {current&&own&&source&&domain==='motionweave'&&<div style={{height:260,display:'flex',flexDirection:'column'}}><GraphCanvas view={animationProjection(source,'pose')} highlighted={Array.isArray(current.activeProgram)?current.activeProgram.filter((id):id is string=>typeof id==='string'):[]} selected="" label="Isolated debugger pose program" disabled onSelect={()=>{}} onMove={()=>{}}/></div>}
  {current&&<pre>{JSON.stringify(current,null,2)}</pre>}
  {own&&source&&domain==='motionweave'&&typeof report?.session==='string'&&<GraphExecutionProfilePanel session={report.session} step={Number(report.step??0)} source={source} disabled={disabled||busy}/> }
 </StudioSection>;
}
