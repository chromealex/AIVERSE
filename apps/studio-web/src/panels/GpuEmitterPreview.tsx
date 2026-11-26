import {useEffect,useState} from 'react';
import {invoke,nativeConnected} from '../bridge';
import type {StudioParams,StudioResult} from '../types';
import {StudioAlert,StudioButton} from '../ui';
import SpectraDockViewport from './SpectraDockViewport';

export default function GpuEmitterPreview({project,source}:{project:string;source:string}){
  const [open,setOpen]=useState(false);
  const [state,setState]=useState<StudioResult['studio.gpuEmitterPreview']>();
  const [error,setError]=useState('');
  useEffect(()=>{
    if(!open)return;
    return ()=>{void invoke('studio.gpuEmitterPreview',{project,close:true}).catch(()=>{});};
  },[open,project]);
  useEffect(()=>{
    if(!open)return;
    let live=true;
    let poll:number|undefined;
    async function update(params:StudioParams['studio.gpuEmitterPreview']){
      try{
        const result=await invoke('studio.gpuEmitterPreview',params);
        if(live){setState(result);setError(result.error);}
      }catch(e){if(live)setError(String(e));}
    }
    const timer=window.setTimeout(async()=>{
      await update({project,source});
      if(live)poll=window.setInterval(()=>void update({project}),400);
    },150);
    return ()=>{live=false;clearTimeout(timer);clearInterval(poll);};
  },[open,project,source]);
  async function control(params:Omit<StudioParams['studio.gpuEmitterPreview'],'project'>){
    try{
      const result=await invoke('studio.gpuEmitterPreview',{project,...params});
      setState(result);setError(result.error);
    }catch(e){setError(String(e));}
  }
  if(!open)return <StudioButton disabled={!nativeConnected()} onClick={()=>setOpen(true)}>Preview emitter</StudioButton>;
  return <section className="effects-playback" aria-label="GPU emitter playback">
    <SpectraDockViewport id="gpuEmitter" label="Live emitter preview"/>
    <div className="au-row">
      <StudioButton onClick={()=>void control({paused:!state?.paused})}>{state?.paused?'Resume':'Pause'}</StudioButton>
      <StudioButton onClick={()=>void control({step:true})}>Step</StudioButton>
      <StudioButton onClick={()=>void control({restart:true})}>Restart</StudioButton>
      <StudioButton onClick={()=>void control({resetCamera:true})}>Reset camera</StudioButton>
      <StudioButton onClick={()=>{setOpen(false);setState(undefined);setError('');}}>Close preview</StudioButton>
    </div>
    {state&&!error&&<small>{'GPU '+(state.gpu??0)+' · CPU '+(state.cpu??0)+' · '+state.time.toFixed(2)+' s'}</small>}
    {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
  </section>;
}
