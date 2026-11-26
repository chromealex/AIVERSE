import {useEffect,useState} from 'react';
import {invoke} from '../bridge';
import type {PrismaViewReport} from '../types';
import {StudioSelect} from '../ui';

function Frame({view:v}:{view:PrismaViewReport}) {
  return <div className="pfx-report">
    <p><strong>{v.width} × {v.height}</strong> · {v.quality} · {v.hdr?'HDR':'SDR'} · Resolve {v.sequence}</p>
    <dl>
      <dt>Scene input</dt><dd>{v.sceneReady?'Ready':'Unavailable'}</dd>
      <dt>Compositor</dt><dd>{v.compositorPasses} encoded passes</dd>
      <dt>Shader graph</dt><dd>{v.shaderGraphFallback?'Fallback · ':''}{v.shaderGraphTextures} textures</dd>
      <dt>Exposure</dt><dd>{v.manualExposureEncoded?v.manualExposureEV+' EV (manual)':v.autoExposureTextureSlot<0?'Inactive':
        (v.autoExposureFrozen?'Frozen':'Adapting')+' · slot '+v.autoExposureTextureSlot+' · highlight rejection '+v.autoExposureHighlightRejection+
          (v.autoExposureRegionMetered?(v.autoExposureBoxMetered?' · box meter':' · circle meter'):'')}</dd>
      <dt>Reactive input</dt><dd>{!v.reactiveInputSupplied?'Unavailable':!v.reactiveInputAccepted?'Rejected':
        v.reactiveWithoutObjectVelocity?'Mask only':v.objectVelocityInputAccepted?'Mask and object velocity':'Mask accepted'}
        {v.motionBlurReactiveEdgesEnabled?' · motion blur edge rejection':''}</dd>
    </dl>
    {v.effects.length>0?<><p>Encoded effects issued draw commands.</p>
      <ul>{v.effects.map(e=><li key={e.type}><b>{e.type}</b>
        <span>{e.requested?'Requested':'Skipped'} · {e.pipelinePrepared?'Pipeline ready':'Pipeline unavailable'} · {e.encoded?'Encoded':'No draw'}
          {e.degraded?' · degraded':''}</span>{e.reason && <small>{e.reason}</small>}</li>)}</ul></>:
      <p>No authored effects in this view.</p>}
  </div>;
}
export default function PrismaFrameDiagnostics({project}:{project:string}) {
  const [open,setOpen]=useState(false);
  const [reports,setReports]=useState<Record<string,PrismaViewReport[]>|null>(null);
  const [picked,setPicked]=useState('');
  const [error,setError]=useState('');
  useEffect(()=>{
    if (!open) return;
    let live=true;
    let timer:ReturnType<typeof setTimeout>;
    async function refresh(){
      try {
        const r=await invoke('studio.gpuStats',{prisma:true,project});
        if(live){setReports(r.prisma??{});setError('');}
      } catch(e){if(live){setReports(null);setError(e instanceof Error?e.message:String(e));}}
      if(live)timer=setTimeout(refresh,1200);
    }
    void refresh();
    return ()=>{live=false;clearTimeout(timer);};
  },[open,project]);
  const views=Object.entries(reports??{}).flatMap(([source,frames])=>frames.map(frame=>({label:source+' · View '+frame.id,frame})));
  const current=views.find(v=>v.label===picked)??views[0];
  return <details className="pfx-diagnostics" onToggle={e=>{const visible=e.currentTarget.open;setOpen(visible);if(!visible)setReports(null);}}>
    <summary>Camera frame diagnostics</summary>
    {error?<p role="alert">{error}</p>:current?<><label>Render view <StudioSelect aria-label="Render view" value={current.label} onChange={e=>setPicked(e.target.value)}>
      {views.map(v=><option key={v.label}>{v.label}</option>)}</StudioSelect></label><Frame view={current.frame}/></>:
      <p>{reports?'Waiting for a camera to render a PostFX frame.':'Reading the last rendered frame…'}</p>}
  </details>;
}
