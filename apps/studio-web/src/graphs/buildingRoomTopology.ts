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
export interface TopologyExteriorAperture {socketId:string;roomId:string;start:number;end:number}
export interface BuildingRoomTopology {exteriorApertures?:readonly TopologyExteriorAperture[];rooms:readonly TopologyRoom[];connections:readonly TopologyConnection[];components:number;withoutElevatorsComponents:number;floors:number}
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
 const exteriorApertures=t.exteriorApertures===undefined?undefined:arr(t.exteriorApertures,4096).map((v):TopologyExteriorAperture=>{
  const a=obj(v),socketId=id(a.socketId),roomId=id(a.roomId),start=num(a.start),end=num(a.end);
  const kind=bySocket.get(socketId)?.kind;
  if(!byId.has(roomId)||byId.get(roomId)?.floor!==0||
     (kind!=='personnel-door'&&kind!=='small-gate'&&kind!=='large-gate')||end-start<=0.00099)
   throw Error('Invalid exterior aperture');
  return {socketId,roomId,start,end};
 });
 if(exteriorApertures&&!unique(exteriorApertures.map(a=>JSON.stringify([a.socketId,a.roomId]))))throw Error('Duplicate exterior aperture');
 return {rooms,connections,components,withoutElevatorsComponents,floors,...(exteriorApertures===undefined?{}:{exteriorApertures})};
}
export const roomNodeId=(r:TopologyRoom)=>'room:'+r.id;
export const connectionNodeId=(e:TopologyConnection)=>'connection:'+e.id;
export const exteriorNodeId=(socketId:string)=>'exterior:'+socketId;
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
 // An exterior node represents one authored aperture, never a shared outdoor
 // navigation vertex. Links are provenance only and are not planner edges.
 const visibleRooms=new Set(rooms.map(r=>r.id));
 const apertures=(data.exteriorApertures??[]).filter(a=>(floor===null||floor===0)&&visibleRooms.has(a.roomId));
 const exteriorIds=[...new Set(apertures.map(a=>a.socketId))];
 const firstRow=Math.max(roomRows.get(0)??0,edgeRows.get(0)??0);
 exteriorIds.forEach((socketId,i)=>{
  const n=exteriorNodeId(socketId);
  nodes.push({id:n,label:'Exterior · '+socketId,x:40,y:40+(firstRow+i)*100,inputs:[],outputs:[{id:'out',type:'room',multiple:true}]});
 });
 for(const a of apertures)links.push({id:JSON.stringify(['exterior',a.socketId,a.roomId]),from:exteriorNodeId(a.socketId),to:'room:'+a.roomId,output:'out',input:'in'});
 return {nodes,links};
}
/** Group diagnostics use native component labels only. Undefined means legacy
 * report: no conclusion about the presence of authored entrance overlaps. */
export function exteriorConnectivity(data:BuildingRoomTopology,withoutElevators=false){
 if(data.exteriorApertures===undefined)return undefined;
 const key=withoutElevators?'withoutElevatorsComponent':'component';
 const entranceRooms=new Set(data.exteriorApertures.map(a=>a.roomId));
 const groups=new Set(data.rooms.filter(r=>entranceRooms.has(r.id)).map(r=>r[key]));
 const roomsWithoutExterior=data.rooms.filter(r=>!groups.has(r[key]));
 return {entranceCount:new Set(data.exteriorApertures.map(a=>a.socketId)).size,
  roomsWithoutExterior,groupsWithoutExterior:[...new Set(roomsWithoutExterior.map(r=>r[key]))].sort((a,b)=>a-b)};
}
export function roomComponentMembers(data:BuildingRoomTopology,roomId:string,withoutElevators=false):readonly TopologyRoom[]{
 const room=data.rooms.find(r=>r.id===roomId);if(!room)return [];
 const key=withoutElevators?'withoutElevatorsComponent':'component';
 return data.rooms.filter(r=>r[key]===room[key]);
}

export interface BuildingRouteQuery {start:string;goal:string;allowStairs:boolean;allowElevators:boolean}
export interface BuildingRouteResult extends BuildingRouteQuery {reachable:boolean;steps:{edgeId:string;from:string;to:string}[]}
/** Validate a native path, never solve one. */
export function parseBuildingRoute(value:unknown,query:BuildingRouteQuery,data:BuildingRoomTopology):BuildingRouteResult {
 const r=obj(value);
 if(!data.rooms.some(x=>x.id===query.start)||!data.rooms.some(x=>x.id===query.goal)||
 r.start!==query.start||r.goal!==query.goal||r.allowStairs!==query.allowStairs||r.allowElevators!==query.allowElevators||typeof r.reachable!=='boolean')throw Error('Route response does not match request');
 const byEdge=new Map(data.connections.map(e=>[e.id,e])),seen=new Set([query.start]);
 let current=query.start;
 const steps=arr(r.steps,127).map(v=>{
  const s=obj(v),edgeId=id(s.edgeId),from=id(s.from),to=id(s.to),edge=byEdge.get(edgeId);
  if(!edge||from!==current||seen.has(to)||!((edge.from===from&&edge.to===to)||(edge.to===from&&edge.from===to))||
     (edge.kind==='stair'&&!query.allowStairs)||(edge.kind==='elevator'&&!query.allowElevators))throw Error('Invalid native route step');
  current=to;seen.add(to);return {edgeId,from,to};
 });
 if(r.reachable?current!==query.goal:steps.length!==0||query.start===query.goal)throw Error('Invalid native route destination');
 return {...query,reachable:r.reachable,steps};
}
