import { useEffect, useMemo, useRef, useState } from 'react';
import { Eye, Pause, Play, RotateCcw, SkipForward, Square, ZoomIn, ZoomOut, Focus } from 'lucide-react';
import { invoke, nativeConnected } from '../bridge';
import type { StudioParams, StudioSpectraPreviewState, StudioSpectraCameraSnapshot } from '../types';
import { StudioAlert, StudioBadge, StudioButton, StudioCheckbox, StudioField, StudioInput, StudioSection, StudioSelect } from '../ui';
import type { SpectraGraphV2 } from './spectraDataInterfaceModel';
import './SpectraNativePreview.css';
import SpectraDockViewport from './SpectraDockViewport';
import { compareSpectraCaptures } from './spectraPixelDiff';
import type { SpectraPixelDiff } from './spectraPixelDiff';
import { parseSpectraReplayPreset } from './spectraReplayPreset';
import type { SpectraReplayPreset } from './spectraReplayPreset';
import { SpectraCommandQueue } from './spectraCommandQueue';

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
  const [gpuBirthRandom,setGpuBirthRandom]=useState(false);
  const [reloadCycle,setReloadCycle]=useState(0);
  const mounted=useRef(false);
  const commandQueue=useRef(new SpectraCommandQueue());
  useEffect(()=>{
    mounted.current=true;
    const queue=commandQueue.current;
    return()=>{mounted.current=false;queue.cancelWaiting();};
  },[]);
  // Every command captures this mounted project's identity. In-flight RPCs
  // cannot be unsent, but their continuations cannot issue another command
  // after unmount, and the native host rejects stale project requests.
  const invokePreview=async(input:StudioParams['studio.spectraPreview'])=>{
    if(!mounted.current)throw new Error('SpectraFX preview closed');
    const result=await commandQueue.current.enqueue({...input,expectedProjectPath:projectPath},request=>{
      if(!mounted.current)throw new Error('SpectraFX preview closed');
      return invoke('studio.spectraPreview',request);
    });
    if(!mounted.current)throw new Error('SpectraFX preview closed');
    return result;
  };
  const graphSignature=useMemo(()=>JSON.stringify(graph),[graph]);
  const lastLoadedGraph=useRef('');
  const reloadBusy=useRef(false);
  const [seek,setSeek]=useState(0);
  const [burstCount,setBurstCount]=useState(1);
  const [captureSide,setCaptureSide]=useState(512);
  const [capturePending,setCapturePending]=useState(false);
  const [captureId,setCaptureId]=useState<number|undefined>(undefined);
  const [captureImage,setCaptureImage]=useState('');
  const [compareA,setCompareA]=useState('');
  const [compareB,setCompareB]=useState('');
  const [captureSlot,setCaptureSlot]=useState<'A'|'B'|null>(null);
  const [compareMode,setCompareMode]=useState<'side'|'split'>('side');
  const [splitPosition,setSplitPosition]=useState(50);
  const [lockedCamera,setLockedCamera]=useState<StudioSpectraCameraSnapshot|null>(null);
  const [replayPreset,setReplayPreset]=useState<SpectraReplayPreset|null>(null);
  const [presetName,setPresetName]=useState('');
  const [presetEntries,setPresetEntries]=useState<Array<{name:string;tick?:number;ok:boolean}>>([]);
  const [presetSelected,setPresetSelected]=useState('');
  const [presetSearch,setPresetSearch]=useState('');
  const [presetCompatibility,setPresetCompatibility]=useState('');
  const [presetLibraryBusy,setPresetLibraryBusy]=useState(false);
  const [liveScalars,setLiveScalars]=useState<Record<string,number>>({});
  const [replayRestoring,setReplayRestoring]=useState(false);
  const replayCancelled=useRef(false);
  const replayGeneration=useRef<number|undefined>(undefined);
  const [replaySeed,setReplaySeed]=useState('');
  const [gpuAllocationSeed,setGpuAllocationSeed]=useState('');
  const [gpuShaderSeed,setGpuShaderSeed]=useState('');
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
    setCapturePending(false);setCaptureId(undefined);setCaptureImage('');setCaptureError('');
    setCompareA('');setCompareB('');setCaptureSlot(null);
    setPixelDiff(null);setDiffError('');
    setCaptureMetaA(null);setCaptureMetaB(null);pendingMeta.current=null;setLockedCamera(null);setReplayPreset(null);setLiveScalars({});setReplaySeed('');setGpuAllocationSeed('');
    lastLoadedGraph.current='';
  },[projectPath]);
  useEffect(()=>{
    if(!active||busy)return;
    let closed=false,inFlight=false;
    const timer=setInterval(()=>{
      if(closed||inFlight)return;
      inFlight=true;
      void invokePreview({action:'status'}).then(next=>{
        if(!closed)setState(next);
      }).catch(e=>{if(!closed&&state?.ready)setError(message(e));}).finally(()=>{inFlight=false;});
    },500);
    return()=>{closed=true;clearInterval(timer);};
  },[active,state?.ready,projectPath,busy]);

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
      void invokePreview({action:'load',graph,gpuBirthRandom,expectedGeneration:state.generation}).then(async next=>{
        if(closed)return;
        if(!next.ready || next.error) {
          setState(next);
          setError(next.error||'Native graph reload rejected');
          return;
        }
        let result=next;
        if(wasPlaying)result=await invokePreview({action:'play',expectedGeneration:next.generation});
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
      void invokePreview({action:'captureStatus',captureId}).then(result=>{
        if(closed)return;
        setState(result);
        const capture=result.capture;
        if(capture?.pending)return;
        setCapturePending(false);
        if(capture?.ready && typeof capture.image==='string' &&
           capture.image.startsWith('data:image/jpeg;base64,') &&
           capture.image.length<=900000) {
          setCaptureImage(capture.image);
          if(captureSlot==='A'){setCompareA(capture.image);setCaptureMetaA({...pendingMeta.current,native:capture.provenance??null});}
          if(captureSlot==='B'){setCompareB(capture.image);setCaptureMetaB({...pendingMeta.current,native:capture.provenance??null});}
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
  },[active,capturePending,captureSlot,captureId,projectPath]);

  useEffect(()=>{
    let cancelled=false;
    setPixelDiff(null);setDiffError('');
    if(!compareA||!compareB)return;
    void compareSpectraCaptures(compareA,compareB,diffThreshold).then(result=>{
      if(!cancelled)setPixelDiff(result);
    }).catch(e=>{if(!cancelled)setDiffError(message(e));});
    return()=>{cancelled=true;};
  },[compareA,compareB,diffThreshold]);

  const nativeCaptureMismatch=useMemo(()=>{
    if(!captureMetaA||!captureMetaB)return [];
    const a=captureMetaA.native as Record<string,unknown>|null|undefined;
    const b=captureMetaB.native as Record<string,unknown>|null|undefined;
    if(!a||!b)return ['missing native provenance'];
    const mismatches:string[]=[];
    for(const key of ['tick','generation','backend','gpu','background','replayReady']){
      if(a[key]!==b[key])mismatches.push(key);
    }
    if(JSON.stringify(a.camera)!==JSON.stringify(b.camera))mismatches.push('native camera');
    if(a.frameExact!==true||b.frameExact!==true)mismatches.push('frame identity not verified');
    if(a.frameExact===true&&b.frameExact===true&&
       a.frameSerialAtRequest!==b.frameSerialAtRequest)
      mismatches.push('capture frame serial');
    return mismatches;
  },[captureMetaA,captureMetaB]);
  const exportComparison=()=>{
    if(!pixelDiff||!compareA||!compareB)return;
    const report={kind:'spectrafx-native-jpeg-ab',formatVersion:1,
      threshold:diffThreshold,width:pixelDiff.width,height:pixelDiff.height,
      changed:pixelDiff.changed,total:pixelDiff.total,
      changedPercent:pixelDiff.changedPercent,meanAbsoluteError:pixelDiff.meanAbsoluteError,
      captureA:captureMetaA,captureB:captureMetaB,
      nativeCaptureMismatch,frameExact:false,
      caveat:'8-bit decoded JPEG sRGB comparison; not HDR/alpha or backend parity'};
    const blob=new Blob([JSON.stringify(report,null,2)],{type:'application/json'});
    const url=URL.createObjectURL(blob);
    const link=document.createElement('a');link.href=url;link.download='spectrafx-ab-report.json';
    link.click();window.setTimeout(()=>URL.revokeObjectURL(url),1000);
  };
  const restoreCamera=async()=>{
    if(!lockedCamera||!active||busy||capturePending||disabled)return;
    setBusy(true);setError('');
    try{setState(await invokePreview({action:'cameraRestore',camera:lockedCamera,expectedGeneration:state?.generation}));}
    catch(e){setError(message(e));}finally{setBusy(false);}
  };
  const saveReplayPreset=()=>{
    if(!state?.ready||!state.camera)return;
    if(replaySeed!==''&&(!Number.isInteger(Number(replaySeed))||Number(replaySeed)<0||Number(replaySeed)>2147483647)){
      setError('CPU replay seed must be integer 0..2147483647');return;
    }
    if(gpuAllocationSeed!==''&&(!Number.isInteger(Number(gpuAllocationSeed))||Number(gpuAllocationSeed)<0||Number(gpuAllocationSeed)>2147483647||replaySeed!=='')){
      setError('GPU allocation seed must be integer 0..2147483647 and cannot be combined with CPU seed');return;
    }
    if(gpuShaderSeed!==''&&(!Number.isInteger(Number(gpuShaderSeed))||Number(gpuShaderSeed)<0||Number(gpuShaderSeed)>4294967295||replaySeed!==''||gpuAllocationSeed!=='')){setError('GPU shader seed must be uint32 and independent');return;}
    setPresetCompatibility('Not checked · in-memory recipe');
    setReplayPreset({format:'aiverse.spectrafx.replay',version:1,parameters:{...liveScalars},...(replaySeed!==''?{seed:Number(replaySeed)}:{}),
      ...(gpuShaderSeed!==''?{gpuShaderSeed:Number(gpuShaderSeed)}:{}),
      ...(gpuAllocationSeed!==''?{gpuAllocationSeed:Number(gpuAllocationSeed)}:{}),
      camera:{...state.camera},tick:state.tick,speed:state.speed,
      loop:state.loop,background:state.background,grid:state.grid,axes:state.axes,
      graphSignature,projectPath,gpuBirthRandom});
  };
  const presetOp=async(op:string,params:Record<string,unknown>={})=>{
    const response=await invoke('studio.op',{op,params:{...params,expectedProjectPath:projectPath}});
    if(!mounted.current)throw new Error('SpectraFX preview closed');
    return response.result as Record<string,unknown>;
  };
  const refreshPresetLibrary=async()=>{
    const result=await presetOp('fx_replay_preset_list');
    if(!Array.isArray(result.presets))throw new Error('Invalid replay library response');
    setPresetEntries(result.presets.filter((entry):entry is {name:string;tick?:number;ok:boolean}=>
      !!entry&&typeof entry==='object'&&typeof entry.name==='string'&&typeof entry.ok==='boolean'));
  };
  useEffect(()=>{
    if(active)void refreshPresetLibrary().catch(e=>{if(mounted.current)setError(message(e));});
  },[active,projectPath]);
  const saveNamedPreset=async()=>{
    if(!replayPreset||presetLibraryBusy||!active||!(/^[a-z0-9_]{1,48}$/).test(presetName)||presetName==='fx')return;
    setPresetLibraryBusy(true);setError('');
    try{
      await presetOp('fx_replay_preset_save',{name:presetName,preset:replayPreset});
      await refreshPresetLibrary();setPresetSelected(presetName);
    }catch(e){setError(message(e));}finally{setPresetLibraryBusy(false);}
  };
  const selectNamedPreset=async()=>{
    if(!presetSelected||presetLibraryBusy||!active)return;
    setPresetLibraryBusy(true);setError('');
    try{
      const result=await presetOp('fx_replay_preset_get',{name:presetSelected,expectedGraphSignature:graphSignature});
      const preset=parseSpectraReplayPreset(JSON.stringify(result.preset));
      if(preset.projectPath!==projectPath||preset.graphSignature!==graphSignature)
        throw new Error('Replay project or graph revision mismatch');
      setReplayPreset(preset);
      const compatibility=result.compatibility as {state?:string}|undefined;
      setPresetCompatibility(compatibility?.state??'unverified');
      setReplaySeed(preset.seed===undefined?'':String(preset.seed));
      setGpuAllocationSeed(preset.gpuAllocationSeed===undefined?'':String(preset.gpuAllocationSeed));
      setGpuShaderSeed(preset.gpuShaderSeed===undefined?'':String(preset.gpuShaderSeed));
    }catch(e){setError(message(e));}finally{setPresetLibraryBusy(false);}
  };
  const exportReplayPreset=()=>{
    if(!replayPreset)return;
    const blob=new Blob([JSON.stringify(replayPreset,null,2)],{type:'application/json'});
    if(blob.size>65536){setError('Replay preset exceeds 64 KiB');return;}
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
      setReplayPreset(preset);setReplaySeed(preset.seed===undefined?'':String(preset.seed));
      setGpuAllocationSeed(preset.gpuAllocationSeed===undefined?'':String(preset.gpuAllocationSeed));setError('');
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
      replayCancelled.current=false;
      replayGeneration.current=undefined;
      if(replayPreset.dependencies){
        const check=await presetOp('fx_replay_preset_check',{preset:replayPreset});
        setPresetCompatibility(String(check.state??'unverified'));
        if(check.state!=='matching')throw new Error('Replay direct resources changed or are unavailable');
      }
      const restored=await invokePreview({action:'replayRestore',expectedGeneration:state?.generation,
        ...(replayPreset.gpuBirthRandom===undefined?{}:{graph,gpuBirthRandom:replayPreset.gpuBirthRandom}),
        camera:replayPreset.camera,tick:replayPreset.tick,parameters:replayPreset.parameters,
        background:replayPreset.background,grid:replayPreset.grid,axes:replayPreset.axes,
        speed:replayPreset.speed,loop:replayPreset.loop,
        ...(replayPreset.seed===undefined?{}:{seed:replayPreset.seed}),
        ...(replayPreset.gpuShaderSeed===undefined?{}:{gpuShaderSeed:replayPreset.gpuShaderSeed}),
        ...(replayPreset.gpuAllocationSeed===undefined?{}:{gpuAllocationSeed:replayPreset.gpuAllocationSeed})});
      if(restored.error||!restored.ready)throw new Error(restored.error||'Native replay restore failed');
      const expectedGeneration=restored.generation;
      replayGeneration.current=expectedGeneration;
      setState(restored);
      let settled=false;
      for(let attempt=0;attempt<280;++attempt){
        if(replayCancelled.current)return;
        const next=await invokePreview({action:'status'});
        if(replayCancelled.current)return;
        if(next.replayAttemptGeneration!==expectedGeneration)
          throw new Error('Replay operation was superseded');
        setState(next);
        if(next.replayOutcome==='failed'||next.replayOutcome==='cancelled')
          throw new Error(next.replayError||'Replay restore rolled back');
        if(next.generation!==expectedGeneration)
          throw new Error('Replay runtime generation changed');
        if(next.gpuSeedFailed)throw new Error(next.error||'GPU seed reset failed after retry budget');
        if((next.error&&!next.gpuSeedPending)||!next.ready)
          throw new Error(next.error||'Replay backend unavailable');
        setState(next);
        if(next.tick===replayPreset.tick&&next.replayReady&&next.replayOutcome==='committed'){settled=true;break;}
        if(next.tick>replayPreset.tick)throw new Error('Replay overshot requested tick');
        await new Promise<void>(resolve=>window.setTimeout(resolve,100));
      }
      if(!settled){
        await invokePreview({action:'replayCancel',expectedGeneration}).catch(()=>{});
        throw new Error('Replay seek did not settle within the polling budget');
      }
      if(replayPreset.gpuBirthRandom!==undefined){
        setGpuBirthRandom(replayPreset.gpuBirthRandom);lastLoadedGraph.current=graphSignature;
      }
      setSeek(replayPreset.tick);
      setLiveScalars({...replayPreset.parameters});
    }catch(e){if(!replayCancelled.current)setError(message(e));}
    finally{replayGeneration.current=undefined;setBusy(false);setReplayRestoring(false);}
  };
  const cancelReplay=async()=>{
    const expectedGeneration=replayGeneration.current;
    if(expectedGeneration===undefined)return;
    replayCancelled.current=true;
    try{setState(await invokePreview({action:'replayCancel',expectedGeneration}));}
    catch(e){setError(message(e));}
  };
  const captureFrame=async(slot:'A'|'B'|null=null)=>{
    if(!active||busy||disabled||capturePending||!state?.ready)return;
    setBusy(true);setCaptureError('');setCaptureSlot(slot);
    pendingMeta.current={graphSignature,projectPath,tick:state.tick,
      backend:state.backend,camera:state.camera??null,snapshotMaxSide:captureSide,
      capturedAt:new Date().toISOString()};
    try {
      const result=await invokePreview({
        action:'capture',maxSide:captureSide,expectedGeneration:state.generation,
        ...(replayPreset&&state.replayReady?{requireReplayReady:true}:{})
      });
      setState(result);
      if(result.capture?.error){setCaptureSlot(null);setCaptureError(result.capture.error);}
      else {setCaptureId(result.capture?.captureId);setCapturePending(!!result.capture?.pending);}
    }catch(e){setCaptureSlot(null);setCaptureError(message(e));}
    finally{setBusy(false);}
  };

  const command=async(action:Action,options:Partial<{
    graph:Record<string,unknown>;gpuBirthRandom:boolean;checkpointId:number;tick:number;name:string;value:number;play:boolean;amount:number;dx:number;dy:number;
    background:'dark'|'neutral'|'light'|'black';grid:boolean;axes:boolean;
    preset:'perspective'|'front'|'side'|'top';speed:number;loop:boolean;count:number
  }>={})=>{
    if(busy||disabled||!active)return;
    setBusy(true);setError('');
    try{
      const result=await invokePreview({action,...options,
        ...(state&&action!=='status'?{expectedGeneration:state.generation}:{})});
      if(action==='load'&&result.ready&&!result.error)
        lastLoadedGraph.current=graphSignature;
      if(action==='stop')lastLoadedGraph.current='';
      setState(result);
      setSeek(result.tick);
      if(result.error)setError(result.error);
    }catch(e){setError(message(e));}
    finally{setBusy(false);}
  };
  const exportCheckpointFile=async()=>{
    let transferId:number|undefined;
    setBusy(true);setError('');
    try{
      const first=await invokePreview({action:'checkpointFileExport',expectedGeneration:state?.generation});
      // Acquire the lease before validating metadata so finally can cancel it.
      if(typeof first.transferId==='number'&&Number.isSafeInteger(first.transferId)&&first.transferId>0)
        transferId=first.transferId;
      if(first.error)throw new Error(first.error);
      if(!transferId)throw new Error('Missing transfer ID');
      if(typeof first.fileSize!=='number'||!Number.isSafeInteger(first.fileSize)||first.fileSize<=0||first.fileSize>1048600)
        throw new Error('Invalid checkpoint size');
      const bytes=new Uint8Array(first.fileSize);
      for(let offset=0;offset<bytes.length;offset+=32768){
        const next=await invokePreview({action:'checkpointFileRead',offset,transferId,expectedGeneration:state?.generation});
        const hex=next.fileChunk??'';
        if(next.error||next.fileSize!==bytes.length||hex.length!==2*Math.min(32768,bytes.length-offset)||!/^[0-9a-f]+$/.test(hex))throw new Error(next.error||'Invalid checkpoint chunk');
        for(let i=0;i<hex.length;i+=2)bytes[offset+i/2]=parseInt(hex.slice(i,i+2),16);
      }
      const url=URL.createObjectURL(new Blob([bytes],{type:'application/octet-stream'}));
      const a=document.createElement('a');a.href=url;a.download='spectrafx.spft';a.click();window.setTimeout(()=>URL.revokeObjectURL(url),1000);
    }catch(e){setError(message(e));}finally{if(transferId)await invokePreview({action:'checkpointFileCancel',transferId,expectedGeneration:state?.generation}).catch(()=>{});setBusy(false);}
  };
  const importCheckpointFile=async(file:File)=>{
    let transferId:number|undefined;
    setBusy(true);setError('');
    try{
      if(!file.size||file.size>1048600)throw new Error('Checkpoint exceeds size limit');
      const bytes=new Uint8Array(await file.arrayBuffer());
      const begin=await invokePreview({action:'checkpointFileBegin',expectedGeneration:state?.generation});
      if(begin.error||!begin.transferId)throw new Error(begin.error||'Missing transfer ID');
      transferId=begin.transferId;
      for(let offset=0;offset<bytes.length;offset+=32768){
        const chunk=Array.from(bytes.subarray(offset,offset+32768),b=>b.toString(16).padStart(2,'0')).join('');
        const next=await invokePreview({action:'checkpointFileAppend',offset,chunk,transferId,expectedGeneration:state?.generation});
        if(next.error)throw new Error(next.error);
      }
      const result=await invokePreview({action:'checkpointFileImport',transferId,expectedGeneration:state?.generation});
      setState(result);if(result.error)throw new Error(result.error);
      transferId=undefined; // Native queue owns the uploaded bytes.
    }catch(e){if(transferId)await invokePreview({action:'checkpointFileCancel',transferId,expectedGeneration:state?.generation}).catch(()=>{});setError(message(e));}finally{setBusy(false);}
  };
  const launch=async()=>{
    // Actual graph validation and device eligibility occur in StudioScene.
    await command('load',{graph,gpuBirthRandom});
  };
  const setLiveValue=async()=>{
    const numeric=Number(value);
    if(!parameter || !Number.isFinite(numeric)){
      setError('Select a named parameter and enter a finite value.');return;
    }
    if(busy||disabled||!active)return;
    setBusy(true);setError('');
    try{
      const next=await invokePreview({action:'parameter',name:parameter,value:numeric,expectedGeneration:state?.generation});
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
    {replayRestoring&&<div className="spectra-native__hint">
      Replay restore: {state?.tick??0}/{replayPreset?.tick??0} ticks
      <StudioButton size="sm" disabled={replayGeneration.current===undefined} onClick={()=>void cancelReplay()}>Cancel replay</StudioButton>
    </div>}
    {state?.dispatch&&<small className="spectra-native__hint">Native dispatch #{state.dispatch.sequence} · {state.dispatch.action} · {state.dispatch.elapsedMs.toFixed(2)} ms · failed {state.dispatch.failed}</small>}
    <div className="spectra-native__toolbar">
      <StudioButton size="sm" variant="primary" disabled={!active||!state||busy||disabled}
        onClick={()=>void launch()}><Eye size={14}/> Load graph</StudioButton>
      <StudioCheckbox label="GPU birth random (applies on Load)" checked={gpuBirthRandom} disabled={!active||busy}
        onChange={e=>setGpuBirthRandom(e.target.checked)}/>
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
      {capturePending&&<StudioButton size="sm" onClick={()=>void invokePreview({action:'captureCancel',captureId}).then(()=>{setCapturePending(false);setCaptureSlot(null);}).catch(e=>setCaptureError(message(e)))}>Cancel capture</StudioButton>}
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
      <StudioField label="CPU seed (optional)" htmlFor="spectra-replay-seed">
        <StudioInput id="spectra-replay-seed" type="number" min={0} max={2147483647} step={1}
          value={replaySeed} onChange={e=>setReplaySeed(e.target.value)}/>
      </StudioField>
      <StudioField label="Metal slot allocation seed (optional)" htmlFor="spectra-gpu-seed">
        <StudioInput id="spectra-gpu-seed" type="number" min={0} max={2147483647} step={1}
          value={gpuAllocationSeed} onChange={e=>setGpuAllocationSeed(e.target.value)}/>
      </StudioField>
      <StudioButton size="sm" disabled={!state?.gpu||state.playing||busy||state.checkpointPending} onClick={()=>void invokePreview({action:'checkpointSave',expectedGeneration:state?.generation}).then(setState).catch(e=>setError(message(e)))}>Save GPU checkpoint</StudioButton>
      {state?.checkpointPending&&<><small>Checkpoint pending ({state.checkpointWaitFrames??0}/240 frames)</small><StudioButton size="sm" onClick={()=>void invokePreview({action:'checkpointCancel',expectedGeneration:state?.generation}).then(setState).catch(e=>setError(message(e)))}>Cancel checkpoint</StudioButton></>}
      {(state?.checkpoints??[]).map(cp=><StudioButton key={cp.id} size="sm" disabled={busy||!!state?.playing||!!state?.checkpointPending} onClick={()=>void invokePreview({action:'checkpointRestore',checkpointId:cp.id,expectedGeneration:state?.generation}).then(setState).catch(e=>setError(message(e)))}>Restore tick {cp.tick} (#{cp.id})</StudioButton>)}
      <StudioButton size="sm" disabled={busy||!state?.gpu||state.playing||state.checkpointPending} onClick={()=>void exportCheckpointFile()}>Export checkpoint file</StudioButton>
      <label>Import checkpoint <input type="file" accept=".spft" disabled={busy||!state?.gpu||state.playing||state.checkpointPending} onChange={e=>{const file=e.target.files?.[0];if(file)void importCheckpointFile(file);e.target.value='';}}/></label>
      <StudioField label="Metal shader seed (optional)" htmlFor="spectra-shader-seed">
        <StudioInput id="spectra-shader-seed" type="number" min={0} max={4294967295} step={1} value={gpuShaderSeed} onChange={e=>setGpuShaderSeed(e.target.value)}/>
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
      <StudioField label="Search replay presets" htmlFor="spectra-preset-search">
        <StudioInput id="spectra-preset-search" value={presetSearch} onChange={e=>setPresetSearch(e.target.value)}/>
      </StudioField>
      {presetCompatibility&&<small>Direct resource compatibility: {presetCompatibility}</small>}
      <StudioField label="Saved Replay Presets" htmlFor="spectra-preset-library">
        <StudioSelect id="spectra-preset-library" value={presetSelected} disabled={!active||presetLibraryBusy||busy}
          onChange={e=>setPresetSelected(e.target.value)}>
          <option value="">Select saved preset…</option>
          {presetEntries.filter(p=>p.name.includes(presetSearch.toLowerCase().trim())||p.name===presetSelected).map(p=><option key={p.name} value={p.name} disabled={!p.ok}>{p.name}{p.ok?' · tick '+p.tick:' · incompatible'}</option>)}
        </StudioSelect>
      </StudioField>
      <StudioButton size="sm" disabled={!active||!presetSelected||presetLibraryBusy||busy} onClick={()=>void selectNamedPreset()}>Load saved preset</StudioButton>
      <StudioButton size="sm" disabled={!active||presetLibraryBusy||busy} onClick={()=>void refreshPresetLibrary().catch(e=>setError(message(e)))}>Refresh presets</StudioButton>
      <StudioField label="New preset name" htmlFor="spectra-preset-name">
        <StudioInput id="spectra-preset-name" value={presetName} onChange={e=>setPresetName(e.target.value)}/>
      </StudioField>
      <StudioButton size="sm" disabled={!active||!replayPreset||presetLibraryBusy||busy||!(/^[a-z0-9_]{1,48}$/).test(presetName)||presetName==='fx'}
        onClick={()=>void saveNamedPreset()}>Save to project Assets</StudioButton>
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
      {nativeCaptureMismatch.length>0&&<StudioAlert tone="warning">
        Native A/B comparison caveats: {nativeCaptureMismatch.join(', ')}.
      </StudioAlert>}
      <small>Native metadata is sampled when GPU readback is requested, not at fence completion.</small>
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
