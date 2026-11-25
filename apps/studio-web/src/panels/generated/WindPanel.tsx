import {useCallback,useEffect,useState} from 'react';
import type {StudioSnapshot,WindState,WindSettings,WindDebugView,WindMask} from '../../types';
import {invoke,nativeConnected,onNativeEvent} from '../../bridge';
import {StudioPanel,StudioSection,StudioAlert,StudioEmptyState,StudioField,StudioInput,StudioSelect,StudioCheckbox,StudioBadge,StudioButton} from '../../ui';
import './WindPanel.css';

/** Wind: preview wind for every model with hierarchical wind masks
 * (<model>.wind.json) and rig debug views. Session-only; never writes files. */
const DEBUG:Array<[WindDebugView,string]>=[['off','Off'],['parts','Parts'],['depth','Hierarchy Depth'],['weight','Sway Weight'],['skeleton','Skeleton']];

function MaskCurve({mask}:{mask:WindMask}){
  const points=mask.samples.map((v,i)=>`${(i/(mask.samples.length-1))*100},${(1-v)*40}`).join(' ');
  return <svg className="wind-curve" viewBox="0 0 100 40" preserveAspectRatio="none" role="img" aria-label={'Mask '+mask.name}>
    <polyline points={points} fill="none" stroke="currentColor" strokeWidth="1.5" vectorEffect="non-scaling-stroke"/>
  </svg>;
}

export default function WindPanel({snapshot}:{snapshot:StudioSnapshot|null}){
  const [state,setState]=useState<WindState|null>(null);
  const [busy,setBusy]=useState(false),[error,setError]=useState('');
  const projectLoaded=Boolean(snapshot?.project.path);
  const reload=useCallback(async()=>{try{setState(await invoke('studio.wind',{}));setError('');}catch(e){setError(String(e));}},[]);
  useEffect(()=>{void reload();},[reload,snapshot?.project.path,snapshot?.scene.name]);
  useEffect(()=>onNativeEvent(event=>{if(event==='document.changed'||event==='workspace.changed')void reload();}),[reload]);
  const apply=async(patch:Partial<WindSettings>)=>{
    if(Object.values(patch).some(v=>typeof v==='number'&&!Number.isFinite(v)))return;
    setBusy(true);
    try{setState(await invoke('studio.windSet',patch));setError('');}catch(e){setError(String(e));}finally{setBusy(false);}
  };
  const s=state?.settings;
  const native=nativeConnected();
  const number=(key:'speed'|'direction'|'gust'|'scale',label:string,min:number,max:number,step:number,hint?:string)=>
    <StudioField label={label} htmlFor={'wind-'+key} hint={hint}>
      <StudioInput id={'wind-'+key} type="range" min={min} max={max} step={step} disabled={!native||busy||!s}
        value={s?.[key]??0} onChange={e=>void apply({[key]:e.currentTarget.valueAsNumber})}/>
    </StudioField>;
  return <StudioPanel className="studio-panel-wind" title="Wind" eyebrow="WORLD / WIND" subtitle="Hierarchical sway masks"
    actions={<StudioBadge tone={state?.scene.riggedInstances?'success':'neutral'}>{state?.scene.riggedInstances??0} rigged objects</StudioBadge>}>
    <div className="au-stack">
      {error&&<StudioAlert title="Error">{error}</StudioAlert>}
      {!projectLoaded?<StudioEmptyState title="No Project" description="Open a project in native Studio."/>:<>
      <StudioSection title="Wind Preview">
        <StudioCheckbox label="Wind in Scene" checked={s?.enabled??false} disabled={!native||busy||!s} onChange={e=>void apply({enabled:e.currentTarget.checked})}/>
        {number('speed',`Speed: ${(s?.speed??0).toFixed(1)} m/s`,0,30,.5)}
        {number('direction',`Direction: ${Math.round(s?.direction??0)}° (0 = +X, 90 = +Z)`,-180,180,5)}
        {number('gust',`Gusts: ${Math.round((s?.gust??0)*100)}%`,0,1,.05)}
        {number('scale',`Amplitude: ${((s?.scale??0)*100).toFixed(1)} cm per m/s`,0,.15,.005,'Tip displacement for a part with mask and flexibility set to 1')}
      </StudioSection>
      <StudioSection title="Debug View">
        <StudioField label="Display" htmlFor="wind-debug">
          <StudioSelect id="wind-debug" value={s?.debug??'off'} disabled={!native||busy||!s} onChange={e=>void apply({debug:e.currentTarget.value as WindDebugView})}>
            {DEBUG.map(([id,label])=><option key={id} value={id}>{label}</option>)}
          </StudioSelect>
        </StudioField>
        <p className="wind-legend">{s?.debug==='depth'?'Trunk: brown; branches: yellow; twigs: green; leaves: cyan. Brightness represents mask weight.':
          s?.debug==='weight'?'Blue: stationary; green: medium; red: maximum sway.':
          s?.debug==='parts'?'Each part has its own color; brightness increases towards the tip according to its mask.':
          s?.debug==='skeleton'?'Lines from each attachment point to its tip follow GPU sway.':'Regular materials.'}</p>
        {state&&<p className="wind-legend">Rigged parts: {state.scene.riggedParts??0} · models: {state.scene.models??0}</p>}
      </StudioSection>
      <StudioSection title={`Project Masks (${state?.library.uniqueMasks??0} unique)`} actions={<StudioButton disabled={busy} onClick={()=>void reload()}>Reload</StudioButton>}>
        {(state?.library.warnings??[]).map(w=><StudioAlert key={w} title="wind/*.json">{w}</StudioAlert>)}
        <div className="wind-masks">{(state?.library.masks??[]).map(m=><div className="wind-mask" key={m.name}>
          <MaskCurve mask={m}/><span>{m.name}</span><small>#{m.index}</small></div>)}</div>
        <p className="wind-legend">Custom masks: wind/masks.json; profiles: wind/profiles.json; model bindings: &lt;model&gt;.wind.json beside the model. Identical curves are stored only once.</p>
      </StudioSection>
      {(state?.library.profiles.length??0)>0&&<StudioSection title="Profiles">
        {state!.library.profiles.map(p=><div key={p.name} className="wind-profile"><strong>{p.name}</strong>
          {p.levels.map((l,i)=><span key={i}>L{i}: {l.mask} · stiffness {l.stiffness.toFixed(2)} · flexibility {l.flex.toFixed(2)}</span>)}</div>)}
      </StudioSection>}
      </>}
    </div>
  </StudioPanel>;
}
