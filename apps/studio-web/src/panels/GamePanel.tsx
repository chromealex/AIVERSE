import { useEffect, useRef, useState } from 'react';
import { dockDragActive, watchDockDrag, watchOverlays, viewportRectChanged, forgetViewportRect, visibleViewport } from '../nativeOverlay';
import { Menu, RotateCcw } from 'lucide-react';
import { invoke } from '../bridge';
import type { StudioGameState, StudioGameDevice, StudioGameDeviceOption } from '../types';
import type { StudioPanelContext } from './StudioPanelRenderer';
import { StudioPanel, StudioButton, StudioAlert, StudioEmptyState, StudioBadge, StudioSelect, StudioField, StudioCheckbox } from '../ui';
import './GamePanel.css';
import { gameViewportSize } from './gameViewport';

export default function GamePanel({snapshot,connected,busy,setBusy}:StudioPanelContext) {
  const [state,setState]=useState<StudioGameState|null>(null);
  const [error,setError]=useState('');
  const [devices,setDevices]=useState<StudioGameDeviceOption[]>([]);
  const [areaSize,setAreaSize]=useState({width:0,height:0});
  const area=useRef<HTMLDivElement>(null);
  const surface=useRef<HTMLDivElement>(null);
  useEffect(()=>{
    let live=true;
    if(connected)void invoke('studio.gameDevices',{}).then(v=>{if(live)setDevices(v);}).catch(e=>{if(live)setError(String(e));});
    return ()=>{live=false;};
  },[connected]);
  useEffect(()=>{
    if(!area.current)return;
    const observer=new ResizeObserver(entries=>{
      const b=entries[0].contentRect;setAreaSize({width:b.width,height:b.height});
    });observer.observe(area.current);return ()=>observer.disconnect();
  },[]);
  const device=state?.device;
  // A device preview is drawn as the device (legacy Game window): a bezel around the screen; the native
  // view draws the rounded screen corners, the camera cut-out and the safe area over the game.
  const framed=!!device?.width;
  const bezel=framed?(device?.touch?14:6):0;
  const fitted=gameViewportSize(areaSize.width-(framed?2*bezel+8:0),areaSize.height-(framed?2*bezel+8:0),device?.width||0,device?.height||0);
  const screenScale=framed&&device?.width?fitted.width/device.width:0;
  const surfaceStyle=framed?{...fitted,borderRadius:(device?.chrome?.corner??0)*screenScale,
    boxShadow:`0 0 0 ${bezel}px #0c0c0e, 0 0 0 ${bezel+1}px #ffffff1f, 0 12px 32px ${bezel}px #0009`}:fitted;
  const loaded=connected && !!snapshot?.project.path;
  useEffect(()=>{
    let live=true,inFlight=false;
    const refresh=async()=>{
      if(!connected || inFlight)return;
      inFlight=true;
      try {const value=await invoke('studio.gameState',{});if(live)setState(value);}
      catch(e){if(live)setError(String(e));}
      finally{inFlight=false;}
    };
    void refresh();const timer=window.setInterval(()=>void refresh(),300);
    return ()=>{live=false;window.clearInterval(timer);};
  },[connected]);
  useEffect(()=>{
    if(!connected || !surface.current)return;
    const host=surface.current;let frame=0;
    const update=()=>{
      cancelAnimationFrame(frame);
      frame=requestAnimationFrame(()=>{
        const b=host.getBoundingClientRect();
        const fit=visibleViewport(b);
        const rect={x:fit.x,y:fit.y,width:fit.width,height:fit.height,
          dpr:window.devicePixelRatio||1,visible:!!state?.active && document.visibilityState==='visible' && !dockDragActive() && fit.visible};
        if(!viewportRectChanged('game',rect))return;
        void invoke('studio.viewportRect',{id:'game',...rect}).catch(e=>{forgetViewportRect('game');setError(String(e));});
      });
    };
    const stopOverlays=watchOverlays(update);
    const stopDockDrag=watchDockDrag(update);
    const observer=new ResizeObserver(update);observer.observe(host);
    window.addEventListener('resize',update);window.addEventListener('scroll',update,true);
    document.addEventListener('visibilitychange',update);update();
    return ()=>{
      cancelAnimationFrame(frame);observer.disconnect();stopOverlays();stopDockDrag();
      window.removeEventListener('resize',update);window.removeEventListener('scroll',update,true);
      document.removeEventListener('visibilitychange',update);
      forgetViewportRect('game');
      void invoke('studio.viewportRect',{id:'game',x:0,y:0,width:0,height:0,dpr:1,visible:false}).catch(()=>{});
    };
  },[connected,state?.active]);
  const act=async(entry:'map'|'menu'|'stop')=>{
    if(busy)return;setBusy(true);setError('');
    try {setState(entry==='stop'?await invoke('studio.gameStop',{}):await invoke('studio.gameStart',{entry}));}
    catch(e){setError(e instanceof Error?e.message:String(e));}
    finally{setBusy(false);}
  };
  const control=async(action:'pause'|'resume'|'step'|'restart')=>{
    if(busy)return;setBusy(true);setError('');
    try{setState(await invoke('studio.gameControl',{action}));}
    catch(e){setError(e instanceof Error?e.message:String(e));}
    finally{setBusy(false);}
  };
  const configure=async(change:Partial<Pick<StudioGameDevice,'id'|'landscape'|'safeArea'>>)=>{
    if(busy)return;setBusy(true);setError('');
    try{setState(await invoke('studio.gameDevice',{id:device?.id||'none',landscape:device?.landscape??true,safeArea:device?.safeArea??true,...change}));}
    catch(e){setError(e instanceof Error?e.message:String(e));}
    finally{setBusy(false);}
  };
  return <StudioPanel title="Game" className="studio-game" flush
    actions={<>
      {/* Play / Pause / Step / Stop live in the title bar (⌘P); only what it lacks is here. */}
      <StudioButton disabled={!loaded||busy||!!state?.active} onClick={()=>void act('menu')} title="Start from the project's own menus"><Menu size={14}/>Project menu</StudioButton>
      <StudioButton disabled={!state?.active||busy} onClick={()=>void control('restart')}><RotateCcw size={14}/>Restart</StudioButton>
    </>}
    footer={<><StudioBadge>{state?.active?(state.loading?`Loading ${Math.round(state.progress*100)}%`:state.inMatch?`${state.paused?'Paused':'Running'} · tick ${state.tick}`:'Project menu'):'Stopped'}</StudioBadge><span>Native game client · click the viewport to focus input</span></>}>
    {(error||state?.error) && <StudioAlert tone="danger">{error||state?.error}</StudioAlert>}
    <div className="studio-game__devices">
      <StudioField label="Device" htmlFor="studio-game-device"><StudioSelect id="studio-game-device" aria-label="Game device" value={device?.id||'none'} disabled={!connected||busy} onChange={e=>void configure({id:e.target.value})}>
        {!devices.length && <option value="none">Free size</option>}
        {devices.map(d=><option key={d.id} value={d.id}>{d.group?`${d.group} · `:''}{d.name}</option>)}
      </StudioSelect></StudioField>
      <StudioButton disabled={!device?.width||busy} onClick={()=>void configure({landscape:!device?.landscape})}>{device?.landscape?'Landscape':'Portrait'}</StudioButton>
      <StudioCheckbox label="Safe area" checked={device?.safeArea??true} disabled={!device?.width||busy} onChange={e=>void configure({safeArea:e.target.checked})}/>
      {!!device?.width && <StudioBadge>{device.width} × {device.height} · {device.touch?'Touch':'Mouse'}</StudioBadge>}
    </div>
    <div ref={area} className="studio-game__area">
    <div style={surfaceStyle} ref={surface} className={'studio-game__surface'+(framed?' studio-game__surface--device':'')}
      data-orientation={device?.landscape?'landscape':'portrait'} aria-label="Native Game viewport">
      {!state?.active && <StudioEmptyState title={loaded?'Ready to play':'Open a project'}
        description={loaded?'Run the saved map or open the project’s own menus. Stop returns to authoring without copying runtime changes into the map.':'Play uses the native game client and renderer.'}/>}
    </div>
    </div>
  </StudioPanel>;
}
