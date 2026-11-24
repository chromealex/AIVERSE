import { useEffect, useRef, useState } from 'react';
import { Database, RefreshCw } from 'lucide-react';
import { invoke, nativeConnected } from '../bridge';
import type { StudioWorldOps, StudioWorldStatus, StudioWorldBackup, StudioWorldRetention, StudioWorldPage, StudioWorldJob } from '../types';
import {
  StudioPanel, StudioSection, StudioButton, StudioIconButton, StudioField, StudioInput, StudioSelect,
  StudioBadge, StudioAlert, StudioEmptyState, StudioProgress, StudioTabs
} from '../ui';
import './WorldPersistencePanel.css';

type Op = keyof StudioWorldOps;
const required: string[] = ['world_save_list','world_save_status','world_save_create','world_save_checkpoint',
  'world_save_backups','world_save_retention_preview','world_save_retention_apply','world_save_fork','world_save_page','world_job_start','world_job_status','world_job_cancel','world_job_release'];
const isUuid = (value: string) => /^[0-9a-f]{32}$/.test(value) && /[1-9a-f]/.test(value);
const isCounter = (value: string) => /^(0|[1-9][0-9]{0,19})$/.test(value) && BigInt(value)<=18446744073709551615n;
const generateUuid = () => Array.from(crypto.getRandomValues(new Uint8Array(16)),b=>b.toString(16).padStart(2,'0')).join('');
const message = (e: unknown) => e instanceof Error ? e.message : String(e);
async function worldOp<K extends Op>(expectedProject:string, op: K, params: StudioWorldOps[K]['params']): Promise<StudioWorldOps[K]['result']> {
  const reply = await invoke('studio.op', {op,params,expectedProject});
  if (!reply.result || typeof reply.result !== 'object') throw new Error('Invalid native world response');
  const result = reply.result as StudioWorldOps[K]['result'];
  if ('error' in result && result.error) throw new Error(String(result.error));
  return result;
}
async function jobOp<T>(op:string,params:Record<string,unknown>,expectedProject:string):Promise<T>{
  const reply=await invoke('studio.op',{op,params,expectedProject});
  if(!reply.result||typeof reply.result!=='object')throw new Error('Invalid native job response');
  return reply.result as T;
}
const pause = () => new Promise<void>(resolve=>setTimeout(resolve,150));
function JobProgress({job,onCancel,onRelease,detached}:{job:StudioWorldJob;onCancel:()=>void;onRelease:()=>void;detached:boolean}){
  const p=job.progress,done=p?.checkpointBytes,total=p?.checkpointTotal;
  const terminal=job.state!=='running';
  const percent=done&&total&&isCounter(done)&&isCounter(total)&&BigInt(total)>0n?
    Number(BigInt(done)*10000n/BigInt(total))/100:undefined;
  return <StudioSection title="Background operation">
    <StudioBadge tone={job.state==='failed'?'danger':job.state==='succeeded'?'success':'neutral'}>{job.state}</StudioBadge>
    <div>{job.operation} · {job.phase}</div>
    {detached&&<small>Existing job from {job.project}. Its result belongs to that project.</small>}
    {percent!==undefined&&<><StudioProgress label="Checkpoint reading" value={percent} max={100}/><small>{done} / {total} checkpoint bytes</small></>}
    {p?.frames!==undefined&&<small>WAL: {p.frames} frames · {p.walBytes} bytes · sequence {p.sequence}</small>}
    {p?.assetsRead!==undefined&&<small>Assets read: {p.assetsRead} / {p.assetsTotal}</small>}
    {!terminal&&<small>{job.cancelRequested?'Cancellation requested':job.cancelAllowed?'Cancellation available':'Finishing non-cancellable operation'}</small>}
    {job.error&&<StudioAlert tone="danger">{job.error}</StudioAlert>}
    {detached&&terminal&&job.result!==undefined&&<details><summary>Result</summary><pre className="world-persistence-result">{JSON.stringify(job.result,null,2)}</pre></details>}
    {!terminal&&<StudioButton disabled={!job.cancelAllowed} onClick={onCancel}>Cancel job</StudioButton>}
    {detached&&terminal&&<StudioButton onClick={onRelease}>Release result</StudioButton>}
  </StudioSection>;
}
type Tab = 'overview'|'backups'|'retention'|'entities';
type ConfirmPlan = {world:string;seed:string;keepPrevious:string;maxBytes:string;plan:StudioWorldRetention};

function RegionMap({status}:{status:StudioWorldStatus}) {
  if (!status.regions.length) return <StudioEmptyState title="No occupied regions"/>;
  const rows=status.regions;
  const minX=Math.min(...rows.map(r=>r.cell[0])),maxX=Math.max(...rows.map(r=>r.cell[0]));
  const minZ=Math.min(...rows.map(r=>r.cell[2])),maxZ=Math.max(...rows.map(r=>r.cell[2]));
  const span=Math.max(1,maxX-minX+1,maxZ-minZ+1);
  const scale=180/span;
  return <div className="world-persistence-map">
    <svg viewBox="0 0 200 200" role="img" aria-label="Occupied region projection on the X/Z plane">
      {rows.map(r=><rect key={r.cell.join(',')} x={10+(r.cell[0]-minX)*scale} y={10+(maxZ-r.cell[2])*scale}
        width={Math.max(1,scale*.92)} height={Math.max(1,scale*.92)}
        className="world-persistence-cell" opacity={.25+.75*r.materialized/Math.max(1,r.live)}>
        <title>{r.cell.join(', ')} — {r.materialized}/{r.live} materialized</title>
      </rect>)}
    </svg>
    <small>Ownership cells, X/Z projection. Brightness = materialized share. Not collision residency.</small>
    {status.regionsTruncated&&<StudioAlert tone="warning">Showing {rows.length} of {status.regionCount} regions.</StudioAlert>}
  </div>;
}

export default function WorldPersistencePanel({projectPath}:{projectPath:string}) {
  // A project change remounts all maintenance forms and invalidates local confirmations.
  return <WorldPersistenceBody key={projectPath} projectPath={projectPath}/>;
}
function WorldPersistenceBody({projectPath}:{projectPath:string}) {
  const [available,setAvailable]=useState(false),[checking,setChecking]=useState(true);
  const [worlds,setWorlds]=useState<string[]>([]),[world,setWorld]=useState(''),[seed,setSeed]=useState('1');
  const [newWorld,setNewWorld]=useState(''),[retain,setRetain]=useState('65536');
  const [keep,setKeep]=useState('2'),[maxBytes,setMaxBytes]=useState('0');
  const [generation,setGeneration]=useState(''),[forkWorld,setForkWorld]=useState('');
  const [status,setStatus]=useState<StudioWorldStatus|null>(null);
  const [backups,setBackups]=useState<StudioWorldBackup[]>([]),[plan,setPlan]=useState<ConfirmPlan|null>(null);
  const [page,setPage]=useState<StudioWorldPage|null>(null);
  const [tab,setTab]=useState<Tab>('overview');
  const [busy,setBusy]=useState(''),[error,setError]=useState(''),[notice,setNotice]=useState('');
  const [jobState,setJobState]=useState<StudioWorldJob|null>(null);
  const [detached,setDetached]=useState(false);
  const lock=useRef(false),alive=useRef(true);
  useEffect(()=>{alive.current=true;return()=>{alive.current=false}},[]);
  async function check() {
    setChecking(true);setAvailable(false);setError('');setPlan(null);
    try {
      if(!projectPath||!nativeConnected())return;
      const info=await invoke('studio.ops',{panel:'world_persistence'});
      if(!alive.current)return;
      if(!required.every(op=>info.ops.some(item=>item.name===op)))throw new Error('World persistence backend is not included in this native build.');
      const existing=await jobOp<StudioWorldJob>('world_job_status',{},projectPath);
      if(!alive.current)return;
      if(existing.state!=='idle'){
        setJobState(existing);setDetached(true);setAvailable(true);return;
      }
      setJobState(null);setDetached(false);
      const list=await worldOp(projectPath,'world_save_list',{});
      if(alive.current){setWorlds(list.worlds);setAvailable(true);}
    }catch(e){if(alive.current)setError(message(e));}
    finally{if(alive.current)setChecking(false);}
  }
  useEffect(()=>{void check()},[projectPath]); // remounted by path
  useEffect(()=>{
    if(!detached||!jobState?.id||jobState.state!=='running')return;
    let active=true;
    const id=jobState.id;
    async function poll(){
      while(active){
        try{
          const result=await jobOp<StudioWorldJob>('world_job_status',{id},projectPath);
          if(!active)return;
          setJobState(result);
          if(result.state!=='running')return;
        }catch(e){if(active)setError(message(e));return;}
        await pause();
      }
    }
    void poll();
    return()=>{active=false;};
  },[detached,jobState?.id,jobState?.state,projectPath]);
  const selected=available&&isUuid(world)&&worlds.includes(world)&&isCounter(seed)&&!busy&&!jobState;
  const scope={world,seed};
  function clearSelectionData(){setStatus(null);setBackups([]);setPlan(null);setPage(null);setGeneration('');setNotice('');setError('');}
  async function run(label:string,job:()=>Promise<void>){
    if(lock.current||!available||jobState)return;
    lock.current=true;setBusy(label);setError('');setNotice('');
    try{await job();}catch(e){if(alive.current)setError(message(e));}
    finally{lock.current=false;if(alive.current)setBusy('');}
  }
  async function execute<K extends Op>(op:K,params:StudioWorldOps[K]['params']):Promise<StudioWorldOps[K]['result']>{
    let id:string|undefined;
    try{
      const start=await jobOp<{id:string}>('world_job_start',{operation:op,params},projectPath);
      id=start.id;
      while(alive.current){
        const state=await jobOp<StudioWorldJob>('world_job_status',{id},projectPath);
        if(!alive.current)break;
        setJobState(state);
        if(state.state==='running'){await pause();continue;}
        if(state.state==='idle')throw new Error('World job result is no longer available');
        const release=await jobOp<{released:boolean}>('world_job_release',{id},projectPath);
        if(!release.released)throw new Error('World job result could not be released');
        id=undefined;
        if(alive.current)setJobState(null);
        if(state.state!=='succeeded')throw new Error(state.error||state.state);
        return state.result as StudioWorldOps[K]['result'];
      }
      throw new Error('Panel closed; job retained for rediscovery');
    }catch(e){
      if(alive.current){
        // Do not resubmit on an ambiguous RPC failure. Rediscover retained slot.
        setDetached(true);
        try{const existing=await jobOp<StudioWorldJob>('world_job_status',id?{id}:{},projectPath);setJobState(existing.state==='idle'?null:existing);}catch{}
      }
      throw e;
    }
  }
  async function cancelJob(){
    if(!jobState?.id)return;
    try{
      const reply=await jobOp<{accepted:boolean}>('world_job_cancel',{id:jobState.id},projectPath);
      if(!reply.accepted)setNotice('Cancellation refused; operation may have entered its durable phase.');
    }catch(e){setError(message(e));}
  }
  async function releaseJob(){
    if(!jobState?.id)return;
    try{
      const reply=await jobOp<{released:boolean}>('world_job_release',{id:jobState.id},projectPath);
      if(!reply.released)throw new Error('Job is still running');
      setJobState(null);setDetached(false);await check();
    }catch(e){setError(message(e));}
  }
  async function refreshWorlds(){const r=await worldOp(projectPath,'world_save_list',{});if(alive.current)setWorlds(r.worlds);}
  async function inspect(){setPlan(null);setPage(null);setStatus(null);const r=await execute('world_save_status',scope);if(alive.current)setStatus(r);}
  async function readBackups(verify:boolean){setBackups([]);setGeneration('');const r=await execute('world_save_backups',{...scope,verify});if(alive.current){setBackups(r.backups);if(!r.complete)setNotice('Backup scan incomplete.');}}
  async function readPage(after:string,bootstrap:boolean,expectedRevision:string){
    const r=await execute('world_save_page',{...scope,after,limit:'100',bootstrap,expectedRevision});
    if(alive.current){setPage(r);if(r.resetRequired)setNotice('History changed or compacted. Refresh status and restart bootstrap.');}
  }
  return <StudioPanel title="World Persistence" eyebrow="WORLD / STORAGE"
    subtitle="Offline save maintenance"
    actions={<StudioIconButton aria-label="Refresh world list" icon={<RefreshCw size={15}/>} disabled={!!busy||checking||!projectPath} onClick={()=>void check()}/>}
    footer={<StudioBadge tone={error?'danger':available?'success':'neutral'}>{busy|| (available?'Offline tools ready':'Unavailable')}</StudioBadge>}>
    <div className="au-stack world-persistence">
      {!projectPath?<StudioEmptyState title="Open a project" icon={<Database size={24}/>}/>:
      !available?<StudioEmptyState title={checking?'Checking native tools…':'World persistence unavailable'}
        description="Requires the integrated Open World native backend. No mock saves are created."/>:<>
        <StudioAlert>Stop the server before maintenance. Only saved project documents and project-owned world-saves are used.</StudioAlert>
        <StudioSection title="World">
          <StudioField label="Saved world UUID" htmlFor="wp-world">
            <StudioSelect id="wp-world" value={world} disabled={!!busy} onChange={e=>{setWorld(e.target.value);clearSelectionData()}}>
              <option value="">Choose a world…</option>{worlds.map(id=><option key={id} value={id}>{id}</option>)}
            </StudioSelect>
          </StudioField>
          <StudioField label="Original simulation seed" htmlFor="wp-seed"><StudioInput id="wp-seed" value={seed} disabled={!!busy} onChange={e=>{setSeed(e.target.value);clearSelectionData()}}/></StudioField>
          <StudioButton variant="primary" disabled={!selected} onClick={()=>void run('Recovering world…',inspect)}>Inspect / recover offline</StudioButton>
          <StudioField label="New world UUID" htmlFor="wp-new" hint="32 lowercase hexadecimal characters"><StudioInput id="wp-new" value={newWorld} disabled={!!busy} onChange={e=>setNewWorld(e.target.value)} spellCheck={false}/></StudioField>
          <div className="world-persistence-actions">
            <StudioButton disabled={!!busy} onClick={()=>setNewWorld(generateUuid())}>Generate UUID</StudioButton>
            <StudioButton disabled={!!busy||!!jobState||!isUuid(newWorld)||!isCounter(seed)||worlds.includes(newWorld)}
              onClick={()=>void run('Creating world…',async()=>{const r=await execute('world_save_create',{world:newWorld,seed});await refreshWorlds();if(alive.current){setWorld(newWorld);clearSelectionData();setStatus(r);setNewWorld('');}})}>Create</StudioButton>
          </div>
        </StudioSection>
        <StudioTabs<Tab> label="World maintenance sections" selected={tab} onChange={setTab}
          items={[{id:'overview',label:'Overview'},{id:'backups',label:'Backups'},{id:'retention',label:'Retention'},{id:'entities',label:'Entities'}]}/>
        {tab==='overview'&&<StudioSection title="Finalized state">
          {!status?<StudioEmptyState title="No inspected save" description="Select a world and inspect it."/>:<>
            <dl className="world-persistence-stats">
              {([['Tick',status.tick],['Sequence',status.sequence],['Generation',status.generation],['Revision',status.revision],
                ['Live',status.live],['Materialized',status.materialized],['Tombstones',status.tombstones],
                ['Ledger bytes',status.ledgerArchiveBytes],['Checkpoint bytes',status.checkpointBytes],
                ['History floor',status.historyFloor],['Retired floor',status.retiredFloor],['Next ID',status.nextIdentity]] as const).map(([k,v])=><div key={k}><dt>{k}</dt><dd>{v}</dd></div>)}
            </dl>
            <StudioProgress label="Ledger record capacity" value={status.live+status.tombstones} max={status.capacity}/>
            <RegionMap status={status}/>
            <StudioField label="Retain revisions" htmlFor="wp-revisions"><StudioInput id="wp-revisions" disabled={!!busy} value={retain} onChange={e=>setRetain(e.target.value)}/></StudioField>
            <StudioButton disabled={!selected||!isCounter(retain)} onClick={()=>void run('Writing checkpoint…',async()=>{setPlan(null);setPage(null);setBackups([]);setGeneration('');setStatus(null);const r=await execute('world_save_checkpoint',{...scope,retainRevisions:retain});if(alive.current)setStatus(r);})}>Create checkpoint</StudioButton>
          </>}
        </StudioSection>}
        {tab==='backups'&&<StudioSection title="Backup generations">
          <div className="world-persistence-actions">
            <StudioButton disabled={!selected} onClick={()=>void run('Reading backups…',()=>readBackups(false))}>List metadata</StudioButton>
            <StudioButton disabled={!selected} onClick={()=>void run('Verifying backups…',()=>readBackups(true))}>Verify all</StudioButton>
          </div>
          {backups.length===0?<StudioEmptyState title="No backup results"/>:backups.map(b=><div key={b.generation} className="au-card world-persistence-backup">
            <strong>Generation {b.generation} {b.active?'(active)':''}</strong>
            <StudioBadge tone={b.status==='recoverable'?'success':b.status==='metadata-only'?'neutral':'warning'}>{b.status}</StudioBadge>
            {b.sequence!==undefined&&<small>Sequence {b.sequence} · Revision {b.revision}</small>}
          </div>)}
          <StudioField label="Verified generation" htmlFor="wp-generation"><StudioSelect id="wp-generation" value={generation} disabled={!!busy} onChange={e=>setGeneration(e.target.value)}>
            <option value="">Choose verified backup…</option>{backups.filter(b=>b.status==='recoverable').map(b=><option key={b.generation} value={b.generation}>{b.generation}</option>)}
          </StudioSelect></StudioField>
          <StudioField label="Fork destination UUID" htmlFor="wp-fork"><StudioInput id="wp-fork" value={forkWorld} disabled={!!busy} onChange={e=>setForkWorld(e.target.value)}/></StudioField>
          <StudioButton disabled={!!busy} onClick={()=>setForkWorld(generateUuid())}>Generate fork UUID</StudioButton>
          <StudioButton disabled={!selected||!generation||!isUuid(forkWorld)||worlds.includes(forkWorld)}
            onClick={()=>void run('Forking verified backup…',async()=>{const r=await execute('world_save_fork',{...scope,generation,newWorld:forkWorld});if(!r.ok)throw new Error('Fork refused');await refreshWorlds();if(alive.current){setNotice('Independent world created: '+r.world);setForkWorld('');}})}>Create independent fork</StudioButton>
        </StudioSection>}
        {tab==='retention'&&<StudioSection title="Backup retention">
          <StudioField label="Keep previous backups" htmlFor="wp-keep"><StudioInput id="wp-keep" value={keep} disabled={!!busy} onChange={e=>{setKeep(e.target.value);setPlan(null)}}/></StudioField>
          <StudioField label="Soft disk budget (bytes, 0 = unlimited)" htmlFor="wp-max"><StudioInput id="wp-max" value={maxBytes} disabled={!!busy} onChange={e=>{setMaxBytes(e.target.value);setPlan(null)}}/></StudioField>
          <StudioButton disabled={!selected||!isCounter(keep)||!isCounter(maxBytes)} onClick={()=>void run('Planning retention…',async()=>{
            setPlan(null);const p=await execute('world_save_retention_preview',{...scope,keepPrevious:keep,maxBytes});if(!p.valid)throw new Error('Retention preview refused');
            if(alive.current)setPlan({...scope,keepPrevious:keep,maxBytes,plan:p});
          })}>Preview cleanup</StudioButton>
          {plan&&<StudioAlert tone="warning" title="Review exact deletion plan">
            <div>Retained bytes: {plan.plan.retainedBytes}</div><div>Remove: {plan.plan.removeGenerations.join(', ')||'None'}</div>
            {plan.plan.overBudget&&<div>Protected backups exceed the soft budget.</div>}
            <StudioButton variant="danger" disabled={!selected||!plan.plan.removeGenerations.length} onClick={()=>void run('Applying confirmed cleanup…',async()=>{
              const confirmed=plan;setPlan(null);const r=await execute('world_save_retention_apply',{world:confirmed.world,seed:confirmed.seed,keepPrevious:confirmed.keepPrevious,maxBytes:confirmed.maxBytes,expectedGenerations:confirmed.plan.removeGenerations});
              if(alive.current){setBackups([]);setGeneration('');setNotice('Removed: '+(r.removed.join(', ')||'None'));if(!r.ok)throw new Error('Cleanup refused or incomplete; refresh preview. Failed generation: '+r.failedGeneration);}
            })}>Confirm deletion of listed generations</StudioButton>
            <StudioButton disabled={!!busy} onClick={()=>setPlan(null)}>Cancel plan</StudioButton>
          </StudioAlert>}
        </StudioSection>}
        {tab==='entities'&&<StudioSection title="Stable identities">
          <StudioButton disabled={!selected||!status} onClick={()=>void run('Reading entity page…',()=>readPage('0',true,status!.revision))}>Bootstrap live entities</StudioButton>
          <StudioButton disabled={!selected||!status} onClick={()=>void run('Reading changes…',()=>readPage(status!.historyFloor,false,'0'))}>Changes since history floor</StudioButton>
          {page&&<><small>Revision {page.revision} · {page.bootstrap?'Bootstrap':'Delta'}</small>
            {page.resetRequired?<StudioAlert tone="warning">Reset required. Refresh the world before continuing.</StudioAlert>:page.records.map(r=><div className="au-card world-persistence-backup" key={r.id}>
              <strong>{r.id}</strong><small>Cell {r.cell.join(', ')} · rev {r.revision}</small><StudioBadge tone={r.live?'neutral':'warning'}>{r.live?'Live':'Retired'}</StudioBadge>
            </div>)}
            <StudioButton disabled={!selected||!page.more||page.resetRequired} onClick={()=>void run('Reading next page…',()=>readPage(page.next,page.bootstrap,page.revision))}>Next page</StudioButton>
          </>}
        </StudioSection>}
      </>}
      {jobState&&<JobProgress job={jobState} detached={detached} onCancel={()=>void cancelJob()} onRelease={()=>void releaseJob()}/>}
      {busy&&!jobState&&<StudioAlert><span className="au-spinner" aria-hidden="true"/> {busy} No percentage is available for this operation.</StudioAlert>}
      {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
      {notice&&<StudioAlert>{notice}</StudioAlert>}
    </div>
  </StudioPanel>;
}
