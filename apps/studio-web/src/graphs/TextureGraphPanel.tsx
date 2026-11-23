import {useEffect,useRef,useState} from 'react';
import {invoke} from '../bridge';
import {StudioAlert,StudioButton,StudioInput,StudioSelect,StudioSection} from '../ui';
import GraphCanvas from './GraphCanvas';
import type {GraphView,NodeView,Link} from './graphFramework';
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
 const [names,setNames]=useState<string[]>([]),[catalog,setCatalog]=useState<Obj[]>([]),[selected,setSelected]=useState(''),[output,setOutput]=useState('color'),[textureName,setTextureName]=useState('');
 const [preview,setPreview]=useState<{hex:string;width:number;height:number;source:string}|null>(null),[asset,setAsset]=useState<{guid:string;path:string;key:MaterialKey}|null>(null);
 const [busy,setBusy]=useState(false),[error,setError]=useState(''),[status,setStatus]=useState('');
 const epoch=useRef(0),lock=useRef(false),canvas=useRef<HTMLCanvasElement>(null);
 useEffect(()=>{epoch.current++;lock.current=false;setBusy(false);setOriginal(null);setName('');setText(JSON.stringify(template,null,2));setNames([]);setCatalog([]);setAsset(null);setPreview(null);setError('');setStatus('');
 return()=>{epoch.current++;lock.current=false};},[projectPath]);
 useEffect(()=>{const c=canvas.current;if(!c||!preview||preview.source!==text)return;
 const bytes=new Uint8ClampedArray(preview.hex.length/2);for(let i=0;i<bytes.length;i++)bytes[i]=parseInt(preview.hex.slice(i*2,i*2+2),16);
 c.getContext('2d')?.putImageData(new ImageData(bytes,preview.width,preview.height),0,0);
 },[preview,text]);
 let graph:Obj|null=null;try{const value=JSON.parse(text);if(object(value))graph=value}catch{}
 const edit=(value:Obj)=>{setText(JSON.stringify(value,null,2));setPreview(null);setAsset(null);};
 const run=async(action:string)=>{
  if(lock.current||disabled||!projectPath)return;lock.current=true;setBusy(true);setError('');setStatus('');const token=epoch.current;
  try{
   if(action==='catalog'){
    const [c,l]=await Promise.all([op('texture_graph_catalog'),op('texture_graph_list')]);
    if(token!==epoch.current)return;
    if(!object(c)||!Array.isArray(c.nodes)||!object(l)||!Array.isArray(l.graphs))throw new Error('Catalog unavailable');
    setCatalog(c.nodes.filter(object));setNames(l.graphs.filter((x):x is string=>typeof x==='string'));return;
   }
   if(action==='load'){
    const r=await op('texture_graph_get',{name});if(token!==epoch.current)return;
    if(!object(r)||r.ok!==true||!object(r.graph))throw new Error('Graph unavailable');
    setOriginal({name,graph:r.graph});edit(r.graph);setSelected('');return;
   }
   if(!graph)throw new Error('Invalid graph JSON');
   if(action==='save'){
    const r=await op('texture_graph_set',{name,graph,...(original?.name===name?{expected:original.graph}:{})});if(token!==epoch.current)return;
    if(!object(r)||r.ok!==true||!object(r.graph))throw new Error('Native save rejected');
    setOriginal({name,graph:r.graph});setNames(old=>[...new Set([...old,name])].sort());setStatus('Graph saved');return;
   }
   if(action==='validate'){
    const r=await op('texture_graph_validate',{graph});if(token!==epoch.current)return;if(!object(r)||r.ok!==true)throw new Error('Native validation failed');setStatus('Native topology and work budget valid');return;
   }
   if(action==='previewNode'||action==='previewOutput'){
    setPreview(null);
    const r=await op('texture_graph_preview',{graph,...(action==='previewNode'?{node:selected}:{output})});if(token!==epoch.current)return;
    if(!object(r)||r.ok!==true||typeof r.width!=='number'||typeof r.height!=='number'||!Number.isInteger(r.width)||!Number.isInteger(r.height)||r.width<8||r.height<8||r.width>32||r.height>32||typeof r.rgbaHex!=='string'||r.rgbaHex.length!==r.width*r.height*8||!/^[0-9a-f]+$/.test(r.rgbaHex))throw new Error('Invalid native preview');
    setPreview({hex:r.rgbaHex,width:r.width,height:r.height,source:text});return;
   }
   if(action==='bake'){
    setAsset(null);const r=await op('texture_graph_bake',{graph,output,textureName});if(token!==epoch.current)return;
    if(!object(r)||r.ok!==true||typeof r.path!=='string')throw new Error('Native bake failed');
    setStatus('Baked '+r.path);
    const key=channels[output];if(!key)return;
    const assets=await invoke('studio.assets',{});if(token!==epoch.current)return;
    const a=assets.files.find(a=>'content/'+a.path===r.path&&a.importer==='texture'&&a.guid);
    if(a?.guid)setAsset({guid:a.guid,path:a.path,key});else setStatus('Baked '+r.path+'; refresh Assets and assign with asset picker.');
   }
  }catch(e){if(epoch.current===token)setError(e instanceof Error?e.message:String(e))}
  finally{if(epoch.current===token){lock.current=false;setBusy(false)}}
 };
 const view=graph?projection(graph,catalog):{nodes:[],links:[]};
 const outputs=graph&&object(graph.outputs)?Object.keys(graph.outputs):[];
 return <StudioSection title="Procedural Texture Graph">
  <small>Native CPU DAG. Source graph and baked PNG are separate. Color is sRGB; normal/data outputs require correct importer settings.</small>
  <StudioButton size="sm" disabled={disabled||busy} onClick={()=>void run('catalog')}>Refresh graph / node catalog</StudioButton>
  <StudioSelect aria-label="Saved texture graph" value={names.includes(name)?name:''} disabled={busy||disabled} onChange={e=>{setName(e.target.value);setOriginal(null)}}><option value="">Select saved graph</option>{names.map(n=><option key={n}>{n}</option>)}</StudioSelect>
  <StudioInput aria-label="Graph name" placeholder="Graph name" value={name} disabled={busy||disabled} onChange={e=>setName(e.target.value)}/>
  <div className="au-row" style={{flexWrap:'wrap'}}>{['load','save','validate'].map(action=><StudioButton key={action} size="sm" disabled={busy||disabled||(action!=='validate'&&!name)} onClick={()=>void run(action)}>{action}</StudioButton>)}<StudioButton size="sm" disabled={busy||disabled} onClick={()=>{edit(template);setOriginal(null);setName('')}}>New PBR template</StudioButton></div>
  {catalog.length>0&&<div style={{height:300,display:'flex',flexDirection:'column'}}><GraphCanvas view={view} selected={selected} label="Texture DAG" disabled={busy||disabled} onSelect={setSelected} onMove={(id,x,y)=>{if(graph&&Array.isArray(graph.nodes))edit({...graph,nodes:graph.nodes.map(n=>object(n)&&n.id===id?{...n,position:[x,y]}:n)})}}/></div>}
  <textarea aria-label="Texture graph JSON" value={text} disabled={busy||disabled} rows={12} onChange={e=>{setText(e.target.value);setPreview(null);setAsset(null)}}/>
  <StudioSelect aria-label="Texture output" value={output} disabled={busy||disabled} onChange={e=>{setOutput(e.target.value);setAsset(null)}}>{outputs.map(n=><option key={n}>{n}</option>)}</StudioSelect>
  <div className="au-row"><StudioButton size="sm" disabled={busy||disabled||!selected} onClick={()=>void run('previewNode')}>Preview node</StudioButton><StudioButton size="sm" disabled={busy||disabled||!outputs.includes(output)} onClick={()=>void run('previewOutput')}>Preview output</StudioButton></div>
  {preview&&preview.source===text&&<canvas ref={canvas} width={preview.width} height={preview.height} style={{width:128,height:128,imageRendering:'pixelated'}} aria-label="Native texture preview"/>}
  <StudioInput aria-label="New texture name" value={textureName} disabled={busy||disabled} onChange={e=>setTextureName(e.target.value)}/>
  <StudioButton disabled={busy||disabled||!textureName||!outputs.includes(output)} onClick={()=>void run('bake')}>Bake selected output to new PNG</StudioButton>
  {asset&&<StudioButton disabled={busy||disabled} onClick={()=>onEdit(asset.key,asset.guid)}>Assign {asset.path} to material draft</StudioButton>}
  {status&&<StudioAlert>{status}</StudioAlert>}{error&&<StudioAlert tone="danger">{error}</StudioAlert>}
 </StudioSection>;
}
