import {useEffect,useMemo,useRef,useState} from 'react';
import {invoke,nativeConnected} from '../bridge';
import {StudioAlert,StudioButton,StudioEmptyState,StudioInput,StudioPanel,StudioSection,StudioSelect,StudioTextarea} from '../ui';
import GraphCanvas from './GraphCanvas';
import {DraftHistory} from './graphFramework';
import type {PortRef} from './graphGeometry';
import type {Positioned} from './graphSelection';
import {animationProjection,connectAnimationPose,fxConnectArgs,fxProjection,graphNames,graphReply,object,replaceAnimationEntry} from './nativeGraphAdapters';
import type {FxNodeInfo,GraphObject,NativeGraphDomain} from './nativeGraphAdapters';

const errorText=(e:unknown)=>e instanceof Error?e.message:String(e);
async function op(name:string,params:GraphObject={}):Promise<unknown>{
 return (await invoke('studio.op',{op:name,params})).result;
}
function requireOk(result:unknown){
 if(!object(result)||result.ok!==true)throw new Error(object(result)?JSON.stringify(result.errors??result.error??result):'Native validation failed');
}
export default function NativeGraphPanel({domain,projectPath}:{domain:NativeGraphDomain;projectPath:string}){
 const fx=domain==='spectrafx',prefix=fx?'fx_graph':'animation_graph';
 const [names,setNames]=useState<string[]>([]),[name,setName]=useState('');
 const [catalog,setCatalog]=useState<FxNodeInfo[]>([]),[nodeType,setNodeType]=useState('');
 const [original,setOriginal]=useState<GraphObject|null>(null),[draft,setDraft]=useState<GraphObject|null>(null);
 const history=useRef<DraftHistory<GraphObject>|null>(null);
 const [mode,setMode]=useState<'pose'|'states'>('pose');
 const [selection,setSelection]=useState<string[]>([]),[pending,setPending]=useState<PortRef|null>(null);
 const [positions,setPositions]=useState<Record<string,Positioned>>({});
 const [property,setProperty]=useState(''),[json,setJson]=useState('');
 const [busy,setBusy]=useState(false),[error,setError]=useState(''),[status,setStatus]=useState('');
 const epoch=useRef(0),locked=useRef(false);
 const dirty=!!draft&&!!original&&JSON.stringify(draft)!==JSON.stringify(original);
 const active=selection.at(-1)??'';
 const reset=(doc:GraphObject|null)=>{setOriginal(doc);setDraft(doc);history.current=doc?new DraftHistory(doc):null;setSelection([]);setPending(null);setPositions({});setJson('');};
 const edit=(doc:GraphObject)=>{if(history.current&&doc!==history.current.current())setDraft(history.current.push(doc));};
 const run=async(task:(current:()=>boolean)=>Promise<void>)=>{
  if(locked.current)return;locked.current=true;setBusy(true);setError('');setStatus('');
  const token=epoch.current,current=()=>epoch.current===token;
  try{await task(current);}catch(e){if(current())setError(errorText(e));}
  finally{if(current()){locked.current=false;setBusy(false);}}
 };
 useEffect(()=>{
  const token=++epoch.current;locked.current=false;setBusy(false);setNames([]);setCatalog([]);setName('');reset(null);setError('');setStatus('');
  if(!projectPath||!nativeConnected())return;
  void run(async current=>{
   const list=await op(prefix+'_list');if(!current())return;setNames(graphNames(list));
   if(fx){const result=await op('fx_graph_node_catalog');if(!current())return;
    if(!object(result)||!Array.isArray(result.nodes))throw new Error('Native node catalog unavailable');
    const entries=result.nodes.filter((n):n is FxNodeInfo=>object(n)&&typeof n.type==='string'&&typeof n.group==='string'&&typeof n.v2Only==='boolean'&&Array.isArray(n.ports)&&n.ports.every(p=>typeof p==='string'));
    setCatalog(entries);setNodeType(entries[0]?.type??'');
   }
  });
  return ()=>{if(epoch.current===token)epoch.current++;};
 },[domain,projectPath]);
 const view=useMemo(()=>{
  if(!draft)return {nodes:[],links:[]};
  const result=fx?fxProjection(draft,catalog):animationProjection(draft,mode);
  return {...result,nodes:result.nodes.map(n=>({...n,...positions[mode+':'+n.id]}))};
 },[draft,fx,catalog,mode,positions]);
 const entry=useMemo(()=>{
  const rows=draft?.[fx||mode==='pose'?'nodes':'states'];
  return Array.isArray(rows)?rows.find(n=>object(n)&&n.id===active):undefined;
 },[draft,active,fx,mode]);
 useEffect(()=>{setJson(entry?(fx?'':JSON.stringify(entry,null,2)):'');},[entry,fx]);
 const open=(asset:string)=>void run(async current=>{
  if(!asset||dirty)return;
  const doc=graphReply(await op(prefix+'_get',{name:asset}));if(!current())return;
  setName(asset);reset(doc);
 });
 const nativeEdit=(args:GraphObject)=>void run(async current=>{
  if(!draft||!fx)return;
  const result=await op('fx_graph_edit',{...args,graph:draft});requireOk(result);
  if(!current())return;edit(graphReply(result));setPending(null);
  if(object(result)&&typeof result.newNodeId==='string')setSelection([result.newNodeId]);
  setStatus('Native in-memory edit applied; save explicitly.');
 });
 const connect=(to:PortRef)=>{
  if(!pending||!draft||busy)return;
  if(fx){const args=fxConnectArgs(pending,to);if(!args){setError('Incompatible execution/data ports');return;}nativeEdit(args);}
  else if(mode==='pose'){
   const next=connectAnimationPose(draft,pending,to);
   if(next===draft){setError('Pose inputs must reference an earlier compatible pose node');return;}
   edit(next);setPending(null);
  }
 };
 const move=(next:Positioned[])=>{
  if(!draft)return;
  if(!fx){setPositions(old=>({...old,...Object.fromEntries(next.map(p=>[mode+':'+p.id,p]))}));return;}
  const map=new Map(next.map(p=>[p.id,p]));
  if(!Array.isArray(draft.nodes))return;
  edit({...draft,nodes:draft.nodes.map(n=>{if(!object(n)||typeof n.id!=='string')return n;const p=map.get(n.id);return p?{...n,position:[p.x,p.y]}:n;})});
 };
 const selectedLinks=view.links.filter(e=>e.to===active||e.from===active);
 const disconnect=(id:string)=>{
  if(!draft||!fx||busy)return;
  const link=view.links.find(e=>e.id===id);if(!link)return;
  nativeEdit({action:'disconnect',from:link.from,to:link.to,...(link.input.startsWith('data:')?{toPort:link.input.slice(5)}:{})});
 };
 const validate=()=>void run(async current=>{
  if(!draft)return;
  requireOk(await op(prefix+'_validate',{graph:draft}));if(current())setStatus('Native validation passed. No file written.');
 });
 const save=()=>void run(async current=>{
  if(!draft||!original||!name||!dirty)return;
  requireOk(await op(prefix+'_validate',{graph:draft}));if(!current())return;
  if(fx){
   // Native fx_graph_set currently lacks an expected revision. Preflight is not atomic.
   const actual=graphReply(await op(prefix+'_get',{name}));if(!current())return;
   if(JSON.stringify(actual)!==JSON.stringify(original))throw new Error('Asset changed externally; discard and reload before saving.');
  }
  const result=await op(prefix+'_set',{name,graph:draft,...(!fx?{expected:original}:{})});
  requireOk(result);if(!current())return;
  const saved=graphReply(await op(prefix+'_get',{name}));if(!current())return;reset(saved);setStatus('Saved and read back from native asset.');
 });
 const applyEntry=()=>{
  if(!draft||!active||fx)return;
  try{const next:unknown=JSON.parse(json);if(!object(next))throw new Error('Expected an object');
   const changed=replaceAnimationEntry(draft,mode,active,next);if(changed===draft)throw new Error('Entry ID must remain unchanged');
   edit(changed);setError('');
  }catch(e){setError(errorText(e));}
 };
 return <StudioPanel title={fx?'SpectraFX Graph':'MotionWeave Graph'} flush>
  {!projectPath||!nativeConnected()?<StudioEmptyState title="Open a native project" description="Graph operations require the native Studio host."/>:<>
  <div className="au-row" style={{flexWrap:'wrap'}}>
   <StudioSelect aria-label="Graph asset" value={name} disabled={busy||dirty} onChange={e=>open(e.target.value)}>
    <option value="">Choose graph</option>{names.map(n=><option key={n} value={n}>{n}</option>)}
   </StudioSelect>
   <StudioButton size="sm" disabled={!draft||busy} onClick={validate}>Validate</StudioButton>
   <StudioButton size="sm" disabled={!dirty||busy} onClick={save}>Save</StudioButton>
   <StudioButton size="sm" disabled={!dirty||busy} onClick={()=>reset(original)}>Discard</StudioButton>
   <StudioButton size="sm" disabled={busy||!history.current?.canUndo()} onClick={()=>{if(history.current)setDraft(history.current.undo());setPending(null);}}>Undo</StudioButton>
   <StudioButton size="sm" disabled={busy||!history.current?.canRedo()} onClick={()=>{if(history.current)setDraft(history.current.redo());setPending(null);}}>Redo</StudioButton>
   {!fx&&<StudioSelect aria-label="Graph view" value={mode} disabled={busy} onChange={e=>{setMode(e.target.value==='states'?'states':'pose');setSelection([]);setPending(null);}}><option value="pose">Pose DAG</option><option value="states">State transitions</option></StudioSelect>}
   {fx&&<><StudioSelect aria-label="Node type" value={nodeType} onChange={e=>setNodeType(e.target.value)}>{catalog.filter(n=>draft?.version===2||!n.v2Only).map(n=><option key={n.type} value={n.type}>{n.type}</option>)}</StudioSelect>
   <StudioButton size="sm" disabled={!draft||busy||!nodeType} onClick={()=>nativeEdit({action:'add',type:nodeType})}>Add node</StudioButton>
   <StudioButton size="sm" disabled={!active||busy} onClick={()=>nativeEdit({action:'deleteNode',node:active})}>Delete node</StudioButton></>}
  </div>
  {error&&<StudioAlert tone="danger">{error}</StudioAlert>}{status&&<StudioAlert tone="success">{status}</StudioAlert>}
  {fx?<StudioAlert tone="warning">Published graph save uses a read-back preflight, not an atomic revision lock. Avoid concurrent edits.</StudioAlert>:<StudioAlert>Pose dependencies must point to earlier nodes; state transitions may loop. Layout is local. State transition editing uses the JSON inspector.</StudioAlert>}
  {draft?<><GraphCanvas view={view} selected={active} selection={selection} onSelection={setSelection} label={domain+' graph'}
   disabled={busy} pendingOutput={pending} onOutput={fx||mode==='pose'?setPending:undefined} onInput={fx||mode==='pose'?connect:undefined} onCancel={()=>setPending(null)}
   onSelect={id=>setSelection(id?[id]:[])} onMove={(id,x,y)=>move([{id,x,y}])} onMoveGroup={move}/>
   {entry&&<StudioSection title={'Selected: '+active}>
    {fx?<><pre>{JSON.stringify(entry,null,2)}</pre>
     {selectedLinks.map(link=><div className="au-row" key={link.id}><span>{link.from} → {link.to} ({link.input})</span><StudioButton size="sm" disabled={busy} onClick={()=>disconnect(link.id)}>Disconnect</StudioButton></div>)}
     <StudioInput placeholder="Property path" aria-label="Property path" value={property} onChange={e=>setProperty(e.target.value)}/>
     <StudioTextarea aria-label="Property JSON value" value={json} onChange={e=>setJson(e.target.value)}/>
     <StudioButton disabled={busy||!property.trim()} onClick={()=>{try{nativeEdit({action:'setProperty',node:active,property,value:JSON.parse(json)})}catch(e){setError(errorText(e))}}}>Apply property JSON</StudioButton></>:<>
      <StudioTextarea aria-label="Animation entry JSON" value={json} onChange={e=>setJson(e.target.value)} rows={6}/>
      <StudioButton disabled={busy} onClick={applyEntry}>Apply entry draft</StudioButton></>}
   </StudioSection>}</>:<StudioEmptyState title={busy?'Loading graph assets…':'Choose an existing graph'} />}
  </>}
 </StudioPanel>;
}
