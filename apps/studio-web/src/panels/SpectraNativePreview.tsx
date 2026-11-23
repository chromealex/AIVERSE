import { useEffect, useState } from 'react';
import { Eye, Pause, Play, RotateCcw, SkipForward, Square, ZoomIn, ZoomOut, Focus } from 'lucide-react';
import { invoke, nativeConnected } from '../bridge';
import type { StudioSpectraPreviewState } from '../types';
import { StudioAlert, StudioBadge, StudioButton, StudioField, StudioInput, StudioSection } from '../ui';
import type { SpectraGraphV2 } from './spectraDataInterfaceModel';
import './SpectraNativePreview.css';
import SpectraDockViewport from './SpectraDockViewport';

type Action='status'|'load'|'play'|'pause'|'restart'|'step'|'seek'|'parameter'|'stop'|'cameraReset'|'cameraOrbit'|'cameraZoom';
const message=(e:unknown)=>e instanceof Error?e.message:String(e);
interface Props {
  graph:SpectraGraphV2;
  projectPath:string;
  disabled?:boolean;
}
/** The only particle GPU preview is the actual render-thread-owned native
 * Scene. This UI never claims that its DOM contains GPU particles.
 */
export default function SpectraNativePreview({graph,projectPath,disabled=false}:Props){
  const [state,setState]=useState<StudioSpectraPreviewState|null>(null);
  const [error,setError]=useState('');
  const [busy,setBusy]=useState(false);
  const [seek,setSeek]=useState(0);
  const [parameter,setParameter]=useState('');
  const [value,setValue]=useState('0');
  const active=nativeConnected();
  const params=Object.entries(graph.instanceParameters??{});
  useEffect(()=>{
    setState(null);setSeek(0);setError('');setParameter('');
  },[projectPath]);
  useEffect(()=>{
    if(!active || !state?.ready)return;
    const timer=setInterval(()=>{
      void invoke('studio.spectraPreview',{action:'status'}).then(next=>{
        setState(next);
      }).catch(e=>setError(message(e)));
    },500);
    return()=>clearInterval(timer);
  },[active,state?.ready]);

  const command=async(action:Action,options:Partial<{
    graph:Record<string,unknown>;tick:number;name:string;value:number;play:boolean;amount:number;dx:number;dy:number
  }>={})=>{
    if(busy||disabled||!active)return;
    setBusy(true);setError('');
    try{
      const result=await invoke('studio.spectraPreview',{action,...options});
      setState(result);
      setSeek(result.tick);
      if(result.error)setError(result.error);
    }catch(e){setError(message(e));}
    finally{setBusy(false);}
  };
  const launch=async()=>{
    // Actual graph validation and device eligibility occur in StudioScene.
    await command('load',{graph});
  };
  const setLiveValue=async()=>{
    const numeric=Number(value);
    if(!parameter || !Number.isFinite(numeric)){
      setError('Select a named parameter and enter a finite value.');return;
    }
    await command('parameter',{name:parameter,value:numeric});
  };
  return <StudioSection title="Particle Preview">
    <SpectraDockViewport/>
    <div className="spectra-native__camera">
      <span>VFX camera · Left drag orbit · Wheel zoom</span>
      <StudioButton size="sm" variant="ghost" disabled={!active||busy}
        onClick={()=>void command('cameraZoom',{amount:30})}><ZoomIn size={14}/> Zoom in</StudioButton>
      <StudioButton size="sm" variant="ghost" disabled={!active||busy}
        onClick={()=>void command('cameraZoom',{amount:-30})}><ZoomOut size={14}/> Zoom out</StudioButton>
      <StudioButton size="sm" variant="ghost" disabled={!active||busy}
        onClick={()=>void command('cameraReset')}><Focus size={14}/> Reset camera</StudioButton>
    </div>
    {!active&&<StudioAlert tone="warning">Use the native AIVERSE Studio application for actual Metal/Vulkan Scene rendering. Browser demo cannot access a GPU Device.</StudioAlert>}
    <p className="spectra-native__hint">
      Live native Metal/Vulkan particle stage, embedded in the Graph panel.
      Load this graph and press Play. Orbit the camera in the viewport; no Scene tab or map placement is required.
    </p>
    {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
    <div className="spectra-native__toolbar">
      <StudioButton size="sm" variant="primary" disabled={!active||busy||disabled}
        onClick={()=>void launch()}><Eye size={14}/> Load graph</StudioButton>
      <StudioButton size="sm" disabled={!active||!state?.ready||busy}
        onClick={()=>void command(state?.playing?'pause':'play')}>
        {state?.playing?<Pause size={14}/>:<Play size={14}/>}
        {state?.playing?'Pause':'Play'}
      </StudioButton>
      <StudioButton size="sm" disabled={!active||!state?.ready||busy}
        onClick={()=>void command('step')}><SkipForward size={14}/> Step</StudioButton>
      <StudioButton size="sm" disabled={!active||!state?.ready||busy}
        onClick={()=>void command('restart',{play:false})}><RotateCcw size={14}/> Restart</StudioButton>
      <StudioButton size="sm" variant="danger" disabled={!active||!state?.ready||busy}
        onClick={()=>void command('stop')}><Square size={14}/> Stop</StudioButton>
    </div>
    <div className="spectra-native__status">
      <StudioBadge tone={state?.gpu?'success':state?.ready?'warning':'neutral'}>
        {state?.ready?state.backend:'Preview not loaded'}
      </StudioBadge>
      <span>{state?.ready?(state.playing?'Playing':'Paused'):'No active runtime'}</span>
      <span>Tick {state?.tick??0} / 3600</span>
      <span>Time {(state?.timeSeconds??0).toFixed(2)}s</span>
    </div>
    <div className="spectra-native__timeline">
      <StudioField label="Seek (60 Hz fixed simulation ticks)" htmlFor="spectra-preview-seek">
        <StudioInput id="spectra-preview-seek" type="range" min={0} max={3600} step={1}
          value={seek} disabled={!active||!state?.ready||busy}
          onChange={e=>setSeek(Number(e.target.value))}
          onPointerUp={()=>{if(state?.ready)void command('seek',{tick:seek});}}
          onKeyUp={e=>{if((e.key==='ArrowLeft'||e.key==='ArrowRight')&&state?.ready)
            void command('seek',{tick:seek});}}/>
      </StudioField>
      <span>{(seek/60).toFixed(2)}s / 60s</span>
    </div>
    {!!params.length&&<div className="spectra-native__params">
      <StudioField label="Runtime scalar" htmlFor="spectra-preview-param">
        <select className="au-select" id="spectra-preview-param" value={parameter}
          disabled={!active||!state?.ready||busy} onChange={e=>{
            setParameter(e.target.value);
            const schema=graph.instanceParameters?.[e.target.value];
            if(schema)setValue(String(schema.default));
          }}>
          <option value="">Select parameter…</option>
          {params.map(([name,p])=><option key={name} value={name}>
            {name} · param{p.slot} ({p.min}…{p.max})</option>)}
        </select>
      </StudioField>
      <StudioField label="Value" htmlFor="spectra-preview-value">
        <StudioInput id="spectra-preview-value" type="number" step="any"
          value={value} disabled={!parameter||busy||!state?.ready}
          onChange={e=>setValue(e.target.value)}/>
      </StudioField>
      <StudioButton size="sm" disabled={!active||!state?.ready||!parameter||busy}
        onClick={()=>void setLiveValue()}>Apply live</StudioButton>
    </div>}
    <p className="spectra-native__hint">
      The native backend decides GPU eligibility; CPU fallback renders real particles into the same viewport.
      Graph edits require Load graph again. GPU/CPU numerical parity is a separate hardware test.
    </p>
  </StudioSection>;
}
