import { useEffect, useState } from 'react';
import { ArchiveRestore, LocateFixed, Play, Pause, Square, StepForward } from 'lucide-react';
import { invoke } from '../bridge';
import type { StudioGameState } from '../types';

/** Play / Pause / Step / Stop, centred in the title bar (Unity layout). */
export default function HeaderGameControls({enabled,onPlay,onError,onState}:{
  enabled:boolean;onPlay:()=>void;onError:(message:string)=>void;onState?:(state:StudioGameState|null)=>void;
}) {
  const [state,setState]=useState<StudioGameState|null>(null);
  const [pending,setPending]=useState(false);
  const [note,setNote]=useState('');
  useEffect(()=>{ if(!note)return; const t=window.setTimeout(()=>setNote(''),4000); return ()=>window.clearTimeout(t); },[note]);
  useEffect(()=>{ onState?.(state); },[state,onState]);
  useEffect(()=>{
    if(!enabled){setState(null);return;}
    let live=true,inFlight=false;
    const refresh=async()=>{
      if(inFlight)return;inFlight=true;
      try {const next=await invoke('studio.gameState',{});if(live)setState(next);}
      catch {} finally {inFlight=false;}
    };
    void refresh();const timer=window.setInterval(refresh,500);
    return ()=>{live=false;window.clearInterval(timer);};
  },[enabled]);
  const action=async(kind:'play'|'here'|'pause'|'step'|'stop')=>{
    setPending(true);
    try {
      if(kind==='play'||kind==='here') {onPlay();setState(await invoke('studio.gameStart',{entry:'map',fromScene:kind==='here'}));}
      else if(kind==='stop')setState(await invoke('studio.gameStop',{}));
      else if(kind==='step')setState(await invoke('studio.gameControl',{action:'step'}));
      else setState(await invoke('studio.gameControl',{action:state?.paused?'resume':'pause'}));
    }catch(e){onError(e instanceof Error?e.message:String(e));}
    finally{setPending(false);}
  };
  // ⌘P toggles play, ⇧⌘P pause, ⌥⌘P step (App dispatches the shortcut).
  useEffect(()=>{
    const control=(event:Event)=>{
      const kind=(event as CustomEvent<string>).detail;
      if(pending)return;
      if(kind==='toggle'){ if(state?.active)void action('stop'); else if(enabled)void action('play'); }
      else if(kind==='pause' && state?.canControl)void action('pause');
      else if(kind==='step' && state?.canStep && state?.paused)void action('step');
    };
    window.addEventListener('studio:play-control',control);
    return ()=>window.removeEventListener('studio:play-control',control);
  });
  const playing=!!state?.active;
  // Keep changes is reviewed in the Game panel, never applied by a title-bar shortcut.
  const keep=()=>{onPlay();setNote('Game → Review changes');};
  return <div className={'header-game-controls'+(playing?' is-playing':'')} role="group" aria-label="Play controls">
    {playing
      ? <button className="icon-button is-on" aria-label="Stop" title="Stop (⌘P)" disabled={pending}
          onClick={()=>void action('stop')}><Square size={14}/></button>
      : <button className="icon-button" aria-label="Play" title="Play saved map (⌘P) · ⌥-click: from the Scene view" disabled={!enabled||pending}
          onClick={e=>void action(e.altKey?'here':'play')}><Play size={16}/></button>}
    {!playing && <button className="icon-button" aria-label="Play from here" title="Play from here: start with the Scene view's camera"
      disabled={!enabled||pending} onClick={()=>void action('here')}><LocateFixed size={15}/></button>}
    <button className={'icon-button'+(state?.paused?' is-on':'')} aria-label={state?.paused?'Resume':'Pause'} aria-pressed={!!state?.paused}
      title="Pause (⇧⌘P)" disabled={!state?.canControl||pending} onClick={()=>void action('pause')}><Pause size={16}/></button>
    <button className="icon-button" aria-label="Step one tick" title="Step one tick (⌥⌘P)"
      disabled={!state?.canStep||!state?.paused||pending} onClick={()=>void action('step')}><StepForward size={16}/></button>
    {playing && <button className="icon-button" aria-label="Keep play changes"
      title="Open Game to review and selectively keep play changes"
      disabled={!state?.inMatch||pending} onClick={keep}><ArchiveRestore size={15}/></button>}
    {note && <span className="header-game-note" role="status">{note}</span>}
  </div>;
}
