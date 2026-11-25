import { useEffect, useMemo, useState, type PointerEvent } from 'react';
import type { StudioProperty } from '../types';
import './GranularGridEditor.css';

type Brush = 'grains'|'wall'|'erase';
interface Model {
  width:number; height:number; capacity:number; mass:number[]; solid:number[]; enabled:boolean;
}
interface Props {
  properties:readonly StudioProperty[];
  disabled:boolean;
  onCommit:(serialized:string)=>void;
}
function field(props:readonly StudioProperty[],key:string):unknown {
  return props.find(item=>item.key===key)?.value;
}
function bounded(value:unknown,min:number,max:number,fallback:number) {
  const num=Number(value);
  return Number.isSafeInteger(num)&&num>=min&&num<=max?num:fallback;
}
function parseArray(value:unknown,len:number):number[]|null {
  try {
    const array=typeof value==='string'?JSON.parse(value) as unknown:value;
    if(Array.isArray(array)&&array.length===len&&array.every(
      n=>typeof n==='number'&&Number.isSafeInteger(n)))return array as number[];
  }catch{/* Invalid imported map, not an invitation to replace its data. */}
  return null;
}
function readModel(props:readonly StudioProperty[]):Model|null {
  const width=bounded(field(props,'width'),1,128,1);
  const height=bounded(field(props,'height'),1,128,1);
  const capacity=bounded(field(props,'capacity'),1,1000000,1000);
  const mass=parseArray(field(props,'mass'),width*height)||
    (width*height===1?[bounded(field(props,'initialMass'),0,capacity,0)]:null);
  const solid=parseArray(field(props,'solid'),width*height)||
    (width*height===1?[0]:null);
  if(!mass||!solid||mass.some((v,i)=>v<0||v>capacity||(solid[i]===1&&v!==0))||
    solid.some(v=>v!==0&&v!==1))return null;
  return {width,height,capacity,mass,solid,enabled:field(props,'enabled')!==false};
}
function resized(m:Model,width:number,height:number):Model {
  const mass=Array<number>(width*height).fill(0);
  const solid=Array<number>(width*height).fill(0);
  for(let y=0;y<Math.min(m.height,height);y++)
    for(let x=0;x<Math.min(m.width,width);x++){
      mass[y*width+x]=m.mass[y*m.width+x];
      solid[y*width+x]=m.solid[y*m.width+x];
    }
  return {...m,width,height,mass,solid};
}

/** One map-authoring operation per paint/resize, never a live ECS write. */
export default function GranularGridEditor({properties,disabled,onCommit}:Props){
  const key=JSON.stringify(properties.map(p=>[p.key,p.value]));
  const parsed=useMemo(()=>readModel(properties),[key]);
  const [model,setModel]=useState<Model|null>(parsed);
  const [brush,setBrush]=useState<Brush>('grains');
  const [quantity,setQuantity]=useState(100);
  const [offsetX,setOffsetX]=useState(0);
  const [offsetY,setOffsetY]=useState(0);
  useEffect(()=>setModel(parsed),[parsed]);
  if(!model)return <div className="granular-grid__error" role="alert">
    Invalid GRANULAR cell data. Repair the authored map before editing.
  </div>;
  const cols=Math.min(model.width,16),rows=Math.min(model.height,16);
  const x0=Math.min(offsetX,model.width-cols);
  const y0=Math.min(offsetY,model.height-rows);
  const commit=(next:Model)=>{
    if(disabled)return;
    setModel(next);
    onCommit(JSON.stringify(next));
  };
  const paint=(x:number,y:number)=>{
    if(disabled)return;
    const i=y*model.width+x, mass=model.mass.slice(),solid=model.solid.slice();
    if(brush==='wall'){mass[i]=0;solid[i]=1;}
    else if(brush==='erase'){mass[i]=0;solid[i]=0;}
    else{mass[i]=bounded(quantity,0,model.capacity,0);solid[i]=0;}
    if(mass[i]===model.mass[i]&&solid[i]===model.solid[i])return;
    commit({...model,mass,solid});
  };
  const paintDrag=(event:PointerEvent<HTMLButtonElement>,x:number,y:number)=>{
    if(event.buttons&1){event.preventDefault();paint(x,y);}
  };
  return <section className="granular-grid" aria-label="GRANULAR grid editor">
    <div className="granular-grid__controls">
      <label>Width <input className="au-input" type="number" min={1} max={128}
        disabled={disabled} value={model.width} onChange={event=>{
          const n=bounded(event.target.value,1,128,0);
          if(n)commit(resized(model,n,model.height));
        }}/></label>
      <label>Height <input className="au-input" type="number" min={1} max={128}
        disabled={disabled} value={model.height} onChange={event=>{
          const n=bounded(event.target.value,1,128,0);
          if(n)commit(resized(model,model.width,n));
        }}/></label>
    </div>
    <div className="granular-grid__controls" role="group" aria-label="Granular brush">
      {(['grains','wall','erase'] as const).map(mode=>
        <button key={mode} type="button" disabled={disabled} aria-pressed={brush===mode}
          className={'granular-grid__tool'+(brush===mode?' is-selected':'')}
          onClick={()=>setBrush(mode)}>{mode}</button>)}
      <label>Mass <input className="au-input" type="number" min={0} max={model.capacity}
        disabled={disabled||brush!=='grains'} value={quantity}
        onChange={event=>setQuantity(bounded(event.target.value,0,model.capacity,quantity))}/></label>
    </div>
    {(model.width>16||model.height>16)&&<div className="granular-grid__controls">
      <label>View X <input className="au-input" type="number" min={0} max={model.width-cols}
        value={x0} onChange={event=>setOffsetX(bounded(event.target.value,0,model.width-cols,x0))}/></label>
      <label>View Y <input className="au-input" type="number" min={0} max={model.height-rows}
        value={y0} onChange={event=>setOffsetY(bounded(event.target.value,0,model.height-rows,y0))}/></label>
    </div>}
    <div className="granular-grid__cells"
      style={{gridTemplateColumns:'repeat('+cols+',minmax(0,1fr))'}}>
      {Array.from({length:rows*cols},(_,i)=>{
        const x=x0+i%cols,y=y0+Math.floor(i/cols),index=y*model.width+x;
        const wall=model.solid[index]===1,amount=model.mass[index];
        const label='Cell '+x+', '+y+': '+(wall?'blocked':amount+' grains');
        return <button key={x+':'+y} type="button" disabled={disabled}
          aria-label={label} title={label}
          className={'granular-grid__cell'+(wall?' is-wall':amount?' is-filled':'')}
          onPointerDown={event=>{if(event.button===0){event.preventDefault();paint(x,y);}}}
          onPointerEnter={event=>paintDrag(event,x,y)}>
          {wall?'■':amount?'·':''}
        </button>;
      })}
    </div>
    <small className="granular-grid__hint">
      {model.width} × {model.height} · Brush/resize creates one Undo step · Applied on game setup
    </small>
  </section>;
}
