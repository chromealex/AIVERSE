import { copyText } from '../clipboard';
import type React from 'react';
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import type { DragEvent } from 'react';
import {
  FileAudio, Box, FileCode2, FileImage, FileText, Folder, FolderOpen,
  Grid2X2, List, RefreshCw, Search, ChevronRight, Library, Upload, FolderPlus, X, Link2, Unlink, Trash2
} from 'lucide-react';
import { invoke, nativeConnected, onNativeEvent } from '../bridge';
import type {
  StudioAsset, StudioAssetList, StudioAssetTransferPlan,
  StudioAssetConflictAction, StudioAssetTransferItem, StudioDependencyPage
} from '../types';
import AssetThumb from './AssetThumb';
import { assetKind, assetFolders, assetFileName, assetBreadcrumbs,
  childFolders, visibleAssetFiles, safeAssetPath } from '../assetTree';
import type { AssetFolder, AssetKind } from '../assetTree';
import './AssetsPanel.css';
import AssetImportSettings from './AssetImportSettings';
import AssetTrashDialog from './AssetTrashDialog';
import { StudioContextMenu, type StudioMenuItem } from '../ui';

const ASSET_DRAG_MIME='application/x-aiverse-content-path';
const EMPTY:StudioAssetList={root:'content',files:[],folders:[]};
const ACTIONS: Array<{value:StudioAssetConflictAction;label:string}> = [
  {value:'skip',label:'Skip'},
  {value:'keep-both',label:'Keep both (new name + GUID)'},
  {value:'replace',label:'Replace file (preserve target GUID)'},
  {value:'new-guid',label:'Import with new GUID'},
  {value:'import',label:'Import'}
];
function formatBytes(bytes:number):string{
  if(bytes<1024)return Math.max(0,bytes).toLocaleString()+' B';
  if(bytes<1024*1024)return (bytes/1024).toFixed(1)+' KB';
  return (bytes/(1024*1024)).toFixed(1)+' MB';
}
function AssetIcon({kind,size=27}:{kind:AssetKind;size?:number}){
  switch(kind){
    case 'image':return <FileImage size={size}/>;
    case 'model':return <Box size={size}/>;
    case 'audio':return <FileAudio size={size}/>;
    case 'code':return <FileCode2 size={size}/>;
    default:return <FileText size={size}/>;
  }
}
function choicesFor(item:StudioAssetTransferItem,source:StudioAssetTransferPlan['source']){
  const collision=item.conflict;
  if(!collision)return ACTIONS.filter(a=>a.value==='import'||a.value==='skip');
  return ACTIONS.filter(a=>{
    if(a.value==='skip'||a.value==='keep-both')return true;
    if(a.value==='replace')return source==='external' && !item.directory && item.replacementAvailable===true;
    if(a.value==='new-guid')return source==='external' && !item.directory && !collision.includes('name');
    return false;
  });
}
function PreviewDialog({plan,busy,onCancel,onCommit}:{
  plan:StudioAssetTransferPlan;
  busy:boolean;
  onCancel:()=>void;
  onCommit:(decisions:Array<{id:number;action:StudioAssetConflictAction}>)=>void;
}){
  const [selected,setSelected]=useState<Record<number,StudioAssetConflictAction>>(()=>Object.fromEntries(
    plan.items.map(item=>[item.id,item.conflict?'skip':'import'])));
  const pending=plan.items.filter(item=>selected[item.id]!=='skip').length;
  const mixedReplacement=plan.items.some(item=>selected[item.id]==='replace')&&plan.items.some(item=>selected[item.id]!=='replace'&&selected[item.id]!=='skip');
  const blocked=(plan.references??[]).filter(ref=>!ref.writable&&!ref.validateOnApply&&selected[ref.id]!=='skip');
  const resourceChecks=(plan.references??[]).filter(ref=>ref.validateOnApply&&selected[ref.id]!=='skip');
  return <div className="studio-asset-dialog-overlay">
    <section className="studio-asset-dialog" role="dialog" aria-modal="true"
      aria-label="Resolve asset import and move conflicts">
      <header>
        <div><h3>{plan.source==='external'?'Import into content':'Move content assets'}</h3>
          <p>Review paths and resolve filename or GUID conflicts before applying changes.</p></div>
        <button type="button" aria-label="Close transfer preview" disabled={busy}
          onClick={onCancel}><X size={18}/></button>
      </header>
      <div className="studio-asset-conflicts">
        {plan.items.map(item=><div className="studio-asset-conflict" key={item.id}>
          <div className="studio-asset-conflict-info">
            <strong title={item.from}>{item.name}</strong>
            <span title={item.to}>content/{item.to}</span>
            {item.conflict ?
              <small className="studio-asset-conflict-warning">Conflict: {item.conflict.replaceAll('+',' + ')}</small> :
              <small>Ready · no conflict</small>}
            {(plan.references??[]).filter(ref=>ref.id===item.id).map((ref,index)=>
              <small key={index} className={ref.writable?'':'studio-asset-conflict-warning'}>
                {ref.writable?'Will repair material reference: ':ref.validateOnApply?(ref.preservesDefault?'Default move preserves resource; final choices rechecked: ':ref.repairCandidate?'Will attempt transactional resource path repair: ':'Resource must remain resolvable; final choices checked: '):'Blocked: read-only reference: '}
                {ref.document} / {ref.owner} / {ref.field}
              </small>)}
          </div>
          <label className="studio-asset-conflict-choice">
            <span>Action</span>
            <select disabled={busy} value={selected[item.id]||'skip'}
              onChange={event=>setSelected(current=>({...current,
                [item.id]:event.target.value as StudioAssetConflictAction}))}>
              {choicesFor(item,plan.source).map(choice=>
                <option value={choice.value} key={choice.value}>{choice.label}</option>)}
            </select>
          </label>
        </div>)}
      </div>
      <footer>
        <span>{pending} of {plan.items.length} selected
          {mixedReplacement?' · Apply replacements separately from new imports.':''}
          {blocked.length>0?' · '+blocked.length+' read-only reference(s) block this move':''}
          {resourceChecks.length>0?' · Native resource checks before commit':''}</span>
        <button type="button" disabled={busy} onClick={onCancel}>Cancel</button>
        <button type="button" disabled={busy||blocked.length>0||mixedReplacement} className="studio-asset-confirm"
          onClick={()=>onCommit(plan.items.map(item=>({id:item.id,action:selected[item.id]||'skip'})))}>
          {busy?'Applying…':'Apply decisions'}
        </button>
      </footer>
    </section>
  </div>;
}
function allFolders(catalog:StudioAssetList):AssetFolder[]{
  const known=new Map(assetFolders(catalog.files).map(f=>[f.path,f]));
  for(const asset of catalog.folders??[]){
    const path=safeAssetPath(asset.path);
    if(!path)continue;
    const segments=path.split('/');
    for(let depth=1;depth<=segments.length;depth++){
      const partial=segments.slice(0,depth).join('/');
      if(!known.has(partial))known.set(partial,{
        path:partial,name:segments[depth-1],count:0,depth:depth-1
      });
    }
  }
  return [...known.values()].sort((a,b)=>a.path.localeCompare(b.path));
}
/** Assets are always relative to project/content; no arbitrary JS filesystem IO. */
export default function AssetsPanel({projectPath}:{projectPath:string}){
  const [catalog,setCatalog]=useState<StudioAssetList>(EMPTY);
  const [folder,setFolder]=useState('');
  const [selected,setSelected]=useState<string|null>(null);
  const [pinnedAssetGuid,setPinnedAssetGuid]=useState<string|null>(null);
  const [search,setSearch]=useState('');
  // Usage: files no project data names (filter) and "Find References" for one asset.
  const [unusedOnly,setUnusedOnly]=useState(false);
  const [unused,setUnused]=useState<Set<string>|null>(null);
  const [mentions,setMentions]=useState<{path:string;project:string;rows:Array<{file:string;line:number;text:string}>;graph:StudioDependencyPage|null;graphError:string}|null>(null);
  const [referencePage,setReferencePage]=useState(0);
  const [referenceDirection,setReferenceDirection]=useState<'incoming'|'outgoing'>('incoming');
  const referenceRequest=useRef(0);
  const [referenceLoading,setReferenceLoading]=useState(false);
  const findReferences=async(path:string)=>{
    const request=++referenceRequest.current;
    setReferenceLoading(true);
    const [usage,graph]=await Promise.allSettled([invoke('studio.assetUsage',{path}),invoke('studio.assetDependencyPage',{expectedProject:projectPath,path,depth:16,direction:'incoming',offset:0,limit:50})]);
    if(request!==referenceRequest.current)return;
    setReferencePage(0);setReferenceDirection('incoming');setReferenceLoading(false);
    if(usage.status==='rejected')setError(String(usage.reason));
    setMentions({path,project:projectPath,rows:usage.status==='fulfilled'?usage.value.mentions??[]:[],
      graph:graph.status==='fulfilled'?graph.value:null,graphError:graph.status==='rejected'?String(graph.reason):''});
  };
  const loadReferencePage=async(direction:'incoming'|'outgoing',page:number)=>{
    if(!mentions?.graph||referenceLoading)return;
    const request=++referenceRequest.current, snapshot=mentions;
    setReferenceLoading(true);
    try{
      const graph=await invoke('studio.assetDependencyPage',{expectedProject:projectPath,path:snapshot.path,depth:16,direction,offset:page*50,limit:50,expectedToken:snapshot.graph!.token});
      if(request===referenceRequest.current){setMentions({...snapshot,graph,graphError:''});setReferenceDirection(direction);setReferencePage(page);}
    }catch(e){if(request===referenceRequest.current)setMentions({...snapshot,graphError:String(e)});}
    finally{if(request===referenceRequest.current)setReferenceLoading(false);}
  };
  const toggleUnused=async()=>{
    if(unusedOnly){setUnusedOnly(false);return;}
    try{const r=await invoke('studio.assetUsage',{});setUnused(new Set(r.unused??[]));setUnusedOnly(true);}
    catch(e){setError(String(e instanceof Error?e.message:e))}
  };
  const [mode,setMode]=useState<'grid'|'list'>('grid');
  const [menu,setMenu]=useState<{x:number;y:number;path:string;directory:boolean;background?:boolean}|null>(null);
  const [creatingMaterial,setCreatingMaterial]=useState(false);
  const [materialName,setMaterialName]=useState('');
  const [materialPreset,setMaterialPreset]=useState<'matte'|'metal'|'glass'>('matte');
  const [error,setError]=useState('');
  const [message,setMessage]=useState('');
  const [loading,setLoading]=useState(true);
  const [busy,setBusy]=useState(false);
  const [addingFolder,setAddingFolder]=useState(false);
  const [folderName,setFolderName]=useState('');
  const [trashOpen,setTrashOpen]=useState(false);
  const [renameTarget,setRenameTarget]=useState<string|null>(null);
  const [renameName,setRenameName]=useState('');
  const [deleteTarget,setDeleteTarget]=useState<{path:string;guid:string;project:string;version:string}|null>(null);
  const [plan,setPlan]=useState<StudioAssetTransferPlan|null>(null);
  const [refreshToken,setRefreshToken]=useState(0);
  // Folder tree width: drag the divider (persisted), double-click resets.
  const [treeWidth,setTreeWidth]=useState<number>(()=>{
    try{const v=Number(window.localStorage.getItem('aiverse.studio.assets.treeWidth'));return Number.isFinite(v)&&v>=110&&v<=900?v:170;}catch{return 170;}
  });
  useEffect(()=>{try{window.localStorage.setItem('aiverse.studio.assets.treeWidth',String(Math.round(treeWidth)));}catch{/* optional */}},[treeWidth]);
  const startTreeResize=(event:React.PointerEvent<HTMLDivElement>)=>{
    if(event.button!==0)return;
    event.preventDefault();
    const handle=event.currentTarget;handle.setPointerCapture(event.pointerId);
    const layout=handle.parentElement?.getBoundingClientRect();
    const startX=event.clientX,start=treeWidth;
    const move=(e:PointerEvent)=>{
      const max=Math.max(160,(layout?.width??800)-160);
      setTreeWidth(Math.min(max,Math.max(110,start+e.clientX-startX)));
    };
    const up=()=>{handle.removeEventListener('pointermove',move);handle.removeEventListener('pointerup',up);handle.removeEventListener('pointercancel',up);};
    handle.addEventListener('pointermove',move);handle.addEventListener('pointerup',up);handle.addEventListener('pointercancel',up);
  };
  const native=nativeConnected();
  const reload=useCallback(()=>setRefreshToken(t=>t+1),[]);
  useEffect(()=>onNativeEvent((event,payload)=>{
    if(event==='workspace.changed'||event==='assets.changed')reload();
    if(event==='assets.importPreview' && payload &&
       typeof payload==='object' && 'token' in payload && 'items' in payload){
      const candidate=payload as StudioAssetTransferPlan;
      if(typeof candidate.token==='string' && Array.isArray(candidate.items)){
        setError('');setMessage('');setPlan(candidate);
      }
    }
    if(event==='assets.importError' && payload &&
       typeof payload==='object' && 'message' in payload){
      setError(String(payload.message));
    }
  }),[reload]);
  useEffect(()=>{setFolder('');setSelected(null);setPinnedAssetGuid(null);setSearch('');setPlan(null);setRenameTarget(null);setDeleteTarget(null);setTrashOpen(false);setMentions(null);setReferenceLoading(false);++referenceRequest.current;},[projectPath]);
  // Command palette (# assets) reveals a file: open its folder and select it.
  useEffect(()=>{
    const reveal=(event:Event)=>{
      const path=(event as CustomEvent<{path?:unknown}>).detail?.path;
      if(typeof path!=='string'||!path)return;
      const cut=path.lastIndexOf('/');
      setSearch('');setFolder(cut>0?path.slice(0,cut):'');setSelected(path);
    };
    window.addEventListener('studio:reveal-asset',reveal);
    return ()=>window.removeEventListener('studio:reveal-asset',reveal);
  },[]);
  useEffect(()=>{
    let cancelled=false;
    setLoading(true);setError('');
    if(native&&!projectPath){setCatalog(EMPTY);setLoading(false);return;}
    void invoke('studio.assets',{}).then(result=>{
      if(!cancelled)setCatalog(result);
    }).catch(e=>{
      if(!cancelled)setError(e instanceof Error?e.message:String(e));
    }).finally(()=>{if(!cancelled)setLoading(false)});
    return ()=>{cancelled=true};
  },[projectPath,refreshToken,native]);

  const folders=useMemo(()=>allFolders(catalog),[catalog]);
  const shownFolders=useMemo(()=>search.trim()?[]:childFolders(folders,folder),[folders,folder,search]);
  const shownFiles=useMemo(()=>{
    // "Unused" lists matching files of the whole project, wherever the folder is.
    if(unusedOnly&&unused)return catalog.files.filter(f=>unused.has(f.path)&&(!search||f.path.toLowerCase().includes(search.toLowerCase())));
    return visibleAssetFiles(catalog.files,folder,search);
  },[catalog.files,folder,search,unusedOnly,unused]);
  const crumbs=useMemo(()=>assetBreadcrumbs(folder),[folder]);
  // Asset identities survive renames/moves; a pinned Import Settings panel
  // must follow GUID, never a stale path or a new external selection.
  const selectedAsset=useMemo(()=>pinnedAssetGuid
    ? catalog.files.find(a=>a.guid===pinnedAssetGuid)
    : catalog.files.find(a=>a.path===selected),
    [catalog,pinnedAssetGuid,selected]);
  const toggleAssetPin=()=>setPinnedAssetGuid(current=>current ? null : selectedAsset?.guid ?? null);
  useEffect(()=>{
    if (pinnedAssetGuid && !loading && !catalog.files.some(a=>a.guid===pinnedAssetGuid))
      setPinnedAssetGuid(null);
  },[catalog,pinnedAssetGuid,loading]);

  const goTo=(path:string)=>{setFolder(path);setSearch('');setSelected(null)};
  const createFolder=async()=>{
    if(!native||!projectPath||!folderName.trim()||busy)return;
    setBusy(true);setError('');setMessage('');
    try{
      const result=await invoke('studio.assetFolderCreate',{parent:folder,name:folderName.trim()});
      setAddingFolder(false);setFolderName('');setMessage('Created content/'+result.path);
      reload();
    }catch(e){setError(String(e instanceof Error?e.message:e))}
    finally{setBusy(false)}
  };
  const externalImport=async()=>{
    if(!native||!projectPath||busy)return;
    setBusy(true);setError('');setMessage('');
    try{
      const result=await invoke('studio.assetImportPreview',{destination:folder});
      if(!('cancelled' in result))setPlan(result);
    }catch(e){setError(String(e instanceof Error?e.message:e))}
    finally{setBusy(false)}
  };
  const moveAssets=async(paths:string[],destination:string)=>{
    if(!native||!projectPath||busy)return;
    setBusy(true);setError('');setMessage('');
    try{
      setPlan(await invoke('studio.assetMovePreview',{paths,destination}));
    }catch(e){setError(String(e instanceof Error?e.message:e))}
    finally{setBusy(false)}
  };
  const reviewDelete=async(path:string)=>{
    if(!native||!projectPath||busy)return;
    const entry=[...catalog.files,...(catalog.folders??[])].find(item=>item.path===path);
    if(!entry?.guid){setError('Refresh Assets before deleting');return;}
    setBusy(true);setError('');
    try{
      const h=await invoke('studio.assetHistory',{});
      if(!h.version||h.strokeActive)throw new Error('Finish the active stroke and refresh asset history');
      setDeleteTarget({path,guid:entry.guid,project:projectPath,version:h.version});
    }catch(e){setError(String(e instanceof Error?e.message:e))}
    finally{setBusy(false)}
  };
  const deleteAsset=async()=>{
    if(!deleteTarget||busy||deleteTarget.project!==projectPath)return;
    setBusy(true);setError('');setMessage('');
    try{
      const r=await invoke('studio.assetDelete',{path:deleteTarget.path,guid:deleteTarget.guid,
        expectedProject:deleteTarget.project,expectedVersion:deleteTarget.version,confirmed:true});
      setDeleteTarget(null);setSelected(null);setPinnedAssetGuid(null);
      setMessage('Moved to recovery storage: '+r.path+(r.historyRecorded?' · Undo in History / Assets':' · Recovery: '+r.backup));
      reload();
    }catch(e){setDeleteTarget(null);setError(String(e instanceof Error?e.message:e))}
    finally{setBusy(false)}
  };
  const previewRename=async()=>{
    if(!native||!projectPath||busy||!renameTarget||!renameName.trim())return;
    setBusy(true);setError('');setMessage('');
    try{
      const result=await invoke('studio.assetMovePreview',{paths:[renameTarget],destination:'',newName:renameName.trim()});
      setRenameTarget(null);setPlan(result);
    }catch(e){setError(String(e instanceof Error?e.message:e))}
    finally{setBusy(false)}
  };
  const applyTransfer=async(decisions:Array<{id:number;action:StudioAssetConflictAction}>)=>{
    if(!plan||busy)return;
    setBusy(true);setError('');
    try{
      const response=await invoke('studio.assetTransferCommit',{token:plan.token,decisions});
      setPlan(null);setSelected(null);
      setMessage(response.files.length+' asset(s) processed'+
        (response.repairedMaterialReferences
          ? ' · '+response.repairedMaterialReferences+' material reference(s) repaired' : '')+
        (response.repairedResourceDocuments?' · '+response.repairedResourceDocuments+' model document(s) repaired':'')+
        (response.issues.length?' · '+response.issues.length+' issue(s)':'')+
        (response.files.length&&response.historyRecorded===false?' · Not retained in asset history':''));
      if(response.issues.length)setError(response.issues.map(x=>x.name+': '+x.message).join('; '));
      reload();
    }catch(e){setError(String(e instanceof Error?e.message:e))}
    finally{setBusy(false)}
  };
  const dragStart=(event:DragEvent<HTMLElement>,path:string)=>{
    event.dataTransfer.setData(ASSET_DRAG_MIME,path);
    // The native Scene view accepts plain text "aiverse-asset:<path>" (models → new entity).
    if(assetKind(path)==='model')event.dataTransfer.setData('text/plain','aiverse-asset:'+path);
    event.dataTransfer.effectAllowed='copyMove';
  };
  const acceptInternal=(event:DragEvent<HTMLElement>)=>{
    if(event.dataTransfer.types.includes(ASSET_DRAG_MIME)){
      event.preventDefault();event.dataTransfer.dropEffect='move';
    }
  };
  const dropInternal=(event:DragEvent<HTMLElement>,destination:string)=>{
    const path=event.dataTransfer.getData(ASSET_DRAG_MIME);
    if(!path)return;
    event.preventDefault();event.stopPropagation();
    void moveAssets([path],destination);
  };
  const folderDropProps=(target:string)=>({
    'data-content-drop-target':target,
    onDragOver:acceptInternal,
    onDrop:(event:DragEvent<HTMLElement>)=>dropInternal(event,target)
  });

  const copyPath=(path:string)=>{
    const text='content/'+path;
    void copyText(text).then(ok=>ok?setMessage('Copied '+text):setError('Clipboard unavailable'));
  };
  const createMaterial=async()=>{
    const name=materialName.trim();
    if(!native||!projectPath||!name||busy)return;
    setBusy(true);setError('');setMessage('');
    try{
      await invoke('studio.materialCreate',{name,preset:materialPreset});
      setCreatingMaterial(false);setMaterialName('');
      setMessage('Created material "'+name+'" in materials.json');
    }catch(e){setError(String(e instanceof Error?e.message:e))}
    finally{setBusy(false)}
  };
  // Right-click on empty space: create/import into the current folder.
  const backgroundItems=():StudioMenuItem[]=>[
    {id:'folder',label:'Create Folder',icon:<FolderPlus size={13}/>,disabled:!native||!projectPath||busy,onSelect:()=>setAddingFolder(true)},
    {id:'material',label:'Create Material…',icon:<Library size={13}/>,disabled:!native||!projectPath||busy,
      onSelect:()=>{setCreatingMaterial(true);setMaterialName('')}},
    {id:'import',label:'Import Assets…',icon:<Upload size={13}/>,separator:true,disabled:!native||!projectPath||busy,onSelect:()=>void externalImport()},
    {id:'copy',label:'Copy Folder Path',icon:<Search size={13}/>,separator:true,onSelect:()=>copyPath(folder)},
    {id:'reload',label:'Reload',icon:<RefreshCw size={13}/>,onSelect:reload},
  ];
  const menuItems=(target:{path:string;directory:boolean}):StudioMenuItem[]=>[
    target.directory
      ?{id:'open',label:'Open Folder',icon:<FolderOpen size={13}/>,onSelect:()=>goTo(target.path)}
      :{id:'settings',label:'Import Settings',icon:<Library size={13}/>,disabled:!native,onSelect:()=>setSelected(target.path)},
    {id:'delete',label:'Move to Trash…',disabled:!native||!projectPath||busy,onSelect:()=>void reviewDelete(target.path)},
    {id:'rename',label:'Rename…',disabled:!native||!projectPath||busy,onSelect:()=>{setRenameTarget(target.path);setRenameName(assetFileName(target.path));}},
    {id:'copy',label:'Copy Path',icon:<Search size={13}/>,onSelect:()=>copyPath(target.path)},
    {id:'refs',label:'Find References',icon:<Link2 size={13}/>,disabled:!native||!projectPath,
      onSelect:()=>void findReferences(target.path)},
    {id:'folder',label:'New Folder Here',icon:<FolderPlus size={13}/>,separator:true,disabled:!native||!projectPath||busy,
      onSelect:()=>{if(target.directory)goTo(target.path);setAddingFolder(true)}},
    {id:'import',label:'Import Into Here…',icon:<Upload size={13}/>,disabled:!native||!projectPath||busy,
      onSelect:()=>{if(target.directory)goTo(target.path);void externalImport()}},
    {id:'reload',label:'Reload',icon:<RefreshCw size={13}/>,separator:true,onSelect:reload},
  ];
  const openMenu=(event:React.MouseEvent,path:string,directory:boolean)=>{
    event.preventDefault();event.stopPropagation();
    if(!directory)setSelected(path);
    setMenu({x:event.clientX,y:event.clientY,path,directory});
  };

  return <section className="studio-assets" aria-label="Project Assets"
      data-content-drop-root="true" data-content-drop-target={folder}>
    <div className="studio-assets-toolbar">
      <div className="studio-assets-breadcrumb" aria-label="Asset location">
        {crumbs.map((part,index)=><span className="studio-assets-crumb" key={part.path}>
          {index>0&&<ChevronRight size={13} aria-hidden="true"/>}
          <button type="button" title={part.path?'content/'+part.path:'content/ root'}
            aria-current={part.path===folder?'location':undefined}
            {...folderDropProps(part.path)}
            onClick={()=>goTo(part.path)}>{part.name}</button>
        </span>)}
      </div>
      <label className="studio-assets-search"><Search size={14}/>
        <input value={search} onChange={e=>setSearch(e.target.value)}
          aria-label="Search project assets" placeholder="Find assets…"/>
      </label>
      <button type="button" className={'studio-assets-tool-action'+(unusedOnly?' is-on':'')} aria-pressed={unusedOnly}
        disabled={!native||!projectPath} onClick={()=>void toggleUnused()}
        title="Show only files no rules, map, material, script or UI file names (paths built in scripts are not followed)"
        aria-label="Show unused assets"><Unlink size={16}/></button>
      <button type="button" className="studio-assets-tool-action"
        disabled={!native||!projectPath||busy} onClick={()=>setAddingFolder(true)}
        title="New folder in content" aria-label="Create content folder"><FolderPlus size={16}/></button>
      <button type="button" className="studio-assets-tool-action"
        disabled={!native||!projectPath||busy} onClick={()=>void externalImport()}
        title="Import files or folders" aria-label="Import into content"><Upload size={16}/></button>
      <button type="button" className="studio-assets-tool-action" disabled={!native||!projectPath||busy}
        onClick={()=>setTrashOpen(true)} title="Recover deleted assets and previous versions" aria-label="Open asset Trash"><Trash2 size={16}/></button>
      <div className="studio-assets-views" role="group" aria-label="Asset layout">
        <button type="button" aria-label="Grid view" title="Grid view"
          aria-pressed={mode==='grid'} onClick={()=>setMode('grid')}><Grid2X2 size={15}/></button>
        <button type="button" aria-label="List view" title="List view"
          aria-pressed={mode==='list'} onClick={()=>setMode('list')}><List size={15}/></button>
      </div>
      <button className="studio-assets-reload" onClick={reload} disabled={loading||busy}
        type="button" aria-label="Reload project assets" title="Reload project assets">
        <RefreshCw size={15}/>
      </button>
    </div>
    {addingFolder&&<form className="studio-assets-create-folder" onSubmit={event=>{
      event.preventDefault();void createFolder();
    }}>
      <FolderPlus size={15}/><span>New folder inside content/{folder}</span>
      <input autoFocus aria-label="New content folder name" value={folderName}
        onChange={event=>setFolderName(event.target.value)} placeholder="Folder name"/>
      <button type="submit" disabled={!folderName.trim()||busy}>Create</button>
      <button type="button" onClick={()=>{setAddingFolder(false);setFolderName('')}}>Cancel</button>
    </form>}
    {deleteTarget&&<div className="studio-asset-dialog-overlay" data-native-overlay>
      <section className="studio-asset-dialog" role="dialog" aria-modal="true" aria-label="Confirm asset deletion">
        <header><div><h3>Move to Trash?</h3><p>{'content/'+deleteTarget.path}</p></div></header>
        <p>The asset and metadata move to project recovery storage. Undo is available in History / Assets if retained. Known map, rules and material references block deletion.</p>
        <footer><button type="button" disabled={busy} onClick={()=>setDeleteTarget(null)}>Cancel</button>
          <button type="button" className="studio-asset-confirm" disabled={busy} onClick={()=>void deleteAsset()}>{busy?'Moving…':'Move to Trash'}</button></footer>
      </section>
    </div>}
    {renameTarget&&<form className="studio-assets-create-folder" onSubmit={event=>{
      event.preventDefault();void previewRename();
    }}>
      <span title={renameTarget}>Rename {assetFileName(renameTarget)}</span>
      <input autoFocus aria-label="New asset filename" value={renameName}
        onChange={event=>setRenameName(event.target.value)} placeholder="Name (keep file extension)"/>
      <button type="submit" disabled={!renameName.trim()||busy||renameName.trim()===assetFileName(renameTarget)}>Review</button>
      <button type="button" disabled={busy} onClick={()=>setRenameTarget(null)}>Cancel</button>
    </form>}
    {creatingMaterial&&<form className="studio-assets-create-folder" onSubmit={event=>{
      event.preventDefault();void createMaterial();
    }}>
      <Library size={13}/><span>New material in materials.json</span>
      <input autoFocus aria-label="New material name" value={materialName}
        onChange={event=>setMaterialName(event.target.value)} placeholder="Material name"/>
      <select aria-label="Material preset" value={materialPreset}
        onChange={event=>setMaterialPreset(event.target.value as 'matte'|'metal'|'glass')}>
        <option value="matte">Matte</option><option value="metal">Metal</option><option value="glass">Glass</option>
      </select>
      <button type="submit" disabled={!materialName.trim()||busy}>Create</button>
      <button type="button" onClick={()=>setCreatingMaterial(false)}>Cancel</button>
    </form>}
    {message&&<div className="studio-assets-status" role="status">{message}</div>}
    {error&&<div className="studio-assets-status studio-assets-error" role="alert">{error}</div>}
    <div className="studio-assets-layout">
      <nav className="studio-assets-folders" aria-label="Project asset folders" style={{width:treeWidth,maxWidth:'none',minWidth:0}}>
        <button type="button" className={'studio-assets-folder'+(folder===''?' active':'')}
          {...folderDropProps('')} onClick={()=>goTo('')}>
          <Library size={13}/><span>content</span>
          <small>{catalog.files.length}</small>
        </button>
        {folders.slice(0,300).map(entry=>
          <button type="button" key={entry.path} title={entry.path}
            draggable onDragStart={e=>dragStart(e,entry.path)}
            {...folderDropProps(entry.path)}
            className={'studio-assets-folder'+(folder===entry.path?' active':'')}
            style={{paddingLeft:6+entry.depth*12}}
            onContextMenu={event=>openMenu(event,entry.path,true)}
            onClick={()=>goTo(entry.path)}>
            {folder===entry.path?<FolderOpen size={13}/>:<Folder size={13}/>}
            <span>{entry.name}</span><small>{entry.count}</small>
          </button>)}
      </nav>
      <div className="studio-assets-splitter" role="separator" aria-orientation="vertical" aria-label="Resize folder tree"
        aria-valuenow={Math.round(treeWidth)} tabIndex={0} title="Drag to resize · double-click to reset"
        onPointerDown={startTreeResize} onDoubleClick={()=>setTreeWidth(170)}
        onKeyDown={event=>{if(event.key==='ArrowLeft')setTreeWidth(w=>Math.max(110,w-16));if(event.key==='ArrowRight')setTreeWidth(w=>Math.min(900,w+16));}}/>
      <div className="studio-assets-content" {...folderDropProps(folder)}
        onContextMenu={event=>{event.preventDefault();setMenu({x:event.clientX,y:event.clientY,path:folder,directory:true,background:true})}}>
        {loading?<div className="studio-assets-state">Loading content/…</div>:
          shownFiles.length===0&&shownFolders.length===0?
            <div className="studio-assets-empty">
              <Folder size={26}/>
              <strong>{projectPath?(search||folder?'No matching assets':'content/ is empty'):'Open a project'}</strong>
              <p>{projectPath?'Import assets or drag files here from the Finder.':
                'Assets appear here when an AIVERSE project is open.'}</p>
            </div>:
            <div className={'studio-assets-items studio-assets-items--'+mode} role="list">
              {shownFolders.map(entry=>
                <button key={entry.path} type="button" className="studio-assets-item studio-assets-item--folder"
                  title={entry.path} role="listitem" draggable
                  onDragStart={e=>dragStart(e,entry.path)}
                  {...folderDropProps(entry.path)}
                  onContextMenu={event=>openMenu(event,entry.path,true)}
                  onClick={()=>goTo(entry.path)}>
                  <span className="studio-assets-icon"><Folder size={mode==='grid'?28:14}/></span>
                  <span className="studio-assets-name">{entry.name}</span>
                  <span className="studio-assets-meta">{entry.count} files</span>
                </button>)}
              {shownFiles.map((asset:StudioAsset)=>{
                const kind=assetKind(asset.path);
                return <button key={asset.path} type="button" role="listitem" draggable
                  onDragStart={e=>dragStart(e,asset.path)}
                  className={'studio-assets-item'+(selected===asset.path?' active':'')}
                  data-kind={kind} aria-selected={selected===asset.path}
                  title={'content/'+asset.path} onClick={()=>setSelected(asset.path)}
                  onContextMenu={event=>openMenu(event,asset.path,false)}>
                  <span className="studio-assets-icon">{mode==='grid' && (kind==='model'||kind==='image')
                    ? <AssetThumb path={'content/'+asset.path} fallback={<AssetIcon kind={kind} size={24}/>}/>
                    : <AssetIcon kind={kind} size={mode==='grid'?24:14}/>}</span>
                  <span className="studio-assets-name">{assetFileName(asset.path)}</span>
                  <span className="studio-assets-meta">{formatBytes(asset.bytes)}</span>
                </button>;
              })}
            </div>}
      </div>
      {selectedAsset&&native&&<AssetImportSettings key={selectedAsset.guid||selectedAsset.path}
        asset={selectedAsset} refreshToken={refreshToken} locked={pinnedAssetGuid!==null}
        onToggleLock={toggleAssetPin} onClose={()=>{setSelected(null);setPinnedAssetGuid(null)}}
        onSaved={reload} onError={setError}/>}
    </div>
    <footer className="studio-assets-footer">
      <span>{catalog.files.length} assets</span>
      <span className="studio-assets-current">{selected?
        'content/'+selected:search?'Search in content/':folder?'content/'+folder:'content/'}</span>
    </footer>
    {mentions&&mentions.project===projectPath&&<div className="studio-assets-refs" role="dialog" aria-label={'References to '+mentions.path} data-native-overlay>
      <header><Link2 size={13}/><b>content/{mentions.path}</b><span>Asset dependencies</span>
        <button type="button" aria-label="Close" onClick={()=>{++referenceRequest.current;setMentions(null);setReferenceLoading(false);}}><X size={13}/></button></header>
      <div className="studio-assets-dependency-body">
        {mentions.graphError?<><p role="alert">Dependency graph unavailable: {mentions.graphError}</p><button type="button" disabled={referenceLoading} onClick={()=>void findReferences(mentions.path)}>Refresh graph</button></>:<>
          <div className="studio-assets-dependency-tabs">
            {(['incoming','outgoing'] as const).map(direction=><button key={direction} type="button"
              aria-pressed={referenceDirection===direction} disabled={referenceLoading} onClick={()=>void loadReferencePage(direction,0)}>
              {direction==='incoming'?'Used by':'Depends on'} ({direction==='incoming'?mentions.graph?.incoming??0:mentions.graph?.outgoing??0})
            </button>)}
          </div>
          <ul>{(mentions.graph?.items??[]).map((r,i)=>{
            const endpoint=referenceDirection==='incoming'?r.document:r.path;
            const path=endpoint.startsWith('content/')?endpoint.slice(8):'';
            const canNavigate=!!path&&catalog.files.some(asset=>asset.path===path);
            return <li key={i}>
              {canNavigate?<button type="button" onClick={()=>void findReferences(path)} title={'Inspect '+endpoint}>{endpoint}</button>:<code>{endpoint}</code>}
              <span title={r.document+' · '+r.field}>Hop {r.depth??1} · {r.field}{r.cycle?' · Cycle closing edge':''}{referenceDirection==='outgoing'&&!r.guid?' · No indexed GUID':''}</span>
            </li>;
          })}</ul>
          <div className="studio-assets-dependency-tabs">
            <button type="button" disabled={referenceLoading||referencePage===0} onClick={()=>void loadReferencePage(referenceDirection,referencePage-1)}>Previous</button>
            <span>Page {referencePage+1}</span>
            <button type="button" disabled={referenceLoading||(referencePage+1)*50>=(mentions.graph?.total??0)} onClick={()=>void loadReferencePage(referenceDirection,referencePage+1)}>Next</button>
          </div>
          {mentions.graph&&<><p>Known edges up to 16 hops · {mentions.graph.unresolved} unresolved GUID occurrences. Cycle labels mark closing edges.</p>
          <p>Scanned models: {mentions.graph.coverage.gltf} glTF · {mentions.graph.coverage.glb} GLB · {mentions.graph.coverage.obj} OBJ · {mentions.graph.coverage.dae} DAE. Coverage gaps: {mentions.graph.coverage.opaqueGlb} opaque GLB, {mentions.graph.coverage.unsupportedModels} other models, {mentions.graph.coverage.unsupportedUris} unsupported URIs, {mentions.graph.coverage.missingLibraries} missing MTL, {mentions.graph.coverage.symlinks} symlinks.</p><p>{mentions.graph.coverage.note}</p></>}
        </>}
        <h4>Text mentions ({mentions.rows.length})</h4>
        <ul>{mentions.rows.map((r,i)=><li key={i}><code>{r.file}:{r.line}</code><span>{r.text}</span></li>)}</ul>
      </div>
    </div>}
    {menu&&<StudioContextMenu x={menu.x} y={menu.y} label="Asset actions"
      items={menu.background?backgroundItems():menuItems(menu)} onClose={()=>setMenu(null)}/>}
    {trashOpen&&<AssetTrashDialog key={projectPath} projectPath={projectPath} onClose={()=>setTrashOpen(false)} onChanged={reload}/>}
    {plan&&<PreviewDialog key={plan.token} plan={plan} busy={busy}
      onCancel={()=>setPlan(null)} onCommit={decisions=>void applyTransfer(decisions)}/>}
  </section>;
}
