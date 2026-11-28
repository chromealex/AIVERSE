import {useEffect,useState} from 'react';
import {invoke,nativeConnected,onNativeEvent} from '../bridge';
import type {StudioParams} from '../types';
import {StudioAlert,StudioButton,StudioSelect} from '../ui';
import SpectraDockViewport from './SpectraDockViewport';

const cameraKeys:Record<string,{orbitX?:number;orbitY?:number;zoom?:number}> = {
  ArrowLeft:{orbitX:-12},ArrowRight:{orbitX:12},ArrowUp:{orbitY:12},ArrowDown:{orbitY:-12},
  '+':{zoom:1},'=':{zoom:1},'-':{zoom:-1}
};
export default function EffectPlayback({project,source}:{project:string;source:string}){
  const [open,setOpen]=useState(false),[busy,setBusy]=useState(false);
  const [speed,setSpeed]=useState(1),[loop,setLoop]=useState(true);
  const [moving,setMoving]=useState(false),[quality,setQuality]=useState(3);
  const [error,setError]=useState(''),[stats,setStats]=useState('');
  const command=(params:Omit<StudioParams['studio.effectFilePreview'],'project'>)=>
    invoke('studio.effectFilePreview',{project,...params});
  useEffect(()=>{
    return ()=>{if(nativeConnected())void invoke('studio.effectFilePreview',{project,close:true}).catch(()=>{});};
  },[project]);
  useEffect(()=>{
    if(!open)return;
    let active=true;
    const stop=onNativeEvent((event,payload)=>{
      if(event==='effectFile.previewError')setError(String((payload as {message?:string})?.message??payload));
    });
    const timer=window.setInterval(()=>{
      void invoke('studio.effectFilePreview',{project}).then(r=>{
        if(active)setStats(r.particles+' particles · '+r.drawCalls+' draw calls');
      }).catch(e=>{if(active)setError(String(e));});
    },550);
    return ()=>{active=false;stop();clearInterval(timer);};
  },[project,open]);
  async function replay(){
    setBusy(true);setError('');
    try{await command({source,speed,loop,moving,quality});setOpen(true);}
    catch(e){setError(String(e));}
    finally{setBusy(false);}
  }
  function change(params:Omit<StudioParams['studio.effectFilePreview'],'project'>){
    if(open)void command(params).catch(e=>setError(String(e)));
  }
  return <section className="effects-playback" aria-label="Particle playback">
    {open&&<SpectraDockViewport id="effectFile" label="Effect particle preview"/>}
    <div className="au-row">
      <StudioButton disabled={busy||!nativeConnected()} onClick={()=>void replay()}>{open?'Replay':'Preview effect'}</StudioButton>
      {open&&<StudioButton onClick={()=>{setOpen(false);void command({close:true}).catch(e=>setError(String(e)));}}>Close preview</StudioButton>}
      {open&&<StudioButton onClick={()=>change({resetCamera:true})} onKeyDown={event=>{
        const step=cameraKeys[event.key];
        if(step){event.preventDefault();change(step);}
      }} title="Drag to orbit, scroll to zoom. With this button focused, use arrow keys and + or −.">Reset camera</StudioButton>}
      {open&&<span>{stats}</span>}
    </div>
    {open&&<div className="effects-playback-options">
      <label>Speed · {speed.toFixed(1)}×<input type="range" min=".1" max="2" step=".1" value={speed} onChange={e=>{
        const value=Number(e.target.value);setSpeed(value);change({speed:value});
      }}/></label>
      <label>Quality<StudioSelect value={quality} onChange={e=>{
        const value=Number(e.target.value);setQuality(value);change({quality:value});
      }}>{['Off','Low','Medium','High'].map((tier,i)=><option key={tier} value={i}>{tier}</option>)}</StudioSelect></label>
      <label><input type="checkbox" checked={loop} onChange={e=>{setLoop(e.target.checked);change({loop:e.target.checked});}}/>Loop</label>
      <label><input type="checkbox" checked={moving} onChange={e=>{setMoving(e.target.checked);change({moving:e.target.checked});}}/>Move emitter</label>
    </div>}
    {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
  </section>;
}
