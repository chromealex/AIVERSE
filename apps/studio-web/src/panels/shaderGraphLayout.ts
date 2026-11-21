// Pure display/layout adapter. Graphs are authored by AIVERSE, not executed in the browser.
export interface GraphNode {
 id: string; op: string; inputs?: string[]; position?: number[]; value?: number[];
}
export interface GraphSource {
 version?: number; domain?: string;
 nodes?: GraphNode[]; outputs?: Record<string,string>;
}
export interface PositionedNode { id:string; op:string; x:number; y:number; inputs:string[]; }
export interface GraphEdge { key:string; x1:number; y1:number; x2:number; y2:number; }
const clamp=(n:number)=>Number.isFinite(n)?Math.max(0,Math.min(2000,n)):0;
export function layoutShaderGraph(source:GraphSource):{nodes:PositionedNode[];edges:GraphEdge[];width:number;height:number}{
 const nodes=(source.nodes??[]).slice(0,128).map((node,index)=>{
  const p=node.position;
  return {id:node.id,op:node.op,x:clamp(p?.[0]??(index%5)*180),y:clamp(p?.[1]??Math.floor(index/5)*110),
   inputs:Array.isArray(node.inputs)?node.inputs.slice(0,8).filter(x=>typeof x==='string'):[]};
 });
 const byId=new Map(nodes.map(n=>[n.id,n]));
 const edges:GraphEdge[]=[];
 for(const to of nodes) for(const [index,key] of to.inputs.entries()){
  const from=byId.get(key);if(!from)continue;
  edges.push({key:from.id+'>'+to.id+':'+index,x1:from.x+130,y1:from.y+25,
   x2:to.x,y2:to.y+25+index*9});
 }
 return {nodes,edges,
  width:Math.max(520,...nodes.map(n=>n.x+150)),
  height:Math.max(320,...nodes.map(n=>n.y+90))};
}

/** Immutable node coordinate editing. Does not rewrite connections or operations. */
export function moveShaderGraphNode(source:GraphSource,id:string,x:number,y:number):GraphSource {
 if(!Number.isFinite(x)||!Number.isFinite(y)||!Array.isArray(source.nodes))return source;
 const clamped:[number,number]=[clamp(x),clamp(y)];
 return {...source,nodes:source.nodes.map(node=>node.id===id?
  {...node,position:clamped}:node)};
}

/** Generate one bounded authoring node from the native operation catalog. */
export function addShaderGraphNode(source:GraphSource,op:{id:string;inputs:number}):GraphSource {
 if(!Array.isArray(source.nodes)||source.nodes.length>=128||
    !/^[A-Za-z][A-Za-z0-9_-]{0,63}$/.test(op.id)||
    !Number.isSafeInteger(op.inputs)||op.inputs<0||op.inputs>16)return source;
 const ids=new Set(source.nodes.map(n=>n.id));
 let id='';
 for(let i=1;i<=999;i++){id='node'+i;if(!ids.has(id))break;}
 if(ids.has(id))return source;
 const first=source.nodes[0]?.id;
 const inputs=first?Array(op.inputs).fill(first):[];
 const node:GraphNode={id,op:op.id,inputs,position:[
  50+(source.nodes.length%5)*180,50+Math.floor(source.nodes.length/5)*110]};
 if(op.id==='constant'||op.id==='parameter')node.value=[.2,.6,1,1];
 return {...source,nodes:[...source.nodes,node]};
}
/** Reject deleting a node still referenced by inputs or outputs. */
export function removeShaderGraphNode(source:GraphSource,id:string):GraphSource {
 if(!Array.isArray(source.nodes)||!source.nodes.some(n=>n.id===id))return source;
 if(source.nodes.some(n=>n.id!==id&&n.inputs?.includes(id))||
    Object.values(source.outputs??{}).includes(id))return source;
 return {...source,nodes:source.nodes.filter(n=>n.id!==id)};
}
