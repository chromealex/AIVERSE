import {useMemo,useState} from 'react';
import {StudioAlert,StudioButton,StudioSection,StudioSelect} from '../ui';
import GraphCanvas from './GraphCanvas';
import {connectionNodeId,projectBuildingRoomTopology,roomComponentMembers,roomNodeId} from './buildingRoomTopology';
import type {BuildingRoomTopology} from './buildingRoomTopology';

export function BuildingRoomTopologyView({data,onSelectSocket}:{data:BuildingRoomTopology;onSelectSocket?:(id:string)=>void}){
 const [floor,setFloor]=useState<number|null>(null),[selected,setSelected]=useState(''),[withoutElevators,setWithoutElevators]=useState(false),[target,setTarget]=useState('');
 const view=useMemo(()=>projectBuildingRoomTopology(data,floor),[data,floor]);
 const room=data.rooms.find(r=>roomNodeId(r)===selected);
 const edge=data.connections.find(e=>connectionNodeId(e)===selected);
 const members=room?roomComponentMembers(data,room.id,withoutElevators):[];
 const highlighted=members.map(roomNodeId);
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
   <small>{members.length} of {data.rooms.length} rooms structurally reachable (including selected room); {data.rooms.length-members.length} outside this group.</small>
   {members.length===1&&<StudioAlert tone="warning">This room has no reachable peer under the selected policy.</StudioAlert>}
   <StudioSelect aria-label="Compare structural reachability to room" value={destination?.id??''} onChange={e=>setTarget(e.target.value)}>
    <option value="">Choose destination room</option>{data.rooms.map(r=><option key={r.id} value={r.id}>{r.id}</option>)}
   </StudioSelect>
   {connected!==null&&<StudioAlert tone={connected?'success':'warning'}>{connected?'Connected in native authored topology.':'No authored connection under this policy.'} This is not a runtime path guarantee.</StudioAlert>}
  </div>}
  {edge&&<div className="au-stack"><strong>{edge.id} · {edge.kind}</strong>
   <small>{edge.from} ↔ {edge.to}</small><small>Position {edge.position.join(', ')} m · width {edge.width} m · height {edge.height} m</small>
   {withoutElevators&&edge.kind==='elevator'&&<small>Excluded from the selected connectivity policy.</small>}
   {edge.socketId&&onSelectSocket&&<StudioButton size="sm" onClick={()=>onSelectSocket(edge.socketId!)}>Select source feature</StudioButton>}
  </div>}
  <StudioAlert>Native authored room connectivity only. Elevator operation, door openness, actor clearance, collision, NavMesh and exterior access are not verified by this report. Exterior doors and gates remain in the socket graph.</StudioAlert>
 </StudioSection>;
}
