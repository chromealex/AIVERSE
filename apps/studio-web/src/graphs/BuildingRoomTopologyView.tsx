import {useMemo,useState} from 'react';
import {StudioAlert,StudioButton,StudioSection,StudioSelect} from '../ui';
import GraphCanvas from './GraphCanvas';
import {connectionNodeId,parseBuildingRoute,projectBuildingRoomTopology,roomComponentMembers,roomNodeId} from './buildingRoomTopology';
import type {BuildingRoomTopology,BuildingRouteQuery,BuildingRouteResult} from './buildingRoomTopology';

export function BuildingRoomTopologyView({data,onSelectSocket,onRoute}:{data:BuildingRoomTopology;onSelectSocket?:(id:string)=>void;onRoute?:(query:BuildingRouteQuery)=>Promise<unknown>}){
 const [floor,setFloor]=useState<number|null>(null),[selected,setSelected]=useState(''),[withoutElevators,setWithoutElevators]=useState(false),[target,setTarget]=useState('');
 const view=useMemo(()=>projectBuildingRoomTopology(data,floor),[data,floor]);
 const room=data.rooms.find(r=>roomNodeId(r)===selected);
 const edge=data.connections.find(e=>connectionNodeId(e)===selected);
 const members=room?roomComponentMembers(data,room.id,withoutElevators):[];
 const [allowStairs,setAllowStairs]=useState(true),[pending,setPending]=useState(false);
 const [route,setRoute]=useState<{data:BuildingRoomTopology;query:BuildingRouteQuery;result?:BuildingRouteResult;error?:string}|null>(null);
 const current=route&&route.data===data&&route.query.start===room?.id&&route.query.goal===target&&route.query.allowStairs===allowStairs&&route.query.allowElevators===!withoutElevators?route:null;
 const currentRoute=current?.result;
 const highlighted=currentRoute?.reachable?['room:'+currentRoute.start,...currentRoute.steps.flatMap(s=>['connection:'+s.edgeId,'room:'+s.to])]:currentRoute?[]:members.map(roomNodeId);
 const runRoute=async()=>{
  if(!onRoute||!room||!data.rooms.some(r=>r.id===target)||pending)return;
  const query={start:room.id,goal:target,allowStairs,allowElevators:!withoutElevators};
  setPending(true);setRoute(null);setFloor(null);
  try{setRoute({data,query,result:parseBuildingRoute(await onRoute(query),query,data)});}
  catch(e){setRoute({data,query,error:e instanceof Error?e.message:String(e)});}
  finally{setPending(false);}
 };
 const destination=data.rooms.find(r=>r.id===target);
 const connected=room&&destination?members.some(r=>r.id===destination.id):null;
 const active=view.nodes.some(n=>n.id===selected)?selected:'';
 const groups=withoutElevators?data.withoutElevatorsComponents:data.components;
 return <StudioSection title="Native room connectivity">
  <div className="au-row" style={{flexWrap:'wrap'}}>
   <StudioSelect aria-label="Building topology floor" value={floor===null?'all':String(floor)} onChange={e=>{setFloor(e.target.value==='all'?null:Number(e.target.value));setSelected('')}}>
    <option value="all">All floors</option>{Array.from({length:data.floors},(_,i)=><option key={i} value={i}>Floor {i+1}</option>)}
   </StudioSelect>
   <StudioSelect aria-label="Building topology policy" value={withoutElevators?'stairs':'all'} onChange={e=>setWithoutElevators(e.target.value==='stairs')}>
    <option value="all">All authored links</option><option value="stairs">Exclude elevators</option>
   </StudioSelect>
  </div>
  <small>{data.rooms.length} rooms · {data.connections.length} authored transitions · {groups} connected groups · {view.nodes.length} visible nodes</small>
  {groups>1&&<StudioAlert tone="warning">Disconnected room groups under the selected structural policy. Select a room to inspect its reachable group.</StudioAlert>}
  <div style={{height:340,display:'flex',flexDirection:'column',minWidth:0}}>
   <GraphCanvas view={view} label="Native authored room and transition topology" selected={active} highlighted={highlighted} readOnly onSelect={setSelected} onMove={()=>{}}/>
  </div>
  {room&&<div className="au-stack"><strong>{room.id}</strong>
   <small>Floor {room.floor+1} · grid {room.x+1}, {room.z+1}</small>
   <small>Bounds {room.min.join(', ')} – {room.max.join(', ')} m</small>
   {data.exteriorApertures!==undefined&&<div className="au-stack">
    <small>{data.exteriorApertures.filter(a=>a.roomId===room.id).length} authored exterior aperture overlaps</small>
    {data.exteriorApertures.filter(a=>a.roomId===room.id).map(a=><div key={a.socketId} className="au-row">
     <small>{a.socketId} · {(a.end-a.start).toFixed(2)} m overlap</small>
     {onSelectSocket&&<StudioButton size="sm" onClick={()=>onSelectSocket(a.socketId)}>Select entrance</StudioButton>}
    </div>)}
   </div>}
   <small>{members.length} of {data.rooms.length} rooms structurally reachable with stairs allowed (including selected room); {data.rooms.length-members.length} outside this group.</small>
   {members.length===1&&<StudioAlert tone="warning">This room has no reachable peer under the selected policy.</StudioAlert>}
   <StudioSelect aria-label="Compare structural reachability to room" value={destination?.id??''} onChange={e=>setTarget(e.target.value)}>
    <option value="">Choose destination room</option>{data.rooms.map(r=><option key={r.id} value={r.id}>{r.id}</option>)}
   </StudioSelect>
   {onRoute&&<div className="au-stack">
    <StudioSelect aria-label="Route stair policy" value={allowStairs?'allow':'exclude'} onChange={e=>setAllowStairs(e.target.value==='allow')}>
     <option value="allow">Allow stairs in route</option><option value="exclude">Exclude stairs from route</option>
    </StudioSelect>
    <StudioButton size="sm" disabled={!destination||pending} onClick={()=>void runRoute()}>{pending?'Inspecting route…':'Inspect native route'}</StudioButton>
    {current?.error&&<StudioAlert tone="danger">{current.error}</StudioAlert>}
    {currentRoute&&<StudioAlert tone={currentRoute.reachable?'success':'warning'}>{currentRoute.reachable?('Native authored route: '+currentRoute.steps.length+' transitions'):'No authored route under selected policy'}</StudioAlert>}
    {currentRoute?.steps.map((s,i)=><small key={s.edgeId}>{i+1}. {s.from} → {s.to} · {data.connections.find(e=>e.id===s.edgeId)?.kind} · {s.edgeId}</small>)}
   </div>}
   {connected!==null&&<StudioAlert tone={connected?'success':'warning'}>{connected?'Connected in native authored topology with stairs allowed.':'No authored connection under this policy.'} This is not a runtime path guarantee.</StudioAlert>}
  </div>}
  {edge&&<div className="au-stack"><strong>{edge.id} · {edge.kind}</strong>
   <small>{edge.from} ↔ {edge.to}</small><small>Position {edge.position.join(', ')} m · width {edge.width} m · height {edge.height} m</small>
   {withoutElevators&&edge.kind==='elevator'&&<small>Excluded from the selected connectivity policy.</small>}
   {edge.socketId&&onSelectSocket&&<StudioButton size="sm" onClick={()=>onSelectSocket(edge.socketId!)}>Select source feature</StudioButton>}
  </div>}
  <StudioAlert>Native authored room connectivity only. Elevator operation, door openness, actor clearance, collision, NavMesh and exterior access are not verified by this report. Exterior aperture overlaps, when reported, describe authored geometry only and do not certify exterior access.</StudioAlert>
 </StudioSection>;
}
