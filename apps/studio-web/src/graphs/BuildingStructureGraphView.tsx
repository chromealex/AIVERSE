import {useMemo,useState} from 'react';
import {StudioAlert,StudioButton,StudioSection,StudioTextarea} from '../ui';
import GraphCanvas from './GraphCanvas';
import {BuildingRoomTopologyView} from './BuildingRoomTopologyView';
import {projectBuildingInspection} from './buildingGraphInspection';

interface Props {
 recipe:unknown;inspection:unknown;
 onSelectFeature?:(index:number)=>void;
 onRoute?:(query:import('./buildingRoomTopology').BuildingRouteQuery)=>Promise<unknown>;
}
// Owner panel passes its exact request and native inspect response together.
export function BuildingStructureGraphView({recipe,inspection,onSelectFeature,onRoute}:Props){
 const result=useMemo(()=>{
  try{return {data:projectBuildingInspection(recipe,inspection),error:''};}
  catch(e){return {data:null,error:e instanceof Error?e.message:String(e)};}
 },[recipe,inspection]);
 const [selected,setSelected]=useState('');
 if(!result.data)return <StudioAlert tone="danger">{result.error}</StudioAlert>;
 const data=result.data;
 const active=data.view.nodes.some(n=>n.id===selected)?selected:'';
 const socket=data.sockets.find(s=>'socket:'+s.id===active);
 const feature=socket?.featureIndex??(active.startsWith('feature:')?Number(active.slice(8)):null);
 return <StudioSection title="Building structure graph">
  <small>Read-only owner inspection · seed {data.seed} · {data.triangles} triangles · {data.vertices} vertices</small>
  {data.mode==='features'?<small>{data.floors} floors · {data.roomsX} × {data.roomsZ} room grid · {data.solids} solids · {data.sockets.length} sockets · {data.connectionCount} native connections</small>:<small>Preset mesh inspection; no functional structure graph returned.</small>}
  <div style={{height:320,display:'flex',flexDirection:'column',minWidth:0}}>
   <GraphCanvas view={data.view} label="Building recipe and native sockets" selected={active} readOnly onSelect={setSelected} onMove={()=>{}}/>
  </div>
  {socket&&<div className="au-stack">
   <strong>{socket.id}</strong><small>{socket.kind} · {socket.side} · feature {socket.featureIndex+1}</small>
   <small>{socket.width} × {socket.height} m · authored opening {Math.round(socket.open*100)}%</small>
   <small>Native outside anchor: {socket.outside.join(', ')} m</small>
  </div>}
  {feature!==null&&onSelectFeature&&<StudioButton size="sm" onClick={()=>onSelectFeature(feature)}>Select source feature</StudioButton>}
  <StudioAlert>Recipe links represent socket provenance, not live navigation. Material bindings are authored references, not resolved by native inspection.</StudioAlert>
  {data.topology?<BuildingRoomTopologyView data={data.topology} onRoute={onRoute} onSelectSocket={onSelectFeature?(id=>{const s=data.sockets.find(item=>item.id===id);if(s)onSelectFeature(s.featureIndex)}):undefined}/>:data.mode==='features'&&<StudioAlert>Room topology unavailable in this owner report (legacy count-only response).</StudioAlert>}
 </StudioSection>;
}

// Offline inspection fallback while the Building Generator owner PR is separate.
// Does not call a missing native method, access project files or save changes.
export function BuildingInspectionImport(){
 const [recipe,setRecipe]=useState(''),[inspection,setInspection]=useState('');
 const [pair,setPair]=useState<{recipe:unknown;inspection:unknown}|null>(null);
 const [error,setError]=useState('');
 const load=()=>{
  setPair(null);setError('');
  try{
   if(recipe.length>262144||inspection.length>262144)throw new Error('Inspection input exceeds 262144-character limit');
   const next={recipe:JSON.parse(recipe) as unknown,inspection:JSON.parse(inspection) as unknown};
   projectBuildingInspection(next.recipe,next.inspection);setPair(next);
  }catch(e){setError(e instanceof Error?e.message:String(e));}
 };
 return <StudioSection title="Imported building inspection">
  <small>Paste the exact building request and matching studio.buildingInspect result. Imported JSON is not a verified live native response. No files are written.</small>
  <StudioTextarea aria-label="Building request JSON" placeholder="Building request JSON" rows={4} value={recipe} onChange={e=>{setRecipe(e.target.value);setPair(null);setError('')}}/>
  <StudioTextarea aria-label="Building inspection JSON" placeholder="Native inspection result JSON" rows={4} value={inspection} onChange={e=>{setInspection(e.target.value);setPair(null);setError('')}}/>
  <StudioButton size="sm" disabled={!recipe.trim()||!inspection.trim()} onClick={load}>Inspect supplied data</StudioButton>
  {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
  {pair&&<BuildingStructureGraphView recipe={pair.recipe} inspection={pair.inspection}/>}
 </StudioSection>;
}
