import './LightBakePanel.css';
import { LightBakeAtlas, type AtlasPage } from './LightBakeAtlas';
import { useEffect, useState } from 'react';
import { invoke, nativeConnected, onNativeEvent } from '../bridge';
import type { StudioGraphDocumentScope } from '../types';
import { StudioAlert, StudioButton, StudioCheckbox, StudioField, StudioInput, StudioPanel, StudioSection, StudioSelect } from '../ui';

type Settings = Record<string, string | number | boolean>;
interface BakeInfo { hash?:string; atlasPages?:AtlasPage[]; exists: boolean; folder?: string; mode?: string; stats?: Record<string, number> }
interface BakeState { settings: Settings; bake: BakeInfo }

const sampling = [
  ['texelsPerUnit', 'Texels per meter', .5, 32, .1], ['samples', 'Samples per texel', 4, 1024, 1],
  ['bounces', 'Light bounces', 0, 6, 1], ['aoDistance', 'Ambient occlusion distance', 0, 5, .05],
  ['probeSpacing', 'Probe spacing', 0, 8, .1],
] as const;
const advanced = [
  ['pageSize','Atlas page size',64,8192,1], ['aoStrength','Ambient occlusion strength',0,8,.01],
  ['indirectIntensity','Indirect intensity',0,16,.01], ['maskSamples','Shadow mask samples',1,1024,1],
  ['probeHeight','Probe height',.1,1000,.1], ['probeSamples','Samples per probe',16,16384,1],
  ['padding','Atlas padding',1,16,1],
] as const;

export default function LightBakePanel({scope}:{scope?:StudioGraphDocumentScope}) {
  const [state,setState]=useState<BakeState>();
  const [patch,setPatch]=useState<Settings>({});
  const [busy,setBusy]=useState(false);
  const [job,setJob]=useState<{running:boolean; stage:string; fraction:number; message:string}>();
  const [force,setForce]=useState(false);
  const [error,setError]=useState('');
  const [display,setDisplay]=useState<boolean>();
  const [outputRevision,setOutputRevision]=useState(0);
  const [confirmClear,setConfirmClear]=useState(false);
  const [revision,refresh]=useState(0);
  const project=scope?.projectPath, epoch=scope?.epoch;
  const enabled=Boolean(nativeConnected() && project && scope?.mapPath);
  useEffect(()=>onNativeEvent(event=>{
    if(event==='document.changed' || event==='workspace.changed')refresh(n=>n+1);
  }),[]);
  useEffect(()=>{
    if(!enabled)return;
    let active=true;
    void invoke('studio.op',{op:'lightbake_get',params:{},expectedProject:project,expectedDocumentEpoch:epoch})
      .then(reply=>{if(active)setState(reply.result as BakeState);})
      .catch(reason=>{if(active)setError(String(reason));});
    return ()=>{active=false;};
  },[enabled,project,epoch,revision]);

  useEffect(()=>{
    if(!enabled)return;
    let active=true;
    const read=()=>void invoke('studio.lightBakeDisplay',{}).then(r=>{if(active)setDisplay(r.enabled);})
      .catch(reason=>{if(active)setError(String(reason));});
    read();
    const stop=onNativeEvent(event=>{if(event==='lightBake.displayChanged')read();});
    return ()=>{active=false;stop();};
  },[enabled]);
  useEffect(()=>{
    setJob(undefined);
    if(!enabled || !epoch)return;
    let active=true;
    let sequence=0;
    let timer:ReturnType<typeof setTimeout>;
    const poll=async()=>{
      const request=++sequence;
      clearTimeout(timer);
      try {
        const reply=await invoke('studio.lightBakeJob',{action:'status',expectedDocumentEpoch:epoch});
        if(active && request===sequence){setJob(reply);if(reply.running)timer=setTimeout(poll,250);}
      } catch(reason){if(active)setError(String(reason));}

    };
    void poll();
    const stop=onNativeEvent((event,scope)=>{
      if(scope!==epoch)return;
      if(event==='lightBake.started' || event==='lightBake.finished')void poll();
      if(event==='lightBake.finished'){
        refresh(n=>n+1);setOutputRevision(n=>n+1);
      }
    });
    return ()=>{active=false;clearTimeout(timer);stop();};
  },[enabled,epoch]);
  const runJob=async(action:'start'|'cancel')=>{
    if(!epoch || busy)return;
    setBusy(true);setError('');
    try {setJob(await invoke('studio.lightBakeJob',{action,force,expectedDocumentEpoch:epoch}));}
    catch(reason){setError(String(reason));}
    finally {setBusy(false);}
  };
  const write=async(op:string,params:Settings)=>{
    if(busy || !enabled)return;
    setBusy(true);setError('');
    try {
      await invoke('studio.op',{op,params,expectedProject:project,expectedDocumentEpoch:epoch});
      if(op==='lightbake_set')setPatch({});
      setConfirmClear(false);refresh(n=>n+1);
    } catch(reason){setError(String(reason));}
    finally {setBusy(false);}
  };
  const values={...state?.settings,...patch};
  const change=(key:string,value:string|number|boolean)=>setPatch(previous=>{
    const next={...previous,[key]:value};
    if(value===state?.settings[key])delete next[key];
    return next;
  });
  const dirty=Object.keys(patch).length>0;
  return <StudioPanel className="light-bake" title="Light Bake" subtitle={scope?.mapPath?.split('/').pop()}>
    {error && <StudioAlert tone="danger">{error}</StudioAlert>}
    {!enabled ? <StudioAlert>Open a saved map to configure baked lighting.</StudioAlert> : !state ?
      <StudioAlert>Reading bake settings…</StudioAlert> : <>
      <StudioSection title="Light transport">
        <StudioField label="Bake mode" htmlFor="bake-mode"><StudioSelect id="bake-mode" disabled={busy}
          value={String(values.mode)} onChange={e=>change('mode',e.target.value)}>
          <option value="indirect">Indirect only · realtime sun</option><option value="shadowmask">Shadowmask · baked sun shadows</option>
          <option value="subtractive">Subtractive · baked sunlight</option>
        </StudioSelect></StudioField>
        <StudioField label="Quality preset" htmlFor="bake-quality"><StudioSelect id="bake-quality" value="" disabled={busy || job?.running || dirty}
          onChange={e=>void write('lightbake_set',{quality:e.target.value})}>
          <option value="" disabled>Apply a preset…</option>{['Preview','Medium','High'].map(p=><option key={p} value={p.toLowerCase()}>{p}</option>)}
        </StudioSelect></StudioField>
      </StudioSection>
      <StudioSection title="Sampling">
        {sampling.map(([key,label,min,max,step])=><StudioField key={key} label={label} htmlFor={'bake-'+key}>
          <div className="bake-range"><input id={'bake-'+key} type="range" value={Number(values[key])}
            min={Math.min(min,Number(state.settings[key]))} max={Math.max(max,Number(state.settings[key]))} step={step}
            disabled={busy} onChange={e=>change(key,e.target.valueAsNumber)}/><output htmlFor={'bake-'+key}>{String(values[key])}</output></div>
        </StudioField>)}
        <StudioCheckbox label="Denoise lightmaps" checked={Boolean(values.denoise)} disabled={busy}
          onChange={e=>change('denoise',e.target.checked)}/>
        <details><summary>Advanced sampling</summary>{advanced.map(([key,label,min,max,step])=><StudioField key={key} label={label} htmlFor={'bake-'+key}>
          <StudioInput id={'bake-'+key} type="number" min={min} max={max} step={step} disabled={busy}
            key={String(state.settings[key])} defaultValue={Number(values[key])}
            onChange={e=>{if(e.target.value && e.target.validity.valid)change(key,e.target.valueAsNumber);}}
            onBlur={e=>{if(!e.target.value || !e.target.validity.valid)e.target.value=String(values[key]);}}/>
        </StudioField>)}</details>
        <StudioButton disabled={busy || job?.running || !dirty} variant="primary" onClick={()=>void write('lightbake_set',patch)}>Apply settings</StudioButton>
      </StudioSection>
      <StudioSection title="Bake lighting">
        <div className="bake-job">
        {job?.running ? <>
          <label className="bake-job-progress">
            <span>{job.stage==='scene'?'Preparing scene':job.stage}</span><strong>{Math.round(job.fraction*100)}%</strong>
            <progress max={1} value={job.fraction}/>
          </label>
          <StudioButton disabled={busy} onClick={()=>void runJob('cancel')}>Cancel bake</StudioButton>
        </> : <>
          <StudioCheckbox label="Rebuild unchanged lighting" checked={force} onChange={e=>setForce(e.target.checked)}/>
          <StudioButton variant="primary" disabled={busy || dirty || !job} onClick={()=>void runJob('start')}>Bake lighting</StudioButton>
          {job?.message && <p role="status">{job.message}</p>}
        </>}
        </div>
      </StudioSection>
      <StudioSection title="Baked output" actions={<StudioButton size="sm" disabled={busy} onClick={()=>{
        setConfirmClear(false);setOutputRevision(n=>n+1);refresh(n=>n+1);
      }}>Refresh output</StudioButton>}>
        {state.bake.exists ? <>
          <StudioCheckbox label="Show baked lighting in Scene" checked={display??false} disabled={busy || display===undefined}
            onChange={e=>{setBusy(true);void invoke('studio.lightBakeDisplay',{enabled:e.target.checked})
              .then(r=>setDisplay(r.enabled)).catch(reason=>setError(String(reason))).finally(()=>setBusy(false));}}/>
          <p>{state.bake.mode} · {state.bake.folder}</p>
          <dl>{['pages','regions','texels','probes'].map(key=><div key={key}><dt>{key}</dt><dd>{state.bake.stats?.[key] ?? 0}</dd></div>)}</dl>
          {epoch && state.bake.hash && !!state.bake.atlasPages?.length &&
            <LightBakeAtlas key={state.bake.hash+':'+outputRevision} epoch={epoch} hash={state.bake.hash} pages={state.bake.atlasPages}/>}
          <StudioButton variant="danger" disabled={busy || job?.running} onClick={()=>confirmClear?void write('lightbake_clear',{}):setConfirmClear(true)}>
            {confirmClear?'Confirm clear':'Clear baked files'}</StudioButton>
          {confirmClear && <StudioButton disabled={busy} onClick={()=>setConfirmClear(false)}>Cancel</StudioButton>}
        </> : <p>No baked lightmaps for this map. Lighting uses realtime sources.</p>}
      </StudioSection>
    </>}
  </StudioPanel>;
}
