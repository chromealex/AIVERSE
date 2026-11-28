import {useId} from 'react';
import {StudioInput,StudioSelect} from '../ui';
import {EffectNumber} from './EffectRangeEditor';
import EffectVector from './EffectVector';

export default function EffectSimulation({emitter,patch}:{emitter:Record<string,unknown>;patch:(path:(string|number)[],value:unknown)=>void}){
  const textures=useId();
  const limit=Number(emitter.maxParticles??0);
  return <details className="effects-range"><summary>Particle texture and simulation</summary>
    <div className="effects-emitter-fields">
      <label>Particle texture<StudioInput list={textures} value={String(emitter.texture??'builtin:soft')}
        onChange={e=>patch(['texture'],e.target.value||undefined)} placeholder="Project texture path"/>
        <datalist id={textures}>{['soft','spark','ring','smoke'].map(id=><option key={id} value={'builtin:'+id}/>)}</datalist>
      </label>
      <label>Simulation space<StudioSelect value={String(emitter.space??'world')} onChange={e=>patch(['space'],e.target.value)}>
        <option value="world">World</option><option value="local">Local to emitter</option>
      </StudioSelect></label>
      <EffectNumber label="Particle limit (0 = unlimited)" min={0} max={100000} step={1} value={limit} change={v=>patch(['maxParticles'],v)}/>
    </div>
    <label className="effects-emitter-loop"><input type="checkbox" checked={emitter.prewarm===true} disabled={!emitter.loop}
      onChange={e=>patch(['prewarm'],e.target.checked)}/>Prewarm looping emitter</label>
    {limit>0&&<label className="effects-emitter-loop"><input type="checkbox" checked={emitter.ringBuffer===true}
      onChange={e=>patch(['ringBuffer'],e.target.checked)}/>Recycle oldest particles at limit</label>}
    <EffectVector label="Gravity (m/s²)" value={emitter.gravity} initial={[0,0,0]} min={-100} max={100}
      change={(axis,v)=>patch(axis===null?['gravity']:['gravity',axis],v)}/>
  </details>;
}
