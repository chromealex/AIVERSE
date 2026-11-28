import {useEffect, useRef, useState} from 'react';
import {invoke} from '../bridge';
import type {StudioGraphDocumentScope} from '../types';

export default function PlacementGhost({scope,type,x,z,name}:{scope:StudioGraphDocumentScope;type:string;x:string;z:string;name:string}) {
  const canvas=useRef<HTMLCanvasElement>(null);
  const [ready,setReady]=useState(false);
  const [error,setError]=useState('');
  const queue=useRef(Promise.resolve());
  const {epoch,revision}=scope;
  useEffect(()=>{
    let active=true;
    setReady(false);setError('');
    canvas.current?.getContext('2d')?.clearRect(0,0,512,512);
    const timer=window.setTimeout(()=>{
      queue.current=queue.current.then(async ()=>{
        if(!active)return;
        try {
          const frame=await invoke('studio.placementGhost',{placeType:type,
            x:x===''?undefined:Number(x),z:z===''?undefined:Number(z),
            expectedDocumentEpoch:epoch,expectedDocumentRevision:revision});
          if(!active)return;
          const image=new Image();
          image.src=frame.url;
          await image.decode();
          if(!active)return;
          const ctx=canvas.current?.getContext('2d');
          if(!ctx)throw Error('Placement canvas unavailable');
          ctx.drawImage(image,0,0,512,512);
          setReady(true);
        } catch(e){if(active)setError(String(e));}
      });
    },120);
    return ()=>{active=false;window.clearTimeout(timer);};
  },[epoch,revision,type,x,z]);
  return <div className="placement-model-preview" aria-busy={!ready&&!error}>
    <canvas ref={canvas} width={512} height={512} role="img" aria-label={name+' placement model'}/>
    {error?<p role="alert">{error}</p>:!ready&&<p role="status">Rendering model…</p>}
  </div>;
}
