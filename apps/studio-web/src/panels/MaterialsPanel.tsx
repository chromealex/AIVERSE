import { useEffect, useMemo, useState } from 'react';
import { Eye, Layers, RefreshCw, Save, Search, ShieldAlert } from 'lucide-react';
import { invoke, nativeConnected, onNativeEvent } from '../bridge';
import AssetReferencePicker from '../components/AssetReferencePicker';
import PropertyField from '../components/PropertyField';
import MaterialGraphPanel from '../graphs/MaterialGraphPanel';
import {StudioButton} from '../ui';
import { StudioLoader } from '../ui';
import { editablePatch, materialFields, materialNames, materialSwatchColor, MATERIAL_PRESETS } from './materialSchema';
import type { MaterialData, MaterialKey, MaterialLibrary, MaterialPatch, MaterialPreset } from './materialSchema';
import './MaterialsPanel.css';

import SpectraDockViewport from './SpectraDockViewport';
const EMPTY:MaterialLibrary={library:{}};
export default function MaterialsPanel({projectPath,previewMaterial,textureOnly=false,openTextureGraph}:{projectPath:string;previewMaterial?:string|null;textureOnly?:boolean;openTextureGraph?:()=>void}){
 const [library,setLibrary]=useState<MaterialLibrary>(EMPTY);
 const [name,setName]=useState('');
 const [query,setQuery]=useState('');
 const [newName,setNewName]=useState('');
 const [newPreset,setNewPreset]=useState<MaterialPreset>('matte');
 const [colorMap,setColorMap]=useState('');
 const [normalMap,setNormalMap]=useState('');
 const [repeatMetres,setRepeatMetres]=useState('1');
 const [creating,setCreating]=useState(false);
 const [draft,setDraft]=useState<MaterialPatch>({});
 const [pending,setPending]=useState<string|null>(null);
 const [loading,setLoading]=useState(true);
 const [busy,setBusy]=useState(false);
 const [previewing,setPreviewing]=useState(false);
 const [error,setError]=useState('');
 const [message,setMessage]=useState('');
 const [nonce,setNonce]=useState(0);
 const [materialMode,setMaterialMode]=useState<'properties'|'texture'|'dependencies'|'quick'>(textureOnly?'texture':'properties');
 const connected=nativeConnected();
 const names=useMemo(()=>materialNames(library),[library]);
 const selected:MaterialData=library.library[name]??{};
 const changes=Object.keys(draft).length;
 const props=useMemo(()=>materialFields(selected,draft),[selected,draft]);
 const reload=()=>setNonce(n=>n+1);

 useEffect(()=>{
  let revision=0;
  const unsubscribe=onNativeEvent(event=>{
   if(event==='workspace.changed'){reload();return;}
   if(event!=='assets.changed'||!connected||!projectPath)return;
   const request=++revision;
   void invoke('studio.materials',{}).then(data=>{
    if(request===revision)setLibrary(data);
   }).catch(e=>{if(request===revision)setError(String(e));});
  });
  return ()=>{++revision;unsubscribe();};
 },[connected,projectPath]);
 useEffect(()=>{
  let canceled=false;
  setLoading(true);setError('');
  if(connected&&!projectPath){
   setLibrary(EMPTY);setName('');setLoading(false);return;
  }
  void invoke('studio.materials',{}).then(data=>{
   if(canceled)return;
   setLibrary(data);
   const all=materialNames(data);
   setName(previous=>previous||(all[0]??''));
  }).catch(e=>{
   if(!canceled)setError(e instanceof Error?e.message:String(e));
  }).finally(()=>{if(!canceled)setLoading(false)});
  return ()=>{canceled=true};
 },[projectPath,nonce,connected]);
 useEffect(()=>{setDraft({});setPending(null);setName('');setMessage('');},[projectPath]);
 const switchTo=(next:string)=>{
  if(next===name)return;
  if(changes>0){setPending(next);setMessage('Unsaved draft. Apply or discard changes to switch materials.');return;}
  setName(next);setError('');setMessage('');setPending(null);setDraft({});
 };
 const edit=(key:string,value:string|number|boolean)=>{
  setDraft(current=>({...current,[key as MaterialKey]:value}));
  setMessage('');
 };
 const recipeValid=newPreset!=='triplanar' || (colorMap.trim() && Number(repeatMetres)>=0.001 && Number(repeatMetres)<=100000);
 const create=async()=>{
  if(busy||loading||!connected||!projectPath)return;
  if(changes>0){setError('Apply or discard the current material draft first.');return;}
  if(!/^[A-Za-z0-9_-]{1,64}$/.test(newName)){setError('Use 1–64 letters, digits, underscore or hyphen.');return;}
  setBusy(true);setError('');setMessage('');
  try{
   const response=await invoke('studio.materialCreate',{name:newName,preset:newPreset,...(newPreset==='triplanar'?{colorGuid:colorMap,normalGuid:normalMap,repeatMetres:Number(repeatMetres)}:{})});
   setLibrary(response);setName(newName);setDraft({});setPending(null);setNewName('');setCreating(false);
   setMessage('Material file created in content/materials.');
  }catch(e){setError(e instanceof Error?e.message:String(e));}
  finally{setBusy(false);}
 };
 const apply=async()=>{
  if(!name||!names.includes(name)||changes===0||busy||!connected)return;
  setBusy(true);setError('');setMessage('');
  try {
   const response=await invoke('studio.materialPatch',{name,patch:editablePatch(draft)});
   setLibrary(response);setDraft({});setPending(null);
   setMessage('Material file saved.');
  } catch(e){setError(e instanceof Error?e.message:String(e));}
  finally{setBusy(false);}
 };
 const convert=async()=>{
  setBusy(true);setError('');
  try{setLibrary(await invoke('studio.materialsToFiles',{}));setMessage('Material files saved in content/materials.');}
  catch(e){setError(String(e));}finally{setBusy(false);}
 };
 useEffect(()=>{
  const open=()=>{
   const key='studio.materialToOpen:'+projectPath;
   const path=sessionStorage.getItem(key);
   const match=Object.entries(library.files??{}).find(([,file])=>file===path);
   if(match){sessionStorage.removeItem(key);switchTo(match[0]);}
  };
  open();window.addEventListener('studio:open-material',open);
  return ()=>window.removeEventListener('studio:open-material',open);
 },[library,projectPath,changes]);
 const preview=async()=>{
  if(busy||previewing||!connected||!projectPath||!name||changes>0)return;
  setPreviewing(true);setError('');
  try{
    await invoke('studio.materialPreview',{name:previewMaterial===name?'':name});
    setMessage(previewMaterial===name?'Material preview cleared.':'Showing saved PBR factors.');
  }catch(e){setError(e instanceof Error?e.message:String(e));}
  finally{setPreviewing(false);}
 };
 const discard=()=>{
  setDraft({});
  if(pending!==null){setName(pending);setPending(null);}
  setMessage('Draft discarded.');
 };
 return <section className="materials-editor">
  <div className="materials-heading">
   <span><Layers size={16}/> {textureOnly?'Texture Graph · target material':'Material Library'}</span>
   <button className="icon-button" aria-label="Reload materials" title="Reload materials"
    disabled={busy||loading||changes>0} onClick={reload}><RefreshCw size={15}/></button>
  </div>
  {names.some(n=>!library.files?.[n])&&<button disabled={busy||changes>0} onClick={()=>void convert()}>Convert library to material files</button>}
  <p className="materials-warning"><ShieldAlert size={15}/>
   Apply saves the material file. Map Undo applies to scene edits.</p>
  {!connected&&<p className="materials-notice">DEMO — no native material library; editing is disabled.</p>}
  {connected&&!projectPath&&<p className="materials-notice">Open a project to view the material library.</p>}
  {error&&<p role="alert" className="materials-error">{error}</p>}
  {message&&<p role="status" className="materials-notice">{message}</p>}
  {name&&previewMaterial===name&&<SpectraDockViewport id="material" label="Material GPU preview"/>}
  <div className="materials-main">
   <aside className="materials-list">
    <button className="materials-new-button" disabled={!connected||!projectPath||busy}
      onClick={()=>setCreating(value=>!value)}>{creating?'Cancel new material':'+ New material'}</button>
    {creating&&<div className="materials-new-form">
      <label>Name<input aria-label="New material name" value={newName}
        placeholder="matte_stone" maxLength={64} onChange={e=>setNewName(e.target.value)}/></label>
      <label>Preset<select aria-label="Material preset" value={newPreset}
        onChange={e=>setNewPreset(e.target.value as MaterialPreset)}>
        {MATERIAL_PRESETS.map(p=><option key={p} value={p}>{p}</option>)}
      </select></label>
      {newPreset==='triplanar'&&<>
        <div className="property-row"><span>Colour texture</span><AssetReferencePicker label="Colour texture" value={colorMap} kinds={['image']} disabled={busy} onChange={setColorMap}/></div>
        <div className="property-row"><span>Normal texture (optional)</span><AssetReferencePicker label="Normal texture (optional)" value={normalMap} kinds={['image']} disabled={busy} onChange={setNormalMap}/></div>
        <label>Repeat length (metres)<input type="number" min="0.001" max="100000" step="any" value={repeatMetres} onChange={e=>setRepeatMetres(e.target.value)}/></label>
      </>}
      <button disabled={busy||!newName||!recipeValid||changes>0} onClick={()=>void create()}>Create and save</button>
    </div>}
    <label className="materials-search"><Search size={14}/>
     <input placeholder="Filter materials…" aria-label="Filter materials"
      value={query} onChange={e=>setQuery(e.target.value)}/>
    </label>
    {loading?<p className="materials-empty"><StudioLoader size={30} label="Loading…"/></p>:
     names.length===0?<p className="materials-empty">{connected && projectPath ? 'Choose New material to create the first material.' : 'No library materials.'}</p>:
     names.filter(n=>n.toLowerCase().includes(query.toLowerCase())).map(n=>
      <button key={n} type="button" aria-pressed={name===n}
       className={'materials-list-item'+(n===name?' active':'')} onClick={()=>switchTo(n)}>
       <span className="material-chip" style={{background:materialSwatchColor(library.library[n])}}/>
       <span title={n}>{n}</span>
      </button>)
    }
   </aside>
   <div className={'materials-detail'+(materialMode==='texture'?' materials-detail--graph':'')}>
    {!name||!names.includes(name)?<div className="materials-empty">{name?'Selected material is no longer present. Discard the draft and choose another material.':'Select a material from the library.'}</div>:
     <><div className="materials-selected">
      <span className="materials-swatch" style={{background:materialSwatchColor(selected)}}/>
      <strong>{name}</strong>{library.files?.[name]&&<button title={library.files[name]} onClick={()=>{
       window.dispatchEvent(new CustomEvent('studio:show-panel',{detail:'assets'}));
       const path=library.files![name].slice('content/'.length);
       window.setTimeout(()=>window.dispatchEvent(new CustomEvent('studio:reveal-asset',{detail:{path}})),80);
      }}>Show in Assets</button>}
     </div>
     <div className="materials-modes" role="group" aria-label="Material workspace">
      {(textureOnly?(['texture'] as const):(['properties','dependencies','quick'] as const)).map(mode=><StudioButton key={mode} size="sm" aria-pressed={materialMode===mode} disabled={mode!=='properties'&&(!connected||!projectPath)} onClick={()=>setMaterialMode(mode)}>{mode==='properties'?'Properties':mode==='texture'?'Texture Graph':mode==='dependencies'?'Dependencies':'Quick texture'}</StudioButton>)}
      {!textureOnly&&<StudioButton size="sm" disabled={!connected||!projectPath} onClick={openTextureGraph}>Open Texture Graph</StudioButton>}
     </div>
     {connected&&projectPath&&<div hidden={materialMode==='properties'} className="materials-graph-host"><MaterialGraphPanel name={name} material={selected} draft={draft} projectPath={projectPath} disabled={busy||loading} onEdit={edit} mode={materialMode}/></div>}
     <div className="materials-fields" hidden={materialMode!=='properties'}>
      {props.map(field=><PropertyField key={name+':'+field.key} property={field}
       disabled={busy||!connected||!projectPath} onChange={v=>edit(field.key,v)}/>)}
     </div></>}
   </div>
  </div>
  <footer className="materials-footer">
   <span>{changes>0 ? changes+' pending PBR edits' : 'No pending edits'}</span>
   <button className="materials-preview" disabled={!connected||!projectPath||!name||changes>0||busy||previewing}
     title="Show the saved PBR factors on a GPU-rendered sphere"
     onClick={()=>void preview()}><Eye size={14}/> {previewMaterial===name?'Hide preview':'Preview material'}</button>
   {pending!==null&&<button className="materials-discard" onClick={discard}>Discard &amp; switch</button>}
   <button className="materials-discard" onClick={discard} disabled={changes===0}>Discard</button>
   <button className="materials-apply" disabled={busy||loading||changes===0||!connected||!projectPath||!names.includes(name)}
    onClick={()=>void apply()}><Save size={14}/> Apply &amp; save</button>
  </footer>
 </section>;
}
