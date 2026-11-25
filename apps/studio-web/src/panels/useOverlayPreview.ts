import { useCallback, useEffect, useRef, useState } from 'react';
import { invoke, onNativeEvent } from '../bridge';
import type { StudioGraphDocumentScope } from '../types';

type PreviewState = { epoch:string; version:number; preview:string[]; placeType?:string; x?:number; z?:number };
type Patch = { entry?:{id:string;visible:boolean}; placeType?:string; x?:number|null; z?:number|null };

export function useOverlayPreview(scope?:StudioGraphDocumentScope) {
  const [state,setState] = useState<PreviewState>();
  const [error,setError] = useState('');
  const {epoch,projectPath} = scope??{};
  const live = useRef(false);
  const accept = useCallback((value:unknown)=>{
    const next=value as PreviewState;
    if(!live.current || next?.epoch!==epoch || !Array.isArray(next.preview))return;
    setState(previous=>previous && previous.version>next.version?previous:next);
  },[epoch]);
  const request=useCallback(async(patch?:Patch)=>{
    if(!projectPath || !epoch)return;
    try {
      const reply=await invoke('studio.op',{op:'overlay_preview',params:patch?{...patch,reveal:false}:{},
        expectedProject:projectPath,expectedDocumentEpoch:epoch});
      if(live.current){accept(reply.result);setError('');}
    } catch(e){if(live.current)setError(String(e));}
  },[projectPath,epoch,accept]);
  useEffect(()=>{
    live.current=true;
    const stop=onNativeEvent((event,payload)=>{if(event==='overlay.preview')accept(payload);});
    void request();
    return ()=>{live.current=false;stop();};
  },[accept,request]);
  return {state,error,request};
}
