import {useEffect,useRef,useState} from 'react';
import type {CSSProperties} from 'react';
import {invoke} from '../bridge';
import {StudioAlert,StudioButton,StudioInput,StudioSelect,StudioSection} from '../ui';
import GraphCanvas from './GraphCanvas';
import {validateTopology,DraftHistory} from './graphFramework';
import {textureLayout,duplicateTextureNode,texturePreviewKey,instantiateTextureGraph,textureFunctionLibrary,textureFunctionSpec,defineTextureFunction,addTextureFunctionCall,removeTextureFunction,checkFunctionDraft,textureFunctionLinks,makeTextureFunctionAsset,linkTextureFunctionAsset,refreshTextureFunctionAsset,detachTextureFunctionAsset,textureImageDependencyPaths,refreshTextureImagePins,textureLinkedSnapshot} from './textureGraphAuthoring';
import type {GraphView,NodeView,Link} from './graphFramework';
import type {PortRef} from './graphGeometry';
import type {MaterialKey,FieldValue} from '../panels/materialSchema';
import {object} from './nativeGraphAdapters';
import './TextureGraphPanel.css';
type Obj=Record<string,unknown>;
type StudioGpuTarget={kind:'pbr'|'scene';asset?:string;expectedVersion:string;expectedModelPublication:string;materialIndex:number};
type PreviewImage={hex:string;width:number;height:number;source:string};
function targetStamp(r:unknown,kind:StudioGpuTarget['kind'],asset?:string):StudioGpuTarget{
 if(!object(r)||r.ok!==true||typeof r.expectedVersion!=='string'||typeof r.expectedModelPublication!=='string'||typeof r.materialIndex!=='number'||!Number.isInteger(r.materialIndex)||r.materialIndex<0)throw new Error('Native material target unavailable');
 return {kind,asset,expectedVersion:r.expectedVersion,expectedModelPublication:r.expectedModelPublication,materialIndex:r.materialIndex};
}
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
 const nodes:NodeView[]=[],links:Link[]=[];
 if(!Array.isArray(graph.nodes))return {nodes,links};
 graph.nodes.slice(0,64).filter(object).forEach((n,i)=>{
  if(typeof n.id!=='string'||typeof n.op!=='string')return;
  const t=textureFunctionSpec(graph,n,catalog);const pos=Array.isArray(n.position)&&n.position.length===2&&n.position.every(v=>typeof v==='number'&&Number.isFinite(v))?n.position:[30+(i%4)*180,30+Math.floor(i/4)*125];
  nodes.push({id:n.id,label:String(t?.label??n.op)+' / '+n.id,x:Number(pos[0]),y:Number(pos[1]),inputs:(Array.isArray(t?.inputs)?t.inputs:[]).map((p,i)=>({id:String(i),type:String(p)})),outputs:[{id:'out',type:String(t?.output??'unknown')}]});
  if(Array.isArray(n.inputs))n.inputs.forEach((from,j)=>{if(typeof from==='string'&&from)links.push({id:n.id+':'+j,from,output:'out',to:String(n.id),input:String(j)});});
 });
 return {nodes,links};
}
export default function TextureGraphPanel({projectPath,disabled,onEdit,active=true}:{projectPath:string;disabled:boolean;active?:boolean;onEdit:(key:MaterialKey,value:FieldValue)=>void}){
 const [reuseName,setReuseName]=useState('');
 const [functionAssetName,setFunctionAssetName]=useState(''),[functionAssetRecord,setFunctionAssetRecord]=useState<{name:string;graph:Obj}|null>(null);
 const [functionName,setFunctionName]=useState(''),[functionChoice,setFunctionChoice]=useState(''),[functionOutput,setFunctionOutput]=useState('');
 const [selection,setSelection]=useState<string[]>([]),[groupTitle,setGroupTitle]=useState(''),[snippet,setSnippet]=useState<Obj[]>([]);
 const [text,setText]=useState(JSON.stringify(template,null,2)),[name,setName]=useState(''),[original,setOriginal]=useState<{name:string;graph:Obj}|null>(null);
 const [overrideText,setOverrideText]=useState('{}'),[parameterName,setParameterName]=useState(''),[parameterField,setParameterField]=useState(''),[variantName,setVariantName]=useState(''),[baseMaterial,setBaseMaterial]=useState(''),[bakedSet,setBakedSet]=useState('');
 const [templates,setTemplates]=useState<Obj[]>([]),[outputTypes,setOutputTypes]=useState<Obj>({}),[newOp,setNewOp]=useState('constant'),[pending,setPending]=useState<PortRef|null>(null);
 const [assets,setAssets]=useState<{guid:string;path:string;key:MaterialKey}[]>([]);
 const [imageAssets,setImageAssets]=useState<string[]>([]),[imageAssetChoice,setImageAssetChoice]=useState('');
 const [names,setNames]=useState<string[]>([]),[catalog,setCatalog]=useState<Obj[]>([]),[selected,setSelected]=useState(''),[output,setOutput]=useState('color'),[textureName,setTextureName]=useState('');
 const [gpuRequest,setGpuRequest]=useState<{id:string;source:string;overrides:string;output:string;resolution:number;bake:boolean;setBake:boolean;previewKey?:string;studioTarget?:StudioGpuTarget}|null>(null),[gpuReady,setGpuReady]=useState(false),[gpuStatus,setGpuStatus]=useState(''),[requireGpu,setRequireGpu]=useState(true),[materialized,setMaterialized]=useState(false),[fusePointwise,setFusePointwise]=useState(false),[incremental,setIncremental]=useState(false),[gpuTextureTransfer,setGpuTextureTransfer]=useState(false),[gpuTextureMipmaps,setGpuTextureMipmaps]=useState(true),[gpuTextureFenceOnly,setGpuTextureFenceOnly]=useState(false),[incrementalOutputs,setIncrementalOutputs]=useState(false),[gpuResolution,setGpuResolution]=useState(128);
 const [queuePriority,setQueuePriority]=useState('1'),[queueTicket,setQueueTicket]=useState(''),[queueInfo,setQueueInfo]=useState<Obj|null>(null);
 const [queueDrafts,setQueueDrafts]=useState<Record<string,{source:string;overrides:string;output:string;resolution:number}>>({});
 const [residentReady,setResidentReady]=useState(false),[liveType,setLiveType]=useState('0'),[liveChoice,setLiveChoice]=useState('0'),[liveMaterial,setLiveMaterial]=useState('0');
 const [liveTarget,setLiveTarget]=useState<{typeIndex:number;choiceIndex:number;materialIndex:number;expectedVersion:string;expectedModelPublication:string;modelPath:string;materialName:string;proceduralLiveImages:number;proceduralTombstones:number;pendingImageRetirement:number;automaticCollectionEnabled:boolean;automaticRecyclingEnabled:boolean;automaticCollectionDetached:string;automaticCollectionDeferred:string}|null>(null);
 const [liveApply,setLiveApply]=useState('');
 const [preview,setPreview]=useState<{hex:string;width:number;height:number;source:string}|null>(null),[asset,setAsset]=useState<{guid:string;path:string;key:MaterialKey}|null>(null);
 const [busy,setBusy]=useState(false),[error,setError]=useState(''),[status,setStatus]=useState('');
 const [inspectorTab,setInspectorTab]=useState<'node'|'outputs'|'parameters'>('node');
 const [gpuPolling,setGpuPolling]=useState(false),[previewKind,setPreviewKind]=useState('');
 const [gpuCaps,setGpuCaps]=useState<{available:boolean;residentApply:boolean;studioResidentApply:boolean;studioPbrPreview:boolean;studioPbrEnvironment:boolean;reason:string}|null>(null);
 const [pbrShape,setPbrShape]=useState('sphere'),[pbrAsset,setPbrAsset]=useState(''),[pbrIndex,setPbrIndex]=useState('0'),[modelAssets,setModelAssets]=useState<string[]>([]),[sceneTargets,setSceneTargets]=useState<Obj[]>([]),[sceneAsset,setSceneAsset]=useState(''),[sceneMaterial,setSceneMaterial]=useState('0');
 const [pbrReady,setPbrReady]=useState(''),[pbrCapture,setPbrCapture]=useState(''),[pbrImage,setPbrImage]=useState<PreviewImage|null>(null),[previewMode,setPreviewMode]=useState<'texture'|'pbr'>('texture');
 const [hdriAssets,setHdriAssets]=useState<string[]>([]),[renderedHdri,setRenderedHdri]=useState(''),[hdri,setHdri]=useState(''),[hdriYaw,setHdriYaw]=useState(0),[hdriIntensity,setHdriIntensity]=useState(1);
 const [pbrYaw,setPbrYaw]=useState(.65),[pbrPitch,setPbrPitch]=useState(.42),[pbrZoom,setPbrZoom]=useState(1),[pbrLight,setPbrLight]=useState(1.25),[pbrLightYaw,setPbrLightYaw]=useState(.6);
 const pbrCanvas=useRef<HTMLCanvasElement>(null);
 const [autoDependencies,setAutoDependencies]=useState(false),[dependencyStatus,setDependencyStatus]=useState('');
 const linkedBaselines=useRef(new Map<string,string>());
 const [autoPreview,setAutoPreview]=useState(true),[search,setSearch]=useState(''),[showSearch,setShowSearch]=useState(false),[inspectorWidth,setInspectorWidth]=useState(300),[expanded,setExpanded]=useState(false);
 const searchInput=useRef<HTMLInputElement>(null),workspace=useRef<HTMLElement>(null);
 const [clipboard,setClipboard]=useState<Obj|null>(null),[thumbnails,setThumbnails]=useState<{key:string;images:Record<string,string>}>({key:'',images:{}});
 const history=useRef(new DraftHistory(JSON.stringify(template,null,2),64)),autoAttempt=useRef('');
 const epoch=useRef(0),lock=useRef(false),canvas=useRef<HTMLCanvasElement>(null);
 const latestDraft=useRef(text);latestDraft.current=text;
 useEffect(()=>{epoch.current++;lock.current=false;linkedBaselines.current.clear();setAutoDependencies(false);setDependencyStatus('');setBusy(false);setOriginal(null);setName('');setOverrideText('{}');setBakedSet('');setGpuRequest(null);setGpuPolling(false);setPreviewKind('');setGpuCaps(null);setPbrReady('');setPbrCapture('');setPbrImage(null);setSceneTargets([]);setSceneAsset('');setSceneMaterial('0');setModelAssets([]);setHdriAssets([]);setRenderedHdri('');setHdri('');setHdriYaw(0);setHdriIntensity(1);setImageAssets([]);setImageAssetChoice('');setPbrAsset('');setPreviewMode('texture');setQueueTicket('');setQueueInfo(null);setQueueDrafts({});setGpuReady(false);setResidentReady(false);setLiveTarget(null);setLiveApply('');setGpuStatus('');history.current=new DraftHistory(JSON.stringify(template,null,2),64);autoAttempt.current='';setClipboard(null);setSnippet([]);setSelection([]);setSelected('');setGroupTitle('');setReuseName('');setFunctionAssetName('');setFunctionAssetRecord(null);setFunctionName('');setFunctionChoice('');setFunctionOutput('');setThumbnails({key:'',images:{}});setText(history.current.current());setNames([]);setCatalog([]);setTemplates([]);setOutputTypes({});setAssets([]);setPending(null);setAsset(null);setPreview(null);setError('');setStatus('');
 return()=>{epoch.current++;lock.current=false};},[projectPath]);
 useEffect(()=>{const c=canvas.current;if(!c||!preview||preview.source!==text)return;
 const bytes=new Uint8ClampedArray(preview.hex.length/2);for(let i=0;i<bytes.length;i++)bytes[i]=parseInt(preview.hex.slice(i*2,i*2+2),16);
 c.getContext('2d')?.putImageData(new ImageData(bytes,preview.width,preview.height),0,0);
 },[preview,text,previewMode]);
 useEffect(()=>{const c=pbrCanvas.current;if(!c||!pbrImage||previewMode!=='pbr')return;const bytes=new Uint8ClampedArray(pbrImage.hex.length/2);for(let i=0;i<bytes.length;i++)bytes[i]=parseInt(pbrImage.hex.slice(i*2,i*2+2),16);c.getContext('2d')?.putImageData(new ImageData(bytes,pbrImage.width,pbrImage.height),0,0);},[pbrImage,previewMode]);
 let graph:Obj|null=null;try{const value=JSON.parse(text);if(object(value))graph=value}catch{}
 const changeText=(next:string,record=true)=>{
  if(record)history.current.push(next);
  autoAttempt.current='';
  setText(next);setPreview(null);setAsset(null);setAssets([]);setBakedSet('');setGpuReady(false);setResidentReady(false);
 };
 const edit=(value:Obj)=>changeText(JSON.stringify(value,null,2));
 const replaceDocument=(value:Obj)=>{linkedBaselines.current.clear();setDependencyStatus('');const next=JSON.stringify(value,null,2);history.current=new DraftHistory(next,64);autoAttempt.current='';changeText(next,false);setSelected('');setSelection([]);setPending(null);setClipboard(null);setSnippet([]);setFunctionChoice('');setFunctionOutput('');setFunctionAssetName('');setFunctionAssetRecord(null);};
 const undo=()=>{if(history.current.canUndo()){changeText(history.current.undo(),false);setSelected('');setPending(null);}};
 const redo=()=>{if(history.current.canRedo()){changeText(history.current.redo(),false);setSelected('');setPending(null);}};
 const semantic=graph?texturePreviewKey(graph,overrideText,output,gpuResolution,[requireGpu,materialized,fusePointwise,incremental]):'';
 const pbrKey=graph?texturePreviewKey(graph,overrideText,'pbr',0,[]):'';
 const selectedScene=sceneTargets.find(m=>m.asset===sceneAsset);
 const selectedSceneMaterial=selectedScene&&Array.isArray(selectedScene.materials)?selectedScene.materials.filter(object).find(m=>String(m.materialIndex)===sceneMaterial):null;
 const thumbnailKey=graph?texturePreviewKey(graph,overrideText,'nodes',32,[]):'';
 const openSearch=()=>{setShowSearch(true);setSearch('');};
 useEffect(()=>{if(showSearch)searchInput.current?.focus();},[showSearch]);
 useEffect(()=>{if(!active)setExpanded(false);},[active]);
 useEffect(()=>{if(!expanded)return;const fn=(e:KeyboardEvent)=>{if(e.key==='Escape')setExpanded(false);};document.addEventListener('keydown',fn);return()=>document.removeEventListener('keydown',fn);},[expanded]);
 const run=async(action:string)=>{
  if(lock.current||disabled||!projectPath)return;lock.current=true;setBusy(true);setError('');setStatus('');const token=epoch.current;
  try{
   if(action==='refreshDependencies'||action==='watchDependencies'){
    if(!graph)return;
    const captured=text,automatic=action==='watchDependencies';
    let candidate=graph,changedLinks=0,unverified=0;
    const nextBaselines=new Map(linkedBaselines.current);
    const links=textureFunctionLinks(graph);
    for(const alias of Object.keys(links).sort()){
     const link=links[alias];if(!object(link)||typeof link.asset!=='string')throw new Error('Invalid linked dependency');
     const before=textureLinkedSnapshot(candidate,alias);
     const baseline=linkedBaselines.current.get(alias);
     const r=await op('texture_graph_get',{name:link.asset});if(token!==epoch.current||captured!==latestDraft.current)return;
     if(!object(r)||r.ok!==true||!object(r.graph))throw new Error('Linked function unavailable: '+alias);
     const refreshed=refreshTextureFunctionAsset(candidate,alias,r.graph,catalog);
     const after=textureLinkedSnapshot(refreshed,alias);
     if(before===after){nextBaselines.set(alias,before);continue;}
     // Never overwrite an unverified or independently edited embedded function,
     // even on bulk manual refresh. Existing explicit per-link refresh establishes trust.
     if(baseline!==before){unverified++;continue;}
     candidate=refreshed;changedLinks++;nextBaselines.set(alias,after);
    }
    const paths=textureImageDependencyPaths(candidate);
    if(paths.length){
     const r=await op('texture_graph_dependency_revisions',{assets:paths});if(token!==epoch.current||captured!==latestDraft.current)return;
     if(!object(r)||r.ok!==true||!Array.isArray(r.revisions))throw new Error('Dependency revision scan failed');
     const verified=new Map<string,string>();
     for(const row of r.revisions){
      if(!object(row)||row.status!=='ready'||typeof row.asset!=='string'||typeof row.revision!=='string')throw new Error(object(row)?String(row.error??'Source unavailable'):'Invalid revision response');
      verified.set(row.asset,row.revision);
     }
     candidate=refreshTextureImagePins(candidate,verified);
    }
    if(candidate!==graph){
     const overrides=JSON.parse(overrideText||'{}');
     const v=await op('texture_graph_validate',{graph:candidate,overrides});
     if(token!==epoch.current||captured!==latestDraft.current)return;
     if(!object(v)||v.ok!==true)throw new Error('Dependency update rejected; previous draft retained');
     edit(candidate); // existing history + semantic preview invalidation
    }
    linkedBaselines.current=nextBaselines;
    setDependencyStatus((candidate!==graph?'Dependencies updated in draft':'Dependencies current')+' · '+paths.length+' image source(s) · '+changedLinks+' function update(s)'+(unverified?' · '+unverified+' link(s) require explicit refresh':''));
    if(!automatic)setStatus('Dependency scan complete; save/bake explicitly.');
    return;
   }
   if(action==='catalog'){
    const [c,l,t,caps]=await Promise.all([op('texture_graph_catalog'),op('texture_graph_list'),op('texture_graph_templates'),op('texture_graph_gpu_preview',{action:'capabilities'})]);
    if(token!==epoch.current)return;
    if(!object(c)||!Array.isArray(c.nodes)||!object(l)||!Array.isArray(l.graphs))throw new Error('Catalog unavailable');
    setGpuCaps({available:object(caps)&&caps.available===true,residentApply:object(caps)&&caps.residentApply===true,studioResidentApply:object(caps)&&caps.studioResidentApply===true,studioPbrPreview:object(caps)&&caps.studioPbrPreview===true,studioPbrEnvironment:object(caps)&&caps.studioPbrEnvironment===true,reason:object(caps)?String(caps.reason??''):'GPU capability unavailable'});
    setCatalog(c.nodes.filter(object));setOutputTypes(object(c.outputTypes)?c.outputTypes:{});setTemplates(object(t)&&Array.isArray(t.templates)?t.templates.filter(object):[]);setNames(l.graphs.filter((x):x is string=>typeof x==='string'));return;
   }
   if(action==='load'){
    const r=await op('texture_graph_get',{name});if(token!==epoch.current)return;
    if(!object(r)||r.ok!==true||!object(r.graph))throw new Error('Graph unavailable');
    setOriginal({name,graph:r.graph});replaceDocument(r.graph);setSelected('');setPending(null);return;
   }
   if(action==='inspectFunctionAsset'){
    if(!functionAssetName)throw new Error('Choose a saved function asset');
    const r=await op('texture_graph_get',{name:functionAssetName});if(token!==epoch.current)return;
    if(!object(r)||r.ok!==true||!object(r.graph)||!object(r.graph.functionAsset)||r.graph.functionAsset.version!==1)throw new Error('Not a standalone function asset');
    setFunctionAssetRecord({name:functionAssetName,graph:r.graph});setStatus('Loaded function asset revision '+functionAssetName);return;
   }
   if(action==='publishFunctionAsset'||action==='reviseFunctionAsset'){
    if(!graph||!functionChoice||!/^[A-Za-z0-9_-]{1,64}$/.test(functionAssetName))throw new Error('Choose a definition and valid asset name');
    const revise=action==='reviseFunctionAsset';
    if(revise&&(!functionAssetRecord||functionAssetRecord.name!==functionAssetName))throw new Error('Inspect the saved revision before updating');
    const probes=makeTextureFunctionAsset(graph,catalog,functionChoice);
    if(!probes.length)throw new Error('Function asset has no result');
    if(revise&&functionAssetRecord){
     const oldMarker=functionAssetRecord.graph.functionAsset,newMarker=probes[0].functionAsset;
     if(!object(oldMarker)||!object(newMarker)||oldMarker.definition!==newMarker.definition)throw new Error('Revision must retain the published definition name');
    }
    for(const probe of probes){const v=await op('texture_graph_validate',{graph:probe});if(token!==epoch.current)return;if(!object(v)||v.ok!==true)throw new Error('Native function validation failed');}
    const r=await op('texture_graph_set',{name:functionAssetName,graph:probes[0],...(revise&&functionAssetRecord?{expected:functionAssetRecord.graph}:{})});if(token!==epoch.current)return;
    if(!object(r)||r.ok!==true||!object(r.graph))throw new Error('Function asset publication failed');
    setFunctionAssetRecord({name:functionAssetName,graph:r.graph});setNames(old=>[...new Set([...old,functionAssetName])].sort());setStatus('Published function asset '+functionAssetName+'; linked graphs refresh explicitly.');return;
   }
   if(action==='linkFunctionAsset'||action==='refreshFunctionAsset'){
    if(!graph)throw new Error('Open a texture graph');
    const refresh=action==='refreshFunctionAsset';
    const links=textureFunctionLinks(graph),link=Object.hasOwn(links,functionChoice)?links[functionChoice]:null;
    const source=refresh&&object(link)?String(link.asset??''):functionAssetName;
    if(!source)throw new Error('Select a function asset or linked definition');
    const r=await op('texture_graph_get',{name:source});if(token!==epoch.current)return;
    if(!object(r)||r.ok!==true||!object(r.graph)||!object(r.graph.functionAsset)||typeof r.graph.functionAsset.definition!=='string')throw new Error('Linked function asset unavailable; current snapshot retained');
    for(const probe of makeTextureFunctionAsset(r.graph,catalog,r.graph.functionAsset.definition)){const v=await op('texture_graph_validate',{graph:probe});if(token!==epoch.current)return;if(!object(v)||v.ok!==true)throw new Error('Native function revision validation failed');}
    if(refresh){
     const next=refreshTextureFunctionAsset(graph,functionChoice,r.graph,catalog);
     const v=await op('texture_graph_validate',{graph:next});if(token!==epoch.current)return;
     if(!object(v)||v.ok!==true)throw new Error('Updated graph rejected; current snapshot retained');
     linkedBaselines.current.set(functionChoice,textureLinkedSnapshot(next,functionChoice));
     edit(next);setStatus('Refreshed linked definition '+functionChoice+'; existing instances retained.');
    }else{
     const next=linkTextureFunctionAsset(graph,source,r.graph,catalog);
     linkedBaselines.current.set(next.alias,textureLinkedSnapshot(next.graph,next.alias));
     edit(next.graph);setFunctionChoice(next.alias);const def=textureFunctionLibrary(next.graph)[next.alias];setFunctionOutput(object(def)&&object(def.outputs)?Object.keys(def.outputs)[0]??'':'');setStatus('Linked '+source+' using an offline-safe snapshot.');
    }
    setClipboard(null);setSnippet([]);setFunctionAssetRecord({name:source,graph:r.graph});return;
   }
   if(action==='insertSavedGraph'){
    if(!graph||!reuseName)throw new Error('Select a saved reusable graph');
    const r=await op('texture_graph_get',{name:reuseName});if(token!==epoch.current)return;
    if(!object(r)||r.ok!==true||!object(r.graph))throw new Error('Saved reusable graph unavailable');
    const instance=instantiateTextureGraph(graph,r.graph);
    edit(instance.graph);setSelection(instance.ids);setSelected(instance.ids.at(-1)??'');setPending(null);
    setStatus('Inserted independent graph '+reuseName+' · outputs: '+Object.entries(instance.outputs).map(([k,v])=>k+' → '+v).join(', ')+' · '+Object.keys(instance.parameters).length+' exposed parameters. Existing output bindings unchanged.');return;
   }
   if(action==='variant'){
    if(!bakedSet)throw new Error('Bake a set and configure its importer first');
    const r=await op('texture_graph_material_variant',{textureName:bakedSet,materialName:variantName,base:baseMaterial});if(token!==epoch.current)return;
    if(!object(r)||r.ok!==true)throw new Error('Native material variant creation failed');setStatus('Created inherited material '+variantName+'; refresh Materials.');return;
   }
   if(action==='cache'||action==='clearCache'){const r=await op('texture_graph_cache',{clear:action==='clearCache'});if(token===epoch.current)setStatus(JSON.stringify(r));return;}
   if(action==='studioModelTargets'){
    const r=await op('texture_graph_gpu_preview',{action});if(token!==epoch.current)return;
    if(!object(r)||r.ok!==true||!Array.isArray(r.models))throw new Error('Native model targets unavailable');
    const models=r.models.filter(object);setSceneTargets(models);setSceneAsset(old=>models.some(m=>m.asset===old)?old:String(models[0]?.asset??''));setSceneMaterial('0');return;
   }
   if(action==='studioHdrAssets'){
    const a=await invoke('studio.assets',{});if(token!==epoch.current)return;
    setHdriAssets(a.files.filter(f=>/\.hdr$/i.test(f.path)).map(f=>'content/'+f.path));return;
   }
   if(action==='textureImageAssets'){
    const a=await invoke('studio.assets',{});if(token!==epoch.current)return;
    setImageAssets(a.files.filter(f=>/\.(png|jpe?g|hdr|exr|dds|ktx2)$/i.test(f.path)).map(f=>'content/'+f.path));return;
   }
   if(action==='pinTextureImageAsset'){
    if(!graph||!current||(current.op!=='imageAsset'&&current.op!=='imageAssetChannel'&&current.op!=='hdrAsset')||!imageAssetChoice)throw new Error('Choose a PNG/JPEG/HDR/EXR/DDS/KTX2 project texture');
    const r=await op('texture_graph_image_asset',{asset:imageAssetChoice});if(token!==epoch.current)return;
    if(!object(r)||r.ok!==true||r.asset!==imageAssetChoice||typeof r.revision!=='string'||! /^[0-9a-f]{64}$/.test(r.revision)||typeof r.width!=='number'||typeof r.height!=='number')throw new Error('Native image asset inspection failed');
    patchNode(String(current.id),{properties:{...(object(current.properties)?current.properties:{}),asset:imageAssetChoice,revision:r.revision}});
    setStatus('Pinned '+imageAssetChoice+' · '+r.width+' × '+r.height+' · '+(typeof r.format==='string'?r.format:'original resolution'));return;
   }
   if(action==='studioModelAssets'){
    const a=await invoke('studio.assets',{});if(token!==epoch.current)return;
    setModelAssets(a.files.filter(f=>f.importer==='model'||/\.(glb|gltf|obj|fbx)$/i.test(f.path)).map(f=>'content/'+f.path));return;
   }
   if(action==='studioPbrRender'||action==='studioPbrReloadEnvironment'){
    if(!pbrReady||pbrReady!==pbrKey)throw new Error('Generate current PBR graph first');
    const r=await op('texture_graph_gpu_preview',{action:'studioPbrRender',yaw:pbrYaw,pitch:pbrPitch,zoom:pbrZoom,light:pbrLight,lightYaw:pbrLightYaw,environment:hdri,environmentYaw:hdriYaw,environmentIntensity:hdriIntensity,reloadEnvironment:action==='studioPbrReloadEnvironment'});if(token!==epoch.current)return;
    if(!object(r)||r.ok!==true||typeof r.capture!=='string'||r.state!=='pending')throw new Error('PBR render rejected');
    setPbrCapture(r.capture);return;
   }
   if(action==='studioPbrRead'){
    const r=await op('texture_graph_gpu_preview',{action});if(token!==epoch.current)return;
    if(!object(r)||r.ok!==true||r.capture!==pbrCapture)throw new Error('PBR capture replaced');
    if(r.state==='pending')return;
    setPbrCapture('');
    if(r.state!=='ready'||r.width!==256||r.height!==256||typeof r.rgbaHex!=='string'||r.rgbaHex.length!==256*256*8||!/^[0-9a-f]+$/.test(r.rgbaHex))throw new Error(String(r.reason??'Native PBR capture failed'));
    if(pbrReady===pbrKey){setPbrImage({hex:r.rgbaHex,width:256,height:256,source:pbrKey});setRenderedHdri(typeof r.environment==='string'?r.environment:'');}return;
   }
   if(action==='gpuMaterialTarget'){
     const parse=(s:string)=>{if(!/^\d{1,5}$/.test(s)||Number(s)>65535)throw new Error('Invalid resident target index');return Number(s)};
     setLiveTarget(null);
     const typeIndex=parse(liveType),choiceIndex=parse(liveChoice),materialIndex=parse(liveMaterial);
     const r=await op('texture_graph_gpu_preview',{action:'materialTarget',typeIndex,choiceIndex,materialIndex});
     if(token!==epoch.current)return;
     if(!object(r)||r.ok!==true||r.typeIndex!==typeIndex||r.choiceIndex!==choiceIndex||r.materialIndex!==materialIndex||
        typeof r.expectedVersion!=='string'||typeof r.expectedModelPublication!=='string'||
        !/^[1-9]\d{0,19}$/.test(r.expectedVersion)||!/^[1-9]\d{0,19}$/.test(r.expectedModelPublication)||
        typeof r.modelPath!=='string'||typeof r.materialName!=='string')throw new Error('Live editor appearance target unavailable');
     setLiveTarget({typeIndex,choiceIndex,materialIndex,expectedVersion:r.expectedVersion,expectedModelPublication:r.expectedModelPublication,modelPath:r.modelPath,materialName:r.materialName,proceduralLiveImages:Number(r.proceduralLiveImages??0),proceduralTombstones:Number(r.proceduralTombstones??0),pendingImageRetirement:Number(r.pendingImageRetirement??0),automaticCollectionEnabled:r.automaticCollectionEnabled===true,automaticRecyclingEnabled:r.automaticRecyclingEnabled===true,automaticCollectionDetached:String(r.automaticCollectionDetached??"0"),automaticCollectionDeferred:String(r.automaticCollectionDeferred??"0")});
     setLiveApply('');return;
   }
   if(action==='gpuMaterialApply'){
     if(!liveTarget||!residentReady||!gpuRequest||gpuRequest.source!==text||gpuRequest.overrides!==overrideText)throw new Error('Inspect the target and read a matching accepted GPU-resident set first');
     const {typeIndex,choiceIndex,materialIndex,expectedVersion,expectedModelPublication}=liveTarget;
     if(String(typeIndex)!==String(Number(liveType))||String(choiceIndex)!==String(Number(liveChoice))||String(materialIndex)!==String(Number(liveMaterial)))throw new Error('Target changed; inspect again');
     const r=await op('texture_graph_gpu_preview',{action:'applyMaterial',request:gpuRequest.id,typeIndex,choiceIndex,materialIndex,expectedVersion,expectedModelPublication});
     if(token!==epoch.current)return;
     if(!object(r)||r.ok!==true||r.request!==gpuRequest.id||r.state!=='queued')throw new Error('Native application not queued');
     setLiveApply('Queued for editor frame boundary');setLiveTarget(null);setResidentReady(false);setGpuReady(false);return;
   }
   if(action==='gpuMaterialStatus'){
     const r=await op('texture_graph_gpu_preview',{action:'materialApplyStatus'});if(token!==epoch.current)return;
     if(!object(r)||r.ok!==true||!gpuRequest||r.request!==gpuRequest.id)throw new Error('No matching material application');
     setLiveApply(String(r.state)+' — '+String(r.reason??''));return;
   }
   if(action==='gpuCancel'){
    if(!gpuRequest)throw new Error('No GPU request to cancel');
    const r=await op('texture_graph_gpu_preview',{action:'cancel',request:gpuRequest.id});if(token!==epoch.current)return;
    if(!object(r)||r.ok!==true||r.request!==gpuRequest.id||r.state!=='cancelled')throw new Error('GPU cancellation rejected');
    setAutoPreview(false);setGpuPolling(r.busy===true||r.draining===true);if(r.busy!==true&&r.draining!==true)setGpuRequest(null);setGpuReady(false);setResidentReady(false);setPreview(null);setGpuStatus('cancelled'+(r.draining===true?' · waiting for GPU retirement':'')+' — '+String(r.reason??''));return;
   }
   if(action==='queueRead'||action==='queueCancel'||action==='queueRelease'){
     const r=await op('texture_graph_gpu_preview',{action,ticket:queueTicket,...(action==='queueRelease'?{discardResult:true}:{})});if(token!==epoch.current)return;
     if(!object(r)||r.ok!==true)throw new Error('GPU queue operation failed');setQueueInfo(r);
     if(action==='queueRead'&&typeof r.activeTicket==='string'&&typeof r.activeRequest==='string'&&r.activeRequest){
       const draft=queueDrafts[r.activeTicket];if(draft){setGpuRequest({id:r.activeRequest,...draft,bake:true,setBake:true});setGpuReady(false);setResidentReady(false);setQueueTicket(r.activeTicket);}
     }
     if(action==='queueRelease'&&r.activeTicket!==queueTicket){setQueueDrafts(old=>{const next={...old};delete next[queueTicket];return next});setQueueTicket('');setGpuRequest(null);setGpuReady(false);setResidentReady(false);}
     if(action==='queueCancel'&&r.activeTicket!==queueTicket){setQueueDrafts(old=>{const next={...old};delete next[queueTicket];return next});setQueueTicket('');}
     return;
   }
   if(action==='gpuRead'){
     if(!gpuRequest)throw new Error('No GPU request to read');
     const r=await op('texture_graph_gpu_preview',{action:'read'});if(token!==epoch.current)return;
     if(!object(r)||r.ok!==true||r.request!==gpuRequest.id){setGpuReady(false);throw new Error('GPU request replaced or unavailable');}
     setGpuPolling(r.busy===true||r.state==='queued'||r.state==='pending');
     if(r.state==='cancelled'&&r.busy!==true&&r.draining!==true)setGpuRequest(null);
     const matches=gpuRequest.bake?gpuRequest.source===text&&gpuRequest.overrides===overrideText&&(gpuRequest.setBake||gpuRequest.output===output):gpuRequest.previewKey===semantic;
     setGpuStatus(String(r.state)+(r.draining===true?' · waiting for GPU retirement':'')+' / '+String(r.backend)+' · '+String(r.completedOutputs??0)+' / '+String(r.totalOutputs??0)+' maps · '+String(r.receivedBytes??0)+' / '+String(r.totalBytes??0)+' bytes · passes '+String(r.encodedPasses??0)+' / '+String(r.plannedPasses??0)+' · slot bytes '+String(r.plannedSlotBytes??0)+' · cached plans '+String(r.idlePipelinePlans??0)+(r.pipelineCacheHit===true?' · pipeline cache hit':'')+' · shared scratch hits '+String(r.sharedScratchHits??0)+(r.materializedRequested===true?' · materialized nodes '+String(r.materializedNodePasses??0)+(r.incrementalRequested===true?' · reused passes '+String(r.incrementalReusedPasses??r.incrementalReusedNodes??0):'')+(r.fusionRequested===true?' · fused '+String(r.fusionElidedPasses??0)+' passes':'')+' / packs '+String(r.materializedPackPasses??0)+(r.materializedFailed===true?' · fallback':'')+' · retained '+String(r.materializedIdleBytes??0)+' bytes · pipeline hits '+String(r.materializedPipelineCacheHits??0)+(r.materializedBufferCacheHit===true?' · buffer cache hit':''):'')+(r.gpuTextureTransferRequested===true?' · GPU textures '+String(r.gpuTextureAccepted??0)+(r.gpuTextureSetReady===true?' ready':'')+(r.gpuTextureMipmaps===true?' · mips':''):'')+(r.incrementalOutputsRequested===true?' · retained outputs '+(Array.isArray(r.incrementalReusedOutputs)?r.incrementalReusedOutputs.join(', '):''):'')+' — '+String(r.reason??'')+(matches?'':' · draft changed'));
     setGpuReady(matches&&r.bake===true&&r.fileExportAvailable!==false&&r.state==='ready'&&r.published!==true&&r.busy!==true);
     setPreviewKind(String(r.backend??'unknown'));
     setResidentReady(matches&&r.gpuTextureSetReady===true&&r.busy!==true&&r.published!==true);
     if(matches&&gpuRequest.studioTarget&&r.gpuTextureSetReady===true&&r.busy!==true&&r.published!==true){
      const {kind,...stamp}=gpuRequest.studioTarget;
      const applied=await op('texture_graph_gpu_preview',{...stamp,request:gpuRequest.id,action:kind==='pbr'?'studioPbrApply':'studioModelApply'});if(token!==epoch.current)return;
      if(!object(applied)||applied.ok!==true||applied.state!=='applied')throw new Error('GPU material publication rejected');
      setGpuRequest(null);setGpuReady(false);setResidentReady(false);setGpuPolling(false);
      if(kind==='pbr'){
       setPbrReady(pbrKey);setPreviewMode('pbr');
       const capture=await op('texture_graph_gpu_preview',{action:'studioPbrRender',yaw:pbrYaw,pitch:pbrPitch,zoom:pbrZoom,light:pbrLight,lightYaw:pbrLightYaw,environment:hdri,environmentYaw:hdriYaw,environmentIntensity:hdriIntensity});if(token!==epoch.current)return;
       if(!object(capture)||capture.ok!==true||typeof capture.capture!=='string'||capture.state!=='pending')throw new Error('PBR render rejected');
       setPbrCapture(capture.capture);setStatus('GPU PBR set applied to preview model');
      }else{
       setSceneTargets(old=>old.map(m=>m.asset===stamp.asset&&Array.isArray(m.materials)?{...m,materials:m.materials.map(v=>object(v)?{...v,expectedVersion:applied.expectedVersion}:v)}:m));setStatus('GPU textures applied to resident model (session only, no PNG)');
      }
      return;
     }
     if(matches&&r.state==='ready'&&r.bake!==true){
       if(typeof r.width!=='number'||typeof r.height!=='number'||!Number.isInteger(r.width)||!Number.isInteger(r.height)||r.width<8||r.height<8||r.width>256||r.height>256||typeof r.rgbaHex!=='string'||r.rgbaHex.length!==r.width*r.height*8||!/^[0-9a-f]+$/.test(r.rgbaHex))throw new Error('Invalid GPU preview payload');
       setPreview({hex:r.rgbaHex,width:r.width,height:r.height,source:text});
     }else if(!matches)setPreview(null);
     return;
    }
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
   if(action==='queueEnqueue'){
     setAutoPreview(false);
     const r=await op('texture_graph_gpu_preview',{action:'enqueue',graph,overrides,priority:Number(queuePriority),bakeSet:true,requireGpu:true,materialized,fusePointwise:materialized&&fusePointwise,incremental:materialized&&incremental,gpuTextureTransfer:materialized&&gpuTextureTransfer,gpuTextureFenceOnly:materialized&&gpuTextureTransfer&&gpuTextureFenceOnly,gpuTextureMipmaps:materialized&&gpuTextureTransfer&&gpuTextureMipmaps});if(token!==epoch.current)return;
     if(!object(r)||r.ok!==true||typeof r.ticket!=='string')throw new Error('GPU enqueue rejected');const ticket=r.ticket;
     setQueueDrafts(old=>({...old,[ticket]:{source:text,overrides:overrideText,output,resolution:gpuResolution}}));setQueueTicket(ticket);setStatus('Waiting GPU ticket '+ticket);return;
   }
   if(action==='gpuPlan'){
     const r=await op('texture_graph_gpu_preview',{action:'plan',fusePointwise,incremental:materialized&&incremental,graph,overrides,bakeSet:true,resolution:gpuResolution,gpuTextureTransfer:materialized&&gpuTextureTransfer,gpuTextureFenceOnly:materialized&&gpuTextureTransfer&&gpuTextureFenceOnly,gpuTextureMipmaps:materialized&&gpuTextureTransfer&&gpuTextureMipmaps});if(token!==epoch.current)return;
     if(!object(r)||r.ok!==true||r.state!=='planned'||r.executed!==false||!Array.isArray(r.passes)||!Array.isArray(r.slotBytes))throw new Error('Materialized DAG plan rejected');
     setStatus('Plan only · '+String(r.resolution)+' px · '+r.passes.length+' passes · '+r.slotBytes.length+' slots · '+String(r.allocatedBytes)+' / '+String(r.budgetBytes)+' logical bytes including result buffer · '+String(r.nodeShaderCount)+' node / '+String(r.packShaderCount)+' pack shaders · '+String(r.fusionElidedPasses??0)+' planned passes elided; no GPU dispatch');return;
    }
    if(action==='gpuPublish'){
    if(!gpuRequest?.bake||!gpuReady||gpuRequest.source!==text||gpuRequest.overrides!==overrideText||(!gpuRequest.setBake&&gpuRequest.output!==output))throw new Error('No matching completed bake');
    const r=await op('texture_graph_gpu_preview',{action:'publish',request:gpuRequest.id,textureName});if(token!==epoch.current)return;
    if(!object(r)||r.ok!==true)throw new Error('Native GPU bake publication failed');
    if(gpuRequest.setBake){
      if(!Array.isArray(r.files))throw new Error('Invalid set publication response');
      setGpuReady(false);setResidentReady(false);setGpuRequest(null);setAssets([]);setAsset(null);setBakedSet('');
      setStatus('Published '+r.files.length+' maps via '+String(r.backend));
      const index=await invoke('studio.assets',{});if(token!==epoch.current)return;
      const mapped:{guid:string;path:string;key:MaterialKey}[]=[];let configured=0;
      for(const row of r.files.filter(object)){
        const a=index.files.find(a=>'content/'+a.path===row.path&&a.importer==='texture'&&a.guid);
        if(!a?.guid||typeof row.sRGB!=='boolean'||typeof row.output!=='string')continue;
        const patched=await invoke('studio.assetMetaPatch',{guid:a.guid,key:'sRGB',value:row.sRGB});if(token!==epoch.current)return;
        if(patched.guid!==a.guid||patched.settings.sRGB!==row.sRGB)throw new Error('Importer color-space verification failed for '+a.path);
        configured++;const key=channels[row.output];if(key)mapped.push({guid:a.guid,path:a.path,key});
      }
      setAssets(mapped);if(configured===r.files.length)setBakedSet(textureName);
      setStatus('Published '+r.files.length+' maps via '+String(r.backend)+'; configured '+configured+' importers.');return;
    }
    if(typeof r.path!=='string'||r.output!==output)throw new Error('Invalid single-output publication response');
    setGpuReady(false);setResidentReady(false);setGpuRequest(null);setStatus('Published '+r.path+' via '+String(r.backend));setAsset(null);
    const index=await invoke('studio.assets',{});if(token!==epoch.current)return;
    const a=index.files.find(a=>'content/'+a.path===r.path&&a.importer==='texture'&&a.guid);
    if(a?.guid){const srgb=output==='color'||output==='emissive';const patched=await invoke('studio.assetMetaPatch',{guid:a.guid,key:'sRGB',value:srgb});if(token!==epoch.current)return;
      if(patched.guid!==a.guid||patched.settings.sRGB!==srgb)throw new Error('Importer color-space verification failed');const key=channels[output];if(key)setAsset({guid:a.guid,path:a.path,key});}
    return;
   }
   if(action==='studioPbrGenerate'||action==='studioModelGenerate'){
    if(gpuPolling||residentReady||gpuRequest?.bake||queueTicket||Object.keys(queueDrafts).length)throw new Error('Finish, publish or cancel current GPU bake / queue first');
    setAutoPreview(false);
    let target:StudioGpuTarget;
    if(action==='studioPbrGenerate'){
      setPbrReady('');setPbrCapture('');setPbrImage(null);setPreviewMode('pbr');
      const prepared=await op('texture_graph_gpu_preview',{action:'studioPbrPrepare',shape:pbrShape,asset:pbrAsset,materialIndex:pbrShape==='model'?Number(pbrIndex):0});if(token!==epoch.current)return;
      target=targetStamp(prepared,'pbr');
    }else target=targetStamp(selectedSceneMaterial,'scene',sceneAsset);
    const r=await op('texture_graph_gpu_preview',{action:'start',graph,overrides,requireGpu:true,materialized:true,fusePointwise:true,bakeSet:true,gpuTextureTransfer:true,gpuTextureMipmaps:true});if(token!==epoch.current)return;
    if(!object(r)||r.ok!==true||typeof r.request!=='string')throw new Error('GPU resident generation rejected');
    setGpuRequest({id:r.request,source:text,overrides:overrideText,output,resolution:gpuResolution,bake:true,setBake:true,studioTarget:target});setGpuPolling(true);setGpuReady(false);setResidentReady(false);
    setGpuStatus('Generating GPU PBR maps · '+String(r.state));return;
   }
   if(action==='gpuStart'||action==='gpuBake'||action==='gpuBakeSet'){
    if(action==='gpuStart'){autoAttempt.current=semantic;}else setAutoPreview(false);
    const reuse=action==='gpuBakeSet'&&materialized&&incremental&&gpuTextureTransfer&&gpuTextureFenceOnly&&incrementalOutputs;
    if(reuse&&!liveTarget)throw new Error('Inspect live material before incremental output generation');
    const r=await op('texture_graph_gpu_preview',{action:'start',graph,overrides,output,requireGpu,materialized,...(reuse&&liveTarget?{incrementalOutputs:true,typeIndex:liveTarget.typeIndex,choiceIndex:liveTarget.choiceIndex,materialIndex:liveTarget.materialIndex,expectedVersion:liveTarget.expectedVersion,expectedModelPublication:liveTarget.expectedModelPublication}:{}),fusePointwise:materialized&&fusePointwise,incremental:materialized&&incremental,gpuTextureTransfer:action==='gpuBakeSet'&&materialized&&gpuTextureTransfer,gpuTextureFenceOnly:action==='gpuBakeSet'&&materialized&&gpuTextureTransfer&&gpuTextureFenceOnly,gpuTextureMipmaps:action==='gpuBakeSet'&&materialized&&gpuTextureTransfer&&gpuTextureMipmaps,resolution:gpuResolution,bake:action==='gpuBake'||action==='gpuBakeSet',bakeSet:action==='gpuBakeSet'});if(token!==epoch.current)return;
    if(!object(r)||r.ok!==true||typeof r.request!=='string')throw new Error(object(r)?String(r.reason??'GPU owner unavailable'):'GPU owner unavailable');
    setGpuPolling(r.busy===true||r.state==='queued'||r.state==='pending');setPreviewKind(String(r.backend??'unknown'));
    {setGpuRequest({id:r.request,source:text,overrides:overrideText,output,resolution:gpuResolution,bake:action==='gpuBake'||action==='gpuBakeSet',setBake:action==='gpuBakeSet',previewKey:action==='gpuStart'?semantic:undefined});setPreview(null);setGpuReady(false);setResidentReady(false);setLiveApply('');}
    
    setGpuStatus(String(r.state)+(r.draining===true?' · waiting for GPU retirement':'')+' / '+String(r.backend)+(r.setBake===true?' · '+String(r.completedOutputs)+' / '+String(r.totalOutputs)+' maps':'')+' · '+String(r.receivedBytes??0)+' / '+String(r.totalBytes??0)+' bytes · '+String(r.readbackBlocks??0)+' blocks · encoded passes '+String(r.encodedPasses??0)+(r.reusedPlan===true?' · reused plan':'')+(r.reusedBuffers===true?' · reused buffers':'')+(r.reusedScratch===true?' · shared scalar scratch':'')+' — '+String(r.reason??''));
    setGpuReady(r.bake===true&&r.fileExportAvailable!==false&&r.state==='ready'&&r.published!==true&&r.busy!==true);
    if(r.state==='ready'&&r.bake!==true){
      if(typeof r.width!=='number'||typeof r.height!=='number'||!Number.isInteger(r.width)||!Number.isInteger(r.height)||r.width<8||r.height<8||r.width>256||r.height>256||typeof r.rgbaHex!=='string'||r.rgbaHex.length!==r.width*r.height*8||!/^[0-9a-f]+$/.test(r.rgbaHex))throw new Error('Invalid GPU preview payload');
      setPreview({hex:r.rgbaHex,width:r.width,height:r.height,source:text});
    }return;
   }
   if(action==='nodeThumbnails'){
    const images:Record<string,string>=Object.create(null) as Record<string,string>;
    const nodes=Array.isArray(graph.nodes)?graph.nodes.filter(object):[];
    setStatus('Generating CPU node thumbnails…');
    for(const n of nodes){
      if(token!==epoch.current)return;
      if(typeof n.id!=='string')continue;
      const r=await op('texture_graph_preview',{graph,overrides,node:n.id});if(token!==epoch.current)return;
      if(!object(r)||r.ok!==true||typeof r.width!=='number'||typeof r.height!=='number'||!Number.isInteger(r.width)||!Number.isInteger(r.height)||r.width<8||r.height<8||r.width>32||r.height>32||typeof r.rgbaHex!=='string'||r.rgbaHex.length!==r.width*r.height*8||!/^[0-9a-f]+$/.test(r.rgbaHex))throw new Error('Invalid native thumbnail for '+n.id);
      const c=document.createElement('canvas');c.width=r.width;c.height=r.height;
      const bytes=new Uint8ClampedArray(r.rgbaHex.length/2);for(let i=0;i<bytes.length;i++)bytes[i]=parseInt(r.rgbaHex.slice(i*2,i*2+2),16);
      const ctx=c.getContext('2d');if(!ctx)throw new Error('Canvas unavailable');
      ctx.putImageData(new ImageData(bytes,r.width,r.height),0,0);images[n.id]=c.toDataURL('image/png');
    }
    setThumbnails({key:thumbnailKey,images});setStatus('Generated '+Object.keys(images).length+' CPU node thumbnails');return;
   }
   if(action==='previewNode'||action==='previewOutput'){
    setAutoPreview(false);setPreview(null);setPreviewKind('CPU');
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
    if(output==='radiance'){setStatus('Exported linear HDR EXR '+r.path);return;}
    const key=channels[output];
    const assets=await invoke('studio.assets',{});if(token!==epoch.current)return;
    const a=assets.files.find(a=>'content/'+a.path===r.path&&a.importer==='texture'&&a.guid);
    if(a?.guid){const srgb=output==='color'||output==='emissive';const patched=await invoke('studio.assetMetaPatch',{guid:a.guid,key:'sRGB',value:srgb});if(token!==epoch.current)return;if(patched.guid!==a.guid||patched.settings.sRGB!==srgb)throw new Error('Importer color-space verification failed');if(key)setAsset({guid:a.guid,path:a.path,key});}else setStatus('Baked '+r.path+'; refresh Assets and assign with asset picker.');
   }
  }catch(e){if(epoch.current===token){if(action==='watchDependencies'||action==='refreshDependencies')setDependencyStatus(e instanceof Error?e.message:String(e));if(action==='gpuRead')setGpuPolling(false);if(action==='studioPbrRead')setPbrCapture('');setError(e instanceof Error?e.message:String(e))}}
  finally{if(epoch.current===token){lock.current=false;setBusy(false)}}
 };
 // Refresh native metadata when entering a project, not on every graph edit.
 useEffect(()=>{if(projectPath&&!disabled)void run('catalog');},[projectPath,disabled]);
 // One bounded poll at a time; the same native read path validates request identity.
 useEffect(()=>{
  if(!gpuPolling||busy||disabled||!gpuRequest||!projectPath)return;
  const timer=setTimeout(()=>void run('gpuRead'),350);
  return()=>clearTimeout(timer);
 },[gpuPolling,busy,disabled,gpuRequest,text,overrideText,output,gpuResolution,projectPath]);
 useEffect(()=>{if(!pbrCapture||busy||disabled||!projectPath)return;const timer=setTimeout(()=>void run('studioPbrRead'),250);return()=>clearTimeout(timer);},[pbrCapture,busy,disabled,pbrReady,pbrKey,projectPath]);
 // Opt-in bounded polling uses the same serialized native action lock.
 // Re-arm on completion; do not poll while a bake, queue or PBR capture is active.
 useEffect(()=>{
  if(!autoDependencies||!active||disabled||busy||gpuPolling||gpuRequest?.bake||queueTicket||Object.keys(queueDrafts).length||pbrCapture||!projectPath||!graph||!catalog.length)return;
  const timer=setTimeout(()=>void run('watchDependencies'),5000);
  return()=>clearTimeout(timer);
 },[autoDependencies,active,disabled,busy,gpuPolling,gpuRequest?.bake,queueTicket,queueDrafts,pbrCapture,projectPath,text,overrideText,catalog]);
 // Auto-preview only in the visible workspace, never replaces a bake or queue.
 useEffect(()=>{
  if(!active||previewMode!=='texture'||!autoPreview||!gpuCaps?.available||busy||disabled||gpuPolling||gpuRequest?.bake||queueTicket||Object.keys(queueDrafts).length||!graph||!catalog.length||!object(graph.outputs)||!graph.outputs[output]||!semantic||autoAttempt.current===semantic)return;
  if(validateTopology(projection(graph,catalog),{cycles:'reject'}).length)return;
  const timer=setTimeout(()=>{autoAttempt.current=semantic;void run('gpuStart');},500);
  return()=>clearTimeout(timer);
 },[active,previewMode,autoPreview,gpuCaps?.available,busy,disabled,gpuPolling,gpuRequest?.bake,queueTicket,queueDrafts,semantic,catalog,projectPath]);
 const projected=graph?projection(graph,catalog):{nodes:[],links:[]};
 const rawView={...projected,nodes:projected.nodes.map(n=>({...n,thumbnail:thumbnails.key===thumbnailKey&&Object.hasOwn(thumbnails.images,n.id)?thumbnails.images[n.id]:undefined}))};
 const arranged=textureLayout(rawView);
 const view={...rawView,nodes:rawView.nodes.map(n=>{const row=Array.isArray(graph?.nodes)?graph.nodes.find(v=>object(v)&&v.id===n.id):null;const pos=object(row)?row.position:null;const xy=arranged.get(n.id);return (Array.isArray(pos)&&pos.length===2&&pos.every(v=>typeof v==='number'&&Number.isFinite(v)))||!xy?n:{...n,x:xy[0],y:xy[1]};})};
 const moveNodes=(positions:readonly {id:string;x:number;y:number}[])=>{
  if(!graph||!Array.isArray(graph.nodes))return;
  const byId=new Map(positions.map(p=>[p.id,p]));
  const next={...graph,nodes:graph.nodes.map(n=>{if(!object(n)||typeof n.id!=='string')return n;const p=byId.get(n.id);const current=view.nodes.find(v=>v.id===n.id);return p?{...n,position:[Math.max(0,Math.min(2000,p.x)),Math.max(0,Math.min(2000,p.y))]}:current&&!Array.isArray(n.position)?{...n,position:[current.x,current.y]}:n;})};
  const nextText=JSON.stringify(next,null,2);history.current.push(nextText);setText(nextText);
  setPreview(old=>old?.source===text?{...old,source:nextText}:old);
  // A bake still requires its exact source document; layout cannot publish stale assets.
  setGpuReady(false);setResidentReady(false);
 };
 const groups=graph&&Array.isArray(graph.editorGroups)?graph.editorGroups.filter(object).filter(g=>typeof g.id==='string'&&typeof g.title==='string'&&Array.isArray(g.members)).map(g=>({id:String(g.id),title:String(g.title),members:(g.members as unknown[]).filter((v):v is string=>typeof v==='string')})):[];
 const selectedIds=selection.filter(id=>view.nodes.some(n=>n.id===id));
 const groupSelection=()=>{if(!graph||!selectedIds.length||!groupTitle.trim())return;let i=1;while(groups.some(g=>g.id==='group_'+i))i++;edit({...graph,editorGroups:[...groups,{id:'group_'+i,title:groupTitle.trim().slice(0,80),members:selectedIds}]});setGroupTitle('');};
 const autoLayout=()=>moveNodes([...arranged].map(([id,[x,y]])=>({id,x,y})));
 const outputs=graph&&object(graph.outputs)?Object.keys(graph.outputs):[];
 const rows=graph&&Array.isArray(graph.nodes)?graph.nodes.filter(object):[];
 const copySubgraph=()=>{const ids=new Set(selectedIds);setSnippet(rows.filter(n=>ids.has(String(n.id))).map(n=>JSON.parse(JSON.stringify(n)) as Obj));setStatus('Copied subgraph; external inputs will need reconnecting');};
 const pasteSubgraph=()=>{if(!graph||!snippet.length||rows.length+snippet.length>64)return;const used=new Set(rows.map(n=>String(n.id))),ids=new Map<string,string>();for(const n of snippet){let i=1;while(used.has('sub_'+i))i++;const id='sub_'+i;used.add(id);ids.set(String(n.id),id);}const nodes:Obj[]=snippet.map(n=>({...n,id:ids.get(String(n.id)),inputs:Array.isArray(n.inputs)?n.inputs.map(v=>ids.get(String(v))??''):[],position:undefined}));const next={...graph,nodes:[...rows,...nodes]};try{if(next.nodes.some(n=>n.op==='function'))checkFunctionDraft(next);}catch(e){setError(e instanceof Error?e.message:String(e));return;}edit(next);setSelection([...ids.values()]);setSelected([...ids.values()].at(-1)??'');};
 const current=rows.find(n=>n.id===selected);
 const spec=graph&&current?textureFunctionSpec(graph,current,catalog):undefined;
 const functionLibrary=graph?textureFunctionLibrary(graph):{};
 const functionLinks=graph?textureFunctionLinks(graph):{};
 const linkedChoice=Object.hasOwn(functionLinks,functionChoice);
 const chosenFunction=Object.hasOwn(functionLibrary,functionChoice)?functionLibrary[functionChoice]:null;
 const functionOutputs=object(chosenFunction)&&object(chosenFunction.outputs)?Object.keys(chosenFunction.outputs):[];
 const currentFunctionParameters=current?.op==='function'&&object(spec?.parameters)?Object.entries(spec.parameters):[];
 const functionEdit=(action:()=>void)=>{try{setError('');action();}catch(e){setError(e instanceof Error?e.message:String(e));}};
 const captureFunction=()=>functionEdit(()=>{if(!graph)return;edit(defineTextureFunction(graph,catalog,selectedIds.length?selectedIds:[selected],selected,functionName));setFunctionChoice(functionName);setFunctionOutput('value');setFunctionName('');setStatus('Function definition created; original nodes preserved.');});
 const insertFunction=()=>functionEdit(()=>{if(!graph)return;const call=addTextureFunctionCall(graph,catalog,functionChoice,functionOutput);edit(call.graph);setSelected(call.id);setSelection([call.id]);setPending(null);setStatus('Function instance added. Connect its inputs.');});
 const deleteFunction=()=>functionEdit(()=>{if(!graph)return;edit(removeTextureFunction(graph,functionChoice));setClipboard(null);setSnippet([]);setFunctionChoice('');setFunctionOutput('');});
 const setFunctionArgument=(key:string,raw:string)=>functionEdit(()=>{if(!current||typeof current.id!=='string'||!object(current.properties))return;const value=JSON.parse(raw);if(value===null)throw new Error('Function parameter cannot be null');patchNode(current.id,{properties:{...current.properties,arguments:{...(object(current.properties.arguments)?current.properties.arguments:{}),[key]:value}}});});
 const patchNode=(id:string,patch:Obj)=>{if(graph)edit({...graph,nodes:rows.map(n=>n.id===id?{...n,...patch}:n)});};
 const connect=(from:string,to:string,slot:number)=>{
  if(!graph)return;
  const source=view.nodes.find(n=>n.id===from),target=view.nodes.find(n=>n.id===to);
  if(!target||!target.inputs[slot]||from&&(!source||source.outputs[0]?.type!==target.inputs[slot].type)){setError('Incompatible texture ports');return;}
  const next={...graph,nodes:rows.map(n=>n.id===to&&Array.isArray(n.inputs)?{...n,inputs:n.inputs.map((v,i)=>i===slot?from:v)}:n)};
  if(validateTopology(projection(next,catalog),{cycles:'reject'}).some(x=>x.code==='cycle')){setError('Texture graph cannot contain cycles');return;}
  edit(next);setPending(null);setError('');
 };
 const addNode=(kind=newOp)=>{
  if(!graph||rows.length>=64)return;const t=catalog.find(n=>n.id===kind);if(!t)return;
  let i=1;while(rows.some(n=>n.id===kind+'_'+i))i++;
  const id=kind+'_'+i,props:Obj={};
  if(object(t.properties))for(const [key,value] of Object.entries(t.properties))if(object(value))props[key]=value.default;
  edit({...graph,nodes:[...rows,{id,op:kind,inputs:Array.isArray(t.inputs)?t.inputs.map(()=> ''):[],properties:props}]});setSelected(id);setPending(null);
 };
 const duplicate=()=>{if(!graph)return;const source=view.nodes.find(n=>n.id===selected);const copy=duplicateTextureNode(source?{...graph,nodes:rows.map(n=>n.id===selected?{...n,position:[source.x,source.y]}:n)}:graph,selected);if(copy){try{if(rows.some(n=>n.op==='function'))checkFunctionDraft(copy.graph);}catch(e){setError(e instanceof Error?e.message:String(e));return;}edit(copy.graph);setSelected(copy.id);setSelection([copy.id]);setPending(null);}};
 const copyNode=()=>{if(!current)return;setClipboard(JSON.parse(JSON.stringify(current)) as Obj);setStatus('Copied node '+selected);};
 const pasteNode=()=>{if(!graph||!clipboard||typeof clipboard.op!=='string'||rows.length>=64)return;
  let i=1;while(rows.some(n=>n.id===clipboard.op+'_'+i))i++;
  const id=clipboard.op+'_'+i,copy=JSON.parse(JSON.stringify(clipboard)) as Obj;
  const inputs=Array.isArray(copy.inputs)?copy.inputs.map(v=>rows.some(n=>n.id===v)?v:''):[];
  const next={...graph,nodes:[...rows,{...copy,id,inputs,position:undefined} as Obj]};try{if(next.nodes.some(n=>n.op==='function'))checkFunctionDraft(next);}catch(e){setError(e instanceof Error?e.message:String(e));return;}edit(next);setSelected(id);setSelection([id]);setPending(null);
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

 return <section ref={workspace} className={"tg-workspace"+(expanded?" tg-expanded":"")} aria-label="Procedural Texture Graph">
  <div className="tg-toolbar">
   <div className="tg-title"><strong>Texture Graph</strong><small>Native PBR generator</small></div><StudioButton size="sm" onClick={()=>setExpanded(v=>!v)}>{expanded?"Restore":"Expand"}</StudioButton>
     <StudioSelect aria-label="Saved texture graph" value={names.includes(name)?name:''} disabled={busy||disabled} onChange={e=>{setName(e.target.value);setOriginal(null)}}><option value="">Select saved graph</option>{names.map(n=><option key={n}>{n}</option>)}</StudioSelect>
  <StudioInput aria-label="Graph name" placeholder="Graph name" value={name} disabled={busy||disabled} onChange={e=>setName(e.target.value)}/>
  <StudioSelect aria-label="Bake resolution" value={String(graph?.size??256)} disabled={!graph||busy||disabled} onChange={e=>graph&&edit({...graph,size:Number(e.target.value)})}>{[64,128,256,512,1024].map(n=><option key={n} value={n}>Bake {n} × {n}</option>)}</StudioSelect>
  <div className="au-row" style={{flexWrap:'wrap'}}>{['load','save','validate'].map(action=><StudioButton key={action} size="sm" disabled={busy||disabled||(action!=='validate'&&!name)} onClick={()=>void run(action)}>{action}</StudioButton>)}<StudioButton size="sm" disabled={busy||disabled} onClick={()=>{replaceDocument(template);setOriginal(null);setName('')}}>New PBR template</StudioButton></div>
     {templates.length>0&&<StudioSelect aria-label="Native PBR template" value="" disabled={busy||disabled} onChange={e=>{const t=templates.find(t=>t.name===e.target.value);if(t&&object(t.graph)){replaceDocument(t.graph);setOriginal(null);setName('');setSelected('');setPending(null)}}}><option value="">Load native PBR template</option>{templates.map(t=><option key={String(t.name)}>{String(t.name)}</option>)}</StudioSelect>}
     <StudioButton size="sm" disabled={disabled||busy} onClick={()=>void run('catalog')}>Refresh graph / node catalog</StudioButton>
  </div>
  <div className="tg-body" style={{"--tg-inspector-width":inspectorWidth+"px"} as CSSProperties}>

   <div className="tg-main">
    <div className="tg-node-toolbar">
     <div className="tg-edit-actions"><StudioButton size="sm" disabled={busy||disabled||!history.current.canUndo()} onClick={undo}>Undo</StudioButton><StudioButton size="sm" disabled={busy||disabled||!history.current.canRedo()} onClick={redo}>Redo</StudioButton><StudioButton size="sm" disabled={busy||disabled||!selected||rows.length>=64} onClick={duplicate}>Duplicate</StudioButton><StudioButton size="sm" disabled={busy||disabled||!selected} onClick={copyNode}>Copy</StudioButton><StudioButton size="sm" disabled={busy||disabled||!clipboard||rows.length>=64} onClick={pasteNode}>Paste</StudioButton><StudioButton size="sm" disabled={busy||disabled||!rows.length} onClick={autoLayout}>Auto layout</StudioButton><StudioButton size="sm" disabled={busy||disabled||rows.length>=64} onClick={openSearch}>+ Search node</StudioButton><StudioButton size="sm" disabled={busy||disabled||gpuPolling||!rows.length} onClick={()=>void run("nodeThumbnails")}>CPU thumbnails</StudioButton>{thumbnails.key===thumbnailKey&&Object.keys(thumbnails.images).length>0&&<StudioButton size="sm" onClick={()=>setThumbnails({key:"",images:{}})}>Hide thumbnails</StudioButton>}</div>
       {graph&&catalog.length>0&&<div className="au-row"><StudioSelect aria-label="Node type" value={newOp} disabled={busy||disabled} onChange={e=>setNewOp(e.target.value)}>{catalog.map(n=><option key={String(n.id)}>{String(n.id)}</option>)}</StudioSelect><StudioButton size="sm" disabled={busy||disabled||rows.length>=64} onClick={()=>addNode()}>Add node</StudioButton><StudioButton size="sm" disabled={busy||disabled||!selected} onClick={removeNode}>Remove node</StudioButton></div>}
     <small>{rows.length} nodes · {outputs.length} outputs</small>
     {showSearch&&<div className="tg-search"><input ref={searchInput} aria-label="Search node catalog" placeholder="Search nodes…" value={search} onChange={e=>setSearch(e.target.value)} onKeyDown={e=>{if(e.key==='Escape')setShowSearch(false);}}/><StudioButton size="sm" onClick={()=>setShowSearch(false)}>Close</StudioButton><div className="tg-search-results">{catalog.filter(n=>[n.id,n.output,n.description].some(v=>String(v??'').toLowerCase().includes(search.toLowerCase()))).map(n=><StudioButton key={String(n.id)} size="sm" disabled={busy||disabled||rows.length>=64} onClick={()=>{addNode(String(n.id));setShowSearch(false);}}>{String(n.id)} · {String(n.output??'')}</StudioButton>)}</div></div>}
    </div>
      <div className="au-row" style={{flexWrap:'wrap'}}><StudioInput aria-label="Group or comment title" placeholder="Group / comment" value={groupTitle} onChange={e=>setGroupTitle(e.target.value)}/><StudioButton size="sm" disabled={busy||disabled||!selectedIds.length||!groupTitle.trim()} onClick={groupSelection}>Group selection</StudioButton><StudioButton size="sm" disabled={busy||disabled||!selectedIds.length} onClick={copySubgraph}>Copy subgraph</StudioButton><StudioButton size="sm" disabled={busy||disabled||!snippet.length||rows.length+snippet.length>64} onClick={pasteSubgraph}>Insert subgraph</StudioButton>{groups.map(g=><StudioButton key={g.id} size="sm" disabled={busy||disabled} onClick={()=>graph&&edit({...graph,editorGroups:groups.filter(x=>x.id!==g.id)})}>Remove group: {g.title}</StudioButton>)}</div>
      <div className="au-row" style={{flexWrap:'wrap'}}><StudioSelect aria-label="Reusable saved texture graph" value={names.includes(reuseName)?reuseName:''} disabled={busy||disabled} onChange={e=>setReuseName(e.target.value)}><option value="">Reusable saved graph</option>{names.map(n=><option key={n}>{n}</option>)}</StudioSelect><StudioButton size="sm" disabled={busy||disabled||!graph||!names.includes(reuseName)||rows.length>=64} onClick={()=>void run('insertSavedGraph')}>Insert saved graph</StudioButton><small>Independent copy · parameters preserved · connect outputs manually</small></div>
      <div className="au-row" style={{flexWrap:'wrap'}}><StudioInput aria-label="New texture function name" placeholder="Function name" value={functionName} disabled={busy||disabled} onChange={e=>setFunctionName(e.target.value)}/><StudioButton size="sm" disabled={busy||disabled||!selected||!functionName.trim()} onClick={captureFunction}>Define from selection</StudioButton><StudioSelect aria-label="Texture function definition" value={Object.hasOwn(functionLibrary,functionChoice)?functionChoice:''} disabled={busy||disabled} onChange={e=>{const name=e.target.value;setFunctionChoice(name);const d=functionLibrary[name];setFunctionOutput(object(d)&&object(d.outputs)?Object.keys(d.outputs)[0]??'':'');}}><option value="">Function library</option>{Object.keys(functionLibrary).sort().map(n=><option key={n}>{n}</option>)}</StudioSelect><StudioSelect aria-label="Function result" value={functionOutput} disabled={busy||disabled||!functionOutputs.length} onChange={e=>setFunctionOutput(e.target.value)}><option value="">Output</option>{functionOutputs.map(n=><option key={n}>{n}</option>)}</StudioSelect><StudioButton size="sm" disabled={busy||disabled||!functionOutputs.includes(functionOutput)} onClick={insertFunction}>Add function</StudioButton><StudioButton size="sm" disabled={busy||disabled||!functionChoice} onClick={deleteFunction}>Remove definition</StudioButton><small>Selected node is the result; boundary edges become typed inputs. Definitions are saved with the graph.</small></div>
      <div className="au-row" style={{flexWrap:'wrap'}}><StudioInput aria-label="Function asset name" placeholder="Function asset name" value={functionAssetName} disabled={busy||disabled} onChange={e=>{setFunctionAssetName(e.target.value);setFunctionAssetRecord(null)}}/><StudioSelect aria-label="Saved function asset document" value={names.includes(functionAssetName)?functionAssetName:''} disabled={busy||disabled} onChange={e=>{setFunctionAssetName(e.target.value);setFunctionAssetRecord(null)}}><option value="">Saved documents</option>{names.map(n=><option key={n}>{n}</option>)}</StudioSelect><StudioButton size="sm" disabled={busy||disabled||!functionAssetName} onClick={()=>void run('inspectFunctionAsset')}>Inspect asset</StudioButton><StudioButton size="sm" disabled={busy||disabled||!functionChoice||!functionAssetName} onClick={()=>void run('publishFunctionAsset')}>Publish new asset</StudioButton><StudioButton size="sm" disabled={busy||disabled||!functionChoice||functionAssetRecord?.name!==functionAssetName} onClick={()=>void run('reviseFunctionAsset')}>Publish revision</StudioButton><StudioButton size="sm" disabled={busy||disabled||!functionAssetName||!graph} onClick={()=>void run('linkFunctionAsset')}>Link asset</StudioButton><StudioButton size="sm" disabled={busy||disabled||!linkedChoice} onClick={()=>void run('refreshFunctionAsset')}>Refresh linked</StudioButton><StudioButton size="sm" disabled={busy||disabled||!linkedChoice} onClick={()=>graph&&edit(detachTextureFunctionAsset(graph,functionChoice))}>Detach link</StudioButton><small>{linkedChoice?'Linked snapshot · refresh explicitly to establish trusted baseline':'Function assets use project storage and explicit revisions'}</small></div>
      <div className="au-row" style={{flexWrap:'wrap'}}><StudioButton size="sm" disabled={busy||disabled||gpuPolling||!!gpuRequest?.bake||!!queueTicket||Object.keys(queueDrafts).length>0||!!pbrCapture||!graph} onClick={()=>void run('refreshDependencies')}>Refresh dependencies</StudioButton><label><input type="checkbox" checked={autoDependencies} onChange={e=>setAutoDependencies(e.target.checked)}/> Auto refresh dependencies</label><small>{dependencyStatus||'Draft only · no automatic save or bake'}</small></div>
      {catalog.length>0&&<div className="tg-canvas"><GraphCanvas view={view} selected={selected} label="Texture DAG" disabled={busy||disabled} onSelect={id=>{setSelected(id);setSelection(id?[id]:[]);}} selection={selectedIds.length?selectedIds:(selected?[selected]:[])} onSelection={ids=>{setSelection(ids);setSelected(ids.at(-1)??'');}} onMoveGroup={moveNodes} groups={groups} onConnect={(from,to)=>connect(from.node,to.node,Number(to.port))} pendingOutput={pending} onOutput={p=>setPending(p)} onInput={p=>{if(pending)connect(pending.node,p.node,Number(p.port))}} onCancel={()=>setPending(null)} enhancedNavigation typedPorts onAddMenu={openSearch} commands={[{id:"add",label:"Search nodes",shortcut:"shift+a",run:openSearch},{id:"undo",label:"Undo",shortcut:"mod+z",disabled:!history.current.canUndo(),run:undo},{id:"redo",label:"Redo",shortcut:"mod+shift+z",disabled:!history.current.canRedo(),run:redo},{id:"duplicate",label:"Duplicate",shortcut:"mod+d",disabled:!selected||rows.length>=64,run:duplicate},{id:"copy",label:"Copy",shortcut:"mod+c",disabled:!selected,run:copyNode},{id:"paste",label:"Paste",shortcut:"mod+v",disabled:!clipboard||rows.length>=64,run:pasteNode},{id:"layout",label:"Auto layout",run:autoLayout},{id:"delete",label:"Remove",shortcut:"delete",disabled:!selected,run:removeNode}]} onMove={(id,x,y)=>moveNodes([{id,x,y}])}/></div>}
    {!catalog.length&&<div className="tg-empty">Loading native node catalog. Use Refresh if it is unavailable.</div>}
   </div>
   <aside className="tg-inspector" style={{width:inspectorWidth,maxWidth:"100%",boxSizing:"border-box"}}>
    <label className="tg-width">Inspector width <input aria-label="Inspector width" type="range" min={235} max={480} step={5} value={inspectorWidth} onChange={e=>setInspectorWidth(Number(e.target.value))}/></label>
    <section className="tg-preview">
     <div className="tg-preview-header"><strong>Preview</strong><small>{previewMode==='pbr'?(pbrImage?.source===pbrKey?'Native PBR':'No PBR result'):preview&&preview.source===text?previewKind:'No result'}</small></div>
     <div className="au-row"><StudioButton size="sm" aria-pressed={previewMode==='texture'} onClick={()=>setPreviewMode('texture')}>2D texture</StudioButton><StudioButton size="sm" disabled={!gpuCaps?.studioPbrPreview} aria-pressed={previewMode==='pbr'} onClick={()=>setPreviewMode('pbr')}>3D PBR</StudioButton></div>
     {previewMode==='pbr'?<>
       <div className="tg-image">{pbrImage?.source===pbrKey?<canvas ref={pbrCanvas} width={256} height={256} aria-label="Native 3D PBR preview" onPointerDown={e=>{e.currentTarget.setPointerCapture(e.pointerId)}} onPointerMove={e=>{if(e.buttons===1){setPbrYaw(v=>Math.max(-100,Math.min(100,v+e.movementX*.012)));setPbrPitch(v=>Math.max(-1.4,Math.min(1.4,v+e.movementY*.012)));}}} onPointerUp={()=>{if(!busy&&!pbrCapture&&pbrReady===pbrKey)void run('studioPbrRender')}}/>:<span>{pbrCapture?'Rendering…':'Generate PBR preview'}</span>}</div>
       <StudioSelect aria-label="PBR preview shape" value={pbrShape} disabled={busy||gpuPolling||!!pbrCapture} onChange={e=>{setPbrShape(e.target.value);setPbrReady('');setPbrImage(null)}}>{['sphere','cube','plane','model'].map(v=><option key={v}>{v}</option>)}</StudioSelect>
       {pbrShape==='model'&&<><StudioButton size="sm" disabled={busy||disabled} onClick={()=>void run('studioModelAssets')}>Load model assets</StudioButton><StudioSelect aria-label="Preview model asset" value={pbrAsset} onChange={e=>{setPbrAsset(e.target.value);setPbrReady('');setPbrImage(null)}}><option value="">Select model</option>{modelAssets.map(a=><option key={a}>{a}</option>)}</StudioSelect><StudioInput type="number" min={0} aria-label="Preview material index" value={pbrIndex} onChange={e=>{setPbrIndex(e.target.value);setPbrReady('');setPbrImage(null)}}/></>}
       <StudioButton size="sm" variant="primary" disabled={busy||disabled||gpuPolling||!!pbrCapture||!!gpuRequest?.bake||!gpuCaps?.studioPbrPreview||(pbrShape==='model'&&!pbrAsset)} onClick={()=>void run('studioPbrGenerate')}>Generate GPU PBR</StudioButton>
       {gpuCaps?.studioPbrEnvironment&&<><StudioButton size="sm" disabled={busy||disabled||!!pbrCapture} onClick={()=>void run('studioHdrAssets')}>Load HDRI assets</StudioButton><StudioSelect aria-label="PBR HDR environment" value={hdri} disabled={busy||disabled||!!pbrCapture} onChange={e=>setHdri(e.target.value)}><option value="">Directional studio lighting</option>{hdriAssets.map(a=><option key={a}>{a}</option>)}</StudioSelect>{hdri&&<div className="tg-pbr-controls"><label>HDRI rotation<input type="range" min={-6.28} max={6.28} step={.02} value={hdriYaw} disabled={busy||!!pbrCapture} onChange={e=>setHdriYaw(Number(e.target.value))}/></label><label>HDRI intensity<input type="range" min={0} max={8} step={.02} value={hdriIntensity} disabled={busy||!!pbrCapture} onChange={e=>setHdriIntensity(Number(e.target.value))}/></label><StudioButton size="sm" disabled={busy||disabled||!!pbrCapture||pbrReady!==pbrKey||!pbrReady} onClick={()=>void run('studioPbrReloadEnvironment')}>Reload HDRI</StudioButton></div>}</>}
       <div className="tg-pbr-controls">{([{label:'Orbit',v:pbrYaw,min:-6.28,max:6.28,set:setPbrYaw},{label:'Pitch',v:pbrPitch,min:-1.4,max:1.4,set:setPbrPitch},{label:'Zoom',v:pbrZoom,min:.6,max:3,set:setPbrZoom},{label:'Light',v:pbrLight,min:0,max:8,set:setPbrLight},{label:'Light angle',v:pbrLightYaw,min:-6.28,max:6.28,set:setPbrLightYaw}]).map(c=><label key={c.label}>{c.label}<input type="range" min={c.min} max={c.max} step={.02} value={c.v} onChange={e=>c.set(Number(e.target.value))}/></label>)}</div>
       <StudioButton size="sm" disabled={busy||disabled||!!pbrCapture||pbrReady!==pbrKey||!pbrReady} onClick={()=>void run('studioPbrRender')}>Render view</StudioButton>
       <small>Native renderer · GPU-resident PBR maps · {renderedHdri&&pbrImage?.source===pbrKey?'HDRI filtered reflections + diffuse irradiance':pbrImage?.source===pbrKey?'directional studio lighting':'render to apply lighting settings'}</small>
     </>:<>
     <div className="tg-image">{preview&&preview.source===text?<canvas ref={canvas} width={preview.width} height={preview.height} aria-label="Native texture preview"/>:<span>No preview generated</span>}</div>
       <StudioSelect aria-label="Texture output" value={output} disabled={busy||disabled} onChange={e=>{setOutput(e.target.value);setAsset(null);setPreview(null)}}>{outputs.map(n=><option key={n}>{n}</option>)}</StudioSelect>
        <StudioSelect aria-label="GPU preview resolution" value={String(gpuResolution)} disabled={busy||disabled} onChange={e=>{setGpuResolution(Number(e.target.value));setPreview(null)}}>{[32,64,128,256].map(n=><option key={n} value={n}>{n} × {n}</option>)}</StudioSelect>
     <div className="au-row"><StudioButton size="sm" variant="primary" disabled={busy||disabled||!gpuCaps?.available||gpuPolling||!outputs.includes(output)} onClick={()=>void run('gpuStart')}>GPU preview</StudioButton><StudioButton size="sm" disabled={busy||disabled||gpuPolling||!outputs.includes(output)} onClick={()=>void run('previewOutput')}>CPU preview</StudioButton></div>
     <div className="au-row"><StudioButton size="sm" disabled={busy||disabled||!gpuRequest} onClick={()=>void run('gpuCancel')}>Cancel</StudioButton><StudioButton size="sm" disabled={busy||disabled||!gpuRequest} onClick={()=>void run('gpuRead')}>Read result</StudioButton></div>
     <label className="au-row"><input type="checkbox" checked={autoPreview} onChange={e=>{autoAttempt.current='';setAutoPreview(e.target.checked);}}/> Auto GPU preview</label>
     <label className="au-row"><input type="checkbox" checked={requireGpu} disabled={busy||disabled} onChange={e=>setRequireGpu(e.target.checked)}/> Require GPU (no fallback)</label>
     </>}
     {previewMode==='pbr'&&gpuRequest&&<StudioButton size="sm" disabled={busy||disabled} onClick={()=>void run('gpuCancel')}>Cancel generation</StudioButton>}
     {gpuCaps&&!gpuCaps.available&&<StudioAlert tone="warning">{gpuCaps.reason||'Native GPU owner unavailable'}</StudioAlert>}
     {gpuStatus&&<small className="tg-status">{gpuStatus}</small>}
     {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
     <StudioButton size="sm" disabled={busy||disabled||gpuPolling||!selected} onClick={()=>void run('previewNode')}>Preview selected node (CPU)</StudioButton>
    </section>
    <div className="tg-tabs" role="group" aria-label="Texture inspector">
     {(['node','outputs','parameters'] as const).map(t=><StudioButton key={t} size="sm" aria-pressed={inspectorTab===t} onClick={()=>setInspectorTab(t)}>{t==='node'?'Node':t==='outputs'?'Outputs':'Parameters'}</StudioButton>)}
    </div>
    <div className="tg-inspector-content">
     {inspectorTab==='node'&&<>
      {!current&&<small>Select a node in the graph to edit its inputs and properties.</small>}
        {current&&spec&&<StudioSection title={'Node: '+selected}>
   {Array.isArray(spec.inputs)&&spec.inputs.map((kind,i)=><label key={i} className="au-row"><span>{Array.isArray(spec.inputNames)?String(spec.inputNames[i]):'Input '+(i+1)} ({String(kind)})</span><StudioSelect aria-label={'Input '+i} value={Array.isArray(current.inputs)?String(current.inputs[i]??''):''} disabled={busy||disabled} onChange={e=>connect(e.target.value,selected,i)}><option value="">Disconnected</option>{view.nodes.filter(n=>n.id!==selected&&n.outputs[0]?.type===kind).map(n=><option key={n.id}>{n.id}</option>)}</StudioSelect></label>)}
   {(current.op==='imageAsset'||current.op==='imageAssetChannel'||current.op==='hdrAsset')&&<><StudioButton size="sm" disabled={busy||disabled} onClick={()=>void run('textureImageAssets')}>Load project textures</StudioButton><StudioSelect aria-label="Original texture asset" value={imageAssetChoice} disabled={busy||disabled} onChange={e=>setImageAssetChoice(e.target.value)}><option value="">Choose PNG/JPEG/HDR/EXR/DDS/KTX2</option>{imageAssets.map(a=><option key={a}>{a}</option>)}</StudioSelect><StudioButton size="sm" disabled={busy||disabled||!imageAssetChoice} onClick={()=>void run('pinTextureImageAsset')}>Pin / refresh original</StudioButton><small>{object(current.properties)?String(current.properties.asset??''):'Not pinned'}</small><small>Original pixels, SHA-256 revision, 16 MiB combined decoded source budget (4 bytes/texel LDR, 16 HDR). HDR exposure and mapping convert linear radiance into the normalized graph; EXR supports uncompressed scanlines. Use Image Asset Channel for a scalar R/G/B/alpha mask. Choose nearest, bilinear or trilinear; base, auto or explicit mip LOD. DDS/KTX2 keep authored mip levels (CPU-readable 2D formats only). All mip levels count toward the same memory budget. Changed files require explicit refresh. Queue retains pinned original pixels; incremental caches use verified source revision.</small></>}
   {current?.op==='image'&&<label className="au-row"><span>Import RGB image (up to 32,768 pixels)</span><input type="file" accept="image/png,image/jpeg,image/webp" disabled={busy||disabled} onChange={async e=>{const file=e.target.files?.[0];if(!file||!current||typeof current.id!=='string')return;const token=epoch.current,source=text;try{if(file.size>16*1024*1024)throw new Error('Image exceeds 16 MiB');const bitmap=await createImageBitmap(file);if(token!==epoch.current||source!==latestDraft.current||lock.current){bitmap.close();return;}const scale=Math.min(1,1024/Math.max(bitmap.width,bitmap.height),Math.sqrt(32768/(bitmap.width*bitmap.height)));const width=Math.max(1,Math.floor(bitmap.width*scale)),height=Math.max(1,Math.floor(bitmap.height*scale));const c=document.createElement('canvas');c.width=width;c.height=height;const ctx=c.getContext('2d');if(!ctx){bitmap.close();throw new Error('Canvas unavailable');}ctx.drawImage(bitmap,0,0,width,height);bitmap.close();const rgba=ctx.getImageData(0,0,width,height).data;let rgb='';for(let i=0;i<rgba.length;i+=4)for(let j=0;j<3;j++)rgb+=rgba[i+j].toString(16).padStart(2,'0');if(!graph)return;const next={...graph,nodes:rows.map(n=>n.id===current.id?{...n,properties:{width,height,rgb}}:n)};if(new TextEncoder().encode(JSON.stringify(next,null,2)+'\n').length>256*1024)throw new Error('Imported image exceeds graph document budget');edit(next);setStatus('Imported RGB '+width+' × '+height+(scale<1?' (downsampled to embedded budget)':''));}catch(err){if(token===epoch.current)setError(String(err));}}}/></label>}
   {current.op==='function'&&<><small>{String(spec.label)} · independent instance arguments</small>{currentFunctionParameters.map(([key,def])=>{if(!object(def))return null;const args=object(current.properties)&&object(current.properties.arguments)?current.properties.arguments:{};const value=Object.hasOwn(args,key)?args[key]:def.default;const raw=JSON.stringify(value)??'null';return <label key={selected+':'+key+':'+raw} className="au-row"><span>{key} (JSON)</span><StudioInput aria-label={'Function argument '+key} defaultValue={raw} disabled={busy||disabled} onBlur={e=>{if(e.target.value!==raw)setFunctionArgument(key,e.target.value)}}/></label>})}<small>Changes are checked by the native compiler on Validate / Preview.</small></>}
   {fieldRows.map(([key,field])=>{if(!object(field))return null;const value=object(current.properties)?current.properties[key]??field.default:field.default;
    if((current.op==='imageAsset'||current.op==='imageAssetChannel'||current.op==='hdrAsset')&&(key==='asset'||key==='revision'))return <small key={key}>{key}: {String(value)}</small>;
    if(current.op==='image')return <small key={key}>{key==='rgb'?'Embedded RGB source':key+': '+String(value)}</small>;
    if(field.type==='compute')return <label key={key}><span>{key} · Compute expression (RPN)</span><textarea aria-label={'Custom compute '+key} rows={4} spellCheck={false} value={String(value)} disabled={busy||disabled} onChange={e=>setProp(key,e.target.value)}/><small>{current.op==='customScalar'?'a b c':'ar ag ab br bg bb c'} · u v · p0 p1 p2 p3 · numbers −16…16 · + − * / min max abs neg invert sat sqrt fract sin cos step mix · 48 instructions · UV reads: {current.op==='customScalar'?'sampleA sampleB sampleC':'sampleAr sampleAg sampleAb sampleBr sampleBg sampleBb sampleC'} (u v sample) · named: sample:name or sample:name.r/g/b · param:name</small><div className="au-row"><StudioButton size="sm" disabled={busy||disabled} onClick={()=>setProp(key,current.op==='customScalar'?'a b c mix':key==='red'?'ar br c mix':key==='green'?'ag bg c mix':'ab bb c mix')}>Mix</StudioButton><StudioButton size="sm" disabled={busy||disabled} onClick={()=>setProp(key,'u p0 * v p1 * + fract')}>UV pattern</StudioButton><StudioButton size="sm" disabled={busy||disabled} onClick={()=>setProp(key,'1 u - v '+(current.op==='customScalar'?'sampleA':key==='red'?'sampleAr':key==='green'?'sampleAg':'sampleAb'))}>Mirror input A</StudioButton><StudioButton size="sm" disabled={busy||disabled} onClick={()=>setProp(key,current.op==='customScalar'?'a p0 * p1 + sat':(key==='red'?'ar':key==='green'?'ag':'ab')+' p0 * p1 + sat')}>Gain / bias</StudioButton></div></label>;
    if(field.type==='json')return <label key={key}><span>{key} · JSON</span><textarea aria-label={key+' JSON'} rows={4} spellCheck={false} key={selected+':'+key+':'+JSON.stringify(value)} defaultValue={JSON.stringify(value,null,2)} disabled={busy||disabled} onBlur={e=>{try{const next=JSON.parse(e.target.value);if(next===null)throw new Error('JSON field cannot be null');if(JSON.stringify(next)!==JSON.stringify(value))setProp(key,next);}catch(err){setError(String(err));}}}/><small>{key==='resources'?'Example: {"albedo":{"slot":0,"kind":"color"}} · u v sample:albedo.r':key==='uniforms'?'Example: {"gain":{"type":"float","value":0.5}} · param:gain':key==='radiance'?'Linear RGB triple, signed finite range −65504…65504.':'Scalar values or RGB triples; normalized [0,1], max 4096 entries.'}</small></label>;
    if(field.type==='text')return <label key={key} className="au-row"><span>{key} (RPN: a b *; + − * min max abs invert)</span><StudioInput value={String(value)} disabled={busy||disabled} onChange={e=>setProp(key,e.target.value)}/></label>;
    if(field.type==='enum'&&Array.isArray(field.options))return <label key={key} className="au-row"><span>{key}</span><StudioSelect value={String(value)} disabled={busy||disabled} onChange={e=>setProp(key,e.target.value)}>{field.options.map(v=><option key={String(v)}>{String(v)}</option>)}</StudioSelect></label>;
    if(field.type==='rgb')return <div key={key} className="au-row"><span>{key}</span>{[0,1,2].map(i=><StudioInput key={i} type="number" min={0} max={1} step={.01} disabled={busy||disabled} value={Array.isArray(value)?Number(value[i]):0} onChange={e=>{if(!e.target.value.trim())return;const v=Array.isArray(value)?value.slice(0,3):[0,0,0];v[i]=Number(e.target.value);setProp(key,v)}}/>)}</div>;
    return <label key={key} className="au-row"><span>{key}</span><StudioInput type="number" min={Number(field.min)} max={Number(field.max)} step={field.type==='integer'?1:.01} value={Number(value)} disabled={busy||disabled} onChange={e=>{if(e.target.value.trim())setProp(key,Number(e.target.value))}}/></label>;
   })}
  </StudioSection>}
     </>}
     {inspectorTab==='outputs'&&<>  {graph&&<StudioSection title="Bind outputs">{Object.entries(outputTypes).map(([key,kind])=><label key={key} className="au-row"><span>{key}</span><StudioSelect value={object(graph?.outputs)?String(graph.outputs[key]??''):''} disabled={busy||disabled} onChange={e=>{if(!graph)return;const next={...(object(graph.outputs)?graph.outputs:{})};if(e.target.value)next[key]=e.target.value;else delete next[key];edit({...graph,outputs:next})}}><option value="">Unassigned</option>{view.nodes.filter(n=>n.outputs[0]?.type===kind).map(n=><option key={n.id}>{n.id}</option>)}</StudioSelect></label>)}</StudioSection>}</>}
     {inspectorTab==='parameters'&&<>  <StudioSection title="Texture graph instance parameters">
   <small>Bindings are stored in the source; overrides affect validation, preview and bake only.</small>
   {current&&fieldRows.length>0&&<div className="au-row"><StudioInput aria-label="Parameter name" placeholder="Parameter name" disabled={busy||disabled} value={parameterName} onChange={e=>setParameterName(e.target.value)}/><StudioSelect aria-label="Bound property" value={parameterField} onChange={e=>setParameterField(e.target.value)} disabled={busy||disabled}><option value="">Property</option>{fieldRows.map(([k])=><option key={k}>{k}</option>)}</StudioSelect><StudioButton size="sm" disabled={busy||disabled||!parameterName||!parameterField} onClick={declareParameter}>Expose</StudioButton></div>}
   {graph&&object(graph.parameters)&&Object.entries(graph.parameters).map(([k,v])=><div className="au-row" key={k}><small>{k}: {JSON.stringify(object(v)?v.default:v)}</small><StudioButton size="sm" disabled={busy||disabled} onClick={()=>{if(!graph)return;const next={...(object(graph.parameters)?graph.parameters:{})};delete next[k];edit({...graph,parameters:next})}}>Remove</StudioButton></div>)}
   <textarea aria-label="Instance overrides JSON" rows={4} value={overrideText} disabled={busy||disabled} onChange={e=>{setOverrideText(e.target.value);setPreview(null);setAssets([]);setAsset(null);setBakedSet('')}}/>
   <div className="au-row"><StudioButton size="sm" disabled={busy||disabled} onClick={()=>void run('cache')}>Cache status</StudioButton><StudioButton size="sm" disabled={busy||disabled} onClick={()=>void run('clearCache')}>Clear cache</StudioButton></div>
  </StudioSection></>}
    </div>
   </aside>
  </div>
  <section className="tg-footer">
   {status&&<StudioAlert>{status}</StudioAlert>}
   <div className="tg-bake-row">
      <StudioInput aria-label="New texture name" value={textureName} disabled={busy||disabled} onChange={e=>setTextureName(e.target.value)}/>
    <StudioButton size="sm" variant="primary" disabled={busy||disabled||!gpuCaps?.available||!outputs.length||gpuPolling} onClick={()=>void run('gpuBakeSet')}>GPU PBR bake</StudioButton>
    <StudioButton size="sm" disabled={busy||disabled||!gpuCaps?.available||!outputs.includes(output)||gpuPolling} onClick={()=>void run('gpuBake')}>GPU output bake</StudioButton>
    <StudioButton size="sm" disabled={busy||disabled||!gpuReady||!textureName||gpuRequest?.source!==text||gpuRequest?.overrides!==overrideText||(!gpuRequest?.setBake&&gpuRequest?.output!==output)} onClick={()=>void run('gpuPublish')}>Publish GPU bake</StudioButton>
      <StudioButton disabled={busy||disabled||!textureName||!outputs.includes(output)} onClick={()=>void run('bake')}>{output==='radiance'?'Export linear EXR (CPU)':'Bake selected output to new PNG'}</StudioButton>
  <StudioButton disabled={busy||disabled||!textureName||!outputs.length} onClick={()=>void run('bakeSet')}>Bake all outputs</StudioButton>
   </div>
   {gpuCaps?.studioResidentApply&&<details className="tg-advanced"><summary>Apply to Scene model (GPU, session only)</summary><div className="tg-advanced-body">
     <StudioButton size="sm" disabled={busy||disabled} onClick={()=>void run('studioModelTargets')}>Refresh resident models</StudioButton>
     <StudioSelect aria-label="Resident model" value={sceneAsset} disabled={busy||gpuPolling} onChange={e=>{setSceneAsset(e.target.value);setSceneMaterial('0')}}><option value="">Select loaded model</option>{sceneTargets.map(m=><option key={String(m.asset)}>{String(m.asset)}</option>)}</StudioSelect>
     <StudioSelect aria-label="Resident material" value={sceneMaterial} disabled={busy||gpuPolling} onChange={e=>setSceneMaterial(e.target.value)}>{selectedScene&&Array.isArray(selectedScene.materials)&&selectedScene.materials.filter(object).map(m=><option key={String(m.materialIndex)} value={String(m.materialIndex)}>{String(m.materialName)} (#{String(m.materialIndex)})</option>)}</StudioSelect>
     <StudioButton size="sm" variant="primary" disabled={busy||disabled||gpuPolling||!!gpuRequest?.bake||!selectedSceneMaterial} onClick={()=>void run('studioModelGenerate')}>Generate and apply GPU maps</StudioButton>
     <small>Applies to loaded instances of this model asset and preserves authored factors. Does not modify files.</small>
   </div></details>}
   <details className="tg-advanced"><summary>Advanced GPU / resident material / queue</summary>
    <div className="tg-advanced-body">
        <small>Native Metal / Vulkan compute, 256×256 maximum. CPU-reference checked; pattern and two-pass normal source supported. Rejected work falls back unless GPU is required. Full-size single-output and PBR set bakes use native readback; CPU set bake remains available.</small>
   <StudioSelect aria-label="GPU preview resolution" value={String(gpuResolution)} disabled={busy||disabled} onChange={e=>{setGpuResolution(Number(e.target.value));setPreview(null)}}>{[32,64,128,256].map(n=><option key={n} value={n}>{n} × {n}</option>)}</StudioSelect>

   <label className="au-row"><input type="checkbox" checked={materialized} disabled={busy||disabled} onChange={e=>setMaterialized(e.target.checked)}/> Experimental materialized GPU DAG (default: fused)</label>
   <label className="au-row"><input type="checkbox" checked={fusePointwise} disabled={busy||disabled||!materialized} onChange={e=>setFusePointwise(e.target.checked)}/> Experimental pointwise pass fusion</label>
   <label className="au-row"><input type="checkbox" checked={incremental} disabled={busy||disabled||!materialized} onChange={e=>setIncremental(e.target.checked)}/> Incremental GPU passes (dedicated slots)</label>
   <label className="au-row"><input type="checkbox" checked={gpuTextureTransfer} disabled={busy||disabled||!materialized||!gpuCaps?.residentApply} onChange={e=>setGpuTextureTransfer(e.target.checked)}/> Experimental GPU-resident PBR set (GPU required)</label>
   <label className="au-row"><input type="checkbox" checked={gpuTextureMipmaps} disabled={busy||disabled||!materialized||!gpuTextureTransfer} onChange={e=>setGpuTextureMipmaps(e.target.checked)}/> Generate GPU mipmaps</label>
   <label className="au-row"><input type="checkbox" checked={incrementalOutputs} disabled={busy||disabled||!materialized||!incremental||!gpuTextureTransfer||!gpuTextureFenceOnly} onChange={e=>setIncrementalOutputs(e.target.checked)}/> Reuse unchanged published outputs (direct live material set)</label>
   <label className="au-row"><input type="checkbox" checked={gpuTextureFenceOnly} disabled={busy||disabled||!materialized||!gpuTextureTransfer} onChange={e=>setGpuTextureFenceOnly(e.target.checked)}/> Fence-only material generation (no CPU parity or PNG export)</label>
   {gpuCaps?.residentApply?<StudioSection title="Apply GPU textures to resident editor material">
    <small>Session-only appearance override. Load the scene model first. Inspect the exact model choice and material, then apply an accepted GPU-resident set. No project asset is changed.</small>
    <div className="au-row"><StudioInput aria-label="Resident model type index" value={liveType} onChange={e=>{setLiveType(e.target.value);setLiveTarget(null)}} disabled={busy||disabled}/><StudioInput aria-label="Resident model choice index" value={liveChoice} onChange={e=>{setLiveChoice(e.target.value);setLiveTarget(null)}} disabled={busy||disabled}/><StudioInput aria-label="Resident material index" value={liveMaterial} onChange={e=>{setLiveMaterial(e.target.value);setLiveTarget(null)}} disabled={busy||disabled}/></div>
    <small>Type index / choice index / material index</small>
    <div className="au-row"><StudioButton size="sm" disabled={busy||disabled} onClick={()=>void run('gpuMaterialTarget')}>Inspect live material</StudioButton><StudioButton size="sm" disabled={busy||disabled||!liveTarget||!residentReady||gpuRequest?.source!==text||gpuRequest?.overrides!==overrideText} onClick={()=>void run('gpuMaterialApply')}>Apply GPU set</StudioButton><StudioButton size="sm" disabled={busy||disabled||!gpuRequest} onClick={()=>void run('gpuMaterialStatus')}>Read apply status</StudioButton></div>
    {liveTarget&&<small>{liveTarget.modelPath} · {liveTarget.materialName||'unnamed material'} · revision {liveTarget.expectedVersion} · procedural images {liveTarget.proceduralLiveImages} / 24 · retired slots {liveTarget.proceduralTombstones} · host pending retirement {liveTarget.pendingImageRetirement} · auto collection {liveTarget.automaticCollectionEnabled?`enabled (${liveTarget.automaticCollectionDetached} detached, ${liveTarget.automaticCollectionDeferred} deferred)`:"requires native root certification"} · slot recycling {liveTarget.automaticRecyclingEnabled?"enabled":"disabled"}</small>}
    {liveApply&&<small>{liveApply}</small>}
   </StudioSection>:<small>Resident model application is not available in this host. Publish validated GPU bakes and assign their assets instead.</small>}
   {materialized&&gpuTextureTransfer&&<small>Full set bake only. Default validates copied pixels. Fence-only mode generates bindable maps without CPU parity or export. Apply to a material or cancel before starting another resident set.</small>}
   <StudioButton size="sm" disabled={busy||disabled||!outputs.length} onClick={()=>void run('gpuPlan')}>Analyze materialized DAG (plan only)</StudioButton>
    <StudioButton size="sm" disabled={busy||disabled||!outputs.length} onClick={()=>void run('gpuBakeSet')}>Queue full-size GPU PBR set</StudioButton>
   <StudioSection title="GPU request queue">
    <small>Non-preemptive, priority 3 highest; equal priority FIFO. Read queue to select an active draft, then Read result above. Publish/apply or explicitly discard and release before the next request.</small>
    <StudioSelect aria-label="GPU queue priority" value={queuePriority} disabled={busy||disabled} onChange={e=>setQueuePriority(e.target.value)}>{[0,1,2,3].map(n=><option key={n} value={String(n)}>Priority {n}</option>)}</StudioSelect>
    <div className="au-row"><StudioButton size="sm" disabled={busy||disabled||gpuPolling||!gpuCaps?.available||!outputs.length} onClick={()=>void run('queueEnqueue')}>Enqueue PBR set</StudioButton><StudioButton size="sm" disabled={busy||disabled} onClick={()=>void run('queueRead')}>Read queue</StudioButton></div>
    <StudioInput aria-label="GPU queue ticket" value={queueTicket} disabled={busy||disabled} onChange={e=>setQueueTicket(e.target.value)}/>
    <div className="au-row"><StudioButton size="sm" disabled={busy||disabled||!queueTicket} onClick={()=>void run('queueCancel')}>Cancel ticket</StudioButton><StudioButton size="sm" disabled={busy||disabled||!queueTicket} onClick={()=>void run('queueRelease')}>Discard / release active</StudioButton></div>
    {queueInfo&&<small>Active {String(queueInfo.activeTicket||'none')} · request {String(queueInfo.activeRequest||'none')} · pending {Array.isArray(queueInfo.pending)?queueInfo.pending.filter(object).map(x=>String(x.ticket)+' (p'+String(x.priority)+')').join(', '):''} · {String(queueInfo.pendingBytes??0)} bytes · {String(queueInfo.error??'')}</small>}
   </StudioSection>
    </div>
   </details>
   <details className="tg-advanced"><summary>Source JSON</summary>  <textarea aria-label="Texture graph JSON" value={text} disabled={busy||disabled} rows={12} onChange={e=>{changeText(e.target.value);setPending(null)}}/></details>
     {bakedSet&&<StudioSection title="Create baked material variant"><small>Inherit an existing material; override only baked channels. No runtime regeneration.</small><StudioInput aria-label="Base material" value={baseMaterial} disabled={busy||disabled} onChange={e=>setBaseMaterial(e.target.value)}/><StudioInput aria-label="New material name" value={variantName} disabled={busy||disabled} onChange={e=>setVariantName(e.target.value)}/><StudioButton disabled={busy||disabled||!baseMaterial||!variantName} onClick={()=>void run('variant')}>Create variant from {bakedSet}</StudioButton></StudioSection>}
  {assets.map(a=><StudioButton key={a.guid} disabled={busy||disabled} onClick={()=>onEdit(a.key,a.guid)}>Assign {a.path} to {a.key}</StudioButton>)}
  {asset&&<StudioButton disabled={busy||disabled} onClick={()=>onEdit(asset.key,asset.guid)}>Assign {asset.path} to material draft</StudioButton>}
  </section>
 </section>;
}
