import type {GraphView} from './graphFramework';
type Obj=Record<string,unknown>;
const obj=(x:unknown):x is Obj=>!!x&&typeof x==='object'&&!Array.isArray(x);
const rows=(x:unknown):Obj[]=>Array.isArray(x)?x.filter(obj):[];
// Deterministic dependency layers, with bounded bands for long chains.
export function textureLayout(view:GraphView):Map<string,[number,number]> {
 const ids=new Set(view.nodes.map(n=>n.id)), degree=new Map([...ids].map(id=>[id,0])), rank=new Map([...ids].map(id=>[id,0])), next=new Map([...ids].map(id=>[id,[] as string[]]));
 for(const e of view.links)if(ids.has(e.from)&&ids.has(e.to)){degree.set(e.to,degree.get(e.to)!+1);next.get(e.from)!.push(e.to);}
 const q=[...ids].filter(id=>degree.get(id)===0);
 for(let i=0;i<q.length;i++)for(const dst of next.get(q[i])!){rank.set(dst,Math.max(rank.get(dst)!,rank.get(q[i])!+1));degree.set(dst,degree.get(dst)!-1);if(!degree.get(dst))q.push(dst);}
 // Cyclic/unfinished drafts still have a deterministic, editable layout.
 const groups=new Map<number,string[]>();
 for(const id of ids){const k=rank.get(id)!;const g=groups.get(k)??[];g.push(id);groups.set(k,g);}
 const out=new Map<string,[number,number]>();let col=0;
 for(const [,group] of [...groups].sort((a,b)=>a[0]-b[0])){
  const perColumn=view.nodes.some(n=>n.thumbnail)?3:4;
  const pitch=perColumn===3?174:112,band=perColumn===3?540:480;
  group.forEach((id,i)=>{const c=col+Math.floor(i/perColumn);out.set(id,[24+(c%10)*190,24+Math.floor(c/10)*band+(i%perColumn)*pitch]);});
  col+=Math.ceil(group.length/perColumn);
 }
 return out;
}
export function duplicateTextureNode(graph:Obj,id:string):{graph:Obj;id:string}|null {
 const nodes=rows(graph.nodes),source=nodes.find(n=>n.id===id);
 if(!source||nodes.length>=64||typeof source.op!=='string')return null;
 let i=1;while(nodes.some(n=>n.id===source.op+'_'+i))i++;
 const copyId=source.op+'_'+i;
 const pos=Array.isArray(source.position)?source.position:[30,30];
 const offset=(v:unknown)=>typeof v==='number'&&Number.isFinite(v)?Math.min(2000,Math.max(0,v+32)):62;
 const copy={...JSON.parse(JSON.stringify(source)) as Obj,id:copyId,position:[offset(pos[0]),offset(pos[1])]};
 return {graph:{...graph,nodes:[...nodes,copy]},id:copyId};
}
// Ignore authoring coordinates when deciding whether to recompute the pixels.
export function texturePreviewKey(graph:Obj,overrides:string,output:string,resolution:number,options:readonly boolean[]):string {
 const nodes=rows(graph.nodes).map(n=>{const {position:_,...semantic}=n;return semantic;});
 const {editorGroups:_,...document}=graph;
 return JSON.stringify([{...document,nodes},overrides,output,resolution,options]);
}

/** Insert an independent, flattened instance of a saved project graph.
 * The native DAG remains the sole execution format; existing output channels are
 * deliberately preserved. Every exposed source parameter receives a new binding.
 */
export function instantiateTextureGraph(target:Obj,source:Obj):{graph:Obj;ids:string[];outputs:Record<string,string>;parameters:Record<string,string>} {
 const safe=(x:unknown):x is string=>typeof x==='string'&&/^[A-Za-z0-9_-]{1,64}$/.test(x);
 const checked=(x:unknown):Obj[]=>{
  if(!Array.isArray(x)||!x.every(obj))throw new Error('Invalid texture graph nodes');
  const seen=new Set<string>();
  for(const n of x){if(!safe(n.id)||seen.has(n.id)||!safe(n.op))throw new Error('Invalid or duplicate texture node ID');seen.add(n.id);}
  return x;
 };
 const old=checked(target.nodes), incoming=checked(source.nodes);
 if(!incoming.length||old.length+incoming.length>64)throw new Error('Reusable graph exceeds 64-node limit');
 const size=target.size??256;
 if(typeof size!=='number'||!Number.isInteger(size)||size<8||size>1024||size*size*(old.length+incoming.length)>4194304)throw new Error('Reusable graph exceeds native pixel-node budget; reduce bake resolution');
 const defs=(x:unknown):Obj=>{if(x==null)return {};if(!obj(x))throw new Error('Invalid texture graph parameters');return x;};
 const oldDefs=defs(target.parameters),newDefs=defs(source.parameters);
 if(Object.keys(oldDefs).length+Object.keys(newDefs).length>32)throw new Error('Reusable graph exceeds 32-parameter limit');
 const used=new Set(old.map(n=>String(n.id))),usedParams=new Set(Object.keys(oldDefs));
 let instance=1;while(incoming.some((_,i)=>used.has('fn_'+instance+'_'+(i+1)))||Object.keys(newDefs).some((k,i)=>usedParams.has('fn_'+instance+'_'+(i+1)+'_'+k.slice(0,40))))instance++;
 const ids=new Map(incoming.map((n,i)=>[String(n.id),'fn_'+instance+'_'+(i+1)]));
 const copy=(x:unknown):unknown=>JSON.parse(JSON.stringify(x));
 const nodes=incoming.map(n=>{
  if(!Array.isArray(n.inputs)||n.inputs.some(v=>typeof v!=='string'||!ids.has(v)))throw new Error('Saved reusable graph contains unresolved input');
  const {position:_,...data}=copy(n) as Obj;
  return {...data,id:ids.get(String(n.id))!,inputs:n.inputs.map(v=>ids.get(String(v))!)};
 });
 const outputs:Record<string,string>=Object.create(null),parameters:Record<string,string>=Object.create(null);
 if(!obj(source.outputs)||!Object.keys(source.outputs).length)throw new Error('Reusable graph has no outputs');
 for(const [key,id] of Object.entries(source.outputs)){if(typeof id!=='string'||!ids.has(id))throw new Error('Invalid reusable graph output');outputs[key]=ids.get(id)!;}
 const additions:Obj=Object.create(null);
 for(const [i,[key,def]] of Object.entries(newDefs).entries()){
  if(!safe(key)||!obj(def)||!Object.hasOwn(def,'default')||def.default==null||!Array.isArray(def.bindings)||!def.bindings.length||def.bindings.length>32)throw new Error('Invalid reusable parameter');
  const name='fn_'+instance+'_'+(i+1)+'_'+key.slice(0,40);
  const bindings=def.bindings.map(b=>{
   if(!obj(b)||typeof b.node!=='string'||!ids.has(b.node)||!safe(b.property))throw new Error('Invalid reusable parameter binding');
   return {node:ids.get(b.node)!,property:b.property};
  });
  parameters[key]=name;additions[name]={default:copy(def.default),bindings};
 }
 const graph:Obj={...target,nodes:[...old,...nodes]};
 if(Object.keys(additions).length)graph.parameters={...oldDefs,...additions};
 if(new TextEncoder().encode(JSON.stringify(graph)).length>256*1024)throw new Error('Reusable graph exceeds 256 KiB document limit');
 return {graph,ids:[...ids.values()],outputs,parameters};
}
