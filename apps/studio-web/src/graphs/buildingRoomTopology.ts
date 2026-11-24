import type {GraphView,NodeView,Link} from './graphFramework';

type Obj=Record<string,unknown>;
const obj=(v:unknown):Obj=>{if(!v||typeof v!=='object'||Array.isArray(v))throw Error('Invalid topology object');return v as Obj;};
const int=(v:unknown,min:number,max:number,label:string):number=>{
 if(typeof v!=='number'||!Number.isSafeInteger(v)||v<min||v>max)throw Error('Invalid '+label);return v;
};
const id=(v:unknown):string=>{if(typeof v!=='string'||!v.length||v.length>128||/[\u0000-\u001f\u007f]/.test(v))throw Error('Invalid topology ID');return v;};
const num=(v:unknown,min=0,max=160):number=>{if(typeof v!=='number'||!Number.isFinite(v)||v<min||v>max)throw Error('Invalid topology coordinate');return v;};
const arr=(v:unknown,max:number):unknown[]=>{if(!Array.isArray(v)||v.length>max)throw Error('Invalid topology array');return v;};
const point=(v:unknown):[number,number,number]=>{const a=arr(v,3);if(a.length!==3)throw Error('Invalid topology point');return [num(a[0]),num(a[1]),num(a[2])];};
export type RoomConnectionKind='passage'|'stair'|'elevator';
export interface TopologyRoom {id:string;floor:number;x:number;z:number;min:[number,number,number];max:[number,number,number];component:number;withoutElevatorsComponent:number}
export interface TopologyConnection {id:string;from:string;to:string;floor:number;kind:RoomConnectionKind;position:[number,number,number];width:number;height:number;socketId?:string}
export interface BuildingRoomTopology {rooms:readonly TopologyRoom[];connections:readonly TopologyConnection[];components:number;withoutElevatorsComponents:number;floors:number}
interface SocketRef {id:string;kind:string}
const unique=(values:readonly string[])=>new Set(values).size===values.length;
/** Validate only the native DTO, never reconstruct room adjacency or calculate runtime routes. */
export function parseBuildingRoomTopology(value:unknown,floors:number,roomsX:number,roomsZ:number,count:number,sockets:readonly SocketRef[]):BuildingRoomTopology {
 const t=obj(value);if(t.version!==1)throw Error('Unsupported room topology version');
 const rawRooms=arr(t.rooms,128),rawConnections=arr(t.connections,512);
 if(rawRooms.length!==floors*roomsX*roomsZ||rawConnections.length!==count||!rawRooms.length)throw Error('Topology counts do not match owner report');
 const components=int(t.components,1,rawRooms.length,'component count');
 const withoutElevatorsComponents=int(t.withoutElevatorsComponents,components,rawRooms.length,'no-elevator component count');
 const rooms=rawRooms.map((v):TopologyRoom=>{
  const r=obj(v),min=point(r.min),max=point(r.max);
  if(min.some((x,i)=>x>=max[i]))throw Error('Invalid room bounds');
  return {id:id(r.id),floor:int(r.floor,0,floors-1,'room floor'),x:int(r.x,0,roomsX-1,'room x'),z:int(r.z,0,roomsZ-1,'room z'),min,max,
   component:int(r.component,0,components-1,'component'),withoutElevatorsComponent:int(r.withoutElevatorsComponent,0,withoutElevatorsComponents-1,'no-elevator component')};
 });
 if(!unique(rooms.map(r=>r.id))||!unique(rooms.map(r=>JSON.stringify([r.floor,r.x,r.z]))))throw Error('Duplicate room ID or cell');
 const byId=new Map(rooms.map(r=>[r.id,r])),bySocket=new Map(sockets.map(s=>[s.id,s]));
 const groups=new Set(rooms.map(r=>r.component)),subgroups=new Map<number,number>();
 for(const r of rooms){
  const parent=subgroups.get(r.withoutElevatorsComponent);
  if(parent!==undefined&&parent!==r.component)throw Error('No-elevator group crosses authored components');
  subgroups.set(r.withoutElevatorsComponent,r.component);
 }
 if(groups.size!==components||subgroups.size!==withoutElevatorsComponents)throw Error('Topology component count mismatch');
 const connections=rawConnections.map((v):TopologyConnection=>{
  const e=obj(v),from=id(e.from),to=id(e.to),a=byId.get(from),b=byId.get(to);
  if(!a||!b||from===to)throw Error('Unknown or self room endpoint');
  const floor=int(e.floor,0,floors-1,'connection floor'),kind=e.kind;
  if(kind!=='passage'&&kind!=='stair'&&kind!=='elevator')throw Error('Unknown connection kind');
  if(a.floor!==floor||(kind==='passage'?b.floor!==floor:b.floor!==floor+1))throw Error('Invalid connection floor');
  if(a.component!==b.component||(kind!=='elevator'&&a.withoutElevatorsComponent!==b.withoutElevatorsComponent))throw Error('Connection conflicts with native components');
  const edge:TopologyConnection={id:id(e.id),from,to,floor,kind,position:point(e.position),width:num(e.width,0.001),height:num(e.height,0.001)};
  if(kind==='passage'){if(e.socketId!==undefined)throw Error('Unexpected passage socket');}
  else {
   edge.socketId=id(e.socketId);
   if(bySocket.get(edge.socketId)?.kind!==(kind==='stair'?'stairs':'elevator'))throw Error('Unknown or mismatched vertical socket');
  }
  return edge;
 });
 if(!unique(connections.map(e=>e.id))||!unique(connections.flatMap(e=>e.socketId?[e.socketId]:[])))throw Error('Duplicate connection or vertical socket ID');
 return {rooms,connections,components,withoutElevatorsComponents,floors};
}
export const roomNodeId=(r:TopologyRoom)=>'room:'+r.id;
export const connectionNodeId=(e:TopologyConnection)=>'connection:'+e.id;
/** Floor filtering includes actual adjacent endpoints of visible edges. No synthetic links. */
export function projectBuildingRoomTopology(data:BuildingRoomTopology,floor:number|null=null):GraphView {
 if(floor!==null)int(floor,0,data.floors-1,'selected floor');
 const roomsById=new Map(data.rooms.map(r=>[r.id,r]));
 const edges=data.connections.filter(e=>floor===null||roomsById.get(e.from)?.floor===floor||roomsById.get(e.to)?.floor===floor);
 const ids=new Set(edges.flatMap(e=>[e.from,e.to]));
 const rooms=data.rooms.filter(r=>floor===null||r.floor===floor||ids.has(r.id));
 const node=(id:string,label:string,x:number,y:number):NodeView=>({id,label,x,y,inputs:[{id:'in',type:'room',multiple:true}],outputs:[{id:'out',type:'room',multiple:true}]});
 const nodes:NodeView[]=[],links:Link[]=[];
 const roomRows=new Map<number,number>(),edgeRows=new Map<number,number>();
 for(const r of rooms){
  const row=roomRows.get(r.floor)??0;roomRows.set(r.floor,row+1);
  nodes.push(node(roomNodeId(r),'F'+(r.floor+1)+' · '+(r.x+1)+','+(r.z+1),40+r.floor*420,40+row*100));
 }
 for(const e of edges){
  const row=edgeRows.get(e.floor)??0;edgeRows.set(e.floor,row+1);
  const n=connectionNodeId(e);
  nodes.push(node(n,e.kind+' · '+(row+1),230+e.floor*420,40+row*100));
  links.push({id:JSON.stringify([e.id,0]),from:'room:'+e.from,to:n,output:'out',input:'in'},
   {id:JSON.stringify([e.id,1]),from:n,to:'room:'+e.to,output:'out',input:'in'});
 }
 return {nodes,links};
}
export function roomComponentMembers(data:BuildingRoomTopology,roomId:string,withoutElevators=false):readonly TopologyRoom[]{
 const room=data.rooms.find(r=>r.id===roomId);if(!room)return [];
 const key=withoutElevators?'withoutElevatorsComponent':'component';
 return data.rooms.filter(r=>r[key]===room[key]);
}
