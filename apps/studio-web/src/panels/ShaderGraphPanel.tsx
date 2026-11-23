import { useEffect, useMemo, useRef, useState } from 'react';
import { GitBranch, RefreshCw, Save, Search, RotateCcw, CheckCircle2 } from 'lucide-react';
import { invoke, nativeConnected, onNativeEvent } from '../bridge';
import type { StudioShaderGraphDocument, StudioShaderGraphOp } from '../types';
import { addShaderGraphNode, removeShaderGraphNode, shaderNodeCatalog, connectShaderInput, disconnectShaderInput, setShaderValue, duplicateShaderNode, copyShaderNodes, pasteShaderNodes, moveShaderNodes, arrangeShaderNodes } from './shaderGraphLayout';
import type {ShaderClipboard} from './shaderGraphLayout';
import GraphCanvas from '../graphs/GraphCanvas';
import {shaderAdapters,shaderProjection} from '../graphs/shaderAdapter';
import type {PortRef} from '../graphs/graphGeometry';
import {samePort} from '../graphs/graphGeometry';
import {DraftHistory} from '../graphs/graphFramework';
import {StudioButton,StudioInput,StudioSelect,StudioSection} from '../ui';
import './ShaderGraphPanel.css';

export default function ShaderGraphPanel({projectPath}:{projectPath:string}) {
 const [names,setNames]=useState<string[]>([]);
 const [operations,setOperations]=useState<StudioShaderGraphOp[]>([]);
 const [selectedOp,setSelectedOp]=useState('');
 const [selection,setSelection]=useState<string[]>([]);
 const selectedNode=selection.at(-1)??'';
 const setSelectedNode=(id:string)=>setSelection(id?[id]:[]);
 const [clipboard,setClipboard]=useState<ShaderClipboard|null>(null);
 const [pendingName,setPendingName]=useState('');
 const [selected,setSelected]=useState('');
 const [document,setDocument]=useState<StudioShaderGraphDocument|null>(null);
 const [draft,setDraft]=useState<StudioShaderGraphDocument['graph']|null>(null);
 const history=useRef<DraftHistory<StudioShaderGraphDocument['graph']>|null>(null);
 const [pendingOutput,setPendingOutput]=useState<PortRef|null>(null);
 const [nodeQuery,setNodeQuery]=useState('');
 const resetDraft=(next:StudioShaderGraphDocument['graph']|null)=>{
  history.current=next?new DraftHistory(next):null;setDraft(next);setPendingOutput(null);
 };
 const editDraft=(next:StudioShaderGraphDocument['graph'])=>{
  if(history.current&&next!==history.current.current())setDraft(history.current.push(next));
 };
 const [message,setMessage]=useState('');
 const [search,setSearch]=useState('');
 const [error,setError]=useState('');
 const [busy,setBusy]=useState(false);
 const [nonce,setNonce]=useState(0);
 const connected=nativeConnected();
 useEffect(()=>onNativeEvent(event=>{if(event==='workspace.changed')setNonce(n=>n+1)}),[]);
 useEffect(()=>{
  let closed=false;
  setError('');
  setOperations([]);setSelectedOp('');
  if(connected&&!projectPath){setNames([]);return;}
  setBusy(true);
  void Promise.all([invoke('studio.shaderGraphs',{}),invoke('studio.shaderGraphCatalog',{})])
    .then(([result,catalog])=>{
      if(!closed){setNames(result.graphs);setOperations(catalog.nodes);}
    })
    .catch(e=>{if(!closed)setError(String(e))})
    .finally(()=>{if(!closed)setBusy(false)});
  return ()=>{closed=true};
 },[connected,projectPath,nonce]);
 useEffect(()=>{setSelected('');setDocument(null);resetDraft(null);setPendingName('');},[projectPath]);
 useEffect(()=>{
  let closed=false;
  setDocument(null);resetDraft(null);setMessage('');setSelectedNode('');
  if(!selected)return;
  setBusy(true);setError('');
  void invoke('studio.shaderGraphGet',{name:selected})
    .then(result=>{if(!closed){setDocument(result);resetDraft(structuredClone(result.graph));}})
    .catch(e=>{if(!closed)setError(String(e))})
    .finally(()=>{if(!closed)setBusy(false)});
  return ()=>{closed=true};
 },[selected]);
 const view=useMemo(()=>shaderProjection(draft??document?.graph??{},operations),[draft,document,operations]);
 const dirty=!!document&&!!draft&&JSON.stringify(draft)!==JSON.stringify(document.graph);
 const compatibleOps=shaderNodeCatalog(operations,draft?.domain??'material',nodeQuery);
 const chosenOp=compatibleOps.find(op=>op.id===selectedOp)??compatibleOps[0];
 const activeNode=draft?.nodes?.find(n=>n.id===selectedNode);
 const selectedIds=selection.filter(id=>draft?.nodes?.some(n=>n.id===id));
 const copyNodes=()=>{if(draft&&selectedIds.length)setClipboard(copyShaderNodes(draft,selectedIds));};
 const pasteNodes=()=>{
  if(!draft||busy||!clipboard)return;
  const result=pasteShaderNodes(draft,clipboard,operations);
  if(result.graph===draft){setError('Cannot paste: incompatible domain, operation or graph limit.');return;}
  editDraft(result.graph);setSelection(result.ids);setError('');
 };
 const ports=Object.fromEntries((draft?.nodes??[]).map(n=>[n.id,{inputs:operations.find(o=>o.id===n.op&&o.domains.includes(draft?.domain??'material'))?.inputs??0,output:true}]));
 const wire=(input:PortRef)=>{
  if(!draft||busy||!pendingOutput)return;
  const next=connectShaderInput(draft,operations,pendingOutput.node,input.node,Number(input.port));
  if(next===draft){setError('Connection unchanged or invalid (check ports and cycles).');return;}
  editDraft(next);setPendingOutput(null);setError('');
 };
 const duplicate=()=>{
  if(!draft||busy||!activeNode)return;
  if(selectedIds.length>1){const result=pasteShaderNodes(draft,copyShaderNodes(draft,selectedIds),operations);if(result.graph!==draft){editDraft(result.graph);setSelection(result.ids);}return;}
  const next=duplicateShaderNode(draft,activeNode.id);
  if(next!==draft){editDraft(next);setSelectedNode(next.nodes?.at(-1)?.id??'');}
 };
 const switchGraph=(name:string)=>{
  if(name===selected)return;
  if(dirty){setPendingName(name);setMessage('Save or discard your current graph draft before switching.');return;}
  setSelected(name);
 };
 const discardAndSwitch=()=>{
  if(pendingName){setSelected(pendingName);setPendingName('');}
 };
 const createNode=()=>{
  if(!draft||busy||!chosenOp)return;
  const next=addShaderGraphNode(draft,chosenOp);
  if(next===draft){setError('Cannot add node: invalid operation or node limit.');return;}
  editDraft(next);
  setSelectedNode(next.nodes?.[next.nodes.length-1]?.id??'');
  setMessage('Node added to local draft. Validate before saving.');
  setError('');
 };
 const deleteNode=()=>{
  if(!draft||!selectedNode||busy)return;
  const next=removeShaderGraphNode(draft,selectedNode);
  if(next===draft){setError('Cannot delete a node still referenced by the graph.');return;}
  editDraft(next);setSelectedNode('');
  setMessage('Unused node removed from local draft.');
  setError('');
 };
 const validate=async()=>{
  if(!draft||busy)return;
  setBusy(true);setError('');setMessage('');
  try{
   const result=await invoke('studio.shaderGraphValidate',{graph:draft});
   setMessage('Compiled: '+result.nodes+' nodes, '+result.samples+' samples. No file written.');
  }catch(e){setError(e instanceof Error?e.message:String(e));}
  finally{setBusy(false);}
 };
 const save=async()=>{
  if(!draft||!selected||busy||!dirty)return;
  setBusy(true);setError('');setMessage('');
  try{
   const result=await invoke('studio.shaderGraphSave',{name:selected,graph:draft});
   setDocument(result);resetDraft(structuredClone(result.graph));
   setMessage('Graph saved to shaders/'+selected+'.json. MapDoc Undo does not apply.');
  }catch(e){setError(e instanceof Error?e.message:String(e));}
  finally{setBusy(false);}
 };
 const discard=()=>{if(document){resetDraft(structuredClone(document.graph));setMessage('Draft discarded.');setError('');}};
 return <section className="studio-shader-graphs">
  <header className="graph-heading"><span><GitBranch size={16}/> Shader Graphs</span>
   <button className="icon-button" aria-label="Reload Shader Graphs"
    disabled={busy||dirty} onClick={()=>{setSelected('');setDocument(null);resetDraft(null);setNonce(n=>n+1)}}><RefreshCw size={15}/></button>
  </header>
  <p className="graph-readonly">Drag nodes to arrange the graph. Validate before explicit Save.
    Select output then input to connect. Shift-click or drag a selection box; middle-drag or Alt-drag to pan. GPU preview still requires the legacy editor.</p>
  {error&&<p className="graph-error" role="alert">{error}</p>}
  {pendingName&&<div className="graph-pending">
    <span>Discard unsaved changes and open {pendingName}?</span>
    <button onClick={discardAndSwitch}>Discard &amp; open</button>
    <button onClick={()=>setPendingName('')}>Cancel</button>
  </div>}
  {message&&<p className="graph-message" role="status">{message}</p>}
  <div className="graph-columns">
   <aside className="graph-library">
    <label className="graph-search"><Search size={14}/>
     <input value={search} aria-label="Search Shader Graphs"
      onChange={e=>setSearch(e.target.value)} placeholder="Graph name…"/>
    </label>
    {names.filter(x=>x.toLowerCase().includes(search.toLowerCase())).map(name=>
     <button key={name} className={selected===name?'active':''} onClick={()=>switchGraph(name)}>{name}</button>)}
    {!busy&&names.length===0&&<p>No project graphs found.</p>}
   </aside>
   <main className="graph-inspector">
    {!selected ? <div className="graph-empty">Select a graph to inspect its nodes and connections.</div> :
     busy ? <div className="graph-empty">Compiling and loading graph…</div> :
     document ? <>
      <div className="graph-summary"><strong>{selected}</strong><span>{document.graph.domain??'Unknown domain'}</span>
       <span>{draft?.nodes?.length??document.nodes} nodes · {document.samples} saved texture samples</span>
       <span>{dirty?'● Unsaved graph draft':'Saved'}</span>
       <StudioInput aria-label="Search node catalog" placeholder="Find node…" value={nodeQuery} onChange={e=>setNodeQuery(e.target.value)}/>
       <select className="graph-add-select" aria-label="Graph node operation"
         value={chosenOp?.id??''} onChange={event=>setSelectedOp(event.target.value)}>
         {compatibleOps.map(op=><option key={op.id} value={op.id}>{op.id}</option>)}
       </select>
       <button disabled={!draft||busy||!chosenOp} onClick={createNode}>+ Node</button>
       <StudioButton size="sm" disabled={!activeNode||busy} onClick={duplicate}>Duplicate</StudioButton>
       <StudioButton size="sm" disabled={!selectedIds.length||busy} onClick={copyNodes}>Copy</StudioButton>
       <StudioButton size="sm" disabled={!draft||!clipboard||busy} onClick={pasteNodes}>Paste</StudioButton>
       <StudioButton size="sm" disabled={!draft||busy} onClick={()=>{if(draft){const next=arrangeShaderNodes(draft);if(next===draft)setError('Layout unavailable: cyclic or exceeds canvas bounds.');else editDraft(next);}}}>Auto layout</StudioButton>
       <span>{selectedIds.length} selected</span>
       <StudioButton size="sm" disabled={!history.current?.canUndo()||busy} onClick={()=>{if(history.current)setDraft(history.current.undo());setPendingOutput(null)}}>Undo</StudioButton>
       <StudioButton size="sm" disabled={!history.current?.canRedo()||busy} onClick={()=>{if(history.current)setDraft(history.current.redo());setPendingOutput(null)}}>Redo</StudioButton>
       <button disabled={!selectedNode||busy} onClick={deleteNode}>Delete node</button>
       <button disabled={!draft||busy} onClick={()=>void validate()}><CheckCircle2 size={13}/> Validate</button>
       <button disabled={!dirty||busy} onClick={discard}><RotateCcw size={13}/> Discard</button>
       <button disabled={!dirty||busy} onClick={()=>void save()}><Save size={13}/> Save graph</button>
      </div>
      <GraphCanvas view={view} selected={selectedNode} label={'Editable node layout for '+selected}
       disabled={!draft||busy} onSelect={setSelectedNode} selection={selectedIds} onSelection={setSelection}
       onMoveGroup={positions=>{if(draft)editDraft(moveShaderNodes(draft,positions))}} pendingOutput={pendingOutput}
       onOutput={port=>setPendingOutput(old=>samePort(old,port)?null:port)} onInput={wire}
       onCancel={()=>setPendingOutput(null)} commands={[
        {id:'copy',label:'Copy',shortcut:'mod+c',disabled:!selectedIds.length,run:copyNodes},
        {id:'paste',label:'Paste',shortcut:'mod+v',disabled:!clipboard,run:pasteNodes},
        {id:'duplicate',label:'Duplicate',shortcut:'mod+d',disabled:!activeNode,run:duplicate},
        {id:'undo',label:'Undo',shortcut:'mod+z',disabled:!history.current?.canUndo(),run:()=>{if(history.current)setDraft(history.current.undo());setPendingOutput(null)}},
        {id:'redo',label:'Redo',shortcut:'mod+shift+z',disabled:!history.current?.canRedo(),run:()=>{if(history.current)setDraft(history.current.redo());setPendingOutput(null)}},
        {id:'all',label:'Select all',shortcut:'mod+a',run:()=>setSelection(view.nodes.map(n=>n.id))}
       ]}
       onMove={(id,x,y)=>{if(draft)editDraft(shaderAdapters.get(draft.domain??'material')?.move(draft,id,x,y)??draft)}}/>
      {pendingOutput&&<StudioButton size="sm" onClick={()=>setPendingOutput(null)}>Cancel connection from {pendingOutput.node}</StudioButton>}
      {activeNode&&draft&&<StudioSection title={'Node: '+activeNode.id}>
       <div className="au-stack">
        <span>{activeNode.op}</span>
        {Array.from({length:ports[activeNode.id]?.inputs??0},(_,i)=><div className="au-row" key={i}>
         <StudioSelect aria-label={'Input '+(i+1)} value={activeNode.inputs?.[i]??''} onChange={e=>{
          const from=e.target.value;
          const next=from?connectShaderInput(draft,operations,from,activeNode.id,i):disconnectShaderInput(draft,activeNode.id,i);
          if(next===draft&&from)setError('Invalid or unchanged connection.');else{editDraft(next);setError('');}
         }}><option value="">Disconnected</option>{draft.nodes?.filter(n=>n.id!==activeNode.id).map(n=><option key={n.id} value={n.id}>{n.id}</option>)}</StudioSelect>
        </div>)}
        {['constant','parameter'].includes(activeNode.op)&&<div className="au-row">
         {['R','G','B','A'].map((label,i)=><StudioInput key={label} aria-label={label} type="number" step="any"
          value={activeNode.value?.[i]??0} onChange={e=>{if(e.target.value.trim()==='')return;const v=Array.from({length:4},(_,j)=>activeNode.value?.[j]??0);v[i]=Number(e.target.value);editDraft(setShaderValue(draft,activeNode.id,v))}}/>)}
        </div>}
       </div>
      </StudioSection>}
      <div className="graph-outputs">Outputs: {Object.entries(draft?.outputs??document.graph.outputs??{}).map(
       ([k,v])=>k+' ← '+v).join(' · ')||'none'}</div>
     </> : <div className="graph-empty">No graph loaded.</div>}
   </main>
  </div>
  <footer className="panel-footer">Existing project assets only · generated GLSL/MSL is not exposed through this UI</footer>
 </section>;
}
