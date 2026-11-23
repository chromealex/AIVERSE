import { useEffect, useMemo, useRef, useState } from 'react';
import { Eye, Pause, Play, RotateCcw, SkipForward, Square, ZoomIn, ZoomOut, Focus } from 'lucide-react';
import { invoke, nativeConnected } from '../bridge';
import type { StudioSpectraPreviewState } from '../types';
import { StudioAlert, StudioBadge, StudioButton, StudioCheckbox, StudioField, StudioInput, StudioSection, StudioSelect } from '../ui';
import type { SpectraGraphV2 } from './spectraDataInterfaceModel';
import './SpectraNativePreview.css';
import SpectraDockViewport from './SpectraDockViewport';

type Action='status'|'load'|'play'|'pause'|'restart'|'step'|'seek'|'parameter'|'stop'|'cameraReset'|'cameraOrbit'|'cameraZoom'|'stage'|'cameraPreset'|'playback'|'burst'|'clear';
const message=(e:unknown)=>e instanceof Error?e.message:String(e);
interface Props {
  graph:SpectraGraphV2;
  projectPath:string;
  disabled?:boolean;
}
/** Independent native FX swapchain on the shared Studio render-owner thread.
 * The map Scene renders separately; React does not generate GPU particles.
 */
export default function SpectraNativePreview({graph,projectPath,disabled=false}:Props){
  const [state,setState]=useState<StudioSpectraPreviewState|null>(null);
  const [error,setError]=useState('');
  const [busy,setBusy]=useState(false);
  const [autoReload,setAutoReload]=useState(false);
  const [reloadCycle,setReloadCycle]=useState(0);
  const mounted=useRef(false);
  useEffect(()=>{
    mounted.current=true;
    return()=>{mounted.current=false;};
  },[]);
  const graphSignature=useMemo(()=>JSON.stringify(graph),[graph]);
  const lastLoadedGraph=useRef('');
  const reloadBusy=useRef(false);
  const [seek,setSeek]=useState(0);
  const [burstCount,setBurstCount]=useState(1);
  const [parameter,setParameter]=useState('');
  const [value,setValue]=useState('0');
  const active=nativeConnected();
  const params=Object.entries(graph.instanceParameters??{});
  useEffect(()=>{
    setState(null);setSeek(0);setError('');setParameter('');
    setAutoReload(false);
    lastLoadedGraph.current='';
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

  // Opt-in, bounded authoring-only live reload. It recompiles the current
  // in-memory graph through native StudioScene; it never saves or publishes
  // asset files, and it preserves the last-good runtime on validation error.
  // The existing Play state is restored only after successful validation.
  useEffect(()=>{
    if(!active || !autoReload || !state?.ready || !lastLoadedGraph.current ||
       graphSignature===lastLoadedGraph.current || busy || disabled)return;
    let closed=false;
    const handle=window.setTimeout(()=>{
      if(closed || reloadBusy.current)return;
      reloadBusy.current=true;
      setBusy(true);setError('');
      const wasPlaying=!!state?.playing;
      // Remember an attempted invalid revision so it does not enter an
      // infinite loop while the user edits another node.
      lastLoadedGraph.current=graphSignature;
      void invoke('studio.spectraPreview',{action:'load',graph}).then(async next=>{
        if(closed)return;
        if(!next.ready || next.error) {
          setState(next);
          setError(next.error||'Native graph reload rejected');
          return;
        }
        let result=next;
        if(wasPlaying)result=await invoke('studio.spectraPreview',{action:'play'});
        if(!closed){setState(result);setSeek(result.tick);}
      }).catch(e=>{if(!closed)setError(message(e));}).finally(()=>{
        reloadBusy.current=false;
        if(mounted.current) {
          setBusy(false);
          // A second graph edit may have arrived while a native compile
          // was in flight. Reevaluate the newest source after it settles.
          setReloadCycle(c=>c+1);
        }
      });
    },650);
    return()=>{closed=true;window.clearTimeout(handle);};
  },[active,autoReload,state?.ready,graphSignature,graph,disabled,reloadCycle]);

  const command=async(action:Action,options:Partial<{
    graph:Record<string,unknown>;tick:number;name:string;value:number;play:boolean;amount:number;dx:number;dy:number;
    background:'dark'|'neutral'|'light'|'black';grid:boolean;axes:boolean;
    preset:'perspective'|'front'|'side'|'top';speed:number;loop:boolean;count:number
  }>={})=>{
    if(busy||disabled||!active)return;
    setBusy(true);setError('');
    try{
      const result=await invoke('studio.spectraPreview',{action,...options});
      if(action==='load'&&result.ready&&!result.error)
        lastLoadedGraph.current=graphSignature;
      if(action==='stop')lastLoadedGraph.current='';
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
    <div className="spectra-native__stage-controls">
      <StudioField label="Background" htmlFor="spectra-stage-background">
        <StudioSelect id="spectra-stage-background" value={state?.background??'dark'}
          disabled={!active||busy} onChange={e=>void command('stage',{
            background:e.target.value as 'dark'|'neutral'|'light'|'black',
            grid:state?.grid??true,axes:state?.axes??true
          })}>
          <option value="dark">Dark studio</option>
          <option value="neutral">Neutral grey</option>
          <option value="light">Light studio</option>
          <option value="black">Pure black</option>
        </StudioSelect>
      </StudioField>
      <StudioField label="Camera view" htmlFor="spectra-camera-preset">
        <StudioSelect id="spectra-camera-preset" defaultValue="perspective"
          disabled={!active||busy} onChange={e=>void command('cameraPreset',{
            preset:e.target.value as 'perspective'|'front'|'side'|'top'
          })}>
          <option value="perspective">Perspective</option>
          <option value="front">Front</option>
          <option value="side">Side</option>
          <option value="top">Top</option>
        </StudioSelect>
      </StudioField>
      <StudioCheckbox label="Grid" checked={state?.grid??true} disabled={!active||busy}
        onChange={e=>void command('stage',{background:state?.background??'dark',
          grid:e.target.checked,axes:state?.axes??true})}/>
      <StudioCheckbox label="Axes" checked={state?.axes??true} disabled={!active||busy}
        onChange={e=>void command('stage',{background:state?.background??'dark',
          grid:state?.grid??true,axes:e.target.checked})}/>
      <StudioField label="Playback speed" htmlFor="spectra-preview-speed">
        <StudioSelect id="spectra-preview-speed" value={String(state?.speed??1)}
          disabled={!active||busy} onChange={e=>void command('playback',{
            speed:Number(e.target.value),loop:state?.loop??true
          })}>
          <option value="0.25">0.25×</option>
          <option value="0.5">0.5×</option>
          <option value="1">1×</option>
          <option value="2">2×</option>
          <option value="4">4×</option>
        </StudioSelect>
      </StudioField>
      <StudioCheckbox label="Loop playback" checked={state?.loop??true}
        disabled={!active||busy} onChange={e=>void command('playback',{
          speed:state?.speed??1,loop:e.target.checked})}/>
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
      <StudioCheckbox label="Auto-reload graph" checked={autoReload} disabled={!active||busy}
        onChange={e=>setAutoReload(e.target.checked)}/>
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
      <StudioField label="Bursts" htmlFor="spectra-burst-count">
        <StudioSelect id="spectra-burst-count" value={String(burstCount)}
          disabled={!active||!state?.ready||busy}
          onChange={e=>setBurstCount(Number(e.target.value))}>
          {[1,2,4,8].map(n=><option value={n} key={n}>{n}×</option>)}
        </StudioSelect>
      </StudioField>
      <StudioButton size="sm" variant="primary" disabled={!active||!state?.ready||busy}
        onClick={()=>void command('burst',{count:burstCount})}>
        Trigger Burst
      </StudioButton>
      <StudioButton size="sm" disabled={!active||!state?.ready||busy}
        onClick={()=>void command('clear')}>
        Clear Particles
      </StudioButton>
    </div>
    <div className="spectra-native__status">
      <StudioBadge tone={state?.gpu?'success':state?.ready?'warning':'neutral'}>
        {state?.ready?state.backend:'Preview not loaded'}
      </StudioBadge>
      <span>{state?.ready?(state.playing?'Playing':'Paused'):'No active runtime'}</span>
      <span>Tick {state?.tick??0} / 3600</span>
      <span>Bursts {state?.bursts??0}
        {state?.queuedBursts?' · queued '+state.queuedBursts:''}
      </span>
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
      Enable Auto-reload to compile the in-memory graph after edits (650 ms debounce).
      Trigger Burst queues additional real native events (up to 8 at once); Clear Particles
      schedules a safe reset inside the next GPU frame without unloading the graph.
      Invalid compiler input keeps the last-good runtime. Publishing remains explicit.
      GPU/CPU numerical parity is a separate hardware test.
    </p>
  </StudioSection>;
}
