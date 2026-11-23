import {useEffect,useRef,useState} from 'react';
import {invoke} from '../bridge';
import {StudioAlert,StudioButton,StudioInput,StudioSelect,StudioSection} from '../ui';
import PropertyField from '../components/PropertyField';
import {materialFields} from '../panels/materialSchema';
import type {MaterialData,MaterialPatch,MaterialKey,FieldValue} from '../panels/materialSchema';
import GraphCanvas from './GraphCanvas';
import TextureGraphPanel from './TextureGraphPanel';
import {materialGraph,textureRecipeParams,textureChannelForOutput} from './materialGraph';
import type {TextureRecipe,TextureOutput} from './materialGraph';

const object=(x:unknown):x is Record<string,unknown>=>!!x&&typeof x==='object'&&!Array.isArray(x);
const op=async(name:string,params:Record<string,unknown>={})=>(await invoke('studio.op',{op:name,params})).result;
export default function MaterialGraphPanel({name,material,draft,projectPath,disabled,onEdit}:{name:string;material:MaterialData;draft:MaterialPatch;projectPath:string;disabled:boolean;onEdit:(key:MaterialKey,value:FieldValue)=>void}){
 const [selected,setSelected]=useState(''),[patterns,setPatterns]=useState<string[]>([]);
 const [recipe,setRecipe]=useState<TextureRecipe>({pattern:'',output:'color',seed:1,size:256,scale:8,name:''});
 const [error,setError]=useState(''),[status,setStatus]=useState(''),[busy,setBusy]=useState(false);
 const [generated,setGenerated]=useState<{guid:string;path:string;output:TextureOutput}|null>(null);
 const epoch=useRef(0),lock=useRef(false);
 useEffect(()=>{
  const token=++epoch.current;lock.current=false;setBusy(false);setGenerated(null);setPatterns([]);setError('');setStatus('');
  if(projectPath)void op('texture_patterns').then(r=>{
   if(epoch.current!==token)return;
   if(!object(r)||!Array.isArray(r.patterns))throw new Error('Native pattern catalog unavailable');
   const list=r.patterns.filter((p):p is string=>typeof p==='string');setPatterns(list);setRecipe(old=>({...old,pattern:list.includes(old.pattern)?old.pattern:list[0]??''}));
  }).catch(e=>{if(epoch.current===token)setError(String(e))});
  return ()=>{if(epoch.current===token)epoch.current++};
 },[projectPath]);
 const generate=async()=>{
  if(disabled||lock.current)return;
  lock.current=true;setBusy(true);setError('');setStatus('');setGenerated(null);
  const token=epoch.current;
  try{
   const params=textureRecipeParams(recipe,patterns),r=await op('texture_generate',params);
   if(epoch.current!==token)return;
   if(!object(r)||r.ok!==true||r.path!==params.path)throw new Error('Native texture generation did not return the requested path');
   setStatus('PNG generated. Resolving its asset GUID…');
   const catalog=await invoke('studio.assets',{});
   if(epoch.current!==token)return;
   const asset=catalog.files.find(a=>'content/'+a.path===r.path&&a.importer==='texture'&&a.guid);
   if(!asset?.guid){setStatus('PNG generated: '+String(r.path));throw new Error('Texture is not yet indexed by Assets. Refresh and assign it using the asset picker.');}
   setGenerated({guid:asset.guid,path:asset.path,output:recipe.output});
   setStatus('Generated '+asset.path+'. Assign explicitly, then Apply & save the material.');
  }catch(e){if(epoch.current===token)setError(e instanceof Error?e.message:String(e))}
  finally{if(epoch.current===token){lock.current=false;setBusy(false)}}
 };
 const view=materialGraph(name,material,draft);
 const fields=materialFields(material,draft).filter(f=>selected==='material'||selected==='factors'?f.type!=='asset':selected==='texture:'+f.key);
 return <StudioSection title="Material dependencies / procedural texture">
  <div style={{height:300,display:'flex',flexDirection:'column'}}>
   <GraphCanvas view={view} selected={selected} label="Material dependency graph" readOnly onSelect={setSelected} onMove={()=>{}}/>
  </div>
  {fields.map(f=><PropertyField key={f.key} property={f} disabled={disabled||busy} onChange={v=>onEdit(f.key,v)}/>)}
  <small>Dependency view of the material draft; edit channels using the existing asset picker. Shader evaluation remains in Shader Graph.</small>
  <TextureGraphPanel projectPath={projectPath} disabled={disabled||busy} onEdit={onEdit}/>
  <StudioSection title="Generate texture">
   <StudioSelect aria-label="Pattern" disabled={busy||disabled} value={recipe.pattern} onChange={e=>setRecipe(r=>({...r,pattern:e.target.value}))}>{patterns.map(p=><option key={p}>{p}</option>)}</StudioSelect>
   <StudioSelect aria-label="Texture output" disabled={busy||disabled} value={recipe.output} onChange={e=>setRecipe(r=>({...r,output:e.target.value as TextureOutput}))}><option value="color">Color / albedo</option><option value="normal">Normal</option><option value="mask">Team mask</option></StudioSelect>
   <StudioInput aria-label="Texture file name" placeholder="New texture name" disabled={busy||disabled} value={recipe.name} onChange={e=>setRecipe(r=>({...r,name:e.target.value}))}/>
   {(['seed','size','scale'] as const).map(k=><label key={k} className="au-row"><span>{k}</span><StudioInput aria-label={k} type="number" disabled={busy||disabled} value={recipe[k]} onChange={e=>{if(e.target.value.trim())setRecipe(r=>({...r,[k]:Number(e.target.value)}))}}/></label>)}
   <StudioButton disabled={disabled||busy||!recipe.name||!patterns.length} onClick={()=>void generate()}>Generate new PNG</StudioButton>
   {generated&&<StudioButton disabled={disabled||busy} onClick={()=>onEdit(textureChannelForOutput(generated.output),generated.guid)}>Assign {generated.path} to draft</StudioButton>}
   {status&&<StudioAlert>{status}</StudioAlert>}{error&&<StudioAlert tone="danger">{error}</StudioAlert>}
  </StudioSection>
 </StudioSection>;
}
