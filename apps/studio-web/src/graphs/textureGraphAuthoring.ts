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
 if(incoming.some(n=>n.op==='function'))throw new Error('Insert function graphs as definitions, not flattened copies');
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

const safeTextureId=(v:unknown):v is string=>typeof v==='string'&&/^[A-Za-z0-9_-]{1,64}$/.test(v);
const textureKinds=new Set(['scalar','color','normal','packed','radiance']);
const copyJson=<T>(x:T):T=>JSON.parse(JSON.stringify(x)) as T;
export function textureFunctionLibrary(graph:Obj):Obj {
 return obj(graph.functions)?graph.functions:{};
}
export function textureFunctionSpec(graph:Obj,node:Obj,catalog:Obj[],stack:readonly string[]=[]):Obj|undefined {
 if(node.op!=='function')return catalog.find(n=>n.id===node.op);
 const p=node.properties;if(!obj(p)||!safeTextureId(p.function)||!safeTextureId(p.output))return;
 if(stack.length>=8||stack.includes(p.function))return;
 const library=textureFunctionLibrary(graph);
 const def=Object.hasOwn(library,p.function)?library[p.function]:undefined;
 if(!obj(def)||!Array.isArray(def.inputs)||!def.inputs.every(v=>obj(v)&&safeTextureId(v.id)&&typeof v.type==='string'&&textureKinds.has(v.type))||!obj(def.outputs)||!Object.hasOwn(def.outputs,p.output))return;
 const result=rows(def.nodes).find(n=>n.id===(def.outputs as Obj)[String(p.output)]);
 const output=result?textureFunctionSpec(graph,result,catalog,[...stack,p.function])?.output:undefined;
 if(typeof output!=='string'||!textureKinds.has(output))return;
 return {id:'function',label:p.function+' / '+p.output,inputs:def.inputs.map(v=>(v as Obj).type),inputNames:def.inputs.map(v=>(v as Obj).id),output,properties:{},parameters:obj(def.parameters)?def.parameters:{}};
}
export function checkFunctionDraft(graph:Obj):void {
 const library=textureFunctionLibrary(graph),nodes=rows(graph.nodes);
 if(Object.keys(library).length>16||nodes.length>64)throw new Error('Texture function definition or node limit exceeded');
 const count=(n:Obj,stack:readonly string[]):number=>{
  if(n.op!=='function')return 1;
  const p=n.properties;
  const name=obj(p)&&safeTextureId(p.function)?p.function:'';
  const d=Object.hasOwn(library,name)?library[name]:undefined;
  if(!obj(d)||!Array.isArray(d.nodes)||!d.nodes.length)throw new Error('Missing function definition');
  if(stack.includes(name)||stack.length>=8)throw new Error('Recursive function or nesting depth exceeds 8');
  let total=0;
  for(const child of rows(d.nodes)){total+=count(child,[...stack,name]);if(total>64)throw new Error('Expanded function exceeds node limit');}
  return total;
 };
 for(const [name,def] of Object.entries(library)){
  if(!safeTextureId(name)||!obj(def)||!Array.isArray(def.nodes)||!def.nodes.length)throw new Error('Invalid function definition');
  let total=0;for(const n of rows(def.nodes)){total+=count(n,[name]);if(total>64)throw new Error('Expanded function exceeds node limit');}
 }
 let expanded=0;for(const n of nodes){expanded+=count(n,[]);if(expanded>64)throw new Error('Expanded function exceeds node limit');}
 const size=graph.size??256;
 if(expanded>64||typeof size!=='number'||!Number.isInteger(size)||size<8||size>1024||expanded*size*size>4194304)throw new Error('Expanded function exceeds native work budget; reduce resolution or node count');
 if(new TextEncoder().encode(JSON.stringify(graph)).length>256*1024)throw new Error('Texture graph exceeds 256 KiB document limit');
}
/** Capture selected primitive nodes as a reusable embedded definition, leaving the
 * original graph intact. Boundary edges become typed formal inputs (max 3).
 */
export function defineTextureFunction(graph:Obj,catalog:Obj[],ids:readonly string[],resultId:string,name:string):Obj {
 if(!safeTextureId(name))throw new Error('Use 1–64 letters, digits, underscores or hyphens for the function name');
 const library=textureFunctionLibrary(graph);
 if(Object.hasOwn(library,name))throw new Error('Function name already exists');
 const all=rows(graph.nodes),selected=new Set(ids),body=all.filter(n=>selected.has(String(n.id)));
 if(!body.length||body.length!==selected.size||!selected.has(resultId))throw new Error('Select function body and result node');
 if(body.some(n=>n.op==='function'))throw new Error('Nested function definitions are not supported');
 const formal:Obj[]=[],external=new Map<string,string>(),reserved=new Set(body.map(n=>String(n.id)));
 const nodes=body.map(n=>{
  const spec=textureFunctionSpec(graph,n,catalog);
  if(!safeTextureId(n.id)||!spec||!Array.isArray(spec.inputs)||!Array.isArray(n.inputs)||spec.inputs.length!==n.inputs.length)throw new Error('Unknown function body node or input arity');
  const inputs=n.inputs.map((v,i)=>{
   if(typeof v!=='string'||!v)throw new Error('Connect all selected body inputs first');
   const upstream=all.find(x=>x.id===v),kind=spec.inputs instanceof Array?spec.inputs[i]:undefined;
   if(!upstream||textureFunctionSpec(graph,upstream,catalog)?.output!==kind)throw new Error('Invalid function body input type');
   if(selected.has(v))return v;
   const prior=external.get(v);if(prior)return prior;
   if(formal.length>=3)throw new Error('Functions allow at most three external inputs');
   let k=1;while(reserved.has('input_'+k))k++;
   const id='input_'+k;reserved.add(id);external.set(v,id);formal.push({id,type:kind});return id;
  });
  const {position:_,...data}=copyJson(n);return {...data,inputs};
 });
 const parameters:Obj=Object.create(null);
 if(obj(graph.parameters))for(const [key,value] of Object.entries(graph.parameters)){
  if(!obj(value)||!Array.isArray(value.bindings))continue;
  const bindings=value.bindings.filter(v=>obj(v)&&selected.has(String(v.node)));
  if(bindings.length)parameters[key]={default:copyJson(value.default),bindings:copyJson(bindings)};
 }
 const next={...graph,functions:{...library,[name]:{inputs:formal,nodes,outputs:{value:resultId},parameters}}};
 checkFunctionDraft(next);return next;
}
export function addTextureFunctionCall(graph:Obj,catalog:Obj[],name:string,output:string):{graph:Obj;id:string} {
 const nodes=rows(graph.nodes);let i=1;while(nodes.some(n=>n.id==='function_'+i))i++;
 const id='function_'+i,call:Obj={id,op:'function',properties:{function:name,output,arguments:{}}};
 const spec=textureFunctionSpec(graph,call,catalog);
 if(!spec||!Array.isArray(spec.inputs))throw new Error('Unknown function or output');
 call.inputs=spec.inputs.map(()=>'');
 const next={...graph,nodes:[...nodes,call]};checkFunctionDraft(next);return {graph:next,id};
}
export function removeTextureFunction(graph:Obj,name:string):Obj {
 const callers=[...rows(graph.nodes),...Object.entries(textureFunctionLibrary(graph)).filter(([key])=>key!==name).flatMap(([,def])=>obj(def)?rows(def.nodes):[])];
 if(callers.some(n=>n.op==='function'&&obj(n.properties)&&n.properties.function===name))throw new Error('Remove all function instances, including nested calls, first');
 const library={...textureFunctionLibrary(graph)};delete library[name];
 const links={...textureFunctionLinks(graph)};delete links[name];
 return {...graph,functions:library,functionLinks:links};
}

/** Project links retain immutable embedded definition snapshots. The engine can
 * compile an offline graph; explicit refresh is the only operation that changes it.
 */
export function textureFunctionLinks(graph:Obj):Obj {
 return obj(graph.functionLinks)?graph.functionLinks:{};
}
function assetDefinition(asset:Obj):{name:string;definition:Obj} {
 const marker=asset.functionAsset;
 if(!obj(marker)||marker.version!==1||!safeTextureId(marker.definition))throw new Error('Not a standalone texture function asset');
 const library=textureFunctionLibrary(asset),name=marker.definition;
 const definition=Object.hasOwn(library,name)?library[name]:undefined;
 if(!obj(definition))throw new Error('Function asset definition missing');
 return {name,definition};
}
function functionInterface(def:Obj,catalog:Obj[],graph:Obj):string {
 if(!Array.isArray(def.inputs)||!obj(def.outputs)||!Object.keys(def.outputs).length)throw new Error('Invalid function interface');
 const inputs=def.inputs.map(v=>{
  if(!obj(v)||!safeTextureId(v.id)||typeof v.type!=='string'||!textureKinds.has(v.type))throw new Error('Invalid formal input');
  return [v.id,v.type];
 });
 const outputs=Object.entries(def.outputs).sort(([a],[b])=>a.localeCompare(b)).map(([name,id])=>{
  const node=rows(def.nodes).find(n=>n.id===id),kind=node?textureFunctionSpec(graph,node,catalog)?.output:undefined;
  if(!safeTextureId(name)||typeof kind!=='string'||!textureKinds.has(kind))throw new Error('Invalid function output');
  return [name,kind];
 });
 const parameters=Object.entries(obj(def.parameters)?def.parameters:{}).sort(([a],[b])=>a.localeCompare(b)).map(([name,p])=>{
  if(!safeTextureId(name)||!obj(p)||!Array.isArray(p.bindings)||!p.bindings.length)throw new Error('Invalid function parameter');
  const types=p.bindings.map(b=>{
   if(!obj(b)||typeof b.property!=='string')throw new Error('Invalid parameter binding');
   const node=rows(def.nodes).find(n=>n.id===b.node),fields=catalog.find(n=>n.id===node?.op)?.properties;
   if(node?.op==='function'&&b.property.startsWith('arguments.')){
    const spec=textureFunctionSpec(graph,node,catalog),arg=b.property.slice(10);
    if(!safeTextureId(arg)||!obj(spec?.parameters)||!Object.hasOwn(spec.parameters,arg))throw new Error('Unknown forwarded function argument');
    return 'functionArgument';
   }
   const field=obj(fields)&&Object.hasOwn(fields,b.property)?fields[b.property]:undefined;
   if(!obj(field)||typeof field.type!=='string')throw new Error('Unknown parameter property');
   return field.type;
  });
  if(types.some(t=>t!==types[0]))throw new Error('Incompatible parameter bindings');
  return [name,types[0]];
 });
 return JSON.stringify([inputs,outputs,parameters]);
}
function functionClosure(graph:Obj,name:string):Obj {
 const library=textureFunctionLibrary(graph),out:Obj=Object.create(null),active=new Set<string>();
 const visit=(key:string,depth:number)=>{
  if(active.has(key)||depth>8)throw new Error('Recursive function or nesting depth exceeds 8');
  if(Object.hasOwn(out,key))return;
  const def=Object.hasOwn(library,key)?library[key]:undefined;
  if(!safeTextureId(key)||!obj(def))throw new Error('Missing function dependency');
  active.add(key);
  for(const n of rows(def.nodes))if(n.op==='function'){
   if(!obj(n.properties)||!safeTextureId(n.properties.function))throw new Error('Invalid nested function call');
   visit(n.properties.function,depth+1);
  }
  active.delete(key);out[key]=def;
 };
 visit(name,1);return out;
}
function importFunctionClosure(asset:Obj,name:string,alias:string,library:Obj,prior:Obj={}):{definitions:Obj;helpers:Obj} {
 const closure=functionClosure(asset,name),mapping:Record<string,string>=Object.create(null),helpers:Obj=Object.create(null);
 mapping[name]=alias;
 const reserved=new Set([...Object.keys(library),alias]);let serial=0;
 for(const key of Object.keys(closure).sort())if(key!==name){
  const old=prior[key];let mapped:string;
  if(safeTextureId(old)&&Object.hasOwn(library,old)&&old!==alias)mapped=old;
  else{do{mapped=alias.slice(0,48)+'_dep_'+(++serial);}while(reserved.has(mapped));}
  reserved.add(mapped);mapping[key]=mapped;helpers[key]=mapped;
 }
 const definitions:Obj=Object.create(null);
 for(const [key,def] of Object.entries(closure)){
  if(!obj(def))throw new Error('Invalid function dependency');
  const copy=copyJson(def);
  copy.nodes=rows(copy.nodes).map(n=>{
   if(n.op!=='function')return n;
   if(!obj(n.properties)||!safeTextureId(n.properties.function)||!Object.hasOwn(mapping,n.properties.function))throw new Error('Missing nested function dependency');
   return {...n,properties:{...n.properties,function:mapping[n.properties.function]}};
  });
  definitions[mapping[key]]=copy;
 }
 return {definitions,helpers};
}
/** Build ordinary native-validatable graph documents for all named results.
 * The first document is the standalone saved asset; the rest are validation probes.
 */
export function makeTextureFunctionAsset(graph:Obj,catalog:Obj[],name:string):Obj[] {
 const library=textureFunctionLibrary(graph),def=Object.hasOwn(library,name)?library[name]:undefined;
 if(!obj(def)||!Array.isArray(def.inputs)||!obj(def.outputs))throw new Error('Select a valid function definition');
 functionInterface(def,catalog,graph);
 const nodes:Obj[]=[],args:string[]=[];
 def.inputs.forEach((input,i)=>{
  if(!obj(input))throw new Error('Invalid function input');
  const id='asset_input_'+i,scalarId=id+'_scalar';
  if(input.type==='scalar')nodes.push({id,op:'constant',inputs:[],properties:{value:.5}});
  else if(input.type==='color')nodes.push({id,op:'color',inputs:[],properties:{value:[.5,.5,.5]}});
  else if(input.type==='radiance')nodes.push({id,op:'hdrConstant',inputs:[],properties:{radiance:[1,1,1]}});
  else if(input.type==='normal'||input.type==='packed'){
   nodes.push({id:scalarId,op:'constant',inputs:[],properties:{value:.5}});
   nodes.push(input.type==='normal'?{id,op:'normal',inputs:[scalarId]}:{id,op:'metallicRoughness',inputs:[scalarId,scalarId]});
  }else throw new Error('Unsupported function input kind');
  args.push(id);
 });
 const channels:Record<string,string>={scalar:'height',color:'color',normal:'normal',packed:'metallicRoughness',radiance:'radiance'};
 return Object.keys(def.outputs).sort().map(output=>{
  const call:Obj={id:'asset_result',op:'function',inputs:args,properties:{function:name,output,arguments:{}}};
  const kind=textureFunctionSpec(graph,call,catalog)?.output;
  if(typeof kind!=='string'||!Object.hasOwn(channels,kind))throw new Error('Invalid function output kind');
  const asset={version:1,size:32,nodes:[...nodes,call],outputs:{[channels[kind]]:'asset_result'},functions:copyJson(functionClosure(graph,name)),functionAsset:{version:1,definition:name}};
  checkFunctionDraft(asset);return asset;
 });
}
export function linkTextureFunctionAsset(graph:Obj,assetName:string,asset:Obj,catalog:Obj[]):{graph:Obj;alias:string} {
 if(!safeTextureId(assetName))throw new Error('Invalid function asset name');
 const {name,definition}=assetDefinition(asset);functionInterface(definition,catalog,asset);
 const library=textureFunctionLibrary(graph),links=textureFunctionLinks(graph);
 if(Object.hasOwn(library,assetName)||Object.hasOwn(links,assetName))throw new Error('Function alias exists; refresh the existing link or choose another asset');
 const imported=importFunctionClosure(asset,name,assetName,library);
 const next={...graph,functions:{...library,...imported.definitions},functionLinks:{...links,[assetName]:{asset:assetName,definition:name,helpers:imported.helpers}}};
 checkFunctionDraft(next);return {graph:next,alias:assetName};
}
export function refreshTextureFunctionAsset(graph:Obj,alias:string,asset:Obj,catalog:Obj[]):Obj {
 const library=textureFunctionLibrary(graph),links=textureFunctionLinks(graph);
 const link=Object.hasOwn(links,alias)?links[alias]:undefined,old=Object.hasOwn(library,alias)?library[alias]:undefined;
 if(!obj(link)||!obj(old))throw new Error('Function is not linked');
 const {name,definition}=assetDefinition(asset);
 if(name!==link.definition||functionInterface(old,catalog,graph)!==functionInterface(definition,catalog,asset))throw new Error('Incompatible function revision: inputs, results or parameter types changed');
 const prior=obj(link.helpers)?link.helpers:{};
 const imported=importFunctionClosure(asset,name,alias,library,prior);
 // Do not overwrite a private helper that has independent external callers.
 const owned=new Set([alias,...Object.values(prior).filter(safeTextureId)]);
 for(const [key,value] of Object.entries(imported.definitions))if(key!==alias&&Object.hasOwn(library,key)&&JSON.stringify(library[key])!==JSON.stringify(value)){
  const outside=[...rows(graph.nodes),...Object.entries(library).filter(([n])=>!owned.has(n)).flatMap(([,def])=>obj(def)?rows(def.nodes):[])];
  if(outside.some(n=>n.op==='function'&&obj(n.properties)&&n.properties.function===key))throw new Error('Linked helper has independent callers: '+key);
 }
 const next={...graph,functions:{...library,...imported.definitions},functionLinks:{...links,[alias]:{...link,helpers:imported.helpers}}};
 checkFunctionDraft(next);return next;
}
export function detachTextureFunctionAsset(graph:Obj,alias:string):Obj {
 const links={...textureFunctionLinks(graph)};delete links[alias];
 return {...graph,functionLinks:links};
}

// A dependency refresh is a draft transaction, never an implicit save.
// Scan root and every embedded definition so nested calls share new pins.
export function textureImageDependencyPaths(graph:Obj):string[] {
 const documents=[graph,...Object.values(textureFunctionLibrary(graph)).filter(obj)];
 const paths=new Set<string>();
 for(const doc of documents){
  for(const node of rows(doc.nodes))if(node.op==='imageAsset'||node.op==='imageAssetChannel'||node.op==='hdrAsset'){
   const p=node.properties;
   if(!obj(p)||typeof p.asset!=='string'||!p.asset||typeof p.revision!=='string'||! /^[0-9a-f]{64}$/.test(p.revision))throw new Error('Unpinned image dependency');
   paths.add(p.asset);
  }
  for(const param of Object.values(obj(doc.parameters)?doc.parameters:{}))if(obj(param))
   for(const binding of rows(param.bindings))if(binding.property==='asset'||binding.property==='revision')
    throw new Error('Automatic refresh cannot rewrite parameter-controlled image pins');
 }
 if(paths.size>64)throw new Error('Dependency scan exceeds 64 distinct image sources');
 return [...paths].sort();
}
export function refreshTextureImagePins(graph:Obj,revisions:ReadonlyMap<string,string>):Obj {
 const paths=textureImageDependencyPaths(graph);
 for(const path of paths)if(!/^[0-9a-f]{64}$/.test(revisions.get(path)??''))throw new Error('Missing verified image revision: '+path);
 let changed=false;
 const update=(doc:Obj):Obj=>{
  const nodes=rows(doc.nodes).map(n=>{
   if(n.op!=='imageAsset'&&n.op!=='imageAssetChannel'&&n.op!=='hdrAsset')return n;
   const p=n.properties as Obj,revision=revisions.get(String(p.asset))!;
   if(p.revision===revision)return n;
   changed=true;return {...n,properties:{...p,revision}};
  });
  return {...doc,nodes};
 };
 const next=update(graph),defs=textureFunctionLibrary(graph);
 if(Object.keys(defs).length)next.functions=Object.fromEntries(Object.entries(defs).map(([name,def])=>[name,obj(def)?update(def):def]));
 return changed?next:graph;
}
// Exact structural snapshot for safe in-session linked function refresh.
// Ignore image revisions, since their independent refresh is intentional.
export function textureLinkedSnapshot(graph:Obj,alias:string):string {
 const closure=functionClosure(graph,alias);
 const normalized=Object.fromEntries(Object.entries(closure).sort(([x],[y])=>x.localeCompare(y)).map(([key,def])=>{
  if(!obj(def))throw new Error('Invalid linked definition');
  return [key,{...def,nodes:rows(def.nodes).map(n=>{
   if((n.op!=='imageAsset'&&n.op!=='imageAssetChannel'&&n.op!=='hdrAsset')||!obj(n.properties))return n;
   const {revision:_,...properties}=n.properties;return {...n,properties};
  })}];
 }));
 return JSON.stringify(normalized);
}
