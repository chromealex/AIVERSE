import {useState} from 'react';
import {StudioButton,StudioInput} from '../ui';
import type {FxNode} from './spectraDataInterfaceModel';

export default function SpectraLiteralEditor({node,disabled,apply}:{
  node:FxNode;disabled:boolean;apply:(property:string,value:unknown)=>void;
}){
  const parameter=node.type==='Parameter';
  const property=parameter?'name':'value';
  const value=node.properties?.[property]??(parameter?Reflect.get(node,'name')??'':undefined);
  const [parts,setParts]=useState<string[]>(()=>Array.isArray(value)?value.map(String):[String(value??'')]);
  const update=(i:number,text:string)=>setParts(parts.map((part,index)=>i===index?text:part));
  const set=(next:unknown)=>apply(property,next);
  const type=node.type;
  if(!['Parameter','Bool','Float','Integer','Vec2','Vec3','Vec4','Color'].includes(type))return null;
  if(value&&typeof value==='object'&&!Array.isArray(value))
    return <small>Connected input · Edit its source node to change the value.</small>;
  if(type==='Bool'&&typeof value==='boolean')return <label>
    <input aria-label="Boolean value" type="checkbox" disabled={disabled} checked={value} onChange={e=>set(e.target.checked)}/> Value
  </label>;
  const vector=['Vec2','Vec3','Vec4'].includes(type);
  const numeric=type==='Float'||type==='Integer';
  const color=type==='Color'&&typeof value==='string'&&/^#[0-9a-f]{6}([0-9a-f]{2})?$/i.test(value);
  if(color)return <div className="spectra-literal">
    <label>Color<StudioInput type="color" aria-label="Color RGB" disabled={disabled} value={value.slice(0,7)}
      onChange={e=>set(e.target.value+value.slice(7))}/></label>
    <label>Alpha<StudioInput type="range" min={0} max={255} aria-label="Color alpha" disabled={disabled}
      value={parseInt(value.slice(7)||'ff',16)} onChange={e=>set(value.slice(0,7)+Number(e.target.value).toString(16).padStart(2,'0'))}/></label>
  </div>;
  if(parameter?typeof value!=='string':vector?(!Array.isArray(value)||value.length!==Number(type.slice(-1))||!value.every(Number.isFinite)):
    !numeric||typeof value!=='number')return null;
  const changed=parts.some((p,i)=>p!==String(vector?(value as number[])[i]:value));
  const valid=parameter||parts.every(p=>p.trim()!==''&&Number.isFinite(Number(p))&&(type!=='Integer'||Number.isSafeInteger(Number(p))));
  return <form className="spectra-literal" onSubmit={e=>{
    e.preventDefault();
    if(valid&&changed)set(parameter?parts[0]:vector?parts.map(Number):Number(parts[0]));
  }}>
    {parts.map((part,i)=>{
      const label=parameter?'Parameter name':vector?'Value '+'XYZW'[i]:'Value';
      return <label key={i}>{label}<StudioInput aria-label={label} type={parameter?'text':'number'}
        step={type==='Integer'?1:'any'} value={part} disabled={disabled} onChange={e=>update(i,e.target.value)}/></label>;
    })}
    {changed&&<StudioButton size="sm" type="submit" disabled={disabled||!valid}>Apply value</StudioButton>}
  </form>;
}
