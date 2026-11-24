import { useEffect, useState } from 'react';
import { invoke } from '../bridge';
import type { WorldProcessDiagnostics as Snapshot, WorldProcessDiagnosticLane } from '../types';
import './WorldProcessDiagnostics.css';

const systems=['evolve','crowd','society'] as const;
function Lane({name,lane,tick}:{name:string;lane:WorldProcessDiagnosticLane;tick:number}){
  if(!lane.available)return <section className="wpdiag__lane"><strong>{name.toUpperCase()}</strong><small role="alert">{lane.error}</small></section>;
  const total=lane.hot+lane.warm+lane.cold;
  return <section className="wpdiag__lane">
    <header><strong>{name.toUpperCase()}</strong><span>{lane.mode} · {lane.selected}/{lane.budget} visits</span></header>
    <div className="wpdiag__bar" aria-label={`Hot ${lane.hot}, warm ${lane.warm}, cold ${lane.cold}`}>
      {(['hot','warm','cold'] as const).map(key=><span key={key} className={'wpdiag__'+key}
        style={{flexGrow:lane[key],display:lane[key]?'block':'none'}}/>)}
    </div>
    <div className="wpdiag__counts"><span>Hot {lane.hot}</span><span>Warm {lane.warm}</span><span>Cold {lane.cold}</span><span>Total {total}</span></div>
    <div className="wpdiag__counts"><span>Phase due {lane.executed}</span><span>Phase deferred {lane.deferred}</span>
      <span>Pending {lane.pending}</span><span>Urgent {lane.urgent}</span><span>Urgent served {lane.urgentSelected}</span>
      <span>Oldest wait {lane.oldestPendingTicks} ticks</span></div>
    <details><summary>Recent batch and pending IDs</summary>
      <small>Selected {lane.selected} · showing up to 32 of each</small>
      <div className="wpdiag__rows">{lane.samples.map(row=><div key={'sample:'+row.id}>
        <code>{row.id}</code><span>Sample {row.sampleTick} · waited {row.sampleTick-row.requestedTick} ticks</span>
      </div>)}{lane.waiting.map(row=><div key={'waiting:'+row.id}>
        <code>{row.id}</code><span>{row.urgent?'Urgent':'Pending'} · {tick-row.requestedTick} ticks</span>
      </div>)}</div>
    </details>
  </section>;
}
export function WorldProcessDiagnostics(){
  const [open,setOpen]=useState(false);
  const [budget,setBudget]=useState('64');
  const [focus,setFocus]=useState('');
  const [settings,setSettings]=useState({budget:64,focus:''});
  const [state,setState]=useState<Snapshot|null>(null);
  const [error,setError]=useState('');
  useEffect(()=>{
    if(!open)return;
    let alive=true,busy=false;
    setState(null);setError('');
    const refresh=async()=>{
      if(busy||document.hidden)return;
      busy=true;
      try{const result=await invoke('studio.worldProcessDiagnostics',settings);
        if(alive){setState(result);setError('');}}
      catch(e){if(alive){setState(null);setError(e instanceof Error?e.message:String(e));}}
      finally{busy=false;}
    };
    void refresh();const timer=window.setInterval(refresh,500);
    return()=>{alive=false;window.clearInterval(timer);};
  },[open,settings]);
  const parsed=Number(budget);
  const valid=budget.trim()!==''&&Number.isInteger(parsed)&&parsed>=0&&parsed<=512&&
    (!focus.trim()||/^\d{1,20}$/.test(focus.trim()));
  return <details className="wpdiag" open={open} onToggle={e=>setOpen(e.currentTarget.open)}>
    <summary>Simulation LOD · diagnostics</summary>
    {open&&<div className="wpdiag__body">
      <small>Local Play mode · advisory reports only. Does not change simulation settings or authoritative work.</small>
      <form className="wpdiag__form" onSubmit={e=>{e.preventDefault();if(valid)setSettings({budget:parsed,focus:focus.trim()});}}>
        <label>Visits / system / sampled tick<input className="au-input" type="number" min={0} max={512} value={budget} onChange={e=>setBudget(e.target.value)}/></label>
        <label>Focus entity ref (optional)<input className="au-input" inputMode="numeric" maxLength={20} value={focus} onChange={e=>setFocus(e.target.value)} placeholder="Runtime entity ref"/></label>
        <button type="submit" disabled={!valid}>Apply preview</button>
      </form>
      <small>Use a live entity reference from the current Play session. Without a valid transform, all work fails HOT. No camera is used.</small>
      {error?<small role="alert">{error}</small>:!state?<small>Reading local diagnostics…</small>:!state.active?<small>Start a local Play match.</small>:<>
        <small>Tick {state.tick} · epoch {state.epoch} · {state.cellSizeCm/100}m cells · {state.observerAvailable?'focus resolved':'no focus — HOT fallback'}</small>
        <small>Preview cadence: 1 / {state.config.warmTicks} / {state.config.coldTicks} ticks. Samples follow editor polling, not every simulation tick.</small>
        {systems.map(name=><Lane key={name} name={name} lane={state.lanes[name]} tick={state.tick}/>)}
      </>}
    </div>}
  </details>;
}
