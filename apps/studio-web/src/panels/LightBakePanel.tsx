import './LightBakePanel.css';
import { useEffect, useState } from 'react';
import { invoke, nativeConnected, onNativeEvent } from '../bridge';
import type { StudioGraphDocumentScope } from '../types';
import { StudioAlert, StudioButton, StudioCheckbox, StudioField, StudioInput, StudioPanel, StudioSection, StudioSelect } from '../ui';

type Settings = Record<string, string | number | boolean>;
interface BakeInfo { exists: boolean; folder?: string; mode?: string; stats?: Record<string, number> }
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
  const [error,setError]=useState('');
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
        <StudioField label="Quality preset" htmlFor="bake-quality"><StudioSelect id="bake-quality" value="" disabled={busy || dirty}
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
            onBlur={e=>{if(e.target.value && e.target.validity.valid)change(key,e.target.valueAsNumber);else e.target.value=String(values[key]);}}/>
        </StudioField>)}</details>
        <StudioButton disabled={busy || !dirty} variant="primary" onClick={()=>void write('lightbake_set',patch)}>Apply settings</StudioButton>
      </StudioSection>
      <StudioSection title="Baked output">
        {state.bake.exists ? <>
          <p>{state.bake.mode} · {state.bake.folder}</p>
          <dl>{['pages','regions','texels','probes'].map(key=><div key={key}><dt>{key}</dt><dd>{state.bake.stats?.[key] ?? 0}</dd></div>)}</dl>
          <StudioButton variant="danger" disabled={busy} onClick={()=>confirmClear?void write('lightbake_clear',{}):setConfirmClear(true)}>
            {confirmClear?'Confirm clear':'Clear baked files'}</StudioButton>
          {confirmClear && <StudioButton disabled={busy} onClick={()=>setConfirmClear(false)}>Cancel</StudioButton>}
        </> : <p>No baked lightmaps for this map. Lighting uses realtime sources.</p>}
      </StudioSection>
    </>}
  </StudioPanel>;
}
