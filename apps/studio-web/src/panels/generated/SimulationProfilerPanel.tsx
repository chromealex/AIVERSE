import { useEffect, useRef, useState } from 'react';
import type { StudioGameProfile, StudioSnapshot } from '../../types';
import { invoke, nativeConnected } from '../../bridge';
import { StudioPanel, StudioSection, StudioButton, StudioBadge, StudioAlert, StudioEmptyState, StudioProgress } from '../../ui';
import './SimulationProfilerPanel.css';

type Action='status'|'start'|'stop';
const ms=(n:number)=>n.toFixed(3)+' ms';
function Profiler({projectPath}:{projectPath:string}){
  const [data,setData]=useState<StudioGameProfile|null>(null),[error,setError]=useState('');
  const [pending,setPending]=useState(false);
  const generation=useRef(0),lock=useRef(false);
  const connected=nativeConnected();
  async function read(action:Action){
    if(lock.current||!projectPath||!connected)return;
    lock.current=true;const token=generation.current;
    if(action!=='status')setPending(true);
    try{
      const result=await invoke('studio.gameProfile',{expectedProject:projectPath,action,...(action==='start'?{steps:60}:{})});
      if(token===generation.current){setData(result);setError('');}
    }catch(e){if(token===generation.current){setData(null);setError(e instanceof Error?e.message:String(e));}}
    finally{if(token===generation.current){lock.current=false;setPending(false);}}
  }
  useEffect(()=>{
    ++generation.current;lock.current=false;
    void read('status');
    const timer=window.setInterval(()=>void read('status'),750);
    return ()=>{++generation.current;lock.current=false;window.clearInterval(timer);};
  },[projectPath,connected]);
  const d=data?.available?data:null,history=d?.historyMs??[],n=history.length;
  const budget=d&&d.tickRate>0?1000/d.tickRate:0;
  const peak=Math.max(0,...history),top=Math.max(.001,budget,peak);
  const points=history.map((v,i)=>[10+i*280/Math.max(1,(d?.target??n)-1),90-80*v/top]);
  return <StudioPanel title="Simulation Profiler" eyebrow="RUNTIME / TIMING" subtitle="Local Game · wall time"
    actions={<StudioBadge>{d?.active?'Capturing':n?'Captured':'Idle'}</StudioBadge>}>
    <div className="au-stack simulation-profiler">
      {!projectPath?<StudioEmptyState title="Open a project"/>:!connected?<StudioEmptyState title="Native Studio required"/>:
        data&&!data.available?<StudioEmptyState title={data.reason}/>:!data&&!error?<StudioBadge>Checking local game…</StudioBadge>:null}
      {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
      {d&&<>
        <div className="simulation-profiler-actions">
          <StudioButton variant="primary" disabled={pending||d.active} onClick={()=>void read('start')}>Capture 60 ticks</StudioButton>
          <StudioButton disabled={pending||!d.active} onClick={()=>void read('stop')}>Stop capture</StudioButton>
          <StudioButton disabled={pending} onClick={()=>void read('status')}>Refresh</StudioButton>
        </div>
        {d.target>0&&<StudioProgress label={'Completed steps '+n+' / '+d.target} value={n} max={d.target}/>}
        {d.active&&d.paused&&<StudioAlert>Game paused. Resume or step to advance capture; Stop retains the partial sample.</StudioAlert>}
        {!n?<StudioEmptyState title="No completed sample" description="Capture while a local match advances."/>:<>
          <StudioSection title="Tick timings">
            <div className="simulation-profiler-summary">
              <div>Mean <strong>{ms(d.totalMs/n)}</strong></div><div>Peak <strong>{ms(peak)}</strong></div>
              <div>Nominal tick interval <strong>{ms(budget)}</strong></div><div>Systems + flush <strong>{ms(d.systemsMs/n)} / step</strong></div>
            </div>
            <svg className="simulation-profiler-chart" viewBox="0 0 300 100" role="img" aria-label="Measured completed tick wall times; dashed line is nominal tick interval">
              <line x1="10" x2="290" y1={90-80*budget/top} y2={90-80*budget/top} className="simulation-profiler-budget"/>
              <polyline points={points.map(p=>p.join(',')).join(' ')} className="simulation-profiler-trace"/>
              {points.map(([x,y],i)=><circle key={i} cx={x} cy={y} r="2" className="simulation-profiler-dot"><title>Step {i+1}: {ms(history[i])}</title></circle>)}
            </svg>
            <small>Sample tick {d.tick} · epoch {d.epoch}. Wall time includes commands and ECS flush, excludes readiness check, rendering and idle time. Nominal interval is not a measured CPU budget.</small>
          </StudioSection>
          <StudioSection title="Systems (average per completed step)">
            {d.systemCount>d.rows.length&&<StudioAlert tone="warning">Showing {d.rows.length} of {d.systemCount} systems in schedule order before sorting. Totals cover all systems.</StudioAlert>}
            <div className="simulation-profiler-rows">
              {[...d.rows].sort((a,b)=>b.totalMs-a.totalMs||a.index-b.index).map(row=><div key={row.index} className="simulation-profiler-system">
                <div><span>{row.name}</span><strong>{ms(row.totalMs/n)}</strong></div>
                <StudioProgress label={row.name+' share of measured tick wall time'} value={row.totalMs} max={Math.max(d.totalMs,.001)}/>
              </div>)}
            </div>
            <small>Includes each system's ECS flush. Per-system entity counts and worker utilization are not instrumented.</small>
          </StudioSection>
        </>}
      </>}
    </div>
  </StudioPanel>;
}
export default function SimulationProfilerPanel({snapshot}:{snapshot:StudioSnapshot|null}){
  const projectPath=snapshot?.project.path??'';
  return <Profiler key={projectPath} projectPath={projectPath}/>;
}
