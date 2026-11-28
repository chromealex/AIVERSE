import { useCallback, useEffect, useState } from 'react';
import { invoke } from '../bridge';
import type { StudioBuildLog } from '../types';

const empty:StudioBuildLog={from:0,next:0,run:0,runs:0,size:0,text:'',label:'',logFile:'',exitCode:0,running:false};

export function useBuildTranscript(project:string,limit?:number) {
  const [state,setState]=useState(empty);
  const [error,setError]=useState('');
  const [revision,setRevision]=useState(0);
  useEffect(()=>{setState(empty);setError('');},[project]);
  useEffect(()=>{
    let live=true,timer=0,from=0,run=-1;
    if(!project)return;
    async function poll(){
      let delay=2000;
      try{
        const chunk=await invoke('studio.buildLog',{project,from,run});
        if(!live)return;
        const changed=run!==chunk.run;
        run=chunk.run;from=chunk.next;
        setState(previous=>{
          if(!changed&&!chunk.text&&previous.running===chunk.running&&previous.runs===chunk.runs&&previous.exitCode===chunk.exitCode)return previous;
          const text=changed?chunk.text:previous.text+chunk.text;
          return {...chunk,text:limit&&text.length>limit?text.slice(-limit):text};
        });
        setError('');
        delay=chunk.next<chunk.size?0:chunk.running?400:2000;
      }catch(e){if(!live)return;setError(e instanceof Error?e.message:String(e));delay=3000;}
      timer=window.setTimeout(poll,delay);
    }
    void poll();
    return ()=>{live=false;window.clearTimeout(timer);};
  },[project,limit,revision]);
  const reload=useCallback((label:string)=>{
    setState(previous=>({...empty,runs:previous.runs,running:true,label}));
    setRevision(v=>v+1);
  },[]);
  return {state,error,reload};
}
