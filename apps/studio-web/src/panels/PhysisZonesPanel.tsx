import {useEffect,useState} from 'react';
import PhysisZonePlan from './PhysisZonePlan';
import {invoke} from '../bridge';
import type {StudioGraphDocumentScope} from '../types';
import {StudioAlert,StudioButton,StudioInput,StudioSelect} from '../ui';

const kinds={wind:'Wind zone',storm:'Storm',gravity:'Gravity field'};
type Kind=keyof typeof kinds;
type Zone=Record<string,unknown>&{id:string};
type Config={windZones?:Zone[];storms?:Zone[];gravityFields?:Zone[]};
type Snapshot={config:Config|null;defaultConfig:Config;revision:number};
type Patch=Record<string,unknown>;
const collection={wind:'windZones',storm:'storms',gravity:'gravityFields'} as const;
const numbers:Record<Kind,Record<string,string>>={
  wind:{radiusMeters:'Radius (m)',gustAmplitude:'Gust amplitude (m/s)',gustPeriodTicks:'Gust period (ticks)',seed:'Seed',startTick:'Start tick',durationTicks:'Duration (ticks)'},
  storm:{eyeRadiusMeters:'Eye radius (m)',outerRadiusMeters:'Outer radius (m)',heightMeters:'Height (m)',tangentialWind:'Tangential wind (m/s)',radialInflow:'Radial inflow (m/s)',updraft:'Updraft (m/s)',precipitationBoost:'Precipitation boost',pressureDrop:'Pressure drop (hPa)',startTick:'Start tick',durationTicks:'Duration (ticks)'},
  gravity:{radiusMeters:'Radius (m)',radialMagnitude:'Radial acceleration (m/s²)',priority:'Priority'}
};
function ZoneForm({zone,kind,save}:{zone:Zone;kind:Kind;save:(patch:Patch)=>void}){
  const [dirty,setDirty]=useState(false);
  const vectors:Record<string,string>={center:'Center (m)'};
  if(kind==='wind')vectors.velocity='Wind velocity (m/s)';
  if(kind==='storm')vectors.driftPerTick='Drift per tick (m)';
  if(kind==='gravity')vectors.acceleration='Acceleration (m/s²)';
  const options=kind==='storm'?{key:'kind',values:['Tornado','Hurricane']}:kind==='gravity'?{key:'mode',values:['Uniform','Attract','Repel']}:undefined;
  const flags=kind==='gravity'?['enabled','overrideBaseline']:kind==='storm'?['enabled','clockwise']:[];
  return <form className="physis-zone-form" onChange={()=>setDirty(true)} onSubmit={e=>{
    e.preventDefault();const data=new FormData(e.currentTarget),patch:Patch={};
    for(const key of Object.keys(numbers[kind])){
      const text=String(data.get(key)??'');const value=text===''?undefined:Number(text);
      if(value!==zone[key])patch[key]=value??null;
    }
    for(const key of Object.keys(vectors)){
      const before=Array.isArray(zone[key])?zone[key] as number[]:[0,0,0];
      const next=before.map((_,i)=>Number(data.get(key+i)));
      if(next.some((v,i)=>v!==before[i]))patch[key]=next;
    }
    if(options&&data.get(options.key)!==(zone[options.key]??options.values[0]))patch[options.key]=data.get(options.key);
    for(const key of flags)if(data.has(key)!==(zone[key]??(key==='enabled')))patch[key]=data.has(key);
    if(Object.keys(patch).length)save(patch);else setDirty(false);
  }}>
    <h4>{zone.id}</h4>
    {options&&<label>{kind==='storm'?'Storm type':'Gravity mode'}<StudioSelect name={options.key} defaultValue={String(zone[options.key]??options.values[0])}>
      {options.values.map(v=><option key={v}>{v}</option>)}</StudioSelect></label>}
    {Object.entries(vectors).map(([key,label])=><fieldset className="effects-vector" key={key}><legend>{label}</legend>
      {['X','Y','Z'].map((axis,i)=><label key={axis}>{axis}<StudioInput name={key+i} type="number" step="any" required defaultValue={Number((zone[key] as number[]|undefined)?.[i]??0)}/></label>)}
    </fieldset>)}
    <div className="physis-controls">{Object.entries(numbers[kind]).map(([key,label])=><label key={key}>{label}
      <StudioInput name={key} type="number" step={/Tick|seed|priority/i.test(key)?1:'any'} defaultValue={zone[key]===undefined?'':String(zone[key])} placeholder="Default"/></label>)}</div>
    {flags.map(key=><label className="physis-toggle" key={key}><input name={key} type="checkbox" defaultChecked={Boolean(zone[key]??(key==='enabled'))}/>
      {key==='enabled'?'Enabled':key==='overrideBaseline'?'Override baseline gravity':'Clockwise'}</label>)}
    <StudioButton type="submit" disabled={!dirty}>Apply changes</StudioButton>
  </form>;
}
export default function PhysisZonesPanel({scope,onChanged}:{scope:StudioGraphDocumentScope;onChanged:()=>void}){
  const [data,setData]=useState<{key:string;snapshot:Snapshot}>();
  const [kind,setKind]=useState<Kind>('wind'),[selected,select]=useState('');
  const [busy,setBusy]=useState(false),[error,setError]=useState('');
  const {projectPath,epoch,revision}=scope;
  const key=JSON.stringify([projectPath,epoch,revision]);
  const guard={expectedProject:projectPath,expectedDocumentEpoch:epoch,expectedDocumentRevision:revision};
  useEffect(()=>{
    let current=true;
    invoke('studio.op',{op:'physis_get',params:{},...guard}).then(reply=>{
      if(current)setData({key,snapshot:reply.result as Snapshot});
    }).catch(e=>{if(current)setError(String(e));});
    return ()=>{current=false;};
  },[key]);
  const snapshot=data?.snapshot;
  const zones=(Object.keys(kinds) as Kind[]).flatMap(type=>(snapshot?.config?.[collection[type]]??[]).map(zone=>({type,zone})));
  const active=zones.find(z=>z.zone.id===selected)??zones[0];
  async function apply(action:string,patch?:Patch){
    if(busy||!snapshot||key!==data?.key)return;
    setBusy(true);setError('');
    try{
      const op=action==='initialize'?'physis_set':'physis_zone';
      const params=action==='initialize'?{config:snapshot.defaultConfig}:{action,kind,id:active?.zone.id,patch};
      const reply=await invoke('studio.op',{op,params:{...params,expectedRevision:snapshot.revision},...guard});
      const result=reply.result as Snapshot&{id?:string};
      if(result.id)select(result.id);
      setData({key,snapshot:{...result,defaultConfig:snapshot.defaultConfig}});onChanged();
    }catch(e){setError(String(e));}finally{setBusy(false);}
  }
  return <section className="physis-zones"><h3>Map zones</h3>
    {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
    <fieldset className="physis-zone-editor" disabled={busy||data?.key!==key}>
      {!snapshot?.config?<StudioButton onClick={()=>void apply('initialize')}>Create PHYSIS settings</StudioButton>:<>
        <div className="physis-zone-sidebar"><div className="physis-zone-actions"><label>Zone type<StudioSelect value={kind} onChange={e=>setKind(e.target.value as Kind)}>
          {(Object.keys(kinds) as Kind[]).map(k=><option key={k} value={k}>{kinds[k]}</option>)}</StudioSelect></label>
          <StudioButton onClick={()=>void apply('add')}>Add zone</StudioButton></div>
        {active&&<><div className="physis-zone-actions"><label>Selected zone<StudioSelect value={active.zone.id} onChange={e=>select(e.target.value)}>
          {zones.map(({type,zone})=><option key={zone.id} value={zone.id}>{zone.id} · {kinds[type]}</option>)}</StudioSelect></label>
          <StudioButton onClick={()=>void apply('duplicate')}>Duplicate</StudioButton><StudioButton onClick={()=>void apply('remove')}>Remove</StudioButton></div>
          <PhysisZonePlan key={active.zone.id} selected={active.zone.id} select={select}
            zones={zones.map(({type,zone})=>{
              const [x,,z]=Array.isArray(zone.center)?zone.center:[0,0,0];
              const defaultRadius=type==='wind'?100:type==='storm'?2000:0;
              return {id:zone.id,type,x,z,radius:Number(zone[type==='storm'?'outerRadiusMeters':'radiusMeters']??defaultRadius)};
            })} move={(x,z)=>void apply('edit',{center:[x,(active.zone.center as number[]|undefined)?.[1]??0,z]})}/>
        </>}</div>
        {active&&<ZoneForm key={active.zone.id+':'+snapshot.revision} zone={active.zone} kind={active.type} save={p=>void apply('edit',p)}/>}
      </>}
    </fieldset></section>;
}
