import {useState} from 'react';
import {getNodeValue,type Node} from 'jsonc-parser';
import {StudioAlert,StudioButton,StudioSelect} from '../ui';
import {EffectNumber} from './EffectRangeEditor';

type Patch=(path:(number|string)[],value:unknown)=>void;
const parameters=[
  {key:'radius',label:'Radius (m)',min:.01,max:1000,initial:5},
  {key:'startRadius',label:'Inner radius (m)',min:0,max:1000,initial:0},
  {key:'length',label:'Length (m)',min:0,max:1000,initial:0},
  {key:'gravity',label:'Attraction',min:-1000,max:1000,initial:0},
  {key:'rotation',label:'Vortex (°/s)',min:-36000,max:36000,initial:0},
  {key:'drag',label:'Drag',min:0,max:100,initial:0},
];
export default function EffectForceFields({fields,busy,patch,remove}:{
  fields:Node|undefined;busy:boolean;patch:Patch;remove:(index:number)=>void
}){
  const [selection,select]=useState(0);
  const nodes=fields?.children??[];
  const index=Math.min(selection,Math.max(0,nodes.length-1));
  const node=nodes[index];
  const field=node?.type==='object'?getNodeValue(node):undefined;
  const shape=field?.shape??'sphere';
  const path=['forceFields',index];
  function setVector(key:string,axis:number,value:number){
    if(Array.isArray(field[key])&&field[key].length===3)patch([...path,key,axis],value);
    else patch([...path,key],[0,1,2].map(i=>i===axis?value:0));
  }
  return <details className="effects-force-fields"><summary>Force fields{nodes.length?' · '+nodes.length:''}</summary>
    {fields&&fields.type!=='array'&&fields.type!=='null'?<StudioAlert tone="warning">Correct forceFields in Effect source to edit fields.</StudioAlert>:
    <fieldset disabled={busy}>
      <p>Fields act in effect space. Set an emitter’s external force multiplier above zero to affect its particles.</p>
      <div className="au-row">
        {nodes.length>0&&<StudioSelect aria-label="Selected force field" value={index} onChange={e=>select(Number(e.target.value))}>
          {nodes.map((entry,i)=><option key={i} value={i}>Field {i+1} · {entry.type==='object'?String(getNodeValue(entry).shape??'sphere'):'invalid'}</option>)}
        </StudioSelect>}
        <StudioButton disabled={nodes.length>=8} onClick={()=>{
          if(fields?.type==='array')patch(['forceFields',nodes.length],{});else patch(['forceFields'],[{}]);
          select(nodes.length);
        }}>Add field</StudioButton>
        {nodes.length>0&&<StudioButton onClick={()=>{remove(index);select(Math.max(0,Math.min(index,nodes.length-2)));}}>Remove field</StudioButton>}
      </div>
      {field&&<div key={index}>
        <div className="effects-emitter-fields">
          <label>Field shape<StudioSelect value={shape} onChange={e=>patch([...path,'shape'],e.target.value)}>
            <option value="sphere">Sphere</option><option value="cylinder">Cylinder</option>
          </StudioSelect></label>
          {parameters.filter(p=>p.key!=='length'||shape==='cylinder').map(p=><EffectNumber key={p.key} label={p.label}
            min={p.min} max={p.max} value={field[p.key]??p.initial} change={v=>patch([...path,p.key],v)}/>)}
        </div>
        {(['position','direction'] as const).map(key=><fieldset className="effects-vector" key={key}>
          <legend>{key==='position'?'Position (m)':'Constant force'}</legend>
          {['X','Y','Z'].map((axis,i)=><EffectNumber key={axis} label={axis} value={field[key]?.[i]??0} min={-1000} max={1000}
            change={v=>setVector(key,i,v)}/>)}
        </fieldset>)}
        <div className="effects-emitter-fields">
          {([['gravityFocus','Gravity focus'],['rotationAttraction','Vortex attraction']] as const).map(([key,label])=>
            <label key={key}>{label} · {field[key]??0}<input aria-label={label} type="range" min="0" max="1" step=".01"
              value={field[key]??0} onChange={e=>patch([...path,key],Number(e.target.value))}/></label>)}
        </div>
      </div>}
    </fieldset>}
  </details>;
}
