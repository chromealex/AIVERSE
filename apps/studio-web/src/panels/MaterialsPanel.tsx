import { useEffect, useMemo, useRef, useState } from 'react';
import { Eye, Layers, RefreshCw, Save, Search, ShieldAlert } from 'lucide-react';
import { invoke, nativeConnected, onNativeEvent } from '../bridge';
import { MaterialRecipeFields, useMaterialRecipe } from '../components/MaterialRecipeFields';
import PropertyField from '../components/PropertyField';
import MaterialGraphPanel from '../graphs/MaterialGraphPanel';
import {StudioButton} from '../ui';
import { StudioLoader } from '../ui';
import { editablePatch, materialFields, materialNames, materialSwatchColor } from './materialSchema';
import type { MaterialData, MaterialKey, MaterialLibrary, MaterialPatch } from './materialSchema';
import { revealProjectAsset } from '../assetNavigation';
import './MaterialsPanel.css';

import SpectraDockViewport from './SpectraDockViewport';
const EMPTY:MaterialLibrary={library:{}};
export default function MaterialsPanel({documentEpoch,projectPath,previewMaterial,textureOnly=false,openTextureGraph}:{documentEpoch?:string;projectPath:string;previewMaterial?:string|null;textureOnly?:boolean;openTextureGraph?:()=>void}){
 const [library,setLibrary]=useState<MaterialLibrary>(EMPTY);
 const requestedFile=useRef<{project:string;path:string}|null>(null);
 const [name,setName]=useState('');
 const [query,setQuery]=useState('');
 const [newName,setNewName]=useState('');
 const recipe=useMaterialRecipe();
 const [creating,setCreating]=useState(false);
 const [draft,setDraft]=useState<MaterialPatch>({});
 const [pending,setPending]=useState<string|null>(null);
 const [loading,setLoading]=useState(true);
 const [busy,setBusy]=useState(false);
 const [targetExpanded,setTargetExpanded]=useState(false);
 const [previewing,setPreviewing]=useState(false);
 const [previewError,setPreviewError]=useState('');
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
  const unsubscribe=onNativeEvent((event,payload)=>{
   if(event==='material.previewError'&&payload&&typeof payload==='object'&&'message' in payload){
    setPreviewError(String(payload.message));return;
   }
   if(event==='material.preview'||event==='workspace.changed')setPreviewError('');
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
 const requestedPreview=useRef<string|null>(null);
 useEffect(()=>{requestedPreview.current=null;},[projectPath]);
 useEffect(()=>{
  if(previewMaterial===requestedPreview.current)requestedPreview.current=null;
 },[previewMaterial]);
 const selectMaterial=(next:string)=>{
  setName(next);
  if(textureOnly||!name||(previewMaterial!==name&&requestedPreview.current!==name))return;
  requestedPreview.current=next;
  void invoke('studio.materialPreview',{name:next}).catch(e=>{
   if(requestedPreview.current===next){requestedPreview.current=null;setError(String(e));}
  });
 };
 const switchTo=(next:string)=>{
  if(next===name)return;
  if(changes>0){setPending(next);setMessage('Open '+next+'? Save or discard the changes to '+name+', or keep editing.');return;}
  selectMaterial(next);setError('');setMessage('');setPending(null);setDraft({});
 };
 const edit=(key:string,value:string|number|boolean)=>{
  setDraft(current=>({...current,[key as MaterialKey]:value}));
  setMessage('');
 };

 const create=async()=>{
  if(busy||loading||!connected||!projectPath||!recipe.valid)return;
  if(changes>0){setError('Apply or discard the current material draft first.');return;}
  if(!/^[A-Za-z0-9_-]{1,64}$/.test(newName)){setError('Use 1–64 letters, digits, underscore or hyphen.');return;}
  setBusy(true);setError('');setMessage('');
  try{
   const response=await invoke('studio.materialCreate',{name:newName,...recipe.params});
   setQuery(current=>newName.toLowerCase().includes(current.toLowerCase())?current:'');
   setLibrary(response);selectMaterial(newName);setDraft({});setPending(null);setNewName('');setCreating(false);
   const file=response.files?.[newName];
   if(file?.startsWith('content/'))revealProjectAsset(projectPath,file.slice('content/'.length));
   setMessage(file ? 'Created '+file : 'Material created.');
  }catch(e){setError(e instanceof Error?e.message:String(e));}
  finally{setBusy(false);}
 };
 const apply=async()=>{
  if(!name||!names.includes(name)||changes===0||busy||!connected)return;
  setBusy(true);setError('');setMessage('');
  try {
   const response=await invoke('studio.materialPatch',{name,patch:editablePatch(draft)});
   setLibrary(response);setDraft({});
   if(pending!==null){selectMaterial(pending);setPending(null);}
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
  if(textureOnly || !projectPath)return;
  const key='studio.materialToOpen:'+projectPath;
  const open=(path:string|null)=>{
   if(!path)return;
   requestedFile.current={project:projectPath,path};
   const match=Object.entries(library.files??{}).find(([,file])=>file===path);
   if(match){
    requestedFile.current=null;
    if(localStorage.getItem(key)===path)localStorage.removeItem(key);
    setQuery(value=>match[0].toLowerCase().includes(value.toLowerCase())?value:'');
    setMaterialMode('properties');
    switchTo(match[0]);
   }
  };
  const onStorage=(event:StorageEvent)=>{if(event.key===key)open(event.newValue);};
  const onOpen=()=>open(localStorage.getItem(key));
  open(localStorage.getItem(key) ?? (requestedFile.current?.project===projectPath?requestedFile.current.path:null));
  window.addEventListener('studio:open-material',onOpen);
  window.addEventListener('storage',onStorage);
  return ()=>{
   window.removeEventListener('studio:open-material',onOpen);
   window.removeEventListener('storage',onStorage);
  };
 },[library,projectPath,changes,textureOnly,name,previewMaterial]);
 const preview=async()=>{
  if(busy||previewing||!connected||!projectPath||!name||changes>0)return;
  setPreviewing(true);setError('');
  try{
    await invoke('studio.materialPreview',{name:previewMaterial===name?'':name});
    setMessage(previewMaterial===name?'Material preview cleared.':'Material preview enabled.');
  }catch(e){setError(e instanceof Error?e.message:String(e));}
  finally{setPreviewing(false);}
 };
 const repair=async(publication:string,path:string)=>{
  if(busy||loading||!connected||!projectPath||!documentEpoch||changes>0)return;
  setBusy(true);setError('');
  try{
   const response=await invoke('studio.materialPublicationRepair',{name:publication,filename:path.split('/').pop()??'',expectedProject:projectPath,expectedDocumentEpoch:documentEpoch});
   if(!response.restored)throw Error('Metadata recovery rejected');
   setLibrary(current=>({...current,pendingPublications:response.pendingPublications}));
   setMessage('Original metadata restored. Refresh Assets after all missing sidecars are repaired.');
  }catch(e){setError(e instanceof Error?e.message:String(e))}
  finally{setBusy(false)}
 };
 const [rollbackReport,setRollbackReport]=useState<import('../types').StudioResult['studio.materialPublicationRollbackPreview']|null>(null);
 const rollback=async(publication:string,commit=false)=>{
  if(busy||loading||!connected||!projectPath||!documentEpoch||changes>0)return;
  setBusy(true);setError('');
  const args={name:publication,expectedProject:projectPath,expectedDocumentEpoch:documentEpoch};
  try{
   if(commit){
    const result=await invoke('studio.materialPublicationRollbackEmpty',args);
    if(!result.rolledBack)throw Error('Rollback rejected');
    setLibrary(current=>({...current,pendingPublications:result.pendingPublications}));
    setRollbackReport(null);setMessage('Empty publication journal archived; no assets removed.');
   }else setRollbackReport(await invoke('studio.materialPublicationRollbackPreview',args));
  }catch(e){setError(e instanceof Error?e.message:String(e))}
  finally{setBusy(false)}
 };
 const recoverPublication=async(action:'quarantine'|'restore'|'retry',id:string)=>{
  if(busy||loading||!connected||!projectPath||!documentEpoch||changes>0)return;
  setBusy(true);setError('');
  const scope={expectedProject:projectPath,expectedDocumentEpoch:documentEpoch};
  try{
   if(action==='retry')await invoke('studio.materialPublicationRetry',{...scope,name:id});
   else await invoke(action==='quarantine'?'studio.materialPublicationQuarantine':'studio.materialPublicationRestore',{...scope,id});
   setMessage(action==='retry'?'Publication retried using the recorded recipe and current style. Original stage retained in quarantine.':'Publication journal moved; no assets removed.');
  }catch(e){setError(e instanceof Error?e.message:String(e))}
  finally{setBusy(false);reload()}
 };
 const finalize=async(publication:string)=>{
  if(busy||loading||!connected||!projectPath||!documentEpoch||changes>0)return;
  setBusy(true);setError('');
  try{
   const response=await invoke('studio.materialPublicationFinalize',{name:publication,expectedProject:projectPath,expectedDocumentEpoch:documentEpoch});
   if(!response.finalized)throw Error('Publication finalization rejected');
   setLibrary(current=>({...current,pendingPublications:response.pendingPublications}));
   setMessage('Verified publication journal archived. Assets unchanged.');
  }catch(e){setError(e instanceof Error?e.message:String(e))}
  finally{setBusy(false)}
 };
 const discard=()=>{
  setDraft({});
  if(pending!==null){selectMaterial(pending);setPending(null);}
  setMessage('Draft discarded.');
 };
 const showTarget=!textureOnly||targetExpanded||!name||changes>0;
 return <section className="materials-editor">
  <div className="materials-heading">
   {textureOnly?<StudioButton size="sm" title={name} aria-expanded={showTarget} disabled={!name||changes>0}
    onClick={()=>setTargetExpanded(v=>!v)}>Target material · {name||'Choose material'} {showTarget?'▴':'▾'}</StudioButton>:<span><Layers size={16}/> Material Library</span>}
   <button className="icon-button" aria-label="Reload materials" title="Reload materials"
    disabled={busy||loading||changes>0} onClick={reload}><RefreshCw size={15}/></button>
  </div>
  {names.some(n=>!library.files?.[n])&&<button disabled={busy||changes>0} onClick={()=>void convert()}>Convert library to material files</button>}
  {showTarget&&<p className="materials-warning"><ShieldAlert size={15}/>
   Apply saves the material file. Map Undo applies to scene edits.</p>}
  {!connected&&<p className="materials-notice">DEMO — no native material library; editing is disabled.</p>}
  {connected&&!projectPath&&<p className="materials-notice">Open a project to view the material library.</p>}
  {error&&<p role="alert" className="materials-error">{error}</p>}
  {message&&<p role="status" className="materials-notice">{message}</p>}
  {name&&previewMaterial===name&&previewError&&<p role="alert" className="materials-error">{previewError}</p>}
  {name&&previewMaterial===name&&<SpectraDockViewport id="material" label="Material GPU preview"/>}
  {library.publicationInspectionError&&<p role="alert" className="materials-error">Publication inspection: {library.publicationInspectionError}</p>}
  {!!library.pendingPublications?.length&&<details className="materials-warning">
   <summary>Pending material publications ({library.pendingPublications.length})</summary>
   <p>Recovery inspection. Restore only missing metadata from recorded evidence. No files are removed automatically.</p>
   {library.pendingPublications.map(p=><div key={p.name}>
    <strong>{p.name}</strong> — {p.status==='material_committed'?'Material committed':p.status==='material_conflict'?'Material conflict':'Incomplete or active'}
    <StudioButton size="sm" disabled={busy||loading||!connected||!documentEpoch||changes>0} onClick={()=>void rollback(p.name)}>Inspect rollback</StudioButton>
    {rollbackReport?.name===p.name&&<div><small>{rollbackReport.blockers.length?rollbackReport.blockers.map(b=>b.kind+': '+b.owner).join(' · '):'No recorded blockers'}</small>{rollbackReport.journalOnlyRollbackAllowed&&<StudioButton size="sm" disabled={busy||loading||changes>0} onClick={()=>void rollback(p.name,true)}>Archive empty transaction</StudioButton>}<p>Scanned {rollbackReport.coverage?.scanned??0} files; skipped {rollbackReport.coverage?.skipped??0}.{rollbackReport.coverage?.truncated?' Scan incomplete.':''} Dynamic and binary dependencies are not verified. Content removal requires manual dependency review.</p></div>}
    {p.quarantinable&&<StudioButton size="sm" disabled={busy||loading||changes>0||!connected||!documentEpoch} onClick={()=>void recoverPublication('quarantine',p.name)}>Quarantine unpublished stage</StudioButton>}
    {p.retryable&&<StudioButton size="sm" disabled={busy||loading||changes>0||!connected||!documentEpoch} onClick={()=>void recoverPublication('retry',p.name)}>Regenerate using recorded recipe</StudioButton>}
    {p.finalizable&&<StudioButton size="sm" disabled={busy||loading||!connected||!documentEpoch||changes>0} onClick={()=>void finalize(p.name)}>Finalize verified publication</StudioButton>}
    {p.assets.map(a=><div key={a.path}>{a.path}: {!a.present?'missing texture':!a.metadataPresent?'missing metadata':!a.guidMatches?'GUID mismatch':a.hashMatches===false?'texture hash mismatch':a.hashMatches===true?'texture, hash and GUID match':'texture and GUID present; hash unverified'}{a.recoverable&&<StudioButton size="sm" disabled={busy||loading||!connected||!documentEpoch||changes>0} onClick={()=>void repair(p.name,a.path)}>Restore original metadata</StudioButton>}</div>)}
   </div>)}
  </details>}
  {!!library.quarantinedPublications?.length&&<details className="materials-warning"><summary>Quarantined material publications ({library.quarantinedPublications.length})</summary>{library.quarantinedPublications.map(p=><div key={p.id}>{p.id}{p.reason&&<small> — {p.reason}</small>}{p.restorable&&<StudioButton size="sm" disabled={busy||loading||changes>0||!connected||!documentEpoch} onClick={()=>void recoverPublication('restore',p.id)}>Restore to pending</StudioButton>}</div>)}</details>}
  <div className="materials-main">
   {showTarget&&<aside className="materials-list">
    <button className="materials-new-button" disabled={!connected||!projectPath||busy}
      onClick={()=>setCreating(value=>!value)}>{creating?'Cancel new material':'+ New material'}</button>
    {creating&&<form className="materials-new-form" onSubmit={event=>{event.preventDefault();void create();}}>
      <label>Name<input autoFocus aria-label="New material name" value={newName}
        placeholder="matte_stone" maxLength={64} onChange={e=>setNewName(e.target.value)}/></label>
      <MaterialRecipeFields recipe={recipe} busy={busy}/>
      {changes>0&&<small>Save or discard your edits to {name} before creating this material.</small>}
      <button type="submit" disabled={busy||!newName||!recipe.valid||changes>0}>Create and save</button>
    </form>}
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
   </aside>}
   <div className={'materials-detail'+(materialMode==='texture'?' materials-detail--graph':'')}>
    {!name||!names.includes(name)?<div className="materials-empty">{name?'Selected material is no longer present. Discard the draft and choose another material.':'Select a material from the library.'}</div>:
     <>{showTarget&&<div className="materials-selected">
      <span className="materials-swatch" style={{background:materialSwatchColor(selected)}}/>
      <div className="materials-file-info"><strong>{name}</strong>
       {library.files?.[name]&&<small>{library.files[name]}</small>}
      </div>{library.files?.[name]&&<button title={library.files[name]} onClick={()=>{
       revealProjectAsset(projectPath, library.files![name].slice('content/'.length));
       window.dispatchEvent(new CustomEvent('studio:show-panel',{detail:'assets'}));
      }}>Show in Assets</button>}
     </div>}
     {!textureOnly&&<div className="materials-modes" role="group" aria-label="Material workspace">
      {(['properties','dependencies','quick'] as const).map(mode=><StudioButton key={mode} size="sm" aria-pressed={materialMode===mode} disabled={mode!=='properties'&&(!connected||!projectPath)} onClick={()=>setMaterialMode(mode)}>{mode==='properties'?'Properties':mode==='dependencies'?'Dependencies':'Quick texture'}</StudioButton>)}
      <StudioButton size="sm" disabled={!connected||!projectPath} onClick={openTextureGraph}>Open Texture Graph</StudioButton>
     </div>}
     {connected&&projectPath&&<div hidden={materialMode==='properties'} className="materials-graph-host"><MaterialGraphPanel documentEpoch={documentEpoch} name={name} material={selected} draft={draft} projectPath={projectPath} disabled={busy||loading} onEdit={edit} onOpenMaterial={base=>{
       if(!names.includes(base)){setError('Base material not found: '+base);return;}
       switchTo(base);
     }} mode={materialMode}/></div>}
     <div className="materials-fields" hidden={materialMode!=='properties'}>
      {props.map(field=><PropertyField key={name+':'+field.key} property={field}
       disabled={busy||!connected||!projectPath} onChange={v=>edit(field.key,v)}/>)}
     </div></>}
   </div>
  </div>
  {showTarget&&<footer className="materials-footer">
   <span>{changes>0 ? changes+' pending PBR edits' : 'No pending edits'}</span>
   <button className="materials-preview" disabled={!connected||!projectPath||!name||changes>0||busy||previewing}
     title="Preview the saved material on a GPU-rendered sphere"
     onClick={()=>void preview()}><Eye size={14}/> {previewMaterial===name?'Hide preview':'Preview material'}</button>
   {pending!==null&&<button className="materials-discard" disabled={busy} onClick={()=>{setPending(null);setMessage('');}}>Keep editing</button>}
   <button className="materials-discard" onClick={discard} disabled={busy||changes===0}>{pending===null?'Discard':'Discard & switch'}</button>
   <button className="materials-apply" disabled={busy||loading||changes===0||!connected||!projectPath||!names.includes(name)}
    onClick={()=>void apply()}><Save size={14}/> {pending===null?'Apply & save':'Save & switch'}</button>
  </footer>}
 </section>;
}
