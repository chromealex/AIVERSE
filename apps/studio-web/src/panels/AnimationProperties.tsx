import {useState} from 'react';
import {Field,type Value} from './RigInspector';

type Fields=Record<string,Value>;
type Collection=Fields|Value[];
const defaults:Record<string,Value>={Number:0,Boolean:false,Text:'',Vector:[0,0,0],Object:{},Array:[]};

function PropertyList({name,value,change,reserved=[],parameters=false}:{parameters?:boolean;name:string;value:Collection;change:(v:Collection)=>void;reserved?:string[]}){
 const [key,setKey]=useState(''),[type,setType]=useState('Number');
 const array=Array.isArray(value);
 const entries=Object.entries(value);
 const field=key.trim();
 const add=()=>{
  const item=structuredClone(defaults[type]);
  if(array)change([...value,item]);
  else if(field&&!reserved.includes(field)&&!Object.hasOwn(value,field)){change({...value,[field]:item});setKey('');}
 };
 return <div className="animation-properties">
  {entries.map(([k,v])=>{
   const label=array?String(Number(k)+1):k;
   const update=(next:Value)=>change(array?value.map((old,i)=>String(i)===k?next:old):{...value,[k]:next});
   const remove=()=>{
    if(array)change(value.filter((_,i)=>String(i)!==k));
    else{const remaining={...value};delete remaining[k];change(remaining);}
   };
   return <div key={k} className="animation-property">
    {parameters&&Array.isArray(v)&&v.length===3&&v.every(n=>typeof n==='number')?
     <Field name={label} value={v} change={update} presets={{}}/>:v!==null&&typeof v==='object'?<details><summary>{label} ({Object.keys(v).length})</summary>
     <PropertyList name={label} value={v} change={update}/></details>:
     v===null?<span>{label} · null</span>:<Field name={label} value={v} change={update} presets={{}}/>}
    <button type="button" aria-label={'Remove '+name+' '+label} onClick={remove}>Remove</button>
   </div>;
  })}
  <div className="animation-property-add">
   {!array&&<input aria-label={'New '+name+' property'} placeholder="Property name" value={key} onChange={e=>setKey(e.target.value)}/>}
   <select aria-label={'New '+name+' value type'} value={type} onChange={e=>setType(e.target.value)}>
    {(parameters?['Number','Boolean','Vector']:Object.keys(defaults)).map(t=><option key={t}>{t}</option>)}
   </select>
   <button type="button" disabled={!array&&(!field||reserved.includes(field)||Object.hasOwn(value,field))} onClick={add}>{array?'Add item':'Add property'}</button>
  </div>
 </div>;
}
export default function AnimationProperties({title,fields,reserved,change,parameters=false}:{parameters?:boolean;title:string;fields:Fields;reserved?:string[];change:(v:Fields)=>void}){
 return <details className="animation-properties-section"><summary>{title}</summary>
  <PropertyList parameters={parameters} name={title} value={fields} reserved={reserved} change={next=>{if(!Array.isArray(next))change(next);}}/>
 </details>;
}
