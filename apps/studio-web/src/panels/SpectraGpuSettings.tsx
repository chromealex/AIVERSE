import {StudioField, StudioInput, StudioSelect} from '../ui';

const levels=['off','low','medium','high'];
const switches=[
  ['allowCpuFallback','Allow CPU fallback',true],
  ['gpuIndirectCompaction','Metal indirect compaction',false],
  ['gpuSortAlpha','Metal alpha sorting',false],
  ['allowUnsortedAlpha','Allow unsorted GPU alpha',false]
] as const;

export default function SpectraGpuSettings({routes,doc,change}:{
  routes:boolean;doc:Record<string,unknown>;change:(field:string,value:unknown)=>void
}) {
  const qualityKey=routes?'sceneQuality':'quality';
  return <div className="gpu-settings">
    <StudioField label={routes?'Scene quality':'Emitter quality'}>
      <StudioSelect aria-label={routes?'Scene quality':'Emitter quality'} value={String(doc[qualityKey]??(routes?'':'high'))}
        onChange={e=>change(qualityKey,e.target.value||undefined)}>
        {routes&&<option value="">Project default</option>}
        {levels.map(level=><option key={level} value={level}>{level[0].toUpperCase()+level.slice(1)}</option>)}
      </StudioSelect>
    </StudioField>
    {routes?<>
      <StudioField label="GPU frame budget (ms)" hint="Zero disables adaptive scaling.">
        <StudioInput aria-label="GPU frame budget (ms)" type="number" min={0} max={1000} step="any" value={String(doc.gpuFrameBudgetMs??0)}
          onChange={e=>{if(e.target.value)change('gpuFrameBudgetMs',e.target.valueAsNumber);}}/>
      </StudioField>
      <StudioField label="Minimum spawn scale">
        <input aria-label="Minimum spawn scale" type="range" min={0} max={1} step={0.01} value={Number(doc.minGpuSpawnScale??0.25)}
          onChange={e=>change('minGpuSpawnScale',e.target.valueAsNumber)}/>
        <output>{Math.round(Number(doc.minGpuSpawnScale??0.25)*100)}%</output>
      </StudioField>
    </>:<>
      <StudioField label="Execution mode"><StudioSelect aria-label="Execution mode" value={String(doc.preference??'auto')}
        onChange={e=>change('preference',e.target.value)}>
        <option value="auto">Auto</option><option value="cpu">CPU only</option><option value="gpu">GPU preferred</option>
      </StudioSelect></StudioField>
      <StudioField label="Particle capacity"><StudioInput aria-label="Particle capacity" type="number" min={1} max={65536} step={1}
        value={String(doc.particles??256)} onChange={e=>{if(e.target.value)change('particles',e.target.valueAsNumber);}}/></StudioField>
      <div className="gpu-switches">{switches.map(([key,label,fallback])=><label key={key}>
        <input type="checkbox" checked={Boolean(doc[key]??fallback)} onChange={e=>change(key,e.target.checked)}/>{label}
      </label>)}</div>
    </>}
  </div>;
}
