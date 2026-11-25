import { useEffect, useState, type ReactNode } from 'react';
import { invoke } from '../bridge';
import type { StudioGraphDocumentScope } from '../types';
import { StudioAlert, StudioField, StudioInput, StudioPanel, StudioSection, StudioSelect } from '../ui';

type Placement = {
  types: {id:string; name:string}[];
  width:number; height:number; planar:boolean;
  rows?:string[]; left:number; top:number; footprintW:number; footprintH:number; ok:boolean; x:number; z:number;
};

function TerrainPatch({data}:{data:Placement}) {
  const rows = data.rows;
  if (!rows?.length) return null;
  const color = data.ok ? '#79d9b1' : '#ff9886';
  return <figure style={{margin:'12px 0'}}>
    <svg role="img" aria-label="Structure footprint on map terrain" viewBox={`0 0 ${rows[0].length} ${rows.length}`}
      style={{width:'100%',height:220,background:'var(--au-surface)',borderRadius:8}}>
      {rows.map((row,z)=>Array.from(row,(cell,x)=><rect key={x+','+z} x={x+.025} y={z+.025} width={.95} height={.95}
        fill={cell==='~'||cell==='='?'#375e86':cell==='#'||cell==='R'?'#656979':'#39544d'}/>))}
      <rect x={data.left} y={data.top} width={data.footprintW} height={data.footprintH} stroke={color} strokeWidth={.09} fill={color} fillOpacity={.38}/>
    </svg>
    <figcaption role="status">{data.ok?'Buildable terrain':'Placement blocked'} · {data.footprintW} × {data.footprintH} cells · Center ({data.x}, {data.z})</figcaption>
  </figure>;
}

export default function OverlayPlacement({scope,tools}:{scope?:StudioGraphDocumentScope;tools:ReactNode}) {
  const [type,setType] = useState('');
  const [x,setX] = useState('');
  const [z,setZ] = useState('');
  const [data,setData] = useState<Placement>();
  const [error,setError] = useState('');
  const {projectPath,epoch,revision}=scope??{};
  useEffect(()=>{
    if (!epoch || !projectPath) return;
    let live = true;
    const params:Record<string,string|number> = {};
    if(type) params.placeType=type;
    if(x!=='') params.x=Number(x);
    if(z!=='') params.z=Number(z);
    setError('');
    void invoke('studio.op',{op:'overlay_placement_preview',params,expectedProject:projectPath,expectedDocumentEpoch:epoch,expectedDocumentRevision:revision})
      .then(r=>{
        if(!live)return;
        const result=r.result as Placement;
        setData(result);
        if(!type && result.planar && result.types.length)setType(result.types[0].id);
      }).catch(e=>{if(live)setError(String(e));});
    return ()=>{live=false;};
  },[projectPath,epoch,revision,type,x,z]);
  return <StudioPanel title="Overlays">
    <StudioSection title="Placement preview">
      {!scope?<p>Open a map to preview placement.</p>:<>
        {error && <StudioAlert tone="danger">{error}</StudioAlert>}
        {data && (!data.planar?<p>Placement requires a planar tile map.</p>:!data.types.length?<p>This project has no structure types.</p>:<>
          <StudioField label="Structure" htmlFor="overlay-structure"><StudioSelect id="overlay-structure" value={type} onChange={e=>setType(e.target.value)}>
            {data.types.map(t=><option key={t.id} value={t.id}>{t.name||t.id}</option>)}
          </StudioSelect></StudioField>
          <div style={{display:'grid',gridTemplateColumns:'repeat(2,minmax(0,1fr))',gap:8}}>
            <StudioField label="Map X" htmlFor="overlay-x"><StudioInput id="overlay-x" type="number" min={0} step="any" placeholder={String(data.width/2)} value={x} onChange={e=>setX(e.target.value)}/></StudioField>
            <StudioField label="Map Z" htmlFor="overlay-z"><StudioInput id="overlay-z" type="number" min={0} step="any" placeholder={String(data.height/2)} value={z} onChange={e=>setZ(e.target.value)}/></StudioField>
          </div>
          {!error && <TerrainPatch data={data}/>}
        </>)}
      </>}
    </StudioSection>
    <details><summary>Overlay tools</summary>{tools}</details>
  </StudioPanel>;
}
