import {useState,useEffect,useRef} from 'react';
import {invoke} from '../bridge';
import {StudioAlert,StudioButton,StudioSection,StudioSelect,StudioTextarea} from '../ui';
import GraphCanvas from './GraphCanvas';
import {animationProjection,object} from './nativeGraphAdapters';
import type {GraphObject} from './nativeGraphAdapters';

const phases=['sync','update','evaluate'] as const;
type Phase=typeof phases[number];
export default function GraphExecutionProfilePanel({session,step,source,disabled}:{session:string;step:number;source:GraphObject;disabled:boolean}){
 const [report,setReport]=useState<GraphObject|null>(null),[error,setError]=useState(''),[busy,setBusy]=useState(false);
 const [phase,setPhase]=useState<Phase>('evaluate'),[showJson,setShowJson]=useState(false);
 const epoch=useRef(0),lock=useRef(false);
 useEffect(()=>{epoch.current++;lock.current=false;setReport(null);setError('');setBusy(false);
 return()=>{epoch.current++;lock.current=false};},[session]);
 const run=async(action:string)=>{
  if(lock.current||disabled||!session)return;lock.current=true;setBusy(true);setError('');
  const token=epoch.current;
  try{
   const r=(await invoke('studio.op',{op:'graph_debug_session',params:{action,session}})).result;
   if(token!==epoch.current)return;
   if(!object(r)||r.session!==session||typeof r.available!=='boolean')throw new Error('Invalid profile response');
   setReport(r);
  }catch(e){if(token===epoch.current)setError(e instanceof Error?e.message:String(e))}
  finally{if(token===epoch.current){lock.current=false;setBusy(false)}}
 };
 const rows=(Array.isArray(report?.nodes)?report.nodes:[]).filter(object).flatMap(n=>{
  const c=n[phase];if(typeof n.node!=='string'||!object(c)||typeof c.totalUs!=='number'||!Number.isFinite(c.totalUs)||c.totalUs<0)return [];
  return [{node:n.node,total:c.totalUs,max:typeof c.maxUs==='number'?c.maxUs:0,calls:String(c.calls??'0')}];
 }).sort((a,b)=>b.total-a.total||a.node.localeCompare(b.node));
 const max=rows[0]?.total??0;
 const heat=Object.fromEntries(rows.map(r=>[r.node,max>0?r.total/max:0]));
 const events=(Array.isArray(report?.events)?report.events:[]).filter(object);
 const latest=report?.step===step;
 return <StudioSection title="MotionWeave CPU node profiler">
  <StudioAlert>Opt-in inclusive CPU scopes. Child graph and rig work is included in the parent node. No GPU or live gameplay timings. Enable before stepping; read after stepping.</StudioAlert>
  <div className="au-row" style={{flexWrap:'wrap'}}>
   {(['profileEnable','profileRead','profileReset','profileDisable'] as const).map((action,i)=><StudioButton key={action} size="sm" disabled={disabled||busy} onClick={()=>void run(action)}>{['Enable / reset','Read profile','Clear counters','Disable'][i]}</StudioButton>)}
  </div>
  {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
  {report?.available===false&&<small>Profiling disabled.</small>}
  {report?.available===true&&<>
   {!latest&&<StudioAlert tone="warning">Report is from step {String(report.step)}. Read again for current step.</StudioAlert>}
   <StudioSelect aria-label="Profile phase" value={phase} onChange={e=>setPhase(e.target.value as Phase)}>{phases.map(p=><option key={p}>{p}</option>)}</StudioSelect>
   <small>Cumulative CPU time since enabling/resetting; heat intensity relative to the most expensive measured node in this phase.</small>
   <div style={{height:260,display:'flex',flexDirection:'column'}}><GraphCanvas view={animationProjection(source,'pose')} heat={heat} selected="" label="CPU node profile heatmap" disabled onSelect={()=>{}} onMove={()=>{}}/></div>
   {rows.map(row=><div className="au-row" key={row.node} style={{justifyContent:'space-between',flexWrap:'wrap'}}><span>{row.node}</span><small>{row.calls} calls · total {row.total.toFixed(3)} µs · max {row.max.toFixed(3)} µs</small></div>)}
   <StudioSection title="Latest step trace">
    <small>{events.length} completed scope events · Dropped {String(report.droppedEvents??'0')}</small>
    <div style={{maxHeight:240,overflow:'auto'}}>{events.map((e,i)=><div key={i} className="au-row"><small>{String(e.startUs)} µs</small><span>{String(e.node)} / {String(e.phase)}</span><small>{String(e.durationUs)} µs</small></div>)}</div>
   </StudioSection>
   <StudioButton size="sm" onClick={()=>setShowJson(v=>!v)}>{showJson?'Hide report JSON':'Show exportable report JSON'}</StudioButton>
   {showJson&&<StudioTextarea aria-label="Profile report JSON" readOnly rows={10} value={JSON.stringify(report,null,2)}/>}
  </>}
 </StudioSection>;
}
