/** SpectraFX Studio Graph v2 canvas: layout only, no parallel compiler.
 * All connections/additions/deletions are validated by native fx_graph_edit.
 */
import type { SpectraGraphV2, FxNode, GraphEdit } from './spectraDataInterfaceModel';

export interface GraphPoint {x:number;y:number}
export interface VisualEdge {id:string;from:string;to:string;toPort?:string;path:string}
const width=170;
const finite=(n:unknown):n is number=>typeof n==='number'&&Number.isFinite(n)&&Math.abs(n)<=100000;
export function nodePoint(node:FxNode,index:number):GraphPoint {
  if(Array.isArray(node.position)&&node.position.length===2&&finite(node.position[0])&&finite(node.position[1]))
    return {x:node.position[0],y:node.position[1]};
  return {x:20+(index%4)*230,y:30+Math.floor(index/4)*125};
}
export function moveGraphNode(graph:SpectraGraphV2,id:string,point:GraphPoint):GraphEdit {
  if(!graph.nodes.some(n=>n.id===id))return {graph,error:'Unknown graph node.'};
  if(!finite(point.x)||!finite(point.y))return {graph,error:'Invalid node position.'};
  return {graph:{...graph,nodes:graph.nodes.map(n=>n.id===id?
    {...n,position:[Math.round(point.x),Math.round(point.y)]}:n)},error:''};
}
export function visualEdges(graph:SpectraGraphV2):VisualEdge[] {
  const locations=new Map(graph.nodes.map((node,i)=>[node.id,nodePoint(node,i)]));
  const edges:VisualEdge[]=[];
  for(const [i,raw] of graph.links.entries()){
    if(!raw||typeof raw!=='object'||Array.isArray(raw))continue;
    const e=raw as {from?:unknown;to?:unknown;toPort?:unknown};
    if(typeof e.from!=='string'||typeof e.to!=='string')continue;
    const a=locations.get(e.from),b=locations.get(e.to);
    if(!a||!b)continue;
    const data=typeof e.toPort==='string';
    const x1=a.x+width,y1=a.y+26,x2=b.x,y2=b.y+26;
    const ctrl=Math.max(40,Math.abs(x2-x1)/2);
    edges.push({id:String(i)+':'+e.from+':'+e.to,from:e.from,to:e.to,
      ...(data?{toPort:e.toPort as string}:{}),
      path:`M${x1} ${y1} C${x1+ctrl} ${y1} ${x2-ctrl} ${y2} ${x2} ${y2}`});
  }
  return edges;
}
export function connectionParams(action:'connect'|'disconnect',graph:SpectraGraphV2,
                                 from:string,to:string,toPort=''):
  Record<string,unknown>|null {
  if(!graph.nodes.some(n=>n.id===from) || !graph.nodes.some(n=>n.id===to) ||
     from===to)return null;
  return {action,graph,from,to,...(toPort?{toPort}:{})};
}
