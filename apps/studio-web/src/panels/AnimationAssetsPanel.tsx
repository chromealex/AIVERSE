import RagdollPanel from './RagdollPanel';
import { useEffect, useId, useState, type ReactNode } from 'react';
import { invoke } from '../bridge';
import './AnimationAssetsPanel.css';

interface Avatar { format:string; model:string; bones:Record<string,string> }
interface Catalog { names:string[]; roles:string[] }
interface Saved { document:Avatar; revision:string }
interface Suggestion { document:Avatar; nodes:string[] }
async function operation<T>(project:string, op:string, params:Record<string,unknown>) {
  return (await invoke('studio.op',{op,expectedProject:project,params})).result as T;
}

export default function AnimationAssetsPanel({project,tools}:{project:string;tools:ReactNode}) {
  const [tab,setTab]=useState<'avatar'|'ragdoll'|'tools'>('avatar');
  const advanced=tab==='tools';
  const [catalog,setCatalog]=useState<Catalog>({names:[],roles:[]});
  const [models,setModels]=useState<string[]>([]);
  const [nodes,setNodes]=useState<string[]>([]);
  const [model,setModel]=useState('');
  const [name,setName]=useState('');
  const [draft,setDraft]=useState<Avatar>();
  const [original,setOriginal]=useState<(Saved & {name:string})>();
  const [dirty,setDirty]=useState(false);
  const [busy,setBusy]=useState(false);
  const [revision,reload]=useState(0);
  const [query,setQuery]=useState('');
  const [message,setMessage]=useState('');
  const [error,setError]=useState('');
  const listId=useId();
  useEffect(()=>{
    if (!project || advanced) return;
    let live=true;
    void operation<Catalog>(project,'animation_asset_list',{kind:'avatar'})
      .then(c=>{if(live){setCatalog(c);if(c.names.length===1)setName(old=>old||c.names[0]);}}).catch(e=>{if(live)setError(String(e));});
    void invoke('studio.assets',{}).then(r=>{
      if(!live)return;
      const available=r.files.filter(f=>/\.(gltf|glb|fbx|obj)$/i.test(f.path)).map(f=>'content/'+f.path);
      setModels(available);
      if(available.length===1)setModel(old=>old||available[0]);
    }).catch(()=>{});
    return ()=>{live=false;};
  },[project,advanced,revision]);
  async function run(action:()=>Promise<void>) {
    setBusy(true);setError('');setMessage('');
    try {await action();}catch(e){setError(e instanceof Error?e.message:String(e));}finally{setBusy(false);}
  }
  const suggest=(file:string)=>operation<Suggestion>(project,'animation_avatar_suggest',{model:file,includeNodes:true});
  async function open() {
    const saved=await operation<Saved>(project,'animation_asset_get',{kind:'avatar',name});
    setDraft(saved.document);setOriginal({...saved,name});setModel(saved.document.model);setDirty(false);setNodes([]);
    const suggested=await suggest(saved.document.model);
    setNodes(suggested.nodes);setMessage('Opened animations/avatar/'+name+'.json');
  }
  function discard() {
    setDraft(original?.document);setModel(original?.document.model??model);
    if(original)setName(original.name);
    setDirty(false);setNodes([]);setError('');setMessage('');
    if(original) void run(async()=>{setNodes((await suggest(original.document.model)).nodes);});
  }
  function assign(role:string,value:string) {
    if(!draft)return;
    const bones={...draft.bones};
    if(value)bones[role]=value;else delete bones[role];
    setDraft({...draft,bones});setDirty(true);setError('');setMessage('');
  }
  if(!project)return <div className="avatar-page">Open a project to edit animation assets.</div>;
  const roles=catalog.roles.filter(role=>(role+' '+(draft?.bones[role]??'')).toLowerCase().includes(query.toLowerCase()));
  const sameModel=!!draft && model===draft.model;
  const feedback=<>{busy&&<p role="status">Working…</p>}{message&&<p role="status">{message}</p>}{error&&<p role="alert">{error}</p>}</>;
  return <div className="avatar-page"><header><h2>Animation Assets</h2><nav aria-label="Animation asset tools">
    <button aria-pressed={tab==='avatar'} onClick={()=>setTab('avatar')}>Humanoid avatar</button>
    <button disabled={busy} aria-pressed={tab==='ragdoll'} onClick={()=>setTab('ragdoll')}>Ragdoll</button>
    <button disabled={dirty||busy} aria-pressed={advanced} onClick={()=>setTab('tools')}>Asset tools</button>
  </nav></header><RagdollPanel project={project} avatar={draft} avatarName={name} active={tab==='ragdoll'}/>{advanced ? tools : tab==='avatar' && <>
    <fieldset disabled={busy}><div className="avatar-source">
      <section><h3>Avatar file</h3><label>Asset name<input list={listId+'-files'} placeholder="hero" value={name} onChange={e=>setName(e.target.value)}/></label>
        <datalist id={listId+'-files'}>{catalog.names.map(n=><option key={n} value={n}/>)}</datalist>
        {catalog.names.length>0 && <label>Saved avatars<select aria-label="Saved avatars" value={catalog.names.includes(name)?name:''} onChange={e=>setName(e.target.value)}>
          <option value="">Select a file</option>{catalog.names.map(n=><option key={n}>{n}</option>)}</select></label>}
        <button disabled={!name||dirty} onClick={()=>void run(open)}>Open avatar</button></section>
      <section><h3>Source skeleton</h3><label>Model path<input placeholder="content/character.gltf" list={listId+'-models'} value={model} onChange={e=>{setModel(e.target.value);setMessage('');}}/></label>
        <datalist id={listId+'-models'}>{models.map(f=><option key={f} value={f}/>)}</datalist>
        <button disabled={!model||dirty} onClick={()=>void run(async()=>{
          const r=await suggest(model);setDraft(r.document);setNodes(r.nodes);setDirty(true);
          const stem=model.split('/').pop()?.replace(/\.[^.]+$/,'').replace(/[^a-zA-Z0-9_-]/g,'_').slice(0,64);
          if(stem)setName(old=>old||stem);
          setMessage('Review the suggested bone paths before saving.');
        })}>Suggest mappings</button></section>
    </div>
    {draft && <><div className="avatar-title"><h3>Bone mapping</h3><small>{Object.keys(draft.bones).length} / {catalog.roles.length} mapped</small></div>
      <label>Filter bones<input type="search" placeholder="Role or node path" value={query} onChange={e=>setQuery(e.target.value)}/></label>
      <div className="avatar-mapping">{roles.map(role=><label key={role}><span>{role.replace(/([a-z])([A-Z])/g,'$1 $2')}{(role==='Head'||role==='Hips')&&<small>Required</small>}</span>
        <input aria-label={role+' bone path'} list={listId+'-nodes'} value={draft.bones[role]??''} placeholder="Unmapped" onChange={e=>assign(role,e.target.value)}/></label>)}
        {!roles.length&&<p>No matching bones.</p>}</div>
      <datalist id={listId+'-nodes'}>{nodes.map((path,i)=><option key={i} value={path}/>)}</datalist>
      <footer>
        <button disabled={!sameModel} onClick={()=>void run(async()=>{const r=await operation<{mapped:number}>(project,'animation_avatar_validate',{document:draft});setMessage('Validated '+r.mapped+' mapped bones.');})}>Validate avatar</button>
        <button className="avatar-save" disabled={!name||!sameModel||(!dirty&&name===original?.name)} onClick={()=>void run(async()=>{
          const r=await operation<Saved>(project,'animation_asset_save',{kind:'avatar',name,document:draft,...(name===original?.name?{expectedRevision:original.revision}:{})});
          setOriginal({...r,name});setDraft(r.document);setDirty(false);reload(v=>v+1);setMessage('Saved animations/avatar/'+name+'.json');
        })}>Save avatar</button>
        {dirty&&<button onClick={discard}>Discard edits</button>}{feedback}
      </footer>{!sameModel&&<p>Restore the mapped model path to validate or save this avatar.</p>}
    </>}</fieldset>
    {!draft&&feedback}
  </>}</div>;
}
