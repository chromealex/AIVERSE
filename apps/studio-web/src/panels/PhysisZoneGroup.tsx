import {useState} from 'react';
import {StudioButton,StudioInput,StudioSelect} from '../ui';

function Coordinates({prefix,label}:{prefix:string;label:string}){
  return <fieldset className="effects-vector"><legend>{label}</legend>
    {['X','Y','Z'].map(axis=><label key={axis}>{axis}<StudioInput name={prefix+axis} type="number" step="any" required defaultValue={0}/></label>)}
  </fieldset>;
}
function point(data:FormData,prefix:string){return ['X','Y','Z'].map(axis=>Number(data.get(prefix+axis)));}
export default function PhysisZoneGroup({ids,selected,toggle,apply}:{ids:string[];selected:string[];toggle:(id:string)=>void;
  apply:(action:string,params?:Record<string,unknown>)=>void}){
  const [pivot,setPivot]=useState('mean');
  return <section className="physis-zone-form physis-group"><h4>Zone group</h4>
    <fieldset className="physis-group-list"><legend>Selection · {selected.length}</legend>
      {ids.map(id=><label key={id} className="physis-toggle"><input type="checkbox" checked={selected.includes(id)} onChange={()=>toggle(id)}/>{id}</label>)}
    </fieldset>
    <fieldset className="physis-group-tools" disabled={!selected.length}>
      <form onSubmit={e=>{e.preventDefault();apply('move',{offset:point(new FormData(e.currentTarget),'offset')});}}>
        <Coordinates prefix="offset" label="Move by (m)"/><StudioButton type="submit">Move group</StudioButton>
      </form>
      <form onSubmit={e=>{
        e.preventDefault();const data=new FormData(e.currentTarget);
        apply('transform',{pivot,point:pivot==='custom'?point(data,'pivot'):undefined,yaw:Number(data.get('yaw')),scale:Number(data.get('scale'))});
      }}>
        <label>Pivot<StudioSelect name="pivot" value={pivot} onChange={e=>setPivot(e.target.value)}>
          <option value="mean">Mean of centers</option><option value="bounds">Selection bounds center</option>
          <option value="active">First selected{selected[0]?' · '+selected[0]:''}</option><option value="origin">World origin</option><option value="custom">Custom point</option>
        </StudioSelect></label>
        {pivot==='custom'&&<Coordinates prefix="pivot" label="Pivot (m)"/>}
        <div className="physis-controls">
          <label>Yaw (°)<StudioInput name="yaw" type="number" step="any" required min={-3600} max={3600} defaultValue={0}/></label>
          <label>Uniform scale<StudioInput name="scale" type="number" step="any" required max={100} defaultValue={1}/></label>
        </div>
        <StudioButton type="submit">Apply transform</StudioButton>
      </form>
      <div className="physis-zone-actions"><StudioButton onClick={()=>apply('duplicate')}>Duplicate group</StudioButton>
        <StudioButton onClick={()=>apply('remove')}>Remove group</StudioButton></div>
    </fieldset>
  </section>;
}
