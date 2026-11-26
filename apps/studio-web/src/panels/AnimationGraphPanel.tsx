import { useEffect, useRef, useState, type ReactNode } from 'react';
import { DraftHistory } from '../graphs/graphFramework';
import { invoke } from '../bridge';
import AnimationPoseCanvas from './AnimationPoseCanvas';
import AnimationProperties from './AnimationProperties';
import type {Value} from './RigInspector';
import AnimationRuntimePanel from './AnimationRuntimePanel';
import AnimationSkeletonPreview from './AnimationSkeletonPreview';
import RigInspector, {type RigUnit,type ItemPresets} from './RigInspector';
import './graphSurface.css';
import './AnimationGraphPanel.css';

type Node = {loop?:boolean;speed?:number;graph?:string;bindings?:Record<string,string>;id:string;kind:string;position?:number[];a?:string;b?:string;input?:string;clip?:string;units?:RigUnit[];weight?:number};
type Condition = {parameter:string;op?:string;value?:number};
type Transition = {to:string;fade?:number;after?:number;when:Condition[]|Record<string,never>};
type State = {id:string;output:string;transitions?:Transition[]};
type Graph = {nodes:Node[];output:string;model?:string;parameters?:Record<string,unknown>;states?:State[];initial?:string;graphs?:Record<string,Graph>};
export interface GraphOpenRequest {project:string;name:string;graph:Graph;revision:string;inline:boolean}
export interface GraphOpenRoute {request?:GraphOpenRequest;consume:()=>void}
// Dock tabs unmount inactive panels. Retain active edit histories for this window.
const drafts=new Map<string,{name:string;history:DraftHistory<Graph>;saved:Graph|null;revision:string;inline:boolean}>();
const kinds=['bind','clip','blend','additive','twoBoneIK','rig','stack','subgraph','motionMatching','native'];
const ports=(n:Node):('a'|'b'|'input')[]=>n.kind==='additive'||n.kind==='blend'?['a','b']:n.input!==undefined?['input']:[];
const nodeOptions=(nodes:Node[])=>nodes.map(n=><option key={n.id}>{n.id}</option>);

export default function AnimationGraphPanel({projectPath,tools,rigMode,route,historyVersion,openAttachments}:{historyVersion?:number|string;openAttachments:()=>void;route:GraphOpenRoute;projectPath:string;tools:ReactNode;rigMode:boolean}){
 const cacheKey=JSON.stringify([projectPath,rigMode]);
 const title=rigMode?'Animation Rig':'Animation Graph';
 const restored=drafts.get(cacheKey);
 const [itemPresets,setItemPresets]=useState<ItemPresets>({});
 const [models,setModels]=useState<Record<string,string>>({});
 const [selectionTarget,setSelectionTarget]=useState({id:'',model:''});
 const target=models[selectionTarget.id]===selectionTarget.model?selectionTarget.id:'';
 useEffect(()=>{
  let active=true;
  if(projectPath)void invoke('studio.rules',{}).then(r=>{
   if(!active)return;
   const next:Record<string,string>={};
   for(const [id,type] of Object.entries(r.resolved)){
    if(!type||typeof type!=='object'||!('view' in type))continue;
    const view=type.view;
    if(view&&typeof view==='object'&&'model' in view&&typeof view.model==='string'&&view.model)next[id]=view.model;
   }
   setModels(next);
  }).catch(e=>{if(active)setError(String(e));});
  return ()=>{active=false;};
 },[projectPath,historyVersion]);
 const [names,setNames]=useState<string[]>([]),[name,setName]=useState(restored?.name??''),[newName,setNewName]=useState('');
 const history=useRef(restored?.history);
 const [draft,setDraft]=useState<Graph|null>(restored?.history.current()??null),[saved,setSaved]=useState<Graph|null>(restored?.saved??null);
 const [inline,setInline]=useState(restored?.inline??false);
 const [revision,setRevision]=useState(restored?.revision??'');
 const [requestedScope,setScope]=useState(''),[selectedNodes,setSelectedNodes]=useState<string[]>([]);
 const selection=selectedNodes.at(-1)??'';
 const select=(id:string)=>setSelectedNodes(id?[id]:[]);
 const [nodeName,setNodeName]=useState(''),[pickedBone,setPickedBone]=useState('');
 useEffect(()=>setPickedBone(''),[name,draft?.model]);
 useEffect(()=>setNodeName(selection),[selection]);
 const [kind,setKind]=useState('clip'),[tab,setTab]=useState<'nodes'|'states'|'rig'|'preview'|'runtime'|'tools'>(rigMode?'rig':'nodes');
 const [stateId,setStateId]=useState(''),[stateName,setStateName]=useState('');
 const [parameter,setParameter]=useState(''),[notice,setNotice]=useState('');
 const [busy,setBusy]=useState(false),[error,setError]=useState('');
 const [childName,setChildName]=useState('');
 const children=draft?.graphs??{};
 const scope=Object.hasOwn(children,requestedScope)?requestedScope:'';
 const scoped=scope?draft?.graphs?.[scope]:draft;
 const graph=scoped&&Array.isArray(scoped.nodes)?scoped:null;
 const scalars=Object.keys(graph?.parameters??{}).filter(k=>!Array.isArray(graph?.parameters?.[k]));
 const current=graph?.nodes.find(n=>n.id===selection),states=graph?.states??[];
 const managed=['id','kind','position',...(current?ports(current):[]),
  ...(current?.kind==='clip'?['clip','loop','speed']:[]),...(current?.weight!==undefined?['weight']:[]),
  ...(current?.kind==='subgraph'?['graph','bindings']:[]),...(current?.kind==='rig'?['units']:[])];
 const state=states.find(s=>s.id===stateId)??states[0];
 const dirty=JSON.stringify(saved)!==JSON.stringify(draft);
 const [preview,setPreview]=useState({token:0,error:''});
 useEffect(()=>{
  if(!projectPath)return;
  void invoke('studio.animationEditorState',{project:projectPath,panel:rigMode?'rig':'graph',graph:draft,
   dirty:!!draft&&JSON.stringify(saved)!==JSON.stringify(draft),token:preview.token,error:error||preview.error})
   .catch(e=>console.warn('Animation editor status:',e));
 },[projectPath,rigMode,draft,saved,preview,error]);
 useEffect(()=>{
  if(history.current&&(inline||dirty||history.current.canUndo()||history.current.canRedo()))
   drafts.set(cacheKey,{name,history:history.current,saved,revision,inline});
  else drafts.delete(cacheKey);
 },[cacheKey,name,dirty,draft,saved,revision,inline]);
 async function op(method:string,params:Record<string,unknown>={}){
  const reply=await invoke('studio.op',{op:'animation_graph_'+method,params,expectedProject:projectPath});
  return reply.result as {graph:Graph;graphs:string[];rigItemPresets:ItemPresets;revision:string};
 }
 useEffect(()=>{
  let active=true;
  if(projectPath)void op('list').then(r=>{if(active){setNames(r.graphs);setItemPresets(r.rigItemPresets??{});}}).catch(e=>{if(active)setError(String(e));});
  return ()=>{active=false;};
 },[projectPath]);
 async function run(fn:()=>Promise<void>){
  if(busy)return;
  setBusy(true);setError('');setNotice('');
  try{await fn();}catch(e){setError(String(e));}finally{setBusy(false);}
 }
 function change(next:Graph){
  if(history.current)setDraft(history.current.push(next));setError('');setNotice('');
 }
 function inScope(next:Graph){
  return scope&&draft?{...draft,graphs:{...draft.graphs,[scope]:next}}:next;
 }
 function patchNode(id:string,patch:Partial<Node>){
  if(graph)change(inScope({...graph,nodes:graph.nodes.map(n=>n.id===id?{...n,...patch}:n)}));
 }
 function updateState(next:State){
  if(graph)change(inScope({...graph,states:states.map(s=>s.id===next.id?next:s)}));
 }
 function openDocument(id:string,doc:Graph,version:string,isInline:boolean){
  history.current=new DraftHistory(doc,40);setDraft(doc);setSaved(doc);setRevision(version);setInline(isInline);
  setName(id);setChildName('');setSelectionTarget({id:'',model:''});setScope('');select('');setStateId('');setError('');setNotice('');
 }
 async function load(id:string){
  const r=await op('get',{name:id});if(!Array.isArray(r.graph?.nodes))throw new Error('Asset has no pose graph');
  openDocument(id,r.graph,r.revision,false);
 }
 useEffect(()=>{
  const request=route.request;
  if(!request||request.project!==projectPath||busy)return;
  route.consume();
  if(dirty){setNotice('Save or discard current edits before opening another graph.');return;}
  openDocument(request.name,request.graph,request.revision,request.inline);
  setTab(rigMode?'rig':'nodes');
 },[route.request,projectPath,busy,dirty,rigMode]);
 async function saveGraph(){
  const r=await op('set',{name,graph:draft,...(revision?{expectedRevision:revision}:{})});
  setInline(false);setNames(current=>current.includes(name)?current:[...current,name].sort());
  history.current=new DraftHistory(r.graph,40);setDraft(r.graph);setSaved(r.graph);setRevision(r.revision);
  return r.revision;
 }
 async function applyGraph(){
  if(!draft||!target||!models[target])return;
  await op('validate',{graph:{...draft,model:models[target]}});
  const saveFirst=dirty||inline;
  const version=saveFirst?await saveGraph():revision;
  try{
   await op('apply',{name,target,expectedRevision:version,expectedModel:models[target]});
  }catch(e){
   if(saveFirst)throw new Error('Graph saved. Could not apply to '+target+': '+String(e));
   throw e;
  }
  setNotice('Applied '+name+' to '+target+'.');
 }
 async function edit(action:string,params:Record<string,unknown>){
  const r=await op('edit',{action,graph:draft,scope,...params});change(r.graph);
 }
 const nodeIds=new Set(graph?.nodes.map(n=>n.id));
 return <section className="anim-editor" aria-label={title}>
  <header><strong>{title}</strong><select aria-label="Animation graph asset" value={name} disabled={busy||dirty} onChange={e=>void run(()=>load(e.target.value))}>
   <option value="" disabled>Select graph</option>{name&&!names.includes(name)&&<option>{name}</option>}{names.map(n=><option key={n}>{n}</option>)}</select>
   <button aria-label="Reload animation graph" disabled={busy||dirty||!projectPath} onClick={()=>void run(async()=>{setNames((await op('list')).graphs);if(name)await load(name);})}>Reload</button>
   <input aria-label="New graph name" placeholder="New graph name" value={newName} onChange={e=>setNewName(e.target.value)}/>
   <button disabled={busy||dirty||!projectPath||!/^[-\w]{1,64}$/.test(newName)||names.includes(newName)} onClick={()=>void run(async()=>{
    const id=newName;const initial=await op('edit',{action:'new'});await op('set',{name:id,graph:initial.graph});
    setNames((await op('list')).graphs);await load(id);setNewName('');
   })}>Create graph</button>
   <button onClick={openAttachments}>Attachments</button></header>
  <nav aria-label="Graph views">{(['nodes','states','rig','preview','runtime','tools'] as const).map(t=><button key={t} aria-pressed={t===tab} onClick={()=>setTab(t)}>{t==='nodes'?'Pose graph':t==='states'?'States':t==='rig'?'Rig units':t==='preview'?'Skeleton preview':t==='runtime'?'Live entities':'Operations'}</button>)}</nav>
  {error&&<p role="alert">{error}</p>}{notice&&<p role="status">{notice}</p>}
  {tab==='runtime'?<AnimationRuntimePanel project={projectPath} rig={rigMode}/>:tab==='tools'?tools:!graph?<p>{projectPath?'Open or create an animation graph.':'Open a project to edit animation graphs.'}</p>:<>
   <div className="anim-actions"><select aria-label="Graph scope" value={scope} disabled={busy} onChange={e=>{setScope(e.target.value);select('');setStateId('');}}>
    <option value="">Root graph</option>{Object.keys(draft?.graphs??{}).map(s=><option key={s}>{s}</option>)}</select>
    <input aria-label="New subgraph name" placeholder="Subgraph name" value={childName} onChange={e=>setChildName(e.target.value)}/>
    <button disabled={busy||!childName.trim()||Object.hasOwn(children,childName.trim())||Object.keys(children).length>=64}
     onClick={()=>void run(async()=>{const child=childName.trim();await edit('addSubgraph',{name:child});setScope(child);select('');setStateId('');setChildName('');})}>Add subgraph</button>
    <label>Model<input value={draft?.model??''} disabled={busy} placeholder="content/model.glb" onChange={e=>{if(draft)change({...draft,model:e.target.value});}}/></label>
    <button disabled={busy||!draft?.model} onClick={()=>void run(async()=>{await op('validate',{graph:draft});setNotice('Graph compiled against '+draft?.model);})}>Validate</button>
    <button disabled={busy||!history.current?.canUndo()} onClick={()=>{if(history.current)setDraft(history.current.undo());}}>Undo</button>
    <button disabled={busy||!history.current?.canRedo()} onClick={()=>{if(history.current)setDraft(history.current.redo());}}>Redo</button>
    <button disabled={busy||!dirty} onClick={()=>{history.current=saved?new DraftHistory(saved,40):undefined;setDraft(saved);setScope('');select('');setError('');}}>Discard</button>
    <button disabled={busy||(!dirty&&!inline)} onClick={()=>void run(async()=>{await saveGraph();})}>Save graph</button>
   </div>
   <div className="anim-actions">
    <label>Target type<select aria-label="Animation target type" value={target} disabled={busy||!Object.keys(models).length}
      onChange={e=>{const id=e.target.value;setSelectionTarget({id,model:models[id]??''});if(id&&draft&&draft.model!==models[id])change({...draft,model:models[id]});}}>
     <option value="">{Object.keys(models).length?'Select a type':'No types with models'}</option>
     {Object.keys(models).sort().map(id=><option key={id}>{id}</option>)}
    </select></label>
    <button disabled={busy||!models[target]} onClick={()=>void run(applyGraph)}>{dirty||inline?'Save and apply':'Apply to type'}</button>
   </div>
   {tab==='preview'&&draft?<AnimationSkeletonPreview onStatus={setPreview} projectPath={projectPath} graph={draft} scope={scope} panel={rigMode?'rig':'graph'} onPick={setPickedBone} busy={busy}
    onKeep={controls=>{if(graph)change(inScope({...graph,parameters:{...graph.parameters,...controls}}));}}/>:tab==='nodes'?<>
    <AnimationPoseCanvas key={JSON.stringify([name,scope])} graph={graph} selection={selectedNodes} busy={busy}
     select={setSelectedNodes} connect={(from,to)=>void run(()=>edit('connect',{source:from.node,id:to.node,port:to.port}))}
     move={updates=>{
      const byId=new Map(updates.map(n=>[n.id,n]));
      change(inScope({...graph,nodes:graph.nodes.map(n=>{const update=byId.get(n.id);return update?{...n,position:[update.x,update.y]}:n;})}));
     }}/>
    <fieldset disabled={busy}><label>Node type<select value={kind} onChange={e=>setKind(e.target.value)}>{kinds.map(k=><option key={k}>{k}</option>)}</select></label>
     <button onClick={()=>void run(async()=>{let i=1;while(nodeIds.has(kind+i))i++;const id=kind+i;
      await edit('add',{kind,id});select(id);})}>Add node</button>
     {current&&<><label>Node name<input value={nodeName} onChange={e=>setNodeName(e.target.value)}/></label>
      <button disabled={!nodeName.trim()||nodeIds.has(nodeName.trim())} onClick={()=>void run(async()=>{const id=nodeName.trim();await edit('rename',{id:current.id,name:id});select(id);})}>Rename</button>{ports(current).map(port=><label key={port}>{port}<select value={current[port]??''} onChange={e=>void run(()=>edit('connect',{source:e.target.value,id:current.id,port}))}>{nodeOptions(graph.nodes.filter(n=>n.id!==current.id))}</select></label>)}
      {current.kind==='subgraph'&&<>
       <label>Child graph<select aria-label="Referenced subgraph" value={current.graph??''} disabled={busy}
        onChange={e=>void run(()=>edit('setSubgraph',{id:current.id,name:e.target.value}))}>
        <option value="" disabled>Select subgraph</option>{Object.keys(children).filter(id=>id!==scope).map(id=><option key={id}>{id}</option>)}
       </select></label>
       {Object.entries(children[current.graph??'']?.parameters??{}).map(([child,value])=><label key={child}>{child}
        <select aria-label={'Bind '+child} value={current.bindings?.[child]??''} disabled={busy} onChange={e=>{
         const bindings={...current.bindings};if(e.target.value)bindings[child]=e.target.value;else delete bindings[child];patchNode(current.id,{bindings});
        }}><option value="">Child default</option>{Object.entries(graph.parameters??{}).filter(([,v])=>Array.isArray(v)===Array.isArray(value)).map(([key])=><option key={key}>{key}</option>)}</select>
       </label>)}
      </>}
      {current.kind==='clip'&&<>
       <label>Clip<input value={current.clip??''} onChange={e=>patchNode(current.id,{clip:e.target.value})}/></label>
       <label>Playback speed<input type="number" min="0" step="any" value={current.speed??1} onChange={e=>{if(e.target.validity.valid&&Number.isFinite(e.target.valueAsNumber))patchNode(current.id,{speed:e.target.valueAsNumber});}}/></label>
       <label><input type="checkbox" checked={current.loop??true} onChange={e=>patchNode(current.id,{loop:e.target.checked})}/>Loop clip</label>
      </>}
      {current.weight!==undefined&&<label>Weight<input type="number" min="0" max="1" step=".05" value={current.weight} onChange={e=>{if(e.target.validity.valid&&Number.isFinite(e.target.valueAsNumber))patchNode(current.id,{weight:e.target.valueAsNumber});}}/></label>}
      <AnimationProperties key={current.id} title="Node properties" reserved={managed}
       fields={Object.fromEntries(Object.entries(current).filter(([key])=>!managed.includes(key))) as Record<string,Value>}
       change={fields=>{const updated={...Object.fromEntries(Object.entries(current).filter(([key])=>managed.includes(key))),...fields} as Node;
        change(inScope({...graph,nodes:graph.nodes.map(n=>n===current?updated:n)}));}}/>
      <button disabled={current.id===graph.output} onClick={()=>change(inScope({...graph,output:current.id}))}>Use as output</button>
      <button onClick={()=>void run(async()=>{await edit('remove',{id:current.id});select('');})}>Remove node</button></>}
    </fieldset>
   </>:tab==='rig'?<RigInspector pickedBone={pickedBone} presets={itemPresets} nodes={graph.nodes.filter(n=>n.kind==='rig')} selected={selection} select={select} busy={busy}
    addRig={()=>void run(async()=>{let i=1;while(nodeIds.has('rig'+i))i++;await edit('add',{kind:'rig',id:'rig'+i});select('rig'+i);})}
    nativeEdit={(action,params)=>void run(()=>edit(action,params))}
    update={(id,units)=>patchNode(id,{units})}/>:<fieldset disabled={busy} className="anim-states">
    <label>New state<input value={stateName} onChange={e=>setStateName(e.target.value)}/></label>
    <button disabled={!stateName.trim()||states.some(s=>s.id===stateName.trim())||states.length>=64} onClick={()=>{const id=stateName.trim();change(inScope({...graph,states:[...states,{id,output:graph.output,transitions:[]}]}));setStateId(id);setStateName('');}}>Add state</button>
    <AnimationProperties parameters title="Parameter defaults" fields={(graph.parameters??{}) as Record<string,Value>}
      change={parameters=>change(inScope({...graph,parameters}))}/>
    <label>Scalar parameter<input value={parameter} placeholder="speed" onChange={e=>setParameter(e.target.value)}/></label>
    <button disabled={!parameter.trim()||Object.hasOwn(graph.parameters??{},parameter.trim())} onClick={()=>{change(inScope({...graph,parameters:{...graph.parameters,[parameter.trim()]:0}}));setParameter('');}}>Add parameter</button>
    {state&&<><label>State<select value={state.id} onChange={e=>setStateId(e.target.value)}>{states.map(s=><option key={s.id}>{s.id}</option>)}</select></label>
     <label>Output pose<select value={state.output} onChange={e=>updateState({...state,output:e.target.value})}>{nodeOptions(graph.nodes)}</select></label>
     <button disabled={(graph.initial??states[0]?.id)===state.id} onClick={()=>change(inScope({...graph,initial:state.id}))}>Set initial state</button>
     <button disabled={states.length<2||(state.transitions?.length??0)>=64} onClick={()=>updateState({...state,transitions:[...state.transitions??[],{to:states.find(s=>s!==state)!.id,fade:.2,after:0,when:[]}]})}>Add transition</button>
     {(state.transitions??[]).map((t,i)=>{const patch=(value:Partial<Transition>)=>updateState({...state,transitions:state.transitions!.map((v,j)=>j===i?{...v,...value}:v)});
      return <div className="anim-transition" key={i}><span>{state.id} →</span><label>Destination<select value={t.to} onChange={e=>patch({to:e.target.value})}>{states.map(s=><option key={s.id}>{s.id}</option>)}</select></label>
       {(['after','fade'] as const).map(k=><label key={k}>{k==='after'?'Delay (s)':'Fade (s)'}<input type="number" min="0" step="any" value={t[k]??(k==='fade'?.2:0)} onChange={e=>{if(e.target.validity.valid&&Number.isFinite(e.target.valueAsNumber))patch({[k]:e.target.valueAsNumber});}}/></label>)}
       {(Array.isArray(t.when)?t.when:[]).map((c,index)=>{
        const conditions=Array.isArray(t.when)?t.when:[];
        const editCondition=(value:Partial<Condition>)=>patch({when:conditions.map((v,j)=>j===index?{...v,...value}:v)});
        return <div className="anim-condition" key={index}><label>When<select value={c.parameter} onChange={e=>editCondition({parameter:e.target.value})}>{scalars.map(k=><option key={k}>{k}</option>)}</select></label>
         <label>Comparison<select value={c.op??'eq'} onChange={e=>editCondition({op:e.target.value})}>{Object.entries({eq:'=',ne:'≠',gt:'>',ge:'≥',lt:'<',le:'≤'}).map(([op,symbol])=><option key={op} value={op}>{symbol}</option>)}</select></label>
         <label>Value<input type="number" step="any" value={c.value??1} onChange={e=>{if(Number.isFinite(e.target.valueAsNumber))editCondition({value:e.target.valueAsNumber});}}/></label>
         <button onClick={()=>patch({when:conditions.filter((_,j)=>j!==index)})}>Remove condition</button></div>;
       })}
       <button disabled={!scalars.length||(Array.isArray(t.when)&&t.when.length>=32)} onClick={()=>patch({when:[...Array.isArray(t.when)?t.when:[],{parameter:scalars[0],op:'gt',value:0}]})}>Add condition</button>
       <button onClick={()=>updateState({...state,transitions:state.transitions!.filter((_,j)=>j!==i)})}>Remove transition</button></div>;})}</>}
   </fieldset>}
  </>}
 </section>;
}
