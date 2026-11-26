import {useEffect, useState} from 'react';
import {invoke} from '../bridge';
import type {StudioGraphDocumentScope} from '../types';
import {StudioAlert, StudioButton, StudioSection, StudioSelect} from '../ui';

interface Shore { side?:number; from:number; to:number; type:string; auto?:boolean }
interface Body { id:number; kind:string; shores?:Shore[]|'auto' }
interface ShoreType {id:string; name:string}

export default function WaterShorePanel({scope,onChanged}:{scope:StudioGraphDocumentScope;onChanged:()=>void}) {
  const [data,setData]=useState<{key:string; bodies:Body[]; types:ShoreType[]}>();
  const [selected,setSelected]=useState<number>();
  const [busy,setBusy]=useState(false);
  const [error,setError]=useState('');
  const [warning,setWarning]=useState('');
  const key=JSON.stringify([scope.projectPath,scope.epoch,scope.revision]);
  const {projectPath,epoch,revision}=scope;
  useEffect(()=>{
    let active=true;
    const guard={expectedProject:projectPath,expectedDocumentEpoch:epoch,expectedDocumentRevision:revision};
    Promise.all([
      invoke('studio.op',{op:'water_network',params:{},...guard}),
      invoke('studio.op',{op:'water_types',params:{},...guard}),
    ]).then(([network,catalog])=>{
      if(active){setData({key,bodies:(network.result as {bodies:Body[]}).bodies,types:(catalog.result as {types:ShoreType[]}).types});setError('');}
    }).catch(e=>{if(active)setError(String(e));});
    return ()=>{active=false;};
  },[key,projectPath,epoch,revision]);
  const body=data?.bodies.find(b=>b.id===selected) ?? data?.bodies[0];
  const shores=Array.isArray(body?.shores)?body.shores:[];
  const disabled=busy || data?.key!==key;
  async function apply(op:string,params:Record<string,unknown>) {
    if(disabled)return;
    setBusy(true);setError('');setWarning('');
    try {
      const result=await invoke('studio.op',{op,params,expectedProject:projectPath,expectedDocumentEpoch:epoch,expectedDocumentRevision:revision});
      setWarning((result.result as {warning?:string}).warning ?? '');
      onChanged();
    }catch(e){setError(String(e));}finally{setBusy(false);}
  }
  return <StudioSection className="water-shores" title="Shorelines" actions={
    <StudioButton size="sm" disabled={disabled||!body} onClick={()=>void apply('water_bake',{})}>Recalculate shores</StudioButton>}>
    {error && <StudioAlert tone="danger">{error}</StudioAlert>}
    {warning && <StudioAlert tone="warning">{warning}</StudioAlert>}
    {body && data ? <>
      <label className="water-shores__body">Water body
        <StudioSelect value={body.id} disabled={busy} onChange={e=>{setSelected(Number(e.target.value));setWarning('');}}>
          {data.bodies.map(b=><option key={b.id} value={b.id}>{b.kind} {b.id}</option>)}
        </StudioSelect></label>
      {(body.kind==='sea'?[1]:[-1,1]).map(side=>{
        const segments=shores.filter(s=>((s.side ?? 1)<0?-1:1)===side).sort((a,b)=>a.from-b.from);
        if(!segments.length)return null;
        const bank=body.kind==='sea'?'Coast':side<0?'Left bank':'Right bank';
        return <section key={side} aria-label={bank} className="water-shores__bank"><h4>{bank}</h4>
          {segments.map(s=><label className="water-shores__segment" key={s.from+'-'+s.to}>
            <span>{s.from}–{s.to} m <small>{s.auto?'Automatic':'Manual'}</small></span>
            <StudioSelect aria-label={bank+' '+s.from+'–'+s.to+' m'} value={s.type} disabled={disabled}
              onChange={e=>void apply('water_shores',{id:body.id,side,from:s.from,to:s.to,type:e.target.value})}>
              {data.types.map(t=><option key={t.id} value={t.id}>{t.name||t.id}</option>)}
            </StudioSelect></label>)}
        </section>;
      })}
      {!shores.length && <p>Recalculate shores to list the generated segments.</p>}
      <StudioButton size="sm" disabled={disabled||!shores.some(s=>!s.auto)} onClick={()=>void apply('water_shores',{id:body.id,reset:true})}>Reset this body to automatic</StudioButton>
    </> : data && <p>Draw a river in Scene or add a water body below.</p>}
  </StudioSection>;
}
