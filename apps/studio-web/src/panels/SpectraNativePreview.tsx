import { useEffect, useMemo, useRef, useState } from 'react';
import { Eye, Pause, Play, RotateCcw, SkipForward, Square, ZoomIn, ZoomOut, Focus } from 'lucide-react';
import { invoke, nativeConnected } from '../bridge';
import type { StudioSpectraPreviewState, StudioSpectraCameraSnapshot } from '../types';
import { StudioAlert, StudioBadge, StudioButton, StudioCheckbox, StudioField, StudioInput, StudioSection, StudioSelect } from '../ui';
import type { SpectraGraphV2 } from './spectraDataInterfaceModel';
import './SpectraNativePreview.css';
import SpectraDockViewport from './SpectraDockViewport';
import { compareSpectraCaptures } from './spectraPixelDiff';
import type { SpectraPixelDiff } from './spectraPixelDiff';
import { parseSpectraReplayPreset } from './spectraReplayPreset';
import type { SpectraReplayPreset } from './spectraReplayPreset';

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
  const [captureSide,setCaptureSide]=useState(512);
  const [capturePending,setCapturePending]=useState(false);
  const [captureImage,setCaptureImage]=useState('');
  const [compareA,setCompareA]=useState('');
  const [compareB,setCompareB]=useState('');
  const [captureSlot,setCaptureSlot]=useState<'A'|'B'|null>(null);
  const [compareMode,setCompareMode]=useState<'side'|'split'>('side');
  const [splitPosition,setSplitPosition]=useState(50);
  const [lockedCamera,setLockedCamera]=useState<StudioSpectraCameraSnapshot|null>(null);
  const [replayPreset,setReplayPreset]=useState<SpectraReplayPreset|null>(null);
  const [liveScalars,setLiveScalars]=useState<Record<string,number>>({});
  const [replayRestoring,setReplayRestoring]=useState(false);
  const [captureMetaA,setCaptureMetaA]=useState<Record<string,unknown>|null>(null);
  const [captureMetaB,setCaptureMetaB]=useState<Record<string,unknown>|null>(null);
  const pendingMeta=useRef<Record<string,unknown>|null>(null);
  const [diffThreshold,setDiffThreshold]=useState(16);
  const [pixelDiff,setPixelDiff]=useState<SpectraPixelDiff|null>(null);
  const [diffError,setDiffError]=useState('');
  const [captureError,setCaptureError]=useState('');
  const [parameter,setParameter]=useState('');
  const [value,setValue]=useState('0');
  const active=nativeConnected();
  const params=Object.entries(graph.instanceParameters??{});
  useEffect(()=>{
    setState(null);setSeek(0);setError('');setParameter('');
    setAutoReload(false);
    setCapturePending(false);setCaptureImage('');setCaptureError('');
    setCompareA('');setCompareB('');setCaptureSlot(null);
    setPixelDiff(null);setDiffError('');
    setCaptureMetaA(null);setCaptureMetaB(null);pendingMeta.current=null;setLockedCamera(null);setReplayPreset(null);setLiveScalars({});
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
       graphSignature===lastLoadedGraph.current || busy || disabled || capturePending)return;
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
  },[active,autoReload,state?.ready,graphSignature,graph,disabled,reloadCycle,capturePending]);

  // A single native GPU capture is completed asynchronously by the RHI
  // fence callback. Poll only while one snapshot is in flight; never
  // transfer a frame every animation tick or create fake browser particles.
  useEffect(()=>{
    if(!active || !capturePending)return;
    let closed=false,inFlight=false;
    const timer=window.setInterval(()=>{
      if(inFlight || closed)return;
      inFlight=true;
      void invoke('studio.spectraPreview',{action:'captureStatus'}).then(result=>{
        if(closed)return;
        setState(result);
        const capture=result.capture;
        if(capture?.pending)return;
        setCapturePending(false);
        if(capture?.ready && typeof capture.image==='string' &&
           capture.image.startsWith('data:image/jpeg;base64,') &&
           capture.image.length<=900000) {
          setCaptureImage(capture.image);
          if(captureSlot==='A'){setCompareA(capture.image);setCaptureMetaA(pendingMeta.current);}
          if(captureSlot==='B'){setCompareB(capture.image);setCaptureMetaB(pendingMeta.current);}
          setCaptureSlot(null);
          setCaptureError('');
        } else {
          setCaptureSlot(null);
          setCaptureError(capture?.error||'Native FX GPU capture failed');
        }
      }).catch(e=>{
        if(!closed) {setCapturePending(false);setCaptureSlot(null);setCaptureError(message(e));}
      }).finally(()=>{inFlight=false;});
    },350);
    return()=>{closed=true;window.clearInterval(timer);};
  },[active,capturePending,captureSlot]);

  useEffect(()=>{
    let cancelled=false;
    setPixelDiff(null);setDiffError('');
    if(!compareA||!compareB)return;
    void compareSpectraCaptures(compareA,compareB,diffThreshold).then(result=>{
      if(!cancelled)setPixelDiff(result);
    }).catch(e=>{if(!cancelled)setDiffError(message(e));});
    return()=>{cancelled=true;};
  },[compareA,compareB,diffThreshold]);

  const exportComparison=()=>{
    if(!pixelDiff||!compareA||!compareB)return;
    const report={kind:'spectrafx-native-jpeg-ab',formatVersion:1,
      threshold:diffThreshold,width:pixelDiff.width,height:pixelDiff.height,
      changed:pixelDiff.changed,total:pixelDiff.total,
      changedPercent:pixelDiff.changedPercent,meanAbsoluteError:pixelDiff.meanAbsoluteError,
      captureA:captureMetaA,captureB:captureMetaB,
      caveat:'8-bit decoded JPEG sRGB comparison; not HDR/alpha or backend parity'};
    const blob=new Blob([JSON.stringify(report,null,2)],{type:'application/json'});
    const url=URL.createObjectURL(blob);
    const link=document.createElement('a');link.href=url;link.download='spectrafx-ab-report.json';
    link.click();window.setTimeout(()=>URL.revokeObjectURL(url),1000);
  };
  const restoreCamera=async()=>{
    if(!lockedCamera||!active||busy||capturePending||disabled)return;
    setBusy(true);setError('');
    try{setState(await invoke('studio.spectraPreview',{action:'cameraRestore',camera:lockedCamera}));}
    catch(e){setError(message(e));}finally{setBusy(false);}
  };
  const saveReplayPreset=()=>{
    if(!state?.ready||!state.camera)return;
    setReplayPreset({format:'aiverse.spectrafx.replay',version:1,parameters:{...liveScalars},
      camera:{...state.camera},tick:state.tick,speed:state.speed,
      loop:state.loop,background:state.background,grid:state.grid,axes:state.axes,
      graphSignature,projectPath});
  };
  const exportReplayPreset=()=>{
    if(!replayPreset)return;
    const blob=new Blob([JSON.stringify(replayPreset,null,2)],{type:'application/json'});
    const url=URL.createObjectURL(blob);
    const link=document.createElement('a');link.href=url;link.download='spectrafx-replay.json';
    link.click();window.setTimeout(()=>URL.revokeObjectURL(url),1000);
  };
  const importReplayPreset=async(file:File|undefined)=>{
    if(!file)return;
    try{
      if(file.size>65536)throw new Error('Replay preset exceeds 64 KiB');
      const preset=parseSpectraReplayPreset(await file.text());
      if(preset.projectPath!==projectPath||preset.graphSignature!==graphSignature)
        throw new Error('Preset project or graph revision mismatch');
      for(const [name,value] of Object.entries(preset.parameters)){
        const spec=graph.instanceParameters?.[name];
        if(!spec||value<spec.min||value>spec.max)
          throw new Error('Invalid scalar for current graph: '+name);
      }
      setReplayPreset(preset);setError('');
    }catch(e){setError(message(e));}
  };
  const restoreReplayPreset=async()=>{
    if(!replayPreset||!active||busy||disabled||capturePending)return;
    if(replayPreset.projectPath!==projectPath||
       replayPreset.graphSignature!==graphSignature){
      setError('Replay preset belongs to a different project or graph revision.');return;
    }
    setBusy(true);setReplayRestoring(true);setError('');
    try{
      // All commands are serialized. Stop playback before restoring tick/camera.
      const actions=[
        {action:'pause' as const},
        {action:'stage' as const,background:replayPreset.background,
          grid:replayPreset.grid,axes:replayPreset.axes},
        {action:'playback' as const,speed:replayPreset.speed,loop:replayPreset.loop},
        {action:'cameraRestore' as const,camera:replayPreset.camera},
        {action:'restart' as const,play:false},
        ...Object.entries(replayPreset.parameters).map(([name,value])=>({action:'parameter' as const,name,value})),
        {action:'seek' as const,tick:replayPreset.tick}
      ];
      let result:StudioSpectraPreviewState|null=null;
      for(const action of actions){
        result=await invoke('studio.spectraPreview',action);
        if(result.error||!result.ready)throw new Error(result.error||'Native replay restore failed');
      }
      let settled=false;
      for(let attempt=0;attempt<160;++attempt){
        const next=await invoke('studio.spectraPreview',{action:'status'});
        if(next.error||!next.ready)throw new Error(next.error||'Replay backend unavailable');
        setState(next);
        if(next.tick===replayPreset.tick){settled=true;break;}
        if(next.tick>replayPreset.tick)throw new Error('Replay overshot requested tick');
        await new Promise<void>(resolve=>window.setTimeout(resolve,100));
      }
      if(!settled)throw new Error('Replay seek did not settle within 16 seconds');
      setSeek(replayPreset.tick);
      setLiveScalars({...replayPreset.parameters});
    }catch(e){setError(message(e));}
    finally{setBusy(false);setReplayRestoring(false);}
  };
  const captureFrame=async(slot:'A'|'B'|null=null)=>{
    if(!active||busy||disabled||capturePending||!state?.ready)return;
    setBusy(true);setCaptureError('');setCaptureSlot(slot);
    pendingMeta.current={graphSignature,projectPath,tick:state.tick,
      backend:state.backend,camera:state.camera??null,snapshotMaxSide:captureSide,
      capturedAt:new Date().toISOString()};
    try {
      const result=await invoke('studio.spectraPreview',{
        action:'capture',maxSide:captureSide
      });
      setState(result);
      if(result.capture?.error){setCaptureSlot(null);setCaptureError(result.capture.error);}
      else setCapturePending(!!result.capture?.pending);
    }catch(e){setCaptureSlot(null);setCaptureError(message(e));}
    finally{setBusy(false);}
  };

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
    if(busy||disabled||!active)return;
    setBusy(true);setError('');
    try{
      const next=await invoke('studio.spectraPreview',{action:'parameter',name:parameter,value:numeric});
      setState(next);
      if(!next.ready||next.error)throw new Error(next.error||'Native scalar rejected');
      setLiveScalars(previous=>({...previous,[parameter]:numeric}));
    }catch(e){setError(message(e));}finally{setBusy(false);}
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
      <StudioField label="Snapshot" htmlFor="spectra-capture-size">
        <StudioSelect id="spectra-capture-size" value={String(captureSide)}
          disabled={!active||busy||capturePending}
          onChange={e=>setCaptureSide(Number(e.target.value))}>
          <option value="256">256 px</option>
          <option value="512">512 px</option>
          <option value="768">768 px</option>
        </StudioSelect>
      </StudioField>
      <StudioButton size="sm" variant="secondary"
        disabled={!active||!state?.ready||busy||capturePending}
        onClick={()=>void captureFrame()}>
        {capturePending?'Capturing GPU frame…':'Capture Frame'}
      </StudioButton>
    </div>
    <div className="spectra-native__toolbar">
      <StudioButton size="sm" disabled={!active||!state?.ready||busy||capturePending}
        onClick={()=>void captureFrame('A')}>Capture A</StudioButton>
      <StudioButton size="sm" disabled={!active||!state?.ready||busy||capturePending}
        onClick={()=>void captureFrame('B')}>Capture B</StudioButton>
      <StudioField label="Compare" htmlFor="spectra-compare-mode">
        <StudioSelect id="spectra-compare-mode" value={compareMode}
          onChange={e=>setCompareMode(e.target.value as 'side'|'split')}>
          <option value="side">Side by side</option>
          <option value="split">Interactive split</option>
        </StudioSelect>
      </StudioField>
      <StudioButton size="sm" disabled={!state?.ready||!state.camera||busy||capturePending}
        onClick={saveReplayPreset}>Save replay preset</StudioButton>
      <StudioButton size="sm" disabled={!replayPreset||busy||capturePending||!state?.ready}
        onClick={()=>void restoreReplayPreset()}>Restore replay preset</StudioButton>
      <StudioButton size="sm" disabled={!replayPreset} onClick={exportReplayPreset}>Export preset</StudioButton>
      <label>Import preset <input type="file" accept=".json,application/json"
        disabled={busy||capturePending} onChange={e=>{
          void importReplayPreset(e.target.files?.[0]);e.target.value='';
        }}/></label>
      {replayRestoring&&<small>Restoring native replay; waiting for simulation tick…</small>}
      {replayPreset&&<small>Preset tick {replayPreset.tick} · {replayPreset.speed}× ·
        camera and stage stored in memory</small>}
      <StudioButton size="sm" disabled={!state?.camera||busy}
        onClick={()=>setLockedCamera(state?.camera??null)}>Lock camera</StudioButton>
      <StudioButton size="sm" disabled={!lockedCamera||busy||capturePending}
        onClick={()=>void restoreCamera()}>Restore camera</StudioButton>
      <StudioButton size="sm" disabled={!compareA&&!compareB}
        onClick={()=>{setCompareA('');setCompareB('');setCaptureMetaA(null);setCaptureMetaB(null);}}>Clear A/B</StudioButton>
    </div>
    {(compareA||compareB)&&<div className="spectra-native__snapshot">
      {compareMode==='split'&&compareA&&compareB&&!diffError?
        <div>
          <StudioField label={`Split position: ${splitPosition}% A`} htmlFor="spectra-split-position">
            <StudioInput id="spectra-split-position" type="range" min={0} max={100} step={1}
              value={splitPosition} onChange={e=>setSplitPosition(Number(e.target.value))}/>
          </StudioField>
          <div style={{position:'relative',width:'100%',overflow:'hidden'}}>
            <img src={compareB} alt="Native capture B" style={{display:'block',width:'100%'}}/>
            <img src={compareA} alt="Native capture A"
              style={{position:'absolute',inset:0,width:'100%',height:'100%',
                clipPath:`inset(0 ${100-splitPosition}% 0 0)`}}/>
            <div aria-hidden="true" style={{position:'absolute',left:`${splitPosition}%`,
              top:0,bottom:0,width:2,background:'white',pointerEvents:'none'}}/>
          </div>
        </div>:
        <div style={{display:'flex',gap:8,alignItems:'flex-start'}}>
          {compareA&&<div style={{flex:1,minWidth:0}}>
            <small>Capture A</small><img src={compareA} alt="Native capture A" style={{width:'100%'}}/>
          </div>}
          {compareB&&<div style={{flex:1,minWidth:0}}>
            <small>Capture B</small><img src={compareB} alt="Native capture B" style={{width:'100%'}}/>
          </div>}
        </div>}
      <div style={{display:'flex',gap:12}}>
        {compareA&&<a href={compareA} download="spectrafx-A.jpg">Save A</a>}
        {compareB&&<a href={compareB} download="spectrafx-B.jpg">Save B</a>}
      </div>
      {lockedCamera&&<small>Saved FX camera pose. Restore before capturing B for matched framing.</small>}
      {captureMetaA&&captureMetaB&&
        (captureMetaA.graphSignature!==captureMetaB.graphSignature||
         captureMetaA.tick!==captureMetaB.tick||
         captureMetaA.backend!==captureMetaB.backend||
         captureMetaA.snapshotMaxSide!==captureMetaB.snapshotMaxSide||
         JSON.stringify(captureMetaA.camera)!==JSON.stringify(captureMetaB.camera))&&
        <StudioAlert tone="warning">A/B capture settings differ (graph revision, tick, backend or snapshot size).
          Pixel differences may reflect these settings rather than an effect change.</StudioAlert>}
      <small>Two native JPEG frames; alignment assumes the same camera pose and dimensions.
        Captures are not synchronized automatically.</small>
    </div>}
    {(compareA&&compareB)&&<div className="spectra-native__snapshot">
      <StudioField label="Pixel difference threshold (0–255)" htmlFor="spectra-diff-threshold">
        <StudioInput id="spectra-diff-threshold" type="range" min={0} max={255} step={1}
          value={diffThreshold} onChange={e=>setDiffThreshold(Number(e.target.value))}/>
      </StudioField>
      <span>Threshold: {diffThreshold}</span>
      {diffError&&<StudioAlert tone="warning">{diffError}</StudioAlert>}
      {pixelDiff&&<>
        <p>Changed: {pixelDiff.changedPercent.toFixed(2)}% ({pixelDiff.changed}/{pixelDiff.total} pixels)
          · Mean RGB error: {pixelDiff.meanAbsoluteError.toFixed(2)}/255</p>
        <img src={pixelDiff.heatmap} alt="Absolute RGB pixel difference heatmap"/>
        <a href={pixelDiff.heatmap} download="spectrafx-ab-difference.png">Save difference heatmap</a>
        <StudioButton size="sm" onClick={exportComparison}>Export JSON report</StudioButton>
      </>}
    </div>}
    {captureError&&<StudioAlert tone="warning">{captureError}</StudioAlert>}
    {captureImage&&<div className="spectra-native__snapshot">
      <img src={captureImage} alt="Captured native SpectraFX GPU frame"/>
      <a href={captureImage} download="spectrafx-preview.jpg">
        Save captured JPEG
      </a>
      <small>Native one-shot GPU readback. Not a simulated browser frame.</small>
    </div>}
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
