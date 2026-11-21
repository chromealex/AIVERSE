import type React from 'react';
import { useCallback, useEffect, useMemo, useState } from 'react';
import type { DragEvent } from 'react';
import {
  FileAudio, Box, FileCode2, FileImage, FileText, Folder, FolderOpen,
  Grid2X2, List, RefreshCw, Search, ChevronRight, Library, Upload, FolderPlus, X
} from 'lucide-react';
import { invoke, nativeConnected, onNativeEvent } from '../bridge';
import type {
  StudioAsset, StudioAssetList, StudioAssetTransferPlan,
  StudioAssetConflictAction, StudioAssetTransferItem
} from '../types';
import { assetKind, assetFolders, assetFileName, assetBreadcrumbs,
  childFolders, visibleAssetFiles, safeAssetPath } from '../assetTree';
import type { AssetFolder, AssetKind } from '../assetTree';
import './AssetsPanel.css';
import AssetImportSettings from './AssetImportSettings';
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
    if(a.value==='replace')return source==='external' && !item.directory && collision.includes('name');
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
  const blocked=(plan.references??[]).filter(ref=>!ref.writable && selected[ref.id]!=='skip');
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
                {ref.writable?'Will repair material reference: ':'Blocked: read-only reference: '}
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
          {blocked.length>0?' · '+blocked.length+' read-only reference(s) block this move':''}</span>
        <button type="button" disabled={busy} onClick={onCancel}>Cancel</button>
        <button type="button" disabled={busy||blocked.length>0} className="studio-asset-confirm"
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
    if(event==='workspace.changed')reload();
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
  useEffect(()=>{setFolder('');setSelected(null);setPinnedAssetGuid(null);setSearch('');setPlan(null);},[projectPath]);
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
  const shownFiles=useMemo(()=>visibleAssetFiles(catalog.files,folder,search),[catalog.files,folder,search]);
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
  const applyTransfer=async(decisions:Array<{id:number;action:StudioAssetConflictAction}>)=>{
    if(!plan||busy)return;
    setBusy(true);setError('');
    try{
      const response=await invoke('studio.assetTransferCommit',{token:plan.token,decisions});
      setPlan(null);setSelected(null);
      setMessage(response.files.length+' asset(s) processed'+
        (response.repairedMaterialReferences
          ? ' · '+response.repairedMaterialReferences+' material reference(s) repaired' : '')+
        (response.issues.length?' · '+response.issues.length+' issue(s)':''));
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
    void navigator.clipboard?.writeText(text).then(()=>setMessage('Copied '+text),()=>setError('Clipboard unavailable'));
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
    {id:'copy',label:'Copy Path',icon:<Search size={13}/>,onSelect:()=>copyPath(target.path)},
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
      <button type="button" className="studio-assets-tool-action"
        disabled={!native||!projectPath||busy} onClick={()=>setAddingFolder(true)}
        title="New folder in content" aria-label="Create content folder"><FolderPlus size={16}/></button>
      <button type="button" className="studio-assets-tool-action"
        disabled={!native||!projectPath||busy} onClick={()=>void externalImport()}
        title="Import files or folders" aria-label="Import into content"><Upload size={16}/></button>
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
                  <span className="studio-assets-icon"><AssetIcon kind={kind} size={mode==='grid'?24:14}/></span>
                  <span className="studio-assets-name">{assetFileName(asset.path)}</span>
                  <span className="studio-assets-meta">{formatBytes(asset.bytes)}</span>
                </button>;
              })}
            </div>}
      </div>
      {selectedAsset&&native&&<AssetImportSettings key={selectedAsset.guid||selectedAsset.path}
        asset={selectedAsset} locked={pinnedAssetGuid!==null}
        onToggleLock={toggleAssetPin} onClose={()=>{setSelected(null);setPinnedAssetGuid(null)}}
        onSaved={reload} onError={setError}/>}
    </div>
    <footer className="studio-assets-footer">
      <span>{catalog.files.length} assets</span>
      <span className="studio-assets-current">{selected?
        'content/'+selected:search?'Search in content/':folder?'content/'+folder:'content/'}</span>
    </footer>
    {menu&&<StudioContextMenu x={menu.x} y={menu.y} label="Asset actions"
      items={menu.background?backgroundItems():menuItems(menu)} onClose={()=>setMenu(null)}/>}
    {plan&&<PreviewDialog key={plan.token} plan={plan} busy={busy}
      onCancel={()=>setPlan(null)} onCommit={decisions=>void applyTransfer(decisions)}/>}
  </section>;
}
