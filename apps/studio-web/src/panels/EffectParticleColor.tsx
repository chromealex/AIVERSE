import {StudioAlert,StudioSelect} from '../ui';
import {ColorFields,Gradient,isColor,type Color,type Patch} from './EffectLifetimeColor';
import {EffectRangeValue} from './EffectRangeEditor';

const white='#ffffffff';
const speedRange={key:'speed',label:'Particle speed (m/s)',initial:[0,1],min:0,max:1000};
function object(value:unknown):value is Record<string,unknown>{
  return !!value&&typeof value==='object'&&!Array.isArray(value);
}
function colorStops(value:unknown):value is [number,Color][]{
  return Array.isArray(value)&&value.length>0&&value.every(s=>Array.isArray(s)&&s.length===2&&Number.isFinite(s[0])&&isColor(s[1]));
}
function StartColor({value=white,patch}:{value:unknown;patch:Patch}){
  const module=object(value)?value:undefined;
  const pair=Array.isArray(value)&&value.length===2&&value.every(isColor)?value:undefined;
  const raw=module?module.gradient:value;
  const gradient=colorStops(raw)?raw:isColor(raw)?[[0,raw],[1,raw]] as [number,Color][]:undefined;
  const mode=pair?'pair':!module&&isColor(value)?'constant':gradient?'gradient':'invalid';
  function switchMode(next:string){
    const stops=gradient?.slice().sort((a,b)=>a[0]-b[0]);
    const first=pair?.[0]??stops?.[0][1]??white;
    const last=pair?.[1]??stops?.[stops.length-1][1]??first;
    patch(['startColor'],next==='constant'?first:next==='pair'?[first,last]:{gradient:[[0,first],[1,last]]});
  }
  function property(name:string,next:unknown){
    if(module)patch(['startColor',name],next);
    else patch(['startColor'],{gradient:raw,[name]:next});
  }
  return <details className="effects-colors"><summary>Particle start color</summary>
    <div className="effects-emitter-fields"><label>Start color mode<StudioSelect value={mode} onChange={e=>switchMode(e.target.value)}>
      {mode==='invalid'&&<option value="invalid">Invalid source</option>}
      <option value="constant">Constant</option><option value="pair">Random between two colors</option><option value="gradient">Gradient</option>
    </StudioSelect></label></div>
    {mode==='constant'&&isColor(value)&&<div className="effects-emitter-fields"><ColorFields color={value} change={v=>patch(['startColor'],v)}/></div>}
    {pair&&pair.map((color,i)=><fieldset key={i} className="effects-key-list"><legend>{i?'Second color':'First color'}</legend>
      <div className="effects-emitter-fields"><ColorFields color={color} change={v=>patch(['startColor',i],v)}/></div>
    </fieldset>)}
    {mode==='gradient'&&<>
      <label className="effects-emitter-loop"><input type="checkbox" checked={module?.overTime===true} onChange={e=>property('overTime',e.target.checked)}/>Sample by emitter time</label>
      <Gradient label="Start gradient" name={module?'gradient':'startColor'} positionLabel="Gradient position" value={raw}
        patch={(path,v)=>patch(module?['startColor',...path]:path,v)}/>
      <label className="effects-emitter-loop"><input type="checkbox" checked={module?.gradientMin!==undefined}
        onChange={e=>property('gradientMin',e.target.checked?raw:undefined)}/>Random between two start gradients</label>
      {module?.gradientMin!==undefined&&<Gradient label="Second start gradient" name="gradientMin" positionLabel="Gradient position"
        value={module.gradientMin} patch={(path,v)=>patch(['startColor',...path],v)}/>}
    </>}
  </details>;
}
export default function EffectParticleColor({emitter,patch}:{emitter:Record<string,unknown>;patch:Patch}){
  const speed=emitter.colorBySpeed;
  const nested:Patch=(path,v)=>patch(['colorBySpeed',...path],v);
  return <><StartColor value={emitter.startColor} patch={patch}/>
    <details className="effects-colors"><summary>Color by particle speed</summary>
      <label className="effects-emitter-loop"><input type="checkbox" checked={speed!==undefined}
        onChange={e=>patch(['colorBySpeed'],e.target.checked?{speed:[0,1],color:[[0,white],[1,white]]}:undefined)}/>Apply speed gradient</label>
      {object(speed)?<>
        <fieldset className="effects-key-list"><legend>{speedRange.label}</legend><EffectRangeValue rangeLabel="Speed interval" emitter={speed} p={speedRange} patch={nested}/></fieldset>
        <Gradient name="color" label="Speed gradient" positionLabel="Relative speed" value={speed.color} patch={nested}/>
      </>:speed!==undefined&&<StudioAlert tone="warning">Correct colorBySpeed in Effect source.</StudioAlert>}
    </details></>;
}
