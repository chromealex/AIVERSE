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
   inputs:Array.isArray(node.inputs)?node.inputs.slice(0,16).map(x=>typeof x==='string'?x:''):[]};
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

/** Authoring edits only. Native compiler remains authoritative. */
export interface ShaderNodeOperation { id:string; inputs:number; domains:readonly string[] }
export function shaderNodeCatalog(ops:readonly ShaderNodeOperation[],domain:string,query=''):ShaderNodeOperation[] {
 const q=query.trim().toLowerCase();
 return ops.filter(op=>op.domains.includes(domain)&&op.id.toLowerCase().includes(q)&&Number.isSafeInteger(op.inputs)&&op.inputs>=0&&op.inputs<=16)
  .slice().sort((a,b)=>a.id.localeCompare(b.id));
}
export function connectShaderInput(source:GraphSource,ops:readonly ShaderNodeOperation[],from:string,to:string,index:number):GraphSource {
 const nodes=source.nodes??[];
 const target=nodes.find(n=>n.id===to);
 const op=ops.find(o=>o.id===target?.op&&o.domains.includes(source.domain??'material'));
 if(!target||!op||!nodes.some(n=>n.id===from)||!Number.isSafeInteger(index)||index<0||index>=op.inputs||op.inputs>16)return source;
 // The new source may not depend on the destination. Other pre-existing invalid branches
 // remain editable; the native compiler validates the complete graph before save.
 const byId=new Map(nodes.map(n=>[n.id,n]));
 const pending=[from],seen=new Set<string>();
 while(pending.length){
  const id=pending.pop()!;
  if(id===to)return source;
  if(seen.has(id))continue;
  seen.add(id);
  pending.push(...(byId.get(id)?.inputs??[]));
 }
 const inputs=Array.from({length:op.inputs},(_,i)=>target.inputs?.[i]??'');
 if(inputs[index]===from)return source;
 inputs[index]=from;
 return {...source,nodes:nodes.map(n=>n.id===to?{...n,inputs}:n)};
}
export function disconnectShaderInput(source:GraphSource,to:string,index:number):GraphSource {
 const node=source.nodes?.find(n=>n.id===to);
 if(!node?.inputs||!Number.isSafeInteger(index)||index<0||index>=node.inputs.length||!node.inputs[index])return source;
 const inputs=node.inputs.slice();inputs[index]='';
 return {...source,nodes:source.nodes!.map(n=>n.id===to?{...n,inputs}:n)};
}
export function setShaderValue(source:GraphSource,id:string,value:readonly number[]):GraphSource {
 const node=source.nodes?.find(n=>n.id===id);
 if(!node||!['constant','parameter'].includes(node.op)||value.length!==4||!value.every(Number.isFinite))return source;
 if(node.value?.length===4&&node.value.every((v,i)=>v===value[i]))return source;
 return {...source,nodes:source.nodes!.map(n=>n.id===id?{...n,value:[...value]}:n)};
}
export function duplicateShaderNode(source:GraphSource,id:string):GraphSource {
 const nodes=source.nodes??[],node=nodes.find(n=>n.id===id);
 if(!node||nodes.length>=128)return source;
 const ids=new Set(nodes.map(n=>n.id));
 let copyId='';for(let i=1;i<=999;i++){copyId='node'+i;if(!ids.has(copyId))break;}
 if(ids.has(copyId))return source;
 const copy={...structuredClone(node),id:copyId,position:[clamp((node.position?.[0]??0)+25),clamp((node.position?.[1]??0)+25)]};
 return {...source,nodes:[...nodes,copy]};
}

export interface ShaderClipboard {domain:string;nodes:GraphNode[]}
export function copyShaderNodes(source:GraphSource,ids:readonly string[]):ShaderClipboard {
 const selected=new Set(ids);
 return {domain:source.domain??'material',nodes:structuredClone((source.nodes??[]).filter(n=>selected.has(n.id)))};
}
export function pasteShaderNodes(source:GraphSource,clip:ShaderClipboard,ops:readonly ShaderNodeOperation[]):{graph:GraphSource;ids:string[]} {
 const nodes=source.nodes??[];
 if(!clip.nodes.length||nodes.length+clip.nodes.length>128||clip.domain!==(source.domain??'material'))return {graph:source,ids:[]};
 if(clip.nodes.some(n=>!ops.some(o=>o.id===n.op&&o.domains.includes(clip.domain))))return {graph:source,ids:[]};
 const remap=new Map<string,string>(),used=new Set(nodes.map(n=>n.id));
 let serial=1;
 for(const n of clip.nodes){
  if(remap.has(n.id))return {graph:source,ids:[]};
  while(used.has('node'+serial))serial++;
  const id='node'+serial++;used.add(id);remap.set(n.id,id);
 }
 const copies=clip.nodes.map(n=>({...structuredClone(n),id:remap.get(n.id)!,
  inputs:n.inputs?.map(input=>remap.get(input)??''),
  position:[clamp((n.position?.[0]??0)+32),clamp((n.position?.[1]??0)+32)]}));
 return {graph:{...source,nodes:[...nodes,...copies]},ids:copies.map(n=>n.id)};
}
export function moveShaderNodes(source:GraphSource,positions:readonly {id:string;x:number;y:number}[]):GraphSource {
 if(!positions.length||positions.some(p=>!Number.isFinite(p.x)||!Number.isFinite(p.y)))return source;
 const map=new Map(positions.map(p=>[p.id,p]));
 return {...source,nodes:(source.nodes??[]).map(n=>{
  const p=map.get(n.id);return p?{...n,position:[clamp(p.x),clamp(p.y)]}:n;
 })};
}
/** Layout acyclic dependencies in columns; refuse cyclic or oversized layouts without mutation. */
export function arrangeShaderNodes(source:GraphSource):GraphSource {
 const nodes=source.nodes??[];
 if(!nodes.length||nodes.length>128)return source;
 const byId=new Map(nodes.map(n=>[n.id,n])),depth=new Map<string,number>(),visiting=new Set<string>();
 if(byId.size!==nodes.length)return source;
 const visit=(id:string):number=>{
  if(depth.has(id))return depth.get(id)!;
  if(visiting.has(id))throw new Error('Cycle');
  visiting.add(id);let d=0;
  for(const input of byId.get(id)?.inputs??[])if(byId.has(input))d=Math.max(d,visit(input)+1);
  visiting.delete(id);depth.set(id,d);return d;
 };
 try{for(const n of nodes)visit(n.id);}catch{return source;}
 const rows=new Map<number,number>(),positions=[];
 for(const n of nodes){
  const d=depth.get(n.id)!,r=rows.get(d)??0;rows.set(d,r+1);
  const x=24+d*180,y=24+r*190;
  if(x>2000||y>2000)return source;
  positions.push({id:n.id,x,y});
 }
 return moveShaderNodes(source,positions);
}

/** Small reusable node-group templates; insertion never changes existing graph outputs. */
export function shaderTintTemplate(domain:string,ops:readonly ShaderNodeOperation[]):ShaderClipboard|null {
 const required=[['input',0],['constant',0],['multiply',2]] as const;
 if(required.some(([id,inputs])=>!ops.some(o=>o.id===id&&o.inputs===inputs&&o.domains.includes(domain))))return null;
 return {domain,nodes:[
  {id:'source',op:'input',position:[20,20]},
  {id:'tint',op:'constant',value:[1,1,1,1],position:[20,120]},
  {id:'result',op:'multiply',inputs:['source','tint'],position:[220,40]}
 ]};
}
export function setShaderOutput(source:GraphSource,key:string,node:string):GraphSource {
 if(!Object.prototype.hasOwnProperty.call(source.outputs??{},key)||!source.nodes?.some(n=>n.id===node)||source.outputs?.[key]===node)return source;
 return {...source,outputs:{...source.outputs,[key]:node}};
}
