import type {GraphView,NodeView,Port,Link} from './graphFramework';
import type {PortRef} from './graphGeometry';

export type GraphObject=Record<string,unknown>;
export type NativeGraphDomain='spectrafx'|'motionweave';
export interface FxNodeInfo {type:string;group:string;ports:string[];v2Only:boolean}
export const object=(v:unknown):v is GraphObject=>!!v&&typeof v==='object'&&!Array.isArray(v);
const records=(v:unknown):GraphObject[]=>Array.isArray(v)?v.filter(object):[];
const text=(v:unknown)=>typeof v==='string'?v:'';
const port=(id:string,type:string,multiple=false):Port=>({id,type,multiple});
const xy=(row:GraphObject,i:number)=>({x:Array.isArray(row.position)&&typeof row.position[0]==='number'&&Number.isFinite(row.position[0])?Math.max(0,Math.min(2000,row.position[0])):40+(i%7)*230,
 y:Array.isArray(row.position)&&typeof row.position[1]==='number'&&Number.isFinite(row.position[1])?Math.max(0,Math.min(2000,row.position[1])):40+Math.floor(i/7)*230});
export function fxProjection(doc:GraphObject,catalog:readonly FxNodeInfo[]):GraphView {
 const nodes:NodeView[]=records(doc.nodes).slice(0,256).map((n,i)=>{
  const id=text(n.id),type=text(n.type),info=catalog.find(x=>x.type===type);
  const data=!!info&&['Values','Math','Vector','Logic','Functions'].includes(info.group);
  const inputs:Port[]=[],outputs:Port[]=[];
  if(info&&!data&&type!=='Spawn')inputs.push(port('exec','exec'));
  if(info&&!data&&!['SpriteOutput','MeshOutput','RibbonOutput'].includes(type))outputs.push(port('exec','exec'));
  if(data)outputs.push(port('value','data'));
  if(doc.version===2)for(const p of info?.ports??[])inputs.push(port('data:'+p,'data'));
  return {id,label:type,...xy(n,i),inputs,outputs};
 });
 const byId=new Map(nodes.map(n=>[n.id,n]));
 const links:Link[]=[];
 records(doc.links).slice(0,512).forEach((e,i)=>{
  const from=text(e.from),to=text(e.to),data=typeof e.toPort==='string';
  const output=data?'value':'exec',input=data?'data:'+e.toPort:'exec';
  const a=byId.get(from),b=byId.get(to);if(!a||!b)return;
  // Existing valid but non-catalog nested ports remain visible, not silently discarded.
  if(!a.outputs.some(p=>p.id===output))a.outputs=[...a.outputs,port(output,data?'data':'exec')];
  if(!b.inputs.some(p=>p.id===input))b.inputs=[...b.inputs,port(input,data?'data':'exec')];
  links.push({id:'edge:'+i,from,to,output,input});
 });
 return {nodes,links};
}
export function fxConnectArgs(from:PortRef,to:PortRef):GraphObject|null {
 if(from.node===to.node)return null;
 if(from.port==='exec'&&to.port==='exec')return {action:'connect',from:from.node,to:to.node};
 if(from.port==='value'&&to.port.startsWith('data:')&&to.port.length>5)return {action:'connect',from:from.node,to:to.node,toPort:to.port.slice(5)};
 return null;
}
const poseInputs=(n:GraphObject)=>['blend','additive'].includes(text(n.kind))?['a','b']:['twoBoneIK','stack','rig','native'].includes(text(n.kind))?['input']:[];
export function animationProjection(doc:GraphObject,mode:'pose'|'states'):GraphView {
 const links:Link[]=[];
 if(mode==='states'){
  const states=records(doc.states);
  const nodes=states.map((s,i)=>({id:text(s.id),label:(doc.initial===s.id?'Initial · ':'')+text(s.output),...xy({},i),inputs:[port('enter','state',true)],outputs:[port('transition','state',true)]}));
  states.forEach(s=>records(s.transitions).forEach((t,i)=>links.push({id:JSON.stringify([s.id,i]),from:text(s.id),to:text(t.to),output:'transition',input:'enter'})));
  return {nodes,links};
 }
 const nodes=records(doc.nodes).map((n,i)=>({id:text(n.id),label:text(n.kind),...xy({},i),inputs:poseInputs(n).map(p=>port(p,'pose')),outputs:[port('pose','pose')]}));
 records(doc.nodes).forEach(n=>poseInputs(n).forEach(p=>{if(text(n[p]))links.push({id:JSON.stringify([n.id,p]),from:text(n[p]),to:text(n.id),output:'pose',input:p});}));
 return {nodes,links};
}
export function connectAnimationPose(doc:GraphObject,from:PortRef,to:PortRef):GraphObject {
 const nodes=records(doc.nodes),i=nodes.findIndex(n=>n.id===from.node),j=nodes.findIndex(n=>n.id===to.node);
 if(from.port!=='pose'||i<0||j<=i||!poseInputs(nodes[j]).includes(to.port)||nodes[j][to.port]===from.node)return doc;
 return {...doc,nodes:nodes.map((n,k)=>k===j?{...n,[to.port]:from.node}:n)};
}
export function replaceAnimationEntry(doc:GraphObject,mode:'pose'|'states',id:string,next:GraphObject):GraphObject {
 const field=mode==='pose'?'nodes':'states',rows=records(doc[field]);
 if(next.id!==id||!rows.some(n=>n.id===id)||rows.length!==(doc[field] as unknown[]).length)return doc;
 return {...doc,[field]:rows.map(n=>n.id===id?next:n)};
}
export function graphReply(result:unknown):GraphObject {
 if(!object(result)||!object(result.graph))throw new Error('Native graph response is missing a graph document');
 if(result.ok===false)throw new Error(text(result.error)||'Native graph operation failed');
 return result.graph;
}
export function graphNames(result:unknown):string[] {
 if(!object(result)||!Array.isArray(result.graphs))throw new Error('Native graph list unavailable');
 return result.graphs.flatMap(g=>typeof g==='string'?[g]:object(g)&&typeof g.name==='string'?[g.name]:[]);
}
