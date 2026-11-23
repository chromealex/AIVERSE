import {useEffect,useRef,useState} from 'react';
import {invoke} from '../bridge';
import {StudioAlert,StudioButton,StudioInput,StudioSelect,StudioSection} from '../ui';
import GraphCanvas from './GraphCanvas';
import {validateTopology} from './graphFramework';
import type {GraphView,NodeView,Link} from './graphFramework';
import type {PortRef} from './graphGeometry';
import type {MaterialKey,FieldValue} from '../panels/materialSchema';
import {object} from './nativeGraphAdapters';
type Obj=Record<string,unknown>;
const op=async(name:string,params:Obj={})=>(await invoke('studio.op',{op:name,params})).result;
const template={version:1,size:256,nodes:[
 {id:'relief',op:'fbm',inputs:[],properties:{period:8,octaves:4,seed:1}},
 {id:'dark',op:'color',inputs:[],properties:{value:[.15,.12,.1]}},
 {id:'light',op:'color',inputs:[],properties:{value:[.7,.6,.45]}},
 {id:'albedo',op:'colorize',inputs:['relief','dark','light']},
 {id:'normal',op:'normal',inputs:['relief'],properties:{strength:2}},
 {id:'roughness',op:'remap',inputs:['relief'],properties:{low:.35,high:.9}},
 {id:'metallic',op:'constant',inputs:[],properties:{value:0}},
 {id:'packed',op:'metallicRoughness',inputs:['roughness','metallic']}
],outputs:{color:'albedo',height:'relief',normal:'normal',roughness:'roughness',metallic:'metallic',metallicRoughness:'packed'}};
const channels:Record<string,MaterialKey>={color:'texture',normal:'normalMap',metallicRoughness:'metallicRoughnessMap',ao:'occlusionMap',emissive:'emissiveMap',mask:'teamMask'};
function projection(graph:Obj,catalog:Obj[]):GraphView{
 const types=new Map(catalog.map(n=>[n.id,n]));
 const nodes:NodeView[]=[],links:Link[]=[];
 if(!Array.isArray(graph.nodes))return {nodes,links};
 graph.nodes.slice(0,32).filter(object).forEach((n,i)=>{
  if(typeof n.id!=='string'||typeof n.op!=='string')return;
  const t=types.get(n.op);const pos=Array.isArray(n.position)&&n.position.length===2&&n.position.every(v=>typeof v==='number'&&Number.isFinite(v))?n.position:[30+(i%4)*180,30+Math.floor(i/4)*125];
  nodes.push({id:n.id,label:n.op+' / '+n.id,x:Number(pos[0]),y:Number(pos[1]),inputs:(Array.isArray(t?.inputs)?t.inputs:[]).map((p,i)=>({id:String(i),type:String(p)})),outputs:[{id:'out',type:String(t?.output??'unknown')}]});
  if(Array.isArray(n.inputs))n.inputs.forEach((from,j)=>{if(typeof from==='string'&&from)links.push({id:n.id+':'+j,from,output:'out',to:String(n.id),input:String(j)});});
 });
 return {nodes,links};
}
export default function TextureGraphPanel({projectPath,disabled,onEdit}:{projectPath:string;disabled:boolean;onEdit:(key:MaterialKey,value:FieldValue)=>void}){
 const [text,setText]=useState(JSON.stringify(template,null,2)),[name,setName]=useState(''),[original,setOriginal]=useState<{name:string;graph:Obj}|null>(null);
 const [overrideText,setOverrideText]=useState('{}'),[parameterName,setParameterName]=useState(''),[parameterField,setParameterField]=useState(''),[variantName,setVariantName]=useState(''),[baseMaterial,setBaseMaterial]=useState(''),[bakedSet,setBakedSet]=useState('');
 const [templates,setTemplates]=useState<Obj[]>([]),[outputTypes,setOutputTypes]=useState<Obj>({}),[newOp,setNewOp]=useState('constant'),[pending,setPending]=useState<PortRef|null>(null);
 const [assets,setAssets]=useState<{guid:string;path:string;key:MaterialKey}[]>([]);
 const [names,setNames]=useState<string[]>([]),[catalog,setCatalog]=useState<Obj[]>([]),[selected,setSelected]=useState(''),[output,setOutput]=useState('color'),[textureName,setTextureName]=useState('');
 const [gpuRequest,setGpuRequest]=useState<{id:string;source:string;overrides:string;output:string;resolution:number;bake:boolean}|null>(null),[gpuReady,setGpuReady]=useState(false),[gpuStatus,setGpuStatus]=useState(''),[requireGpu,setRequireGpu]=useState(false),[gpuResolution,setGpuResolution]=useState(32);
 const [preview,setPreview]=useState<{hex:string;width:number;height:number;source:string}|null>(null),[asset,setAsset]=useState<{guid:string;path:string;key:MaterialKey}|null>(null);
 const [busy,setBusy]=useState(false),[error,setError]=useState(''),[status,setStatus]=useState('');
 const epoch=useRef(0),lock=useRef(false),canvas=useRef<HTMLCanvasElement>(null);
 useEffect(()=>{epoch.current++;lock.current=false;setBusy(false);setOriginal(null);setName('');setOverrideText('{}');setBakedSet('');setGpuRequest(null);setGpuReady(false);setGpuStatus('');setText(JSON.stringify(template,null,2));setNames([]);setCatalog([]);setTemplates([]);setOutputTypes({});setAssets([]);setPending(null);setAsset(null);setPreview(null);setError('');setStatus('');
 return()=>{epoch.current++;lock.current=false};},[projectPath]);
 useEffect(()=>{const c=canvas.current;if(!c||!preview||preview.source!==text)return;
 const bytes=new Uint8ClampedArray(preview.hex.length/2);for(let i=0;i<bytes.length;i++)bytes[i]=parseInt(preview.hex.slice(i*2,i*2+2),16);
 c.getContext('2d')?.putImageData(new ImageData(bytes,preview.width,preview.height),0,0);
 },[preview,text]);
 let graph:Obj|null=null;try{const value=JSON.parse(text);if(object(value))graph=value}catch{}
 const edit=(value:Obj)=>{setText(JSON.stringify(value,null,2));setPreview(null);setAsset(null);setAssets([]);};
 const run=async(action:string)=>{
  if(lock.current||disabled||!projectPath)return;lock.current=true;setBusy(true);setError('');setStatus('');const token=epoch.current;
  try{
   if(action==='catalog'){
    const [c,l,t]=await Promise.all([op('texture_graph_catalog'),op('texture_graph_list'),op('texture_graph_templates')]);
    if(token!==epoch.current)return;
    if(!object(c)||!Array.isArray(c.nodes)||!object(l)||!Array.isArray(l.graphs))throw new Error('Catalog unavailable');
    setCatalog(c.nodes.filter(object));setOutputTypes(object(c.outputTypes)?c.outputTypes:{});setTemplates(object(t)&&Array.isArray(t.templates)?t.templates.filter(object):[]);setNames(l.graphs.filter((x):x is string=>typeof x==='string'));return;
   }
   if(action==='load'){
    const r=await op('texture_graph_get',{name});if(token!==epoch.current)return;
    if(!object(r)||r.ok!==true||!object(r.graph))throw new Error('Graph unavailable');
    setOriginal({name,graph:r.graph});edit(r.graph);setSelected('');setPending(null);return;
   }
   if(action==='variant'){
    if(!bakedSet)throw new Error('Bake a set and configure its importer first');
    const r=await op('texture_graph_material_variant',{textureName:bakedSet,materialName:variantName,base:baseMaterial});if(token!==epoch.current)return;
    if(!object(r)||r.ok!==true)throw new Error('Native material variant creation failed');setStatus('Created inherited material '+variantName+'; refresh Materials.');return;
   }
   if(action==='cache'||action==='clearCache'){const r=await op('texture_graph_cache',{clear:action==='clearCache'});if(token===epoch.current)setStatus(JSON.stringify(r));return;}
   if(!graph)throw new Error('Invalid graph JSON');
   const overrides=action==='save'?{}:JSON.parse(overrideText);if(!object(overrides))throw new Error('Overrides must be a JSON object');
   if(action==='save'){
    const r=await op('texture_graph_set',{name,graph,...(original?.name===name?{expected:original.graph}:{})});if(token!==epoch.current)return;
    if(!object(r)||r.ok!==true||!object(r.graph))throw new Error('Native save rejected');
    setOriginal({name,graph:r.graph});setNames(old=>[...new Set([...old,name])].sort());setStatus('Graph saved');return;
   }
   if(action==='validate'){
    const r=await op('texture_graph_validate',{graph,overrides});if(token!==epoch.current)return;if(!object(r)||r.ok!==true)throw new Error('Native validation failed');setStatus('Native topology and work budget valid');return;
   }
   if(action==='gpuPublish'){
    if(!gpuRequest?.bake||!gpuReady||gpuRequest.source!==text||gpuRequest.overrides!==overrideText||gpuRequest.output!==output)throw new Error('No matching completed bake');
    const r=await op('texture_graph_gpu_preview',{action:'publish',request:gpuRequest.id,textureName});if(token!==epoch.current)return;
    if(!object(r)||r.ok!==true||typeof r.path!=='string'||r.output!==output)throw new Error('Native GPU bake publication failed');
    setGpuReady(false);setStatus('Published '+r.path+' via '+String(r.backend));setAsset(null);
    const index=await invoke('studio.assets',{});if(token!==epoch.current)return;
    const a=index.files.find(a=>'content/'+a.path===r.path&&a.importer==='texture'&&a.guid);
    if(a?.guid){const srgb=output==='color'||output==='emissive';const patched=await invoke('studio.assetMetaPatch',{guid:a.guid,key:'sRGB',value:srgb});if(token!==epoch.current)return;
      if(patched.guid!==a.guid||patched.settings.sRGB!==srgb)throw new Error('Importer color-space verification failed');const key=channels[output];if(key)setAsset({guid:a.guid,path:a.path,key});}
    return;
   }
   if(action==='gpuStart'||action==='gpuBake'||action==='gpuRead'){
    const r=await op('texture_graph_gpu_preview',action!=='gpuRead'?{action:'start',graph,overrides,output,requireGpu,resolution:gpuResolution,bake:action==='gpuBake'}:{action:'read'});if(token!==epoch.current)return;
    if(!object(r)||r.ok!==true||typeof r.request!=='string')throw new Error(object(r)?String(r.reason??'GPU owner unavailable'):'GPU owner unavailable');
    if(action!=='gpuRead'){setGpuRequest({id:r.request,source:text,overrides:overrideText,output,resolution:gpuResolution,bake:action==='gpuBake'});setPreview(null);setGpuReady(false);}
    else if(!gpuRequest||gpuRequest.id!==r.request||gpuRequest.source!==text||gpuRequest.overrides!==overrideText||gpuRequest.output!==output||(!gpuRequest.bake&&gpuRequest.resolution!==gpuResolution))throw new Error('Preview belongs to a different request or draft');
    setGpuStatus(String(r.state)+' / '+String(r.backend)+' · '+String(r.receivedBytes??0)+' / '+String(r.totalBytes??0)+' bytes · '+String(r.readbackBlocks??0)+' blocks · encoded passes '+String(r.encodedPasses??0)+(r.reusedPlan===true?' · reused plan':'')+' — '+String(r.reason??''));
    setGpuReady(r.bake===true&&r.state==='ready'&&r.published!==true&&r.busy!==true);
    if(r.state==='ready'&&r.bake!==true){
      if(typeof r.width!=='number'||typeof r.height!=='number'||!Number.isInteger(r.width)||!Number.isInteger(r.height)||r.width<8||r.height<8||r.width>128||r.height>128||typeof r.rgbaHex!=='string'||r.rgbaHex.length!==r.width*r.height*8||!/^[0-9a-f]+$/.test(r.rgbaHex))throw new Error('Invalid GPU preview payload');
      setPreview({hex:r.rgbaHex,width:r.width,height:r.height,source:text});
    }return;
   }
   if(action==='previewNode'||action==='previewOutput'){
    setPreview(null);
    const r=await op('texture_graph_preview',{graph,overrides,...(action==='previewNode'?{node:selected}:{output})});if(token!==epoch.current)return;
    if(!object(r)||r.ok!==true||typeof r.width!=='number'||typeof r.height!=='number'||!Number.isInteger(r.width)||!Number.isInteger(r.height)||r.width<8||r.height<8||r.width>32||r.height>32||typeof r.rgbaHex!=='string'||r.rgbaHex.length!==r.width*r.height*8||!/^[0-9a-f]+$/.test(r.rgbaHex))throw new Error('Invalid native preview');
    setPreview({hex:r.rgbaHex,width:r.width,height:r.height,source:text});return;
   }
   if(action==='bakeSet'){
    setAssets([]);setAsset(null);setBakedSet('');
    const r=await op('texture_graph_bake_set',{graph,textureName,overrides});if(token!==epoch.current)return;
    if(!object(r)||r.ok!==true||!Array.isArray(r.files))throw new Error('Native set bake failed');
    setStatus('Baked '+r.files.length+' PNGs. Resolving GUIDs and configuring color space…');
    const index=await invoke('studio.assets',{});if(token!==epoch.current)return;
    const mapped:{guid:string;path:string;key:MaterialKey}[]=[];let configured=0;
    for(const row of r.files.filter(object)){
      const a=index.files.find(a=>'content/'+a.path===row.path&&a.importer==='texture'&&a.guid);
      if(!a?.guid||typeof row.sRGB!=='boolean'||typeof row.output!=='string')continue;
      const patched=await invoke('studio.assetMetaPatch',{guid:a.guid,key:'sRGB',value:row.sRGB});if(token!==epoch.current)return;
      if(patched.guid!==a.guid||patched.settings.sRGB!==row.sRGB)throw new Error('Importer color-space verification failed for '+a.path);
      configured++;const key=channels[row.output];if(key)mapped.push({guid:a.guid,path:a.path,key});
    }
    setAssets(mapped);if(configured===r.files.length)setBakedSet(textureName);setStatus('Baked '+r.files.length+' PNGs; configured '+configured+' importer color spaces. Assign explicitly; unmatched files remain in Assets.');return;
   }
   if(action==='bake'){
    setAsset(null);const r=await op('texture_graph_bake',{graph,output,textureName,overrides});if(token!==epoch.current)return;
    if(!object(r)||r.ok!==true||typeof r.path!=='string')throw new Error('Native bake failed');
    setStatus('Baked '+r.path);
    const key=channels[output];
    const assets=await invoke('studio.assets',{});if(token!==epoch.current)return;
    const a=assets.files.find(a=>'content/'+a.path===r.path&&a.importer==='texture'&&a.guid);
    if(a?.guid){const srgb=output==='color'||output==='emissive';const patched=await invoke('studio.assetMetaPatch',{guid:a.guid,key:'sRGB',value:srgb});if(token!==epoch.current)return;if(patched.guid!==a.guid||patched.settings.sRGB!==srgb)throw new Error('Importer color-space verification failed');if(key)setAsset({guid:a.guid,path:a.path,key});}else setStatus('Baked '+r.path+'; refresh Assets and assign with asset picker.');
   }
  }catch(e){if(epoch.current===token)setError(e instanceof Error?e.message:String(e))}
  finally{if(epoch.current===token){lock.current=false;setBusy(false)}}
 };
 const view=graph?projection(graph,catalog):{nodes:[],links:[]};
 const outputs=graph&&object(graph.outputs)?Object.keys(graph.outputs):[];
 const rows=graph&&Array.isArray(graph.nodes)?graph.nodes.filter(object):[];
 const current=rows.find(n=>n.id===selected);
 const spec=catalog.find(n=>n.id===current?.op);
 const patchNode=(id:string,patch:Obj)=>{if(graph)edit({...graph,nodes:rows.map(n=>n.id===id?{...n,...patch}:n)});};
 const connect=(from:string,to:string,slot:number)=>{
  if(!graph)return;
  const source=view.nodes.find(n=>n.id===from),target=view.nodes.find(n=>n.id===to);
  if(!target||!target.inputs[slot]||from&&(!source||source.outputs[0]?.type!==target.inputs[slot].type)){setError('Incompatible texture ports');return;}
  const next={...graph,nodes:rows.map(n=>n.id===to&&Array.isArray(n.inputs)?{...n,inputs:n.inputs.map((v,i)=>i===slot?from:v)}:n)};
  if(validateTopology(projection(next,catalog),{cycles:'reject'}).some(x=>x.code==='cycle')){setError('Texture graph cannot contain cycles');return;}
  edit(next);setPending(null);setError('');
 };
 const addNode=()=>{
  if(!graph||rows.length>=32)return;const t=catalog.find(n=>n.id===newOp);if(!t)return;
  let i=1;while(rows.some(n=>n.id===newOp+'_'+i))i++;
  const id=newOp+'_'+i,props:Obj={};
  if(object(t.properties))for(const [key,value] of Object.entries(t.properties))if(object(value))props[key]=value.default;
  edit({...graph,nodes:[...rows,{id,op:newOp,inputs:Array.isArray(t.inputs)?t.inputs.map(()=> ''):[],properties:props}]});setSelected(id);setPending(null);
 };
 const removeNode=()=>{
  if(!graph||!selected)return;const nextOutputs=object(graph.outputs)?Object.fromEntries(Object.entries(graph.outputs).filter(([,v])=>v!==selected)):{};
  edit({...graph,outputs:nextOutputs,nodes:rows.filter(n=>n.id!==selected).map(n=>({...n,inputs:Array.isArray(n.inputs)?n.inputs.map(v=>v===selected?'':v):[]}))});setSelected('');setPending(null);
 };
 const setProp=(key:string,value:unknown)=>{if(current&&typeof current.id==='string')patchNode(current.id,{properties:{...(object(current.properties)?current.properties:{}),[key]:value}})};
 const fieldRows=object(spec?.properties)?Object.entries(spec.properties):[];
 const declareParameter=()=>{
  if(!graph||!current||!spec||!object(spec.properties)||! /^[A-Za-z0-9_-]{1,64}$/.test(parameterName)||!parameterField)return;
  const field=spec.properties[parameterField];if(!object(field))return;
  const defs=object(graph.parameters)?graph.parameters:{};if(Object.keys(defs).length>=32||Object.prototype.hasOwnProperty.call(defs,parameterName)){setError('Parameter exists or declaration limit reached');return;}
  const value=object(current.properties)?current.properties[parameterField]??field.default:field.default;
  edit({...graph,parameters:{...defs,[parameterName]:{default:value,bindings:[{node:current.id,property:parameterField}]}}});setParameterName('');
 };

 return <StudioSection title="Procedural Texture Graph">
  <small>Native CPU DAG. Source graph and baked PNG are separate. Color is sRGB; normal/data outputs require correct importer settings.</small>
  <StudioButton size="sm" disabled={disabled||busy} onClick={()=>void run('catalog')}>Refresh graph / node catalog</StudioButton>
  <StudioSelect aria-label="Saved texture graph" value={names.includes(name)?name:''} disabled={busy||disabled} onChange={e=>{setName(e.target.value);setOriginal(null)}}><option value="">Select saved graph</option>{names.map(n=><option key={n}>{n}</option>)}</StudioSelect>
  <StudioInput aria-label="Graph name" placeholder="Graph name" value={name} disabled={busy||disabled} onChange={e=>setName(e.target.value)}/>
  <div className="au-row" style={{flexWrap:'wrap'}}>{['load','save','validate'].map(action=><StudioButton key={action} size="sm" disabled={busy||disabled||(action!=='validate'&&!name)} onClick={()=>void run(action)}>{action}</StudioButton>)}<StudioButton size="sm" disabled={busy||disabled} onClick={()=>{edit(template);setOriginal(null);setName('')}}>New PBR template</StudioButton></div>
  {templates.length>0&&<StudioSelect aria-label="Native PBR template" value="" disabled={busy||disabled} onChange={e=>{const t=templates.find(t=>t.name===e.target.value);if(t&&object(t.graph)){edit(t.graph);setOriginal(null);setName('');setSelected('');setPending(null)}}}><option value="">Load native PBR template</option>{templates.map(t=><option key={String(t.name)}>{String(t.name)}</option>)}</StudioSelect>}
  {graph&&catalog.length>0&&<div className="au-row"><StudioSelect aria-label="Node type" value={newOp} disabled={busy||disabled} onChange={e=>setNewOp(e.target.value)}>{catalog.map(n=><option key={String(n.id)}>{String(n.id)}</option>)}</StudioSelect><StudioButton size="sm" disabled={busy||disabled||rows.length>=32} onClick={addNode}>Add node</StudioButton><StudioButton size="sm" disabled={busy||disabled||!selected} onClick={removeNode}>Remove node</StudioButton></div>}
  {catalog.length>0&&<div style={{height:300,display:'flex',flexDirection:'column'}}><GraphCanvas view={view} selected={selected} label="Texture DAG" disabled={busy||disabled} onSelect={setSelected} pendingOutput={pending} onOutput={p=>setPending(p)} onInput={p=>{if(pending)connect(pending.node,p.node,Number(p.port))}} onCancel={()=>setPending(null)} onMove={(id,x,y)=>{if(graph&&Array.isArray(graph.nodes))edit({...graph,nodes:graph.nodes.map(n=>object(n)&&n.id===id?{...n,position:[x,y]}:n)})}}/></div>}
  {current&&spec&&<StudioSection title={'Node: '+selected}>
   {Array.isArray(spec.inputs)&&spec.inputs.map((kind,i)=><label key={i} className="au-row"><span>Input {i+1} ({String(kind)})</span><StudioSelect aria-label={'Input '+i} value={Array.isArray(current.inputs)?String(current.inputs[i]??''):''} disabled={busy||disabled} onChange={e=>connect(e.target.value,selected,i)}><option value="">Disconnected</option>{view.nodes.filter(n=>n.id!==selected&&n.outputs[0]?.type===kind).map(n=><option key={n.id}>{n.id}</option>)}</StudioSelect></label>)}
   {fieldRows.map(([key,field])=>{if(!object(field))return null;const value=object(current.properties)?current.properties[key]??field.default:field.default;
    if(field.type==='enum'&&Array.isArray(field.options))return <label key={key} className="au-row"><span>{key}</span><StudioSelect value={String(value)} disabled={busy||disabled} onChange={e=>setProp(key,e.target.value)}>{field.options.map(v=><option key={String(v)}>{String(v)}</option>)}</StudioSelect></label>;
    if(field.type==='rgb')return <div key={key} className="au-row"><span>{key}</span>{[0,1,2].map(i=><StudioInput key={i} type="number" min={0} max={1} step={.01} disabled={busy||disabled} value={Array.isArray(value)?Number(value[i]):0} onChange={e=>{if(!e.target.value.trim())return;const v=Array.isArray(value)?value.slice(0,3):[0,0,0];v[i]=Number(e.target.value);setProp(key,v)}}/>)}</div>;
    return <label key={key} className="au-row"><span>{key}</span><StudioInput type="number" min={Number(field.min)} max={Number(field.max)} step={field.type==='integer'?1:.01} value={Number(value)} disabled={busy||disabled} onChange={e=>{if(e.target.value.trim())setProp(key,Number(e.target.value))}}/></label>;
   })}
  </StudioSection>}
  {graph&&<StudioSection title="Bind outputs">{Object.entries(outputTypes).map(([key,kind])=><label key={key} className="au-row"><span>{key}</span><StudioSelect value={object(graph?.outputs)?String(graph.outputs[key]??''):''} disabled={busy||disabled} onChange={e=>{if(!graph)return;const next={...(object(graph.outputs)?graph.outputs:{})};if(e.target.value)next[key]=e.target.value;else delete next[key];edit({...graph,outputs:next})}}><option value="">Unassigned</option>{view.nodes.filter(n=>n.outputs[0]?.type===kind).map(n=><option key={n.id}>{n.id}</option>)}</StudioSelect></label>)}</StudioSection>}
  <StudioSection title="Texture graph instance parameters">
   <small>Bindings are stored in the source; overrides affect validation, preview and bake only.</small>
   {current&&fieldRows.length>0&&<div className="au-row"><StudioInput aria-label="Parameter name" placeholder="Parameter name" disabled={busy||disabled} value={parameterName} onChange={e=>setParameterName(e.target.value)}/><StudioSelect aria-label="Bound property" value={parameterField} onChange={e=>setParameterField(e.target.value)} disabled={busy||disabled}><option value="">Property</option>{fieldRows.map(([k])=><option key={k}>{k}</option>)}</StudioSelect><StudioButton size="sm" disabled={busy||disabled||!parameterName||!parameterField} onClick={declareParameter}>Expose</StudioButton></div>}
   {graph&&object(graph.parameters)&&Object.entries(graph.parameters).map(([k,v])=><div className="au-row" key={k}><small>{k}: {JSON.stringify(object(v)?v.default:v)}</small><StudioButton size="sm" disabled={busy||disabled} onClick={()=>{if(!graph)return;const next={...(object(graph.parameters)?graph.parameters:{})};delete next[k];edit({...graph,parameters:next})}}>Remove</StudioButton></div>)}
   <textarea aria-label="Instance overrides JSON" rows={4} value={overrideText} disabled={busy||disabled} onChange={e=>{setOverrideText(e.target.value);setPreview(null);setAssets([]);setAsset(null);setBakedSet('')}}/>
   <div className="au-row"><StudioButton size="sm" disabled={busy||disabled} onClick={()=>void run('cache')}>Cache status</StudioButton><StudioButton size="sm" disabled={busy||disabled} onClick={()=>void run('clearCache')}>Clear cache</StudioButton></div>
  </StudioSection>
  <textarea aria-label="Texture graph JSON" value={text} disabled={busy||disabled} rows={12} onChange={e=>{setText(e.target.value);setPreview(null);setAsset(null);setAssets([]);setPending(null)}}/>
  <StudioSelect aria-label="Texture output" value={output} disabled={busy||disabled} onChange={e=>{setOutput(e.target.value);setAsset(null);setPreview(null)}}>{outputs.map(n=><option key={n}>{n}</option>)}</StudioSelect>
  <div className="au-row"><StudioButton size="sm" disabled={busy||disabled||!selected} onClick={()=>void run('previewNode')}>Preview node</StudioButton><StudioButton size="sm" disabled={busy||disabled||!outputs.includes(output)} onClick={()=>void run('previewOutput')}>Preview output</StudioButton></div>
  <StudioSection title="Experimental GPU output preview">
   <small>Native Metal / Vulkan compute, 128×128 maximum. CPU-reference checked; pattern and two-pass normal source supported. Rejected work falls back unless GPU is required. Full-size single-output bake uses native readback; CPU set bake remains available.</small>
   <StudioSelect aria-label="GPU preview resolution" value={String(gpuResolution)} disabled={busy||disabled} onChange={e=>{setGpuResolution(Number(e.target.value));setPreview(null)}}>{[32,64,128].map(n=><option key={n} value={n}>{n} × {n}</option>)}</StudioSelect>
   <label className="au-row"><input type="checkbox" checked={requireGpu} disabled={busy||disabled} onChange={e=>setRequireGpu(e.target.checked)}/> Require GPU</label>
   <div className="au-row"><StudioButton size="sm" disabled={busy||disabled||!outputs.includes(output)} onClick={()=>void run('gpuStart')}>Queue GPU preview</StudioButton><StudioButton size="sm" disabled={busy||disabled||!gpuRequest} onClick={()=>void run('gpuRead')}>Read result</StudioButton></div>
   <div className="au-row"><StudioButton size="sm" disabled={busy||disabled||!outputs.includes(output)} onClick={()=>void run('gpuBake')}>Queue full-size GPU bake</StudioButton><StudioButton size="sm" disabled={busy||disabled||!gpuReady||!textureName||gpuRequest?.source!==text||gpuRequest?.overrides!==overrideText||gpuRequest?.output!==output} onClick={()=>void run('gpuPublish')}>Publish completed PNG</StudioButton></div>
   {gpuStatus&&<small>{gpuStatus}</small>}
  </StudioSection>
  {preview&&preview.source===text&&<canvas ref={canvas} width={preview.width} height={preview.height} style={{width:128,height:128,imageRendering:'pixelated'}} aria-label="Native texture preview"/>}
  <StudioInput aria-label="New texture name" value={textureName} disabled={busy||disabled} onChange={e=>setTextureName(e.target.value)}/>
  <StudioButton disabled={busy||disabled||!textureName||!outputs.includes(output)} onClick={()=>void run('bake')}>Bake selected output to new PNG</StudioButton>
  <StudioButton disabled={busy||disabled||!textureName||!outputs.length} onClick={()=>void run('bakeSet')}>Bake all outputs</StudioButton>
  {bakedSet&&<StudioSection title="Create baked material variant"><small>Inherit an existing material; override only baked channels. No runtime regeneration.</small><StudioInput aria-label="Base material" value={baseMaterial} disabled={busy||disabled} onChange={e=>setBaseMaterial(e.target.value)}/><StudioInput aria-label="New material name" value={variantName} disabled={busy||disabled} onChange={e=>setVariantName(e.target.value)}/><StudioButton disabled={busy||disabled||!baseMaterial||!variantName} onClick={()=>void run('variant')}>Create variant from {bakedSet}</StudioButton></StudioSection>}
  {assets.map(a=><StudioButton key={a.guid} disabled={busy||disabled} onClick={()=>onEdit(a.key,a.guid)}>Assign {a.path} to {a.key}</StudioButton>)}
  {asset&&<StudioButton disabled={busy||disabled} onClick={()=>onEdit(asset.key,asset.guid)}>Assign {asset.path} to material draft</StudioButton>}
  {status&&<StudioAlert>{status}</StudioAlert>}{error&&<StudioAlert tone="danger">{error}</StudioAlert>}
 </StudioSection>;
}
