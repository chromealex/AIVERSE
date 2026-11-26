import {useState,type ReactNode} from 'react';
import {StudioAlert,StudioButton,StudioInput,StudioSelect} from '../ui';

const properties=[
  {key:'life',label:'Lifetime (s)',min:.01,max:60,initial:[.5,1]},
  {key:'speed',label:'Speed (m/s)',min:-500,max:500,initial:[1,2]},
  {key:'size',label:'Size (m)',min:.001,max:100,initial:[.3,.5]},
  {key:'rate',label:'Emission per second',min:0,max:2000,initial:0},
  {key:'perMeter',label:'Emission per metre',min:0,max:200,initial:0},
  {key:'delay',label:'Start delay (s)',min:0,max:60,initial:0},
  {key:'rotation',label:'Rotation (°)',min:-3600,max:3600,initial:0},
  {key:'spin',label:'Spin (°/s)',min:-36000,max:36000,initial:0},
  {key:'drag',label:'Drag',min:0,max:50,initial:0},
];
type Path=(string|number)[];
type Patch=(path:Path,value:unknown)=>void;
const numeric=(v:unknown):v is number=>typeof v==='number'&&Number.isFinite(v);
const pair=(v:unknown):v is [number,number]=>Array.isArray(v)&&v.length===2&&v.every(numeric);
const keys=(v:unknown):v is [number,number][]=>Array.isArray(v)&&v.length>0&&v.every(pair);

export function EffectNumber({label,value,min,max,change,step="any"}:{step?:number|"any";label:string;value:number;min:number;max:number;change:(v:number)=>void}){
  const [typing,setTyping]=useState<string|null>(null);
  return <label>{label}<StudioInput type="number" step={step} min={min} max={max} value={typing??value}
    onChange={e=>{
      setTyping(e.target.value);
      if(e.target.validity.valid && numeric(e.target.valueAsNumber))change(e.target.valueAsNumber);
    }} onBlur={()=>setTyping(null)} onKeyDown={e=>{if(e.key==='Enter')e.currentTarget.blur();}}/></label>;
}

function CurveKeys({label,curve,path,min,max,patch}:{label:string;curve:unknown;path:Path;min:number;max:number;patch:Patch}){
  const points=numeric(curve)?[[0,curve],[1,curve]]:curve;
  if(!keys(points))return <StudioAlert tone="warning">Correct the curve keys in Effect source.</StudioAlert>;
  const validPoints=points;
  const rows=points.map(([time,value],index)=>({time,value,index})).sort((a,b)=>a.time-b.time);
  const ordered=rows.map(row=>points[row.index]);
  const bottom=ordered.reduce((a,b)=>Math.min(a,b[1]),Infinity);
  const top=ordered.reduce((a,b)=>Math.max(a,b[1]),-Infinity);
  const y=(v:number)=>top===bottom?28:50-(v-bottom)/(top-bottom)*44;
  const line=ordered.map(([t,v])=>Math.max(0,Math.min(1,t))*300+','+y(v)).join(' ');
  function setPoint(i:number,axis:number,v:number){
    if(numeric(curve))patch(path,validPoints.map((point,j)=>j===i?point.map((n,k)=>k===axis?v:n):point));
    else patch([...path,i,axis],v);
  }

  return <fieldset className="effects-key-list"><legend>{label}</legend>
    <svg className="effects-key-plot" viewBox="0 0 300 56" preserveAspectRatio="none" role="img" aria-label={label+' over normalized time'}>
      <polyline points={'0,'+y(ordered[0][1])+' '+line+' 300,'+y(ordered[ordered.length-1][1])} fill="none" stroke="var(--au-accent)" strokeWidth="2" vectorEffect="non-scaling-stroke"/>
      {rows.map(row=><circle key={row.index} cx={Math.max(0,Math.min(1,row.time))*300} cy={y(row.value)} r="2" fill="var(--au-text)"/>)}
    </svg>
    {rows.map(({time:t,value:v,index:i},order)=><div className="effects-key-row" key={i}>
      <EffectNumber label={'Time '+(order+1)} value={t} min={0} max={1} change={n=>setPoint(i,0,n)}/>
      <EffectNumber label={'Value '+(order+1)} value={v} min={min} max={max} change={n=>setPoint(i,1,n)}/>
      <StudioButton disabled={points.length<2} aria-label={'Remove key '+(order+1)} onClick={()=>{
        if(numeric(curve))patch(path,points.filter((_,j)=>j!==i));else patch([...path,i],undefined);
      }}>Remove</StudioButton>
    </div>)}
    <StudioButton onClick={()=>{
      const stops=[[0,ordered[0][1]],...ordered,[1,ordered[ordered.length-1][1]]];
      let gap=0;
      for(let i=1;i<stops.length-1;i++){
        if(stops[i+1][0]-stops[i][0]>stops[gap+1][0]-stops[gap][0])gap=i;
      }
      const next=[(stops[gap][0]+stops[gap+1][0])/2,(stops[gap][1]+stops[gap+1][1])/2];
      if(numeric(curve))patch(path,[...points,next]);else patch([...path,points.length],next);
    }}>Add key</StudioButton>
  </fieldset>;
}

export default function EffectRangeEditor({emitter,patch}:{emitter:Record<string,unknown>;patch:Patch}){
  const [selected,setSelected]=useState('life');
  const p=properties.find(prop=>prop.key===selected)!;
  return <details className="effects-range"><summary>Emission and particle motion</summary>
    <EffectRangeValue emitter={emitter} p={p} patch={patch} selector={<label>Parameter<StudioSelect value={selected} onChange={e=>setSelected(e.target.value)}>
        {properties.map(prop=><option key={prop.key} value={prop.key}>{prop.label}</option>)}
      </StudioSelect></label>}/>
  </details>;
}

export function EffectRangeValue({emitter,p,patch,selector}:{emitter:Record<string,unknown>;p:typeof properties[number];patch:Patch;selector?:ReactNode}){
  const raw=emitter[p.key]??p.initial;
  const obj=raw&&typeof raw==='object'&&!Array.isArray(raw)?raw as Record<string,unknown>:undefined;
  const mode=numeric(raw)?'constant':pair(raw)?'range':keys(raw)||obj?.curve!==undefined?'curve':'invalid';
  const bounds=pair(raw)?raw:numeric(raw)?[raw,raw]:undefined;
  const lowIndex=bounds&&bounds[0]>bounds[1]?1:0;
  const highIndex=1-lowIndex;
  function editBound(position:number,value:number){
    if(pair(emitter[p.key]))patch([p.key,position],value);
    else if(pair(raw))patch([p.key],raw.map((n,i)=>i===position?value:n));
  }
  function changeMode(next:string){
    const source=obj?.curve??raw;
    const anchor=bounds?bounds[highIndex]:numeric(source)?source:keys(source)?source[0][1]:p.min;
    if(next==='constant')patch([p.key],bounds?(bounds[0]+bounds[1])/2:anchor);
    if(next==='range')patch([p.key],[anchor,anchor]);
    if(next==='curve')patch([p.key],{curve:[[0,anchor],[1,anchor]]});
  }
  return <>
    <div className="effects-emitter-fields">
      {selector}
      <label>Value mode<StudioSelect value={mode} onChange={e=>changeMode(e.target.value)}>
        {mode==='invalid'&&<option value="invalid">Invalid source</option>}
        <option value="constant">Constant</option><option value="range">Random range</option><option value="curve">Curve</option>
      </StudioSelect></label>
    </div>
    <div className="effects-emitter-fields" key={p.key+mode}>
      {numeric(raw)&&<EffectNumber label="Value" value={raw} min={p.min} max={p.max} change={v=>patch([p.key],v)}/>}
      {pair(raw)&&<>
        <EffectNumber label="Minimum" value={raw[lowIndex]} min={p.min} max={raw[highIndex]} change={v=>editBound(lowIndex,v)}/>
        <EffectNumber label="Maximum" value={raw[highIndex]} min={raw[lowIndex]} max={p.max} change={v=>editBound(highIndex,v)}/>
      </>}
    </div>
    {mode==='curve'&&<>
      <CurveKeys label="Value curve" curve={obj?obj.curve:raw} path={obj?[p.key,'curve']:[p.key]} min={p.min} max={p.max} patch={patch}/>
      <label className="effects-emitter-loop"><input type="checkbox" checked={obj?.curveMin!==undefined} onChange={e=>{
        if(!obj)patch([p.key],{curve:raw,curveMin:raw});
        else patch([p.key,'curveMin'],e.target.checked?obj.curve:undefined);
      }}/>Random between two curves</label>
      {obj?.curveMin!==undefined&&<CurveKeys label="Minimum curve" curve={obj.curveMin} path={[p.key,'curveMin']} min={p.min} max={p.max} patch={patch}/>}
    </>}
  </>;
}
