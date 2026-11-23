import type {GraphView,Port} from './graphFramework';
import type {Point,Positioned} from './graphSelection';

export interface PortRef {node:string;port:string}
export interface PortGeometry extends PortRef,Point {type:string;direction:'input'|'output'}
export interface WireGeometry {id:string;x1:number;y1:number;x2:number;y2:number}
export const nodeWidth=130;
export const portY=(index:number)=>25+index*14;
export const nodeHeight=(inputs:readonly Port[],outputs:readonly Port[])=>Math.max(54,38+Math.max(inputs.length,outputs.length)*14);
export function graphGeometry(view:GraphView,preview:readonly Positioned[]=[]){
 const moved=new Map(preview.map(p=>[p.id,p]));
 const nodes=view.nodes.map(n=>({...n,...moved.get(n.id),height:nodeHeight(n.inputs,n.outputs)}));
 const byId=new Map(nodes.map(n=>[n.id,n]));
 const ports:PortGeometry[]=[];
 for(const n of nodes){
  n.inputs.forEach((p,i)=>ports.push({node:n.id,port:p.id,type:p.type,direction:'input',x:n.x,y:n.y+portY(i)}));
  n.outputs.forEach((p,i)=>ports.push({node:n.id,port:p.id,type:p.type,direction:'output',x:n.x+nodeWidth,y:n.y+portY(i)}));
 }
 const wires:WireGeometry[]=[];
 for(const e of view.links){
  const from=byId.get(e.from),to=byId.get(e.to);
  if(!from||!to)continue;
  const output=from.outputs.findIndex(p=>p.id===e.output),input=to.inputs.findIndex(p=>p.id===e.input);
  if(output<0||input<0)continue;
  wires.push({id:e.id,x1:from.x+nodeWidth,y1:from.y+portY(output),x2:to.x,y2:to.y+portY(input)});
 }
 return {nodes,ports,wires};
}
export function samePort(a:PortRef|null|undefined,b:PortRef):boolean {
 return !!a&&a.node===b.node&&a.port===b.port;
}
