import {useId,useState} from 'react';
import {StudioButton} from '../ui';

type Footprint={id:string;x:number;z:number;radius:number;type:'wind'|'storm'|'gravity'};
const colors={wind:'#79d5ed',storm:'#f8b955',gravity:'#bc96ef'};
export default function PhysisZonePlan({zones,selected,group,select,move}:{group?:string[];zones:Footprint[];selected:string;select:(id:string)=>void;move:(x:number,z:number)=>void}){
  const gridId=useId().replace(/:/g,'');
  const [placing,place]=useState(false);
  const left=Math.min(...zones.map(f=>f.x-f.radius)),right=Math.max(...zones.map(f=>f.x+f.radius));
  const top=Math.min(...zones.map(f=>f.z-f.radius)),bottom=Math.max(...zones.map(f=>f.z+f.radius));
  const extent=Math.max(40,right-left,bottom-top)*1.3;
  const originX=(left+right-extent)/2,originZ=(top+bottom-extent)/2;
  const x=(v:number)=>(v-originX)/extent*320,z=(v:number)=>(v-originZ)/extent*320;
  const highlighted=new Set(group??[selected]);
  const ordered=[...zones].sort((a,b)=>b.radius-a.radius);
  const metres=Math.pow(10,Math.floor(Math.log10(extent/4))),spacing=metres/extent*320;
  return <section className="physis-zone-plan"><div className="physis-plan-title"><h4>Zone plan</h4><span>X / Z · {metres} m grid</span></div>
    <svg viewBox="0 0 320 320" aria-label="Map zone footprints" className={placing?'is-placing':''} onClick={e=>{
      if(group||!placing)return;
      const matrix=e.currentTarget.getScreenCTM();if(!matrix)return;
      const point=new DOMPoint(e.clientX,e.clientY).matrixTransform(matrix.inverse());
      move(Math.round((originX+point.x*extent/320)*100)/100,Math.round((originZ+point.y*extent/320)*100)/100);
      place(false);
    }}>
      <defs><pattern id={gridId} x={x(0)} y={z(0)} width={spacing} height={spacing} patternUnits="userSpaceOnUse">
        <path d={'M '+spacing+' 0 H 0 V '+spacing} fill="none" stroke="currentColor" opacity=".32"/></pattern></defs>
      <rect width="320" height="320" fill={'url(#'+gridId+')'}/>
      {ordered.map(f=><g key={f.id} aria-label={f.id} role="button" tabIndex={0}
        onKeyDown={e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();select(f.id);place(false);}}}
        onClick={()=>{if(group||!placing)select(f.id);}}>
        <circle cx={x(f.x)} cy={z(f.z)} r={Math.max(4,f.radius/extent*320)} stroke={colors[f.type]} fill={colors[f.type]}
          strokeWidth={highlighted.has(f.id)?2.4:1} fillOpacity={highlighted.has(f.id) ? .2 : .07}/>
        <circle cx={x(f.x)} cy={z(f.z)} r="3" fill={colors[f.type]}/>
      </g>)}
      {zones.filter(f=>highlighted.has(f.id)).map(f=><text key={f.id} x={Math.min(244,x(f.x)+6)} y={Math.max(12,z(f.z)-6)}>{f.id}{f.type==='gravity'&&f.radius===0?' · Global':''}</text>)}
    </svg>
    {!group&&<StudioButton aria-pressed={placing} onClick={()=>place(v=>!v)}>{placing?'Cancel placement':'Move on plan'}</StudioButton>}
    {!group&&placing&&<span role="status">Choose the new center.</span>}
  </section>;
}
