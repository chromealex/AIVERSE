import { useEffect, useMemo, useState } from 'react';
import { Eye, Layers, RefreshCw, Save, Search, ShieldAlert } from 'lucide-react';
import { invoke, nativeConnected, onNativeEvent } from '../bridge';
import PropertyField from '../components/PropertyField';
import { StudioLoader } from '../ui';
import { editablePatch, materialFields, materialNames, materialSwatchColor, MATERIAL_PRESETS } from './materialSchema';
import type { MaterialData, MaterialKey, MaterialLibrary, MaterialPatch, MaterialPreset } from './materialSchema';
import './MaterialsPanel.css';

const EMPTY:MaterialLibrary={library:{}};
export default function MaterialsPanel({projectPath,previewMaterial}:{projectPath:string;previewMaterial?:string|null}){
 const [library,setLibrary]=useState<MaterialLibrary>(EMPTY);
 const [name,setName]=useState('');
 const [query,setQuery]=useState('');
 const [newName,setNewName]=useState('');
 const [newPreset,setNewPreset]=useState<MaterialPreset>('matte');
 const [creating,setCreating]=useState(false);
 const [draft,setDraft]=useState<MaterialPatch>({});
 const [pending,setPending]=useState<string|null>(null);
 const [loading,setLoading]=useState(true);
 const [busy,setBusy]=useState(false);
 const [previewing,setPreviewing]=useState(false);
 const [error,setError]=useState('');
 const [message,setMessage]=useState('');
 const [nonce,setNonce]=useState(0);
 const connected=nativeConnected();
 const names=useMemo(()=>materialNames(library),[library]);
 const selected:MaterialData=library.library[name]??{};
 const changes=Object.keys(draft).length;
 const props=useMemo(()=>materialFields(selected,draft),[selected,draft]);
 const reload=()=>setNonce(n=>n+1);

 useEffect(()=>onNativeEvent(event=>{
  if(event==='workspace.changed')reload();
 }),[]);
 useEffect(()=>{
  let canceled=false;
  setLoading(true);setError('');setMessage('');setDraft({});setPending(null);
  if(connected&&!projectPath){
   setLibrary(EMPTY);setName('');setLoading(false);return;
  }
  void invoke('studio.materials',{}).then(data=>{
   if(canceled)return;
   setLibrary(data);
   const all=materialNames(data);
   setName(previous=>all.includes(previous)?previous:(all[0]??''));
  }).catch(e=>{
   if(!canceled)setError(e instanceof Error?e.message:String(e));
  }).finally(()=>{if(!canceled)setLoading(false)});
  return ()=>{canceled=true};
 },[projectPath,nonce,connected]);
 const switchTo=(next:string)=>{
  if(next===name)return;
  if(changes>0){setPending(next);setMessage('Unsaved draft. Apply or discard changes to switch materials.');return;}
  setName(next);setError('');setMessage('');setPending(null);setDraft({});
 };
 const edit=(key:string,value:string|number|boolean)=>{
  setDraft(current=>({...current,[key as MaterialKey]:value}));
  setMessage('');
 };
 const create=async()=>{
  if(busy||loading||!connected||!projectPath)return;
  if(changes>0){setError('Apply or discard the current material draft first.');return;}
  if(!/^[A-Za-z0-9_-]{1,64}$/.test(newName)){setError('Use 1–64 letters, digits, underscore or hyphen.');return;}
  setBusy(true);setError('');setMessage('');
  try{
   const response=await invoke('studio.materialCreate',{name:newName,preset:newPreset});
   setLibrary(response);setName(newName);setDraft({});setPending(null);setNewName('');setCreating(false);
   setMessage('New material saved to materials.json. MapDoc Undo does not include this edit.');
  }catch(e){setError(e instanceof Error?e.message:String(e));}
  finally{setBusy(false);}
 };
 const apply=async()=>{
  if(!name||changes===0||busy||!connected)return;
  setBusy(true);setError('');setMessage('');
  try {
   const response=await invoke('studio.materialPatch',{name,patch:editablePatch(draft)});
   setLibrary(response);setDraft({});setPending(null);
   setMessage('Saved to materials.json. MapDoc Undo does not include this edit.');
  } catch(e){setError(e instanceof Error?e.message:String(e));}
  finally{setBusy(false);}
 };
 const preview=async()=>{
  if(busy||previewing||!connected||!projectPath||!name||changes>0)return;
  setPreviewing(true);setError('');
  try{
    await invoke('studio.materialPreview',{name:previewMaterial===name?'':name});
    setMessage(previewMaterial===name?'Material preview cleared.':'PBR sphere selected in Scene view (textures/graphs excluded).');
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
   <span><Layers size={16}/> Material Library</span>
   <button className="icon-button" aria-label="Reload materials" title="Reload materials"
    disabled={busy||loading||changes>0} onClick={reload}><RefreshCw size={15}/></button>
  </div>
  <p className="materials-warning"><ShieldAlert size={15}/>
   Apply writes directly to materials.json. The MapDoc Undo stack cannot revert these edits.</p>
  {!connected&&<p className="materials-notice">DEMO — no native material library; editing is disabled.</p>}
  {connected&&!projectPath&&<p className="materials-notice">Open a project to view the material library.</p>}
  {error&&<p role="alert" className="materials-error">{error}</p>}
  {message&&<p role="status" className="materials-notice">{message}</p>}
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
      <button disabled={busy||!newName||changes>0} onClick={()=>void create()}>Create and save</button>
    </div>}
    <label className="materials-search"><Search size={14}/>
     <input placeholder="Filter materials…" aria-label="Filter materials"
      value={query} onChange={e=>setQuery(e.target.value)}/>
    </label>
    {loading?<p className="materials-empty"><StudioLoader size={30} label="Loading…"/></p>:
     names.length===0?<p className="materials-empty">No library materials. Create one with the native editor.</p>:
     names.filter(n=>n.toLowerCase().includes(query.toLowerCase())).map(n=>
      <button key={n} type="button" aria-pressed={name===n}
       className={'materials-list-item'+(n===name?' active':'')} onClick={()=>switchTo(n)}>
       <span className="material-chip" style={{background:materialSwatchColor(library.library[n])}}/>
       <span title={n}>{n}</span>
      </button>)
    }
   </aside>
   <div className="materials-detail">
    {!name?<div className="materials-empty">Select a material from the library.</div>:
     <><div className="materials-selected">
      <span className="materials-swatch" style={{background:materialSwatchColor(selected)}}/>
      <strong>{name}</strong><small>Library PBR factors</small>
     </div>
     <div className="materials-fields">
      {props.map(field=><PropertyField key={name+':'+field.key} property={field}
       disabled={busy||!connected||!projectPath} onChange={v=>edit(field.key,v)}/>)}
     </div></>}
   </div>
  </div>
  <footer className="materials-footer">
   <span>{changes>0 ? changes+' pending PBR edits' : 'No pending edits'}</span>
   <button className="materials-preview" disabled={!connected||!projectPath||!name||changes>0||busy||previewing}
     title="Preview basic PBR factors on the native Scene GPU sphere; texture and shader graph preview not supported"
     onClick={()=>void preview()}><Eye size={14}/> {previewMaterial===name?'Exit PBR preview':'Preview in Scene'}</button>
   {pending!==null&&<button className="materials-discard" onClick={discard}>Discard &amp; switch</button>}
   <button className="materials-discard" onClick={discard} disabled={changes===0}>Discard</button>
   <button className="materials-apply" disabled={busy||loading||changes===0||!connected||!projectPath}
    onClick={()=>void apply()}><Save size={14}/> Apply &amp; save</button>
  </footer>
 </section>;
}
