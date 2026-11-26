import {StudioAlert,StudioButton,StudioInput} from '../ui';
import {EffectNumber} from './EffectRangeEditor';

type Color=string|number[];
type Stop=[number,Color];
type Patch=(path:(string|number)[],value:unknown)=>void;
const defaultFade:Stop[]=[[0,'#ffffffff'],[1,'#ffffff00']];
const clamp=(n:number)=>Math.min(1,Math.max(0,n));
function isColor(value:unknown):value is Color {
  if(typeof value==='string')return /^#[\da-f]{6}([\da-f]{2})?$/i.test(value);
  return Array.isArray(value)&&(value.length===3||value.length===4)&&value.every(Number.isFinite);
}
function rgba(color:Color):number[] {
  if(Array.isArray(color))return [color[0],color[1],color[2],color[3]??1].map(clamp);
  return (color.slice(1)+(color.length===7?'ff':'')).match(/../g)!.map(pair=>parseInt(pair,16)/255);
}
function hex(channels:number[]){
  return '#'+channels.map(n=>Math.round(clamp(n)*255).toString(16).padStart(2,'0')).join('');
}
function Gradient({name,label,value,patch}:{name:string;label:string;value:unknown;patch:Patch}){
  const constant=isColor(value);
  const stops:Stop[]=constant?[[0,value]]:value===undefined?defaultFade:Array.isArray(value)?value:[];
  if(!stops.length||!stops.every(s=>Array.isArray(s)&&s.length===2&&Number.isFinite(s[0])&&isColor(s[1])))
    return <StudioAlert tone="warning">Correct {name} in Effect source to edit its stops.</StudioAlert>;
  const rows=stops.map(([age,color],index)=>({age:clamp(age),color,index})).sort((a,b)=>a.age-b.age);
  const ramp=rows.map(({age,color})=>hex(rgba(color))+' '+age*100+'%');
  function change(index:number,axis:number,next:number|Color){
    if(constant&&axis===1)patch([name],next);
    else if(constant||value===undefined)patch([name],stops.map((s,i)=>i===index?s.map((v,j)=>j===axis?next:v):s));
    else patch([name,index,axis],next);
  }
  function add(){
    const edges=[{...rows[0],age:0},...rows,{...rows[rows.length-1],age:1}];
    let widest=1;
    for(let i=2;i<edges.length;i++)if(edges[i].age-edges[i-1].age>edges[widest].age-edges[widest-1].age)widest=i;
    const left=edges[widest-1],right=edges[widest],b=rgba(right.color);
    const stop:Stop=[(left.age+right.age)/2,rgba(left.color).map((n,i)=>(n+b[i])/2)];
    if(constant||value===undefined)patch([name],[...stops,stop]);
    else patch([name,stops.length],stop);
  }
  return <fieldset className="effects-key-list"><legend>{label}</legend>
    <div className="effects-color-ramp"><div style={{background:rows.length===1?ramp[0].split(' ')[0]:'linear-gradient(to right,'+ramp.join(',')+')'}}/></div>
    {rows.map(({age,color,index})=>{
      const channels=rgba(color);
      return <div className="effects-color-stop" key={index}>
        <EffectNumber label="Particle age" min={0} max={1} value={age} change={v=>change(index,0,v)}/>
        <label>Color<StudioInput type="color" value={hex(channels.slice(0,3))} onChange={e=>{
          const next=rgba(e.target.value);next[3]=channels[3];
          change(index,1,Array.isArray(color)?next:hex(next));
        }}/></label>
        <EffectNumber label="Opacity" min={0} max={1} value={channels[3]} change={alpha=>{
          change(index,1,[...channels.slice(0,3),alpha]);
        }}/>
        <StudioButton disabled={stops.length===1} onClick={()=>{
          if(value===undefined)patch([name],stops.filter((_,i)=>i!==index));
          else patch([name,index],undefined);
        }}>Remove stop</StudioButton>
      </div>;
    })}
    <StudioButton onClick={add}>Add stop</StudioButton>
  </fieldset>;
}
export default function EffectLifetimeColor({emitter,patch}:{emitter:Record<string,unknown>;patch:Patch}){
  return <details className="effects-colors"><summary>Color over particle lifetime</summary>
    <Gradient name="color" label="Lifetime gradient" value={emitter.color} patch={patch}/>
    <label className="effects-emitter-loop"><input type="checkbox" checked={emitter.colorMin!==undefined}
      onChange={e=>patch(['colorMin'],e.target.checked?emitter.color??defaultFade:undefined)}/>Random between two gradients</label>
    {emitter.colorMin!==undefined&&<Gradient name="colorMin" label="Second gradient" value={emitter.colorMin} patch={patch}/>}
  </details>;
}
