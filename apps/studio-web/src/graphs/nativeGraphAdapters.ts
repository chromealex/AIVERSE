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
 if(!Array.isArray(doc[field])||next.id!==id||!rows.some(n=>n.id===id)||rows.length!==doc[field].length)return doc;
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

/** State order and transition order are semantic. Preserve both when editing. */
export function addAnimationTransition(doc:GraphObject,from:PortRef,to:PortRef):GraphObject {
 if(from.port!=='transition'||to.port!=='enter')return doc;
 const states=records(doc.states),source=states.find(s=>s.id===from.node);
 if(!source||!states.some(s=>s.id===to.node))return doc;
 const transitions=records(source.transitions);
 if(transitions.length>=64||transitions.some(t=>t.to===to.node))return doc;
 // Explicit unconditional transition; the editor must expose this default.
 return replaceAnimationEntry(doc,'states',from.node,{...source,transitions:[...transitions,{to:to.node,fade:.2,after:0,when:[]}]});
}
export function editAnimationTransition(doc:GraphObject,stateId:string,index:number,patch:GraphObject|null):GraphObject {
 const state=records(doc.states).find(s=>s.id===stateId);
 if(!state||!Array.isArray(state.transitions)||!Number.isInteger(index)||index<0||index>=state.transitions.length)return doc;
 const transitions=records(state.transitions);
 if(transitions.length!==state.transitions.length)return doc;
 if(patch){
  const next={fade:.2,after:0,...transitions[index],...patch};
  if(!records(doc.states).some(s=>s.id===next.to)||!['fade','after'].every(k=>typeof next[k]==='number'&&Number.isFinite(next[k])&&Number(next[k])>=0))return doc;
  transitions[index]=next;
 }else transitions.splice(index,1);
 return replaceAnimationEntry(doc,'states',stateId,{...state,transitions});
}
export function setAnimationInitial(doc:GraphObject,id:string):GraphObject {
 return records(doc.states).some(s=>s.id===id)?{...doc,initial:id}:doc;
}
export function modelProjection(doc:GraphObject):GraphView {
 const rows=records(doc.nodes);
 const nodes=rows.map((n,i)=>({id:text(n.id),label:object(n.geometry)?text(n.geometry.kind)||'Geometry':'Joint / pivot',...xy({},i),
  inputs:[port('parent','transform')],outputs:[port('children','transform',true)]}));
 const links=rows.flatMap(n=>text(n.parent)?[{id:JSON.stringify([n.id,'parent']),from:text(n.parent),output:'children',to:text(n.id),input:'parent'}]:[]);
 return {nodes,links};
}
export function connectModelParent(doc:GraphObject,from:PortRef,to:PortRef):GraphObject {
 if(from.port!=='children'||to.port!=='parent'||from.node===to.node)return doc;
 const rows=records(doc.nodes),byId=new Map(rows.map(n=>[n.id,n]));
 if(!byId.has(from.node)||!byId.has(to.node))return doc;
 const visited=new Set<string>();let current=from.node;
 while(current){if(current===to.node||visited.has(current))return doc;visited.add(current);current=text(byId.get(current)?.parent);}
 return replaceAnimationEntry(doc,'pose',to.node,{...byId.get(to.node)!,parent:from.node});
}
export function modelingReply(result:unknown):{document:GraphObject;revision:number}{
 if(!object(result)||result.ok!==true||!object(result.document)||typeof result.revision!=='number'||!Number.isSafeInteger(result.revision)||result.revision<1)throw new Error('Invalid native modeling response');
 return {document:result.document,revision:result.revision};
}

/** Geometry dependency view: native CSG operands, not transform hierarchy. */
export function geometryProjection(doc:GraphObject):GraphView {
 const rows=records(doc.nodes).filter(n=>object(n.geometry));
 const nodes=rows.map((n,i)=>{
  const g=n.geometry as GraphObject;
  const modifiers=records(g.modifiers).map(m=>text(m.kind)).filter(Boolean);
  return {id:text(n.id),label:[text(g.kind),...modifiers].join(' → '),...xy({},i),
   inputs:g.kind==='boolean'?[port('a','mesh'),port('b','mesh')]:[],outputs:[port('mesh','mesh',true)]};
 });
 const links:Link[]=[];
 for(const n of rows){
  const g=n.geometry as GraphObject;
  if(g.kind==='boolean')for(const p of ['a','b'])if(text(g[p]))links.push({id:JSON.stringify([n.id,p]),from:text(g[p]),output:'mesh',to:text(n.id),input:p});
 }
 return {nodes,links};
}
export function connectGeometryOperand(doc:GraphObject,from:PortRef,to:PortRef):GraphObject {
 const rows=records(doc.nodes),i=rows.findIndex(n=>n.id===from.node),j=rows.findIndex(n=>n.id===to.node);
 if(from.port!=='mesh'||!['a','b'].includes(to.port)||i<0||j<=i||!object(rows[i].geometry))return doc;
 const target=rows[j],g=target.geometry;
 if(!object(g)||g.kind!=='boolean'||g[to.port]===from.node)return doc;
 return replaceAnimationEntry(doc,'pose',to.node,{...target,geometry:{...g,[to.port]:from.node}});
}
export function setGeometryBoolean(doc:GraphObject,id:string,operation:string):GraphObject {
 const n=records(doc.nodes).find(x=>x.id===id);
 if(!n||!object(n.geometry)||n.geometry.kind!=='boolean'||!['union','subtract','intersect'].includes(operation))return doc;
 return replaceAnimationEntry(doc,'pose',id,{...n,geometry:{...n.geometry,operation}});
}
export function moveGeometryModifier(doc:GraphObject,id:string,index:number,delta:-1|1):GraphObject {
 const n=records(doc.nodes).find(x=>x.id===id);
 if(!n||!object(n.geometry)||!Array.isArray(n.geometry.modifiers)||!Number.isInteger(index)||![-1,1].includes(delta))return doc;
 const modifiers=n.geometry.modifiers.slice(),target=index+delta;
 if(index<0||index>=modifiers.length||target<0||target>=modifiers.length)return doc;
 [modifiers[index],modifiers[target]]=[modifiers[target],modifiers[index]];
 return replaceAnimationEntry(doc,'pose',id,{...n,geometry:{...n.geometry,modifiers}});
}
export const animationComparisons=['eq','ne','gt','ge','lt','le'] as const;
export function animationScalarNames(doc:GraphObject):string[] {
 return object(doc.parameters)?Object.keys(doc.parameters).filter(k=>typeof (doc.parameters as GraphObject)[k]==='boolean'||(typeof (doc.parameters as GraphObject)[k]==='number'&&Number.isFinite((doc.parameters as GraphObject)[k]))):[];
}
export function setAnimationScalar(doc:GraphObject,name:string,value:number|boolean):GraphObject {
 if(!animationScalarNames(doc).includes(name)||typeof value!=='boolean'&&!Number.isFinite(value))return doc;
 return {...doc,parameters:{...(doc.parameters as GraphObject),[name]:value}};
}
export function editAnimationCondition(doc:GraphObject,stateId:string,index:number,conditionIndex:number,patch:GraphObject|null):GraphObject {
 const state=records(doc.states).find(s=>s.id===stateId);
 if(!state||!Array.isArray(state.transitions)||!Number.isInteger(index)||!Number.isInteger(conditionIndex))return doc;
 const transition=state.transitions[index];
 if(!object(transition))return doc;
 if(transition.when!==undefined&&!Array.isArray(transition.when))return doc;
 const conditions=records(transition.when);
 if(Array.isArray(transition.when)&&conditions.length!==transition.when.length)return doc;
 if(conditionIndex<0||conditionIndex>conditions.length)return doc;
 if(patch===null){if(conditionIndex===conditions.length)return doc;conditions.splice(conditionIndex,1);}
 else{
  if(conditionIndex===conditions.length&&conditions.length>=32)return doc;
  const next={...(conditions[conditionIndex]??{}),...patch};
  if(typeof next.parameter!=='string'||!animationScalarNames(doc).includes(next.parameter)||!animationComparisons.some(op=>op===next.op)||typeof next.value!=='number'||!Number.isFinite(next.value))return doc;
  conditions[conditionIndex]=next;
 }
 return editAnimationTransition(doc,stateId,index,{when:conditions});
}
