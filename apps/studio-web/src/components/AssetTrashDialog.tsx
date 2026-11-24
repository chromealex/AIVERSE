import {useEffect,useRef,useState} from 'react';
import {RefreshCw,Trash2,X} from 'lucide-react';
import {invoke,onNativeEvent} from '../bridge';
import type {StudioTrashAction,StudioTrashPage,StudioTrashReview,StudioTrashBatchReview} from '../types';
import {StudioAlert,StudioButton,StudioIconButton,StudioEmptyState,StudioSkeleton,StudioInput} from '../ui';
import './AssetTrashDialog.css';

const PAGE=50;
const message=(e:unknown)=>e instanceof Error?e.message:String(e);
const formatBytes=(bytes:number)=>bytes<1024?bytes+' B':bytes<1048576?(bytes/1024).toFixed(1)+' KiB':(bytes/1048576).toFixed(1)+' MiB';

/** Recovery storage is a native owner, not an Assets folder or the in-memory undo stack. */
export default function AssetTrashDialog({projectPath,onClose,onChanged}:{projectPath:string;onClose:()=>void;onChanged:()=>void}){
  const [page,setPage]=useState<StudioTrashPage|null>(null);
  const [offset,setOffset]=useState(0);
  const [filter,setFilter]=useState('');
  const [refresh,setRefresh]=useState(0);
  const [error,setError]=useState('');
  const [busy,setBusy]=useState(false);
  const [review,setReview]=useState<StudioTrashReview|null>(null);
  const [confirm,setConfirm]=useState(false);
  const [quota,setQuota]=useState('0');
  const [selected,setSelected]=useState<string[]>([]);
  const [batch,setBatch]=useState<StudioTrashBatchReview|null>(null);
  useEffect(()=>{if(page?.storage)setQuota(String(page.storage.quotaMiB));},[page]);
  const request=useRef(false);
  const mounted=useRef(true);
  useEffect(()=>{mounted.current=true;return()=>{mounted.current=false}},[]);
  useEffect(()=>onNativeEvent(event=>{
    if(event==='assets.changed'||event==='workspace.changed'){setReview(null);setConfirm(false);setRefresh(v=>v+1);}
  }),[]);
  useEffect(()=>{
    let live=true;setPage(null);setSelected([]);setBatch(null);setConfirm(false);
    void invoke('studio.assetTrashList',{expectedProject:projectPath,offset,limit:PAGE})
      .then(r=>{if(live){if(offset>=r.total&&offset>0){setOffset(Math.max(0,Math.floor(Math.max(0,r.total-1)/PAGE)*PAGE));return;}setPage(r);}})
      .catch(e=>{if(live)setError(message(e));});
    return()=>{live=false};
  },[projectPath,offset,refresh]);
  const reload=()=>{setError('');setReview(null);setConfirm(false);setRefresh(v=>v+1)};
  const saveQuota=async()=>{
    if(request.current||!page?.storage||!/^\d+$/.test(quota)||Number(quota)>1048576)return;
    request.current=true;setBusy(true);setError('');
    try{await invoke('studio.assetRecoveryQuota',{expectedProject:projectPath,expectedMiB:page.storage.quotaMiB,quotaMiB:Number(quota)});if(mounted.current)reload();}
    catch(e){if(mounted.current)setError(message(e));}
    finally{request.current=false;if(mounted.current)setBusy(false);}
  };
  const prepareBatch=async()=>{
    if(request.current||!selected.length||page?.recoveryPending)return;
    request.current=true;setBusy(true);setError('');setReview(null);setBatch(null);setConfirm(false);
    try{const r=await invoke('studio.assetTrashBatchReview',{expectedProject:projectPath,ids:selected});if(mounted.current)setBatch(r);}
    catch(e){if(mounted.current)setError(message(e));}
    finally{request.current=false;if(mounted.current)setBusy(false);}
  };
  const applyBatch=async()=>{
    if(request.current||!batch||!confirm)return;
    request.current=true;setBusy(true);setError('');
    try{
      const r=await invoke('studio.assetTrashBatchApply',{expectedProject:projectPath,ids:batch.items.map(row=>row.id),token:batch.token,expectedVersion:batch.version,confirmed:true});
      if(mounted.current){onChanged();reload();if(!r.ok)setError('Removed '+r.removed.length+' records; cleanup stopped: '+(r.error??'unknown error'));}
    }catch(e){if(mounted.current){setError(message(e));setBatch(null);setConfirm(false);setRefresh(v=>v+1);}}
    finally{request.current=false;if(mounted.current)setBusy(false);}
  };
  const prepare=async(id:string,action:StudioTrashAction)=>{
    if(request.current||page?.recoveryPending)return;
    request.current=true;setBusy(true);setError('');setReview(null);setBatch(null);setConfirm(false);
    try{const r=await invoke('studio.assetTrashReview',{expectedProject:projectPath,id,action});if(mounted.current)setReview(r);}
    catch(e){if(mounted.current)setError(message(e));}
    finally{request.current=false;if(mounted.current)setBusy(false);}
  };
  const apply=async()=>{
    if(request.current||!review||!confirm)return;
    request.current=true;setBusy(true);setError('');
    try{
      await invoke('studio.assetTrashApply',{expectedProject:projectPath,id:review.id,action:review.action,
        token:review.token,expectedVersion:review.version,confirmed:true});
      if(mounted.current){onChanged();reload();}
    }catch(e){if(mounted.current){setError(message(e));setReview(null);setConfirm(false);setRefresh(v=>v+1);}}
    finally{request.current=false;if(mounted.current)setBusy(false);}
  };
  const items=(page?.items??[]).filter(row=>[row.path,row.kind,row.guid,row.id].some(value=>value?.toLowerCase().includes(filter.toLowerCase())));
  return <div className="studio-asset-dialog-overlay" data-native-overlay>
    <section className="studio-asset-dialog studio-trash" role="dialog" aria-modal="true" aria-label="Asset Trash and recovery">
      <header><div><h3>Asset Trash &amp; Versions</h3><p>Project-local recovery records retained across editor sessions.</p></div>
        <StudioIconButton aria-label="Refresh Trash" icon={<RefreshCw size={15}/>} disabled={busy} onClick={reload}/>
        <StudioIconButton aria-label="Close Trash" icon={<X size={15}/>} disabled={busy} onClick={onClose}/>
      </header>
      <div className="studio-trash__body">
        {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
        {page?.recoveryPending&&<StudioAlert tone="warning">Reopen the project to resolve the pending asset journal before restoring or purging.</StudioAlert>}
        {page?.truncated&&<StudioAlert tone="warning">Listing limited to 10,000 recovery records. No automatic cleanup is performed.</StudioAlert>}
        {page?.storageError&&<StudioAlert tone="warning">Storage accounting unavailable: {page.storageError}</StudioAlert>}
        {page?.storage&&<div className="studio-trash__review">
          <strong>Recovery storage</strong>
          <p>Trash: {formatBytes(page.storage.trashBytes)} · Asset history: {formatBytes(page.storage.checkpointBytes)} · Journals: {formatBytes(page.storage.journalBytes)} · Free disk: {formatBytes(page.storage.availableBytes)}</p>
          {page.storage.warning&&<StudioAlert tone="warning">{page.storage.overQuota?'Recovery quota exceeded. Growth is blocked.':'Recovery storage is approaching its budget.'} No automatic purge.</StudioAlert>}
          <label>Trash quota (MiB, 0 = unlimited)<StudioInput aria-label="Trash quota in MiB" value={quota} onChange={e=>setQuota(e.target.value)} disabled={busy||!!page.recoveryPending}/></label>
          <StudioButton size="sm" disabled={busy||!!page.recoveryPending||!/^\d+$/.test(quota)||Number(quota)>1048576||Number(quota)===page.storage.quotaMiB} onClick={()=>void saveQuota()}>Save quota</StudioButton>
          <p>Logical retained bytes; temporary staging and other project caches are not limited by this quota.</p>
        </div>}
        <StudioInput aria-label="Filter this recovery page" placeholder="Filter this page by path, GUID or kind" value={filter} onChange={e=>setFilter(e.target.value)} disabled={busy}/>
        {!page&&!error&&<StudioSkeleton/>}
        {page&&items.length===0&&<StudioEmptyState icon={<Trash2 size={24}/>} title="No matching recovery records"/>}
        <div className="studio-trash__actions">
          <StudioButton size="sm" disabled={busy||!!page?.recoveryPending||!items.some(r=>r.status==='retained'&&!r.historyProtected)} onClick={()=>{setSelected(items.filter(r=>r.status==='retained'&&!r.historyProtected).map(r=>r.id));setBatch(null);setConfirm(false);}}>Select eligible on page</StudioButton>
          <StudioButton size="sm" disabled={busy||!!page?.recoveryPending||!selected.length} onClick={()=>void prepareBatch()}>Review purge ({selected.length})…</StudioButton>
        </div>
        {items.map(row=><div className="studio-trash__row" key={row.id}>
          {row.status==='retained'&&<input type="checkbox" aria-label={'Select '+(row.path??row.id)} disabled={busy||!!row.historyProtected||!!page?.recoveryPending} checked={selected.includes(row.id)}
            onChange={e=>{setSelected(old=>e.target.checked?[...old,row.id]:old.filter(id=>id!==row.id));setBatch(null);setConfirm(false);}}/>}
          <div className="studio-trash__info">
            <strong title={row.path??row.id}>{row.path??row.id}</strong>
            <small>{row.status==='invalid'?'Invalid record':row.kind==='replacement'?'Previous file version':row.kind==='created'?'Imported / created':'Deleted asset'}{row.directory?' · Folder':''}{row.slotBytes!==undefined?' · '+formatBytes(row.slotBytes):row.bytes!==undefined?' · '+formatBytes(row.bytes):''}</small>
            {row.historyProtected&&<small>Protected by retained Undo/Redo — purge unavailable</small>}
            {row.guid&&<small title={row.guid}>GUID {row.guid}</small>}
            {row.error&&<small role="alert">{row.error}</small>}
          </div>
          {row.status==='retained'&&<div className="studio-trash__actions">
            <StudioButton size="sm" disabled={busy||!!page?.recoveryPending} onClick={()=>void prepare(row.id,'restore')}>Restore…</StudioButton>
            <StudioButton size="sm" variant="danger" disabled={busy||!!row.historyProtected||!!page?.recoveryPending} onClick={()=>void prepare(row.id,'purge')}>Purge…</StudioButton>
          </div>}
        </div>)}
        {batch&&<div className="studio-trash__review">
          <StudioAlert tone="danger" title="Review permanent cleanup">{batch.items.length} records · {formatBytes(batch.bytes)}. Only selected unprotected recovery slots will be removed. Live content and retained history are preserved. Disk errors can stop cleanup partway.</StudioAlert>
          {batch.items.map(row=><small key={row.id}>{row.path} · {formatBytes(row.slotBytes??row.bytes??0)}</small>)}
          <label className="au-checkbox"><input className="au-checkbox__input" type="checkbox" checked={confirm} disabled={busy} onChange={e=>setConfirm(e.target.checked)}/><span>Confirm permanent deletion of these records.</span></label>
          <div className="studio-trash__actions">
            <StudioButton size="sm" disabled={busy} onClick={()=>{setBatch(null);setConfirm(false);}}>Cancel</StudioButton>
            <StudioButton size="sm" variant="danger" disabled={busy||!confirm} onClick={()=>void applyBatch()}>Permanently purge selected</StudioButton>
          </div>
        </div>}
        {review&&<div className="studio-trash__review">
          <StudioAlert tone={review.action==='purge'?'danger':'warning'} title={review.action==='purge'?'Permanently remove recovery data':'Restore this record'}>
            {review.path}. {review.action==='restore'?'Restoring clears the current Assets Undo/Redo history as part of the same transaction.':'Retained Undo/Redo history is preserved.'}
            {review.action==='purge'?' The recovery payload will be permanently deleted. Live content will not be changed.':
              review.kind==='replacement'?' The current version is saved separately; its GUID and metadata are retained.':' The original path and GUID must be available.'}
          </StudioAlert>
          <label className="au-checkbox"><input className="au-checkbox__input" type="checkbox" checked={confirm} disabled={busy} onChange={e=>setConfirm(e.target.checked)}/>
            <span>I understand and confirm this action.</span></label>
          <div className="studio-trash__actions">
            <StudioButton size="sm" disabled={busy} onClick={()=>{setReview(null);setConfirm(false)}}>Cancel</StudioButton>
            <StudioButton size="sm" variant={review.action==='purge'?'danger':'primary'} disabled={busy||!confirm} onClick={()=>void apply()}>{review.action==='purge'?'Permanently purge':'Restore'}</StudioButton>
          </div>
        </div>}
      </div>
      <footer>
        <span>{page?Math.min(offset+1,page.total)+'–'+Math.min(offset+page.items.length,page.total)+' / '+page.total:'Reading recovery records'}</span>
        <StudioButton size="sm" disabled={busy||offset===0} onClick={()=>{setReview(null);setOffset(Math.max(0,offset-PAGE))}}>Previous</StudioButton>
        <StudioButton size="sm" disabled={busy||!page||offset+PAGE>=page.total} onClick={()=>{setReview(null);setOffset(offset+PAGE)}}>Next</StudioButton>
      </footer>
    </section>
  </div>;
}
