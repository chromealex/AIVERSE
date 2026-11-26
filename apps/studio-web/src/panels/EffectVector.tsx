import {EffectNumber} from './EffectRangeEditor';

export default function EffectVector({label,value,initial,min,max,change}:{
  label:string;value:unknown;initial:[number,number,number];min:number;max:number;
  change:(axis:number|null,value:number|number[])=>void
}){
  const existing=Array.isArray(value)&&value.length===3&&value.every(n=>typeof n==='number');
  const vector=existing?value:initial;
  return <fieldset className="effects-vector"><legend>{label}</legend>
    {['X','Y','Z'].map((axis,i)=><EffectNumber key={axis} label={axis} min={min} max={max} value={vector[i]} change={v=>{
      if(existing)change(i,v);else change(null,initial.map((n,j)=>j===i?v:n));
    }}/>)}
  </fieldset>;
}
