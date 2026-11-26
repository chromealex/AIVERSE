import { useEffect, useState, type ReactNode } from 'react';
import { useOverlayPreview } from './useOverlayPreview';
import OverlayPlan from './OverlayPlan';
import PlacementGhost from './PlacementGhost';
import { invoke } from '../bridge';
import type { StudioGraphDocumentScope } from '../types';
import { StudioAlert, StudioField, StudioInput, StudioPanel, StudioSection, StudioSelect } from '../ui';

type Placement = {
  types: {id:string; name:string}[];
  width:number; height:number; planar:boolean;
  rows?:string[]; left:number; top:number; footprintW:number; footprintH:number; ok:boolean; x:number; z:number;
};

function TerrainPatch({data,model}:{data:Placement;model?:ReactNode}) {
  const rows = data.rows;
  if (!rows?.length) return null;
  const color = data.ok ? '#79d9b1' : '#ff9886';
  return <figure style={{margin:'12px 0'}}>
    {model??<svg role="img" aria-label="Structure footprint on map terrain" viewBox={`0 0 ${rows[0].length} ${rows.length}`}
      style={{width:'100%',height:220,background:'var(--au-surface)',borderRadius:8}}>
      {rows.map((row,z)=>Array.from(row,(cell,x)=><rect key={x+','+z} x={x+.025} y={z+.025} width={.95} height={.95}
        fill={cell==='~'||cell==='='?'#375e86':cell==='#'||cell==='R'?'#656979':'#39544d'}/>))}
      <rect x={data.left} y={data.top} width={data.footprintW} height={data.footprintH} stroke={color} strokeWidth={.09} fill={color} fillOpacity={.38}/>
    </svg>}
    <figcaption role="status">{data.ok?'Buildable terrain':'Placement blocked'} · {data.footprintW} × {data.footprintH} cells · Center ({data.x}, {data.z})</figcaption>
  </figure>;
}

export default function OverlayPlacement({scope,tools,selectionKey}:{selectionKey:string;scope?:StudioGraphDocumentScope;tools:ReactNode}) {
  const {state,error:commandError,request}=useOverlayPreview(scope);
  const [draft,setDraft]=useState<{x?:string;z?:string}>({});
  const [showModel,setShowModel]=useState(false);
  const [data,setData] = useState<Placement>();
  const [error,setError] = useState('');
  const {projectPath,epoch,revision}=scope??{};
  const type=state?.placeType??data?.types[0]?.id??'';
  const x=draft.x??String(state?.x??''), z=draft.z??String(state?.z??'');
  function coordinate(axis:'x'|'z',value:string){
    setDraft(old=>({...old,[axis]:value}));
    const n=Number(value), limit=axis==='x'?data?.width:data?.height;
    if(value!=='' && (!Number.isFinite(n) || n<0 || limit===undefined || n>=limit)){
      return;
    }
    void request({[axis]:value===''?null:n});
  }
  useEffect(()=>setDraft({}),[state?.version]);
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

      }).catch(e=>{if(live)setError(String(e));});
    return ()=>{live=false;};
  },[projectPath,epoch,revision,type,x,z]);
  return <StudioPanel title="Overlays">
    {scope && state && data?.planar && <OverlayPlan ids={state.preview} setVisible={(id,visible)=>void request({entry:{id,visible}})} selectionKey={selectionKey} scope={scope} placeType={type} x={x} z={z} pick={(px,pz)=>void request({x:px,z:pz})}/>}
    <StudioSection title="Placement preview">
      {!scope?<p>Open a map to preview placement.</p>:<>
        {(commandError||error) && <StudioAlert tone="danger">{commandError||error}</StudioAlert>}
        {data && (!data.planar?<p>Placement requires a planar tile map.</p>:!data.types.length?<p>This project has no structure types.</p>:<>
          <StudioField label="Structure" htmlFor="overlay-structure"><StudioSelect id="overlay-structure" value={type} onChange={e=>void request({placeType:e.target.value})}>
            <option value="">No structure</option>
            {data.types.map(t=><option key={t.id} value={t.id}>{t.name||t.id}</option>)}
          </StudioSelect></StudioField>
          <div style={{display:'grid',gridTemplateColumns:'repeat(2,minmax(0,1fr))',gap:8}}>
            <StudioField label="Map X" htmlFor="overlay-x"><StudioInput id="overlay-x" type="number" min={0} step="any" placeholder={String(data.width/2)} value={x} onChange={e=>coordinate('x',e.target.value)}/></StudioField>
            <StudioField label="Map Z" htmlFor="overlay-z"><StudioInput id="overlay-z" type="number" min={0} step="any" placeholder={String(data.height/2)} value={z} onChange={e=>coordinate('z',e.target.value)}/></StudioField>
          </div>
          {type && !error && <>
            <div role="group" aria-label="Placement view" className="placement-view-switch">
              <button type="button" aria-pressed={!showModel} onClick={()=>setShowModel(false)}>Footprint</button>
              <button type="button" aria-pressed={showModel} onClick={()=>setShowModel(true)}>3D model</button>
            </div>
            <TerrainPatch data={data} model={showModel?<PlacementGhost scope={scope} type={type} x={x} z={z}
              name={data.types.find(item=>item.id===type)?.name||type}/>:undefined}/>
          </>}
        </>)}
      </>}
    </StudioSection>
    <details><summary>Overlay tools</summary>{tools}</details>
  </StudioPanel>;
}
