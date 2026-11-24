import {useEffect,useRef,useState} from 'react';
import {invoke} from '../bridge';
import {StudioAlert,StudioButton,StudioSection} from '../ui';
import {object} from './nativeGraphAdapters';
import type {GraphObject} from './nativeGraphAdapters';

export default function GraphGpuProfilePanel({projectPath}:{projectPath:string}){
 const [report,setReport]=useState<GraphObject|null>(null),[busy,setBusy]=useState(false),[error,setError]=useState('');
 const epoch=useRef(0),lock=useRef(false);
 useEffect(()=>{epoch.current++;lock.current=false;setReport(null);setBusy(false);setError('');
  return()=>{epoch.current++;lock.current=false};},[projectPath]);
 const run=async(action:'start'|'read'|'stop')=>{
  if(lock.current||!projectPath)return;
  lock.current=true;setBusy(true);setError('');const token=epoch.current;
  try{
   const r=(await invoke('studio.op',{op:'graph_gpu_profile',params:{action}})).result;
   if(token!==epoch.current)return;
   if(!object(r)||typeof r.available!=='boolean')throw new Error('Invalid GPU profile response');
   setReport(r);
  }catch(e){if(token===epoch.current)setError(e instanceof Error?e.message:String(e))}
  finally{if(token===epoch.current){lock.current=false;setBusy(false)}}
 };
 const samples=(Array.isArray(report?.samples)?report.samples:[]).filter(object).filter(s=>typeof s.milliseconds==='number'&&Number.isFinite(s.milliseconds)&&s.milliseconds>0);
 const max=Math.max(0,...samples.map(s=>Number(s.milliseconds)));
 return <StudioSection title="Native GPU scope profiler">
  <StudioAlert>One editor-wide capture. Real completed RHI queries only; viewport timing includes multiple passes. Texture Graph samples identify the request and output, not individual nodes. Dispatch/copy scopes exclude CPU validation and diagnostic readback.</StudioAlert>
  <div className="au-row" style={{flexWrap:'wrap'}}>{(['start','read','stop'] as const).map(action=><StudioButton size="sm" key={action} disabled={busy||!projectPath} onClick={()=>void run(action)}>{action}</StudioButton>)}</div>
  {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
  {report&&<>
   <small>{String(report.backend??'')} · {String(report.device??'')} · {report.enabled===true?'Capturing':'Stopped'}</small>
   {report.available===false&&<StudioAlert>{String(report.reason??(report.scopeBackendImplemented===false?'Scoped query backend unavailable':'No completed valid samples yet'))}</StudioAlert>}
   <small>Accepted {String(report.accepted??'—')} · Pending {String(report.pending??'—')} · Rejected {String(report.rejected??'—')} · Invalid/expired {String(report.invalid??'—')}</small>
   <div style={{maxHeight:320,overflow:'auto'}}>{samples.map((s,i)=><div key={String(s.tag??i)} className="au-stack"><small>{String(s.scope)}{typeof s.request==='string'?' · request '+s.request+' / '+String(s.output??''):''} · frame {String(s.submittedFrame)} · {Number(s.milliseconds).toFixed(3)} ms</small><div style={{height:5,width:(100*Number(s.milliseconds)/max)+'%',background:'var(--au-accent)'}}/></div>)}</div>
   <small>{String(report.note??'')}</small>
  </>}
 </StudioSection>;
}
