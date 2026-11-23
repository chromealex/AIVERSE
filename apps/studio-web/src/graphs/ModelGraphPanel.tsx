import {useEffect,useRef,useState} from 'react';
import {invoke,nativeConnected} from '../bridge';
import {StudioPanel,StudioAlert,StudioEmptyState,StudioButton,StudioSelect,StudioSection,StudioTextarea} from '../ui';
import GraphCanvas from './GraphCanvas';
import StrataAnalysisPanel from './StrataAnalysisPanel';
import GraphDiagnostics from './GraphDiagnostics';
import {AnimationConditions,AnimationParameters} from './AnimationConditions';
import {DraftHistory} from './graphFramework';
import type {PortRef} from './graphGeometry';
import type {Positioned} from './graphSelection';
import {modelProjection,connectModelParent,modelingReply,object,replaceAnimationEntry,animationProjection,connectAnimationPose,addAnimationTransition,geometryProjection,connectGeometryOperand,setGeometryBoolean,moveGeometryModifier,geometryModifierKinds,addGeometryModifier,removeGeometryModifier,modelMaterialIds,bindModelMaterial} from './nativeGraphAdapters';
import type {GraphObject} from './nativeGraphAdapters';

const op=async(name:string,params:GraphObject={})=>(await invoke('studio.op',{op:name,params})).result;
export default function ModelGraphPanel({projectPath}:{projectPath:string}){
 const [names,setNames]=useState<string[]>([]),[name,setName]=useState('');
 const [original,setOriginal]=useState<GraphObject|null>(null),[draft,setDraft]=useState<GraphObject|null>(null);
 const [revision,setRevision]=useState(0),[mode,setMode]=useState<'model'|'geometry'|'pose'|'states'>('model');
 const [selection,setSelection]=useState<string[]>([]),[pending,setPending]=useState<PortRef|null>(null);
 const [positions,setPositions]=useState<Record<string,Positioned>>({}),[json,setJson]=useState('');
 const [modifierKind,setModifierKind]=useState('transform');
 const [busy,setBusy]=useState(false),[error,setError]=useState(''),[status,setStatus]=useState('');
 const history=useRef<DraftHistory<GraphObject>|null>(null),epoch=useRef(0),lock=useRef(false);
 const dirty=!!draft&&!!original&&JSON.stringify(draft)!==JSON.stringify(original);
 const graph=draft&&object(draft.graph)?draft.graph:null;
 const view=draft?(mode==='model'?modelProjection(draft):mode==='geometry'?geometryProjection(draft):graph?animationProjection(graph,mode):{nodes:[],links:[]}):{nodes:[],links:[]};
 const active=selection.at(-1)??'';
 const rows=mode==='model'||mode==='geometry'?draft?.nodes:mode==='pose'?graph?.nodes:graph?.states;
 const entry=Array.isArray(rows)?rows.find(n=>object(n)&&n.id===active):undefined;
 const reset=(doc:GraphObject|null,rev:number)=>{setOriginal(doc);setDraft(doc);setRevision(rev);history.current=doc?new DraftHistory(doc):null;setSelection([]);setPending(null);setPositions({});};
 const edit=(doc:GraphObject)=>{if(history.current&&doc!==history.current.current())setDraft(history.current.push(doc));};
 const run=async(task:(live:()=>boolean)=>Promise<void>)=>{
  if(lock.current)return;lock.current=true;setBusy(true);setError('');setStatus('');
  const id=epoch.current,live=()=>id===epoch.current;
  try{await task(live)}catch(e){if(live())setError(e instanceof Error?e.message:String(e))}
  finally{if(live()){lock.current=false;setBusy(false)}}
 };
 useEffect(()=>{
  const token=++epoch.current;lock.current=false;setBusy(false);setNames([]);setName('');reset(null,0);
  if(projectPath&&nativeConnected())void run(async live=>{
   const result=await op('modeling_list');if(!live())return;
   if(!object(result)||!Array.isArray(result.models))throw new Error('Model list unavailable');
   setNames(result.models.filter((n):n is string=>typeof n==='string'));
  });
  return ()=>{if(epoch.current===token)epoch.current++};
 },[projectPath]);
 useEffect(()=>{setJson(entry?JSON.stringify(entry,null,2):'')},[entry]);
 const open=(asset:string)=>void run(async live=>{if(!asset||dirty)return;
  const r=modelingReply(await op('modeling_get',{name:asset}));if(!live())return;
  setName(asset);reset(r.document,r.revision);
 });
 const connect=(to:PortRef)=>{
  if(!draft||!pending||busy)return;
  if(mode==='model'){
   const next=connectModelParent(draft,pending,to);
   if(next===draft){setError('Invalid parent or cyclic hierarchy');return;}edit(next);
  }else if(mode==='geometry'){
   const next=connectGeometryOperand(draft,pending,to);
   if(next===draft){setError('Geometry operands must reference preceding mesh nodes');return;}edit(next);
  }else if(graph){
   const next=mode==='pose'?connectAnimationPose(graph,pending,to):addAnimationTransition(graph,pending,to);
   if(next===graph){setError('Invalid pose dependency or state transition');return;}edit({...draft,graph:next});
  }
  setPending(null);setError('');
 };
 const apply=()=>{
  if(!draft||!active)return;
  try{const parsed:unknown=JSON.parse(json);if(!object(parsed))throw new Error('Expected object');
   const base=mode==='model'||mode==='geometry'?draft:graph;if(!base)return;
   const next=replaceAnimationEntry(base,mode==='states'?'states':'pose',active,parsed);
   if(next===base)throw new Error('Keep existing entry ID');
   edit(mode==='model'||mode==='geometry'?next:{...draft,graph:next});setError('');
  }catch(e){setError(e instanceof Error?e.message:String(e))}
 };
 const save=()=>void run(async live=>{
  if(!name||!draft||!dirty)return;
  // Native modeling_edit compiles the entire candidate under its revision lock before committing.
  const r=modelingReply(await op('modeling_edit',{name,expectedRevision:revision,commands:[{op:'replace',document:draft}]}));
  if(!live())return;reset(r.document,r.revision);setStatus('Native build and revision-checked commit completed.');
 });
 const validate=()=>void run(async live=>{
  if(!draft)return;
  const result=await op('modeling_validate',{document:draft});
  if(!object(result)||result.ok===false||result.error)throw new Error('Native validation failed: '+JSON.stringify(result));
  if(live())setStatus('Native validation returned a build report. No file written.');
 });
 const move=(p:Positioned[])=>setPositions(old=>({...old,...Object.fromEntries(p.map(n=>[mode+':'+n.id,n]))}));
 return <StudioPanel title="Model source graph" flush>
  {!projectPath||!nativeConnected()?<StudioEmptyState title="Open a native project"/>:<>
  <div className="au-row" style={{flexWrap:'wrap'}}>
   <StudioSelect aria-label="Model asset" value={name} disabled={busy||dirty} onChange={e=>open(e.target.value)}><option value="">Choose model</option>{names.map(n=><option key={n} value={n}>{n}</option>)}</StudioSelect>
   <StudioSelect aria-label="Model graph view" value={mode} onChange={e=>{const v=e.target.value;setMode(v==='pose'||v==='states'||v==='geometry'?v:'model');setSelection([]);setPending(null)}}><option value="model">Part hierarchy</option><option value="geometry">Geometry dependencies</option><option value="pose">Embedded pose graph</option><option value="states">Embedded states</option></StudioSelect>
   <StudioButton size="sm" disabled={!draft||busy} onClick={validate}>Validate</StudioButton>
   <StudioButton size="sm" disabled={!dirty||busy} onClick={save}>Save</StudioButton>
   <StudioButton size="sm" disabled={!dirty||busy} onClick={()=>reset(original,revision)}>Discard</StudioButton>
   <StudioButton size="sm" disabled={busy||!history.current?.canUndo()} onClick={()=>{if(history.current)setDraft(history.current.undo());setPending(null)}}>Undo</StudioButton>
   <StudioButton size="sm" disabled={busy||!history.current?.canRedo()} onClick={()=>{if(history.current)setDraft(history.current.redo());setPending(null)}}>Redo</StudioButton>
   {name&&<span>Revision {revision}{dirty?' · Draft':''}</span>}
  </div>
  {error&&<StudioAlert tone="danger">{error}</StudioAlert>}{status&&<StudioAlert tone="success">{status}</StudioAlert>}
  <StudioAlert>Part hierarchy edits parent transforms. Geometry view edits native CSG operands and modifier order; operands use local geometry, not node transforms. Layout is local. Native validation governs saves.</StudioAlert>
  {draft&&(mode==='model'||mode==='geometry'||graph)?<>
   <GraphCanvas view={{...view,nodes:view.nodes.map(n=>({...n,...positions[mode+':'+n.id]}))}} selected={active} selection={selection} onSelection={setSelection}
    label="Model graph" disabled={busy} pendingOutput={pending} onOutput={setPending} onInput={connect} onCancel={()=>setPending(null)}
    onSelect={id=>setSelection(id?[id]:[])} onMove={(id,x,y)=>move([{id,x,y}])} onMoveGroup={move}/>
   <GraphDiagnostics view={view} policy={{cycles:mode==='states'?'allow':'reject'}}/>
   {name&&(mode==='model'||mode==='geometry')&&<StrataAnalysisPanel projectPath={projectPath} suggestedModel={'models/'+name+'.glb'} disabled={busy}/>}
   {graph&&(mode==='pose'||mode==='states')&&<AnimationParameters doc={graph} disabled={busy} onEdit={next=>edit({...draft,graph:next})}/>}
   {entry&&<StudioSection title={'Entry '+active}>{mode==='geometry'&&object(entry)&&object(entry.geometry)&&<>
    <StudioSelect aria-label="Source material" disabled={busy} value={String(entry.material??'')} onChange={e=>edit(bindModelMaterial(draft,active,e.target.value))}><option value="">Default material</option>{modelMaterialIds(draft).map(id=><option key={id} value={id}>{id}</option>)}</StudioSelect>
    <div className="au-row"><StudioSelect aria-label="Geometry modifier" disabled={busy} value={modifierKind} onChange={e=>setModifierKind(e.target.value)}>{geometryModifierKinds.map(kind=><option key={kind} value={kind}>{kind}</option>)}</StudioSelect><StudioButton size="sm" disabled={busy} onClick={()=>edit(addGeometryModifier(draft,active,modifierKind))}>Add modifier</StudioButton></div>
    {entry.geometry.kind==='boolean'&&<StudioSelect aria-label="Boolean operation" disabled={busy} value={String(entry.geometry.operation??'union')} onChange={e=>edit(setGeometryBoolean(draft,active,e.target.value))}>
     <option value="union">Union</option><option value="subtract">Subtract</option><option value="intersect">Intersect</option></StudioSelect>}
    {Array.isArray(entry.geometry.modifiers)&&entry.geometry.modifiers.map((m,i)=>object(m)&&<div key={i} className="au-row"><span>{String(m.kind)}</span>
     <StudioButton size="sm" disabled={busy||i===0} onClick={()=>edit(moveGeometryModifier(draft,active,i,-1))}>Up</StudioButton>
     <StudioButton size="sm" disabled={busy||i===((entry.geometry as GraphObject).modifiers as unknown[]).length-1} onClick={()=>edit(moveGeometryModifier(draft,active,i,1))}>Down</StudioButton>
     <StudioButton size="sm" disabled={busy} onClick={()=>edit(removeGeometryModifier(draft,active,i))}>Remove</StudioButton>
    </div>)}
   </>}
   {mode==='states'&&graph&&object(entry)&&Array.isArray(entry.transitions)&&entry.transitions.map((t,i)=>object(t)&&<AnimationConditions key={i} doc={graph} state={active} index={i} transition={t} disabled={busy} onEdit={next=>edit({...draft,graph:next})}/>)}
   <StudioTextarea aria-label="Model entry JSON" rows={8} value={json} onChange={e=>setJson(e.target.value)}/>
    <StudioButton disabled={busy} onClick={apply}>Apply entry draft</StudioButton></StudioSection>}
  </>:<StudioEmptyState title={draft?'No embedded graph':'Choose a model source'} />}
  </>}
 </StudioPanel>;
}
