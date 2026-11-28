import {useId} from 'react';
import {StudioInput,StudioSelect} from '../ui';
import {EffectNumber} from './EffectRangeEditor';
import EffectVector from './EffectVector';

const shapes=['point','sphere','disc','ring','hemisphere','cone','box','line','donut','mesh'];
export default function EffectShape({emitter,patch}:{emitter:Record<string,unknown>;patch:(path:(number|string)[],value:unknown)=>void}){
  const meshOptions=useId();
  const shape=String(emitter.shape??'point');
  const arc=['sphere','disc','hemisphere','cone','donut'].includes(shape);
  const arcMode=String(emitter.arcMode??'random');
  function choice(key:string,label:string,options:string[],initial:string){
    return <label>{label}<StudioSelect value={String(emitter[key]??initial)} onChange={e=>patch([key],e.target.value)}>
      {options.map(option=><option key={option}>{option}</option>)}
    </StudioSelect></label>;
  }
  function number(key:string,label:string,min:number,max:number,initial:number){
    return <EffectNumber label={label} min={min} max={max} value={Number(emitter[key]??initial)} change={v=>patch([key],v)}/>;
  }
  function slider(key:string,label:string,min:number,max:number,step:number,initial:number){
    const value=Number(emitter[key]??initial);
    return <label>{label} · {value}<input aria-label={label} type="range" value={value} min={min} max={max} step={step}
      onChange={e=>patch([key],Number(e.target.value))}/></label>;
  }
  function vector(key:string,label:string,min:number,max:number,initial:[number,number,number]){
    return <EffectVector label={label} value={emitter[key]} initial={initial} min={min} max={max}
      change={(axis,v)=>patch(axis===null?[key]:[key,axis],v)}/>;
  }
  return <details className="effects-shape"><summary>Emitter geometry · {shape}</summary>
    <div className="effects-emitter-fields">
      {choice('shape','Emitter shape',shapes,'point')}
      {shape!=='point'&&shape!=='box'&&number('radius','Radius (m)',0,100,0)}
      {shape==='donut'&&number('donutRadius','Tube radius (m)',0,100,.2)}
      {shape==='cone'&&number('length','Cone length (m)',0,1000,0)}
      {shape!=='point'&&shape!=='line'&&choice('emitFrom','Emit from',['volume','shell','edge','vertex','triangle'],'volume')}
      {shape==='mesh'&&<label>Mesh asset<StudioInput list={meshOptions} value={String(emitter.mesh??'')}
        onChange={e=>patch(['mesh'],e.target.value||undefined)} placeholder="Project model path"/>
        <datalist id={meshOptions}>{['cube','sphere','capsule','cylinder','plane','quad'].map(id=><option key={id} value={'builtin:'+id}/>)}</datalist>
      </label>}
    </div>
    {(shape==='box'||shape==='mesh')&&vector('box',shape==='box'?'Box dimensions (m)':'Mesh scale',0,1000,[1,1,1])}
    {arc&&<fieldset className="effects-key-list"><legend>Arc sampling</legend><div className="effects-emitter-fields">
      {slider('thickness','Thickness',0,1,.01,1)}{slider('arc','Arc (°)',0,360,1,360)}
      {choice('arcMode','Distribution',['random','loop','pingpong','spread'],'random')}
      {(arcMode==='loop'||arcMode==='pingpong')&&number('arcSpeed','Revolutions per second',-100,100,1)}
      {slider('arcSpread','Arc step',0,1,.01,0)}
    </div></fieldset>}
    {vector('shapeRotation','Shape rotation (°)',-360,360,[0,0,0])}
    {vector('offset','Offset (m)',-1000,1000,[0,0,0])}
    {vector('direction','Emission direction',-1,1,[0,1,0])}
    <div className="effects-emitter-fields">
      {slider('spread','Spread (°)',0,180,1,180)}{slider('radial','Radial direction',-1,1,.01,0)}
      {slider('randomDirection','Random direction',0,1,.01,0)}{slider('sphericalDirection','Spherical direction',0,1,.01,0)}
      {number('randomPosition','Random position (m)',0,100,0)}{slider('flipRotation','Mirror probability',0,1,.01,0)}
      <label className="effects-emitter-loop"><input type="checkbox" checked={emitter.alignToDirection===true}
        onChange={e=>patch(['alignToDirection'],e.target.checked)}/>Align to direction</label>
    </div>
  </details>;
}
