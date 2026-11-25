import { useEffect, useRef, useState } from 'react';
import { invoke } from '../bridge';
import type { StudioGraphDocumentScope } from '../types';
import { StudioAlert, StudioSection } from '../ui';

type Plan = {catalog:string[]; terrain:string[]; stride:number; width:number; height:number; objects:number[][]; primitives:number[][]};

type PickPosition = (x:number,z:number)=>void;

function PlanCanvas({plan,pick}:{plan:Plan;pick:PickPosition}) {
  const ref = useRef<HTMLCanvasElement>(null);
  const bounds = useRef({scale:0,x:0,z:0});
  useEffect(()=>{
    const canvas = ref.current;
    if (!canvas) return;
    const draw = ()=>{
      const w = canvas.clientWidth, h = canvas.clientHeight;
      if (!w || !h) return;
      const ratio = window.devicePixelRatio || 1;
      canvas.width = Math.round(w * ratio); canvas.height = Math.round(h * ratio);
      const ctx = canvas.getContext('2d');
      if (!ctx) return;
      const scale = Math.min((w-16)/plan.width,(h-16)/plan.height);
      if(scale<=0)return;
      ctx.scale(ratio,ratio);
      bounds.current={scale,x:(w-plan.width*scale)/2,z:(h-plan.height*scale)/2};
      ctx.translate(bounds.current.x,bounds.current.z);
      ctx.scale(scale,scale);
      ctx.fillStyle='#203344'; ctx.fillRect(0,0,plan.width,plan.height);
      ctx.save();
      ctx.beginPath();ctx.rect(0,0,plan.width,plan.height);ctx.clip();
      plan.terrain.forEach((row,z)=>Array.from(row).forEach((tile,x)=>{
        if(tile==='.')return;
        ctx.fillStyle=tile==='~'||tile==='='?'#355d80':tile==='#'||tile==='R'?'#555c65':'#394948';
        ctx.fillRect(x*plan.stride,z*plan.stride,plan.stride,plan.stride);
      }));
      ctx.restore();
      ctx.fillStyle='#a5b8c6';
      for(const [x,z] of plan.objects){
        ctx.beginPath();ctx.arc(x,z,2.4/scale,0,2*Math.PI);ctx.fill();
      }
      ctx.lineWidth=1.5/scale;
      let opaqueFill:number|undefined;
      const flush=()=>{if(opaqueFill!==undefined){ctx.fill();opaqueFill=undefined;}};
      for(const [packed,x1,z1,x2,z2,x3,z3] of plan.primitives){
        const color = `rgba(${packed&255},${(packed>>>8)&255},${(packed>>>16)&255},${(packed>>>24)/255})`;
        const opaqueTriangle = x3!==undefined && (packed>>>24)===255;
        if(!opaqueTriangle || opaqueFill!==packed){
          flush();ctx.beginPath();ctx.fillStyle=color;
          if(opaqueTriangle)opaqueFill=packed;
        }
        ctx.moveTo(x1,z1);ctx.lineTo(x2,z2);
        if(x3===undefined){ctx.strokeStyle=color;ctx.stroke();}
        else {ctx.lineTo(x3,z3);ctx.closePath();if(!opaqueTriangle)ctx.fill();}
      }
      flush();
    };
    const observer=new ResizeObserver(draw);
    observer.observe(canvas);draw();return ()=>observer.disconnect();
  },[plan]);
  return <canvas ref={ref} role="img" aria-label="Ground overlays on the open map"
    onClick={event=>{
      const rect=event.currentTarget.getBoundingClientRect(), b=bounds.current;
      const x=(event.clientX-rect.left-b.x)/b.scale, z=(event.clientY-rect.top-b.z)/b.scale;
      if(x>=0 && x<plan.width && z>=0 && z<plan.height)pick(Math.floor(x*100)/100,Math.floor(z*100)/100);
    }}
    style={{cursor:'crosshair',width:'100%',height:240,display:'block',background:'var(--au-surface)',borderRadius:8}}/>;
}

export default function OverlayPlan({scope,placeType,x,z,pick,selectionKey,ids,setVisible}:{ids:string[];setVisible:(id:string,visible:boolean)=>void;selectionKey:string;pick:PickPosition;scope:StudioGraphDocumentScope;placeType:string;x:string;z:string}) {
  const [catalog,setCatalog]=useState<string[]>([]);
  const [plan,setPlan]=useState<Plan>();
  const [error,setError]=useState('');
  const {epoch,revision,projectPath}=scope;
  useEffect(()=>{
    let live=true;
    const timer=window.setTimeout(()=>{
      const params:Record<string,unknown>={ids,placeType};
      if(x!=='')params.x=Number(x);
      if(z!=='')params.z=Number(z);
      void invoke('studio.op',{op:'overlay_plan_preview',params,expectedProject:projectPath,expectedDocumentEpoch:epoch,expectedDocumentRevision:revision})
        .then(r=>{
          if(!live)return;
          const data=r.result as Plan;
          setCatalog(data.catalog);setPlan(data.primitives?data:undefined);setError('');
        }).catch(e=>{if(live){setPlan(undefined);setError(String(e));}});
    },100);
    return ()=>{live=false;window.clearTimeout(timer);};
  },[epoch,revision,projectPath,ids,placeType,x,z,selectionKey]);
  return <StudioSection title="Ground overlays">
    <div role="group" aria-label="Preview overlays" style={{display:'flex',flexWrap:'wrap',gap:'6px 14px',marginBottom:10}}>
      {catalog.map(id=><label key={id}><input type="checkbox" checked={ids.includes(id)}
        onChange={e=>setVisible(id,e.target.checked)}/> {id}</label>)}
    </div>
    {error && <StudioAlert tone="danger">{error}</StudioAlert>}
    {plan && <><PlanCanvas plan={plan} pick={pick}/>{!plan.primitives.length && <p>No visible geometry at this position.</p>}</>}
  </StudioSection>;
}
