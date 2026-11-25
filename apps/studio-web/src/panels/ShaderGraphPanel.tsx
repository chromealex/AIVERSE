import { useEffect, useMemo, useRef, useState } from 'react';
import { GitBranch, RefreshCw, Save, Search, RotateCcw, CheckCircle2 } from 'lucide-react';
import { invoke, nativeConnected, onNativeEvent } from '../bridge';
import type { StudioShaderGraphDocument, StudioShaderGraphOp } from '../types';
import { layoutShaderGraph, moveShaderGraphNode, addShaderGraphNode, removeShaderGraphNode, setShaderInput, setShaderOutput, shaderOutputNames, effectiveShaderValue, setShaderComponent } from './shaderGraphLayout';
import './graphSurface.css';
import './ShaderGraphPanel.css';

export default function ShaderGraphPanel({projectPath}:{projectPath:string}) {
 const [names,setNames]=useState<string[]>([]);
 const [operations,setOperations]=useState<StudioShaderGraphOp[]>([]);
 const [selectedOp,setSelectedOp]=useState('');
 const [selectedNode,setSelectedNode]=useState('');
 const [pendingName,setPendingName]=useState('');
 const [selected,setSelected]=useState('');
 const [document,setDocument]=useState<StudioShaderGraphDocument|null>(null);
 const [draft,setDraft]=useState<StudioShaderGraphDocument['graph']|null>(null);
 const drag=useRef<{id:string; x:number; y:number; originalX:number; originalY:number}|null>(null);
 const [message,setMessage]=useState('');
 const [search,setSearch]=useState('');
 const [error,setError]=useState('');
 const [busy,setBusy]=useState(false);
 const [nonce,setNonce]=useState(0);
 const connected=nativeConnected();
 useEffect(()=>onNativeEvent(event=>{if(event==='workspace.changed')setNonce(n=>n+1)}),[]);
 useEffect(()=>{
  let closed=false;
  setSelected('');setDocument(null);setDraft(null);setError('');setMessage('');
  setPendingName('');setOperations([]);setSelectedOp('');
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
 useEffect(()=>{
  let closed=false;
  setDocument(null);setDraft(null);setMessage('');setSelectedNode('');
  if(!selected)return;
  setBusy(true);setError('');
  void invoke('studio.shaderGraphGet',{name:selected})
    .then(result=>{if(!closed){setDocument(result);setDraft(structuredClone(result.graph));}})
    .catch(e=>{if(!closed)setError(String(e))})
    .finally(()=>{if(!closed)setBusy(false)});
  return ()=>{closed=true};
 },[selected]);
 const diagram=useMemo(()=>layoutShaderGraph(draft??document?.graph??{}),[draft,document]);
 const inspected=draft?.nodes?.find(node=>node.id===selectedNode);
 const selectedValue=inspected&&draft?effectiveShaderValue(draft,inspected):undefined;
 const editField=(field:'name'|'mask'|'texture'|'srgb',value:string|boolean)=>{
  if(!draft||!inspected)return;
  setDraft({...draft,nodes:draft.nodes?.map(n=>n===inspected?{...n,[field]:value}:n)});
  setMessage('');setError('');
 };
 const sourceOptions=draft?.nodes?.map(node=><option key={node.id} value={node.id}>{node.id} · {node.op}</option>);
 const dirty=!!document&&!!draft&&JSON.stringify(draft)!==JSON.stringify(document.graph);
 const compatibleOps=operations.filter(op=>op.domains.includes(draft?.domain??'material'));
 const chosenOp=compatibleOps.find(op=>op.id===selectedOp)??compatibleOps[0];
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
  setDraft(next);
  setSelectedNode(next.nodes?.[next.nodes.length-1]?.id??'');
  setMessage('Node added to local draft. Validate before saving.');
  setError('');
 };
 const deleteNode=()=>{
  if(!draft||!selectedNode||busy)return;
  const next=removeShaderGraphNode(draft,selectedNode);
  if(next===draft){setError('Cannot delete a node still referenced by the graph.');return;}
  setDraft(next);setSelectedNode('');
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
   setDocument(result);setDraft(structuredClone(result.graph));
   setMessage('Graph saved to shaders/'+selected+'.json. MapDoc Undo does not apply.');
  }catch(e){setError(e instanceof Error?e.message:String(e));}
  finally{setBusy(false);}
 };
 const discard=()=>{if(document){setDraft(structuredClone(document.graph));setMessage('Draft discarded.');setError('');}};
 return <section className="studio-shader-graphs">
  <header className="graph-heading"><span><GitBranch size={16}/> Shader Graphs</span>
   <button className="icon-button" aria-label="Reload Shader Graphs"
    disabled={busy} onClick={()=>setNonce(n=>n+1)}><RefreshCw size={15}/></button>
  </header>
  <p className="graph-readonly">Select a node to edit its inputs. Drag nodes to arrange the graph.</p>
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
       <span>{document.nodes} nodes · {document.samples} texture samples</span>
       <span>{dirty?'Unsaved changes':'Saved'}</span>
       <select className="graph-add-select" aria-label="Graph node operation"
         value={chosenOp?.id??''} onChange={event=>setSelectedOp(event.target.value)}>
         {compatibleOps.map(op=><option key={op.id} value={op.id}>{op.id}</option>)}
       </select>
       <button disabled={!draft||busy||!chosenOp} onClick={createNode}>+ Node</button>
       <button disabled={!selectedNode||busy} onClick={deleteNode}>Delete node</button>
       <button disabled={!draft||busy} onClick={()=>void validate()}><CheckCircle2 size={13}/> Validate</button>
       <button disabled={!dirty||busy} onClick={discard}><RotateCcw size={13}/> Discard</button>
       <button disabled={!dirty||busy} onClick={()=>void save()}><Save size={13}/> Save graph</button>
      </div>
      <div className="graph-canvas-scroll graph-surface">
       <svg width={diagram.width} height={diagram.height} viewBox={'0 0 '+diagram.width+' '+diagram.height}
        role="group" aria-label={'Editable node layout for '+selected}
        onPointerMove={event=>{
          if(!drag.current||!draft)return;
          const current=drag.current;
          // Client pointer delta is in zoomed CSS pixels, while node positions
          // are stored in unscaled SVG viewBox coordinates.
          const zoom=Number(window.document.documentElement.style.zoom)||1;
          const x=current.originalX+(event.clientX-current.x)/zoom;
          const y=current.originalY+(event.clientY-current.y)/zoom;
          setDraft(previous=>previous?moveShaderGraphNode(previous,current.id,x,y):previous);
        }}
        onPointerUp={()=>{drag.current=null;}}
        onPointerCancel={()=>{drag.current=null;}}>
        {diagram.edges.map(edge=><path key={edge.key} d={'M '+edge.x1+' '+edge.y1+
          ' C '+(edge.x1+55)+' '+edge.y1+', '+(edge.x2-55)+' '+edge.y2+', '+edge.x2+' '+edge.y2}
          className="graph-wire" data-emphasis={selectedNode?(edge.from===selectedNode||edge.to===selectedNode?'active':'quiet'):undefined}/>)}
        {diagram.nodes.map(node=><g key={node.id} className="graph-draggable" role="button" tabIndex={0} aria-label={node.op+' · '+node.id}
          aria-pressed={selectedNode===node.id}
          onKeyDown={event=>{if(event.key==='Enter'||event.key===' '){event.preventDefault();setSelectedNode(node.id);}}}
          transform={'translate('+node.x+' '+node.y+')'}
          onPointerDown={event=>{
            if(!draft||busy)return;
            event.preventDefault();
            setSelectedNode(node.id);
            drag.current={id:node.id,x:event.clientX,y:event.clientY,originalX:node.x,originalY:node.y};
            event.currentTarget.setPointerCapture(event.pointerId);
          }}
          onPointerUp={event=>{
            drag.current=null;
            if(event.currentTarget.hasPointerCapture(event.pointerId))
              event.currentTarget.releasePointerCapture(event.pointerId);
          }}>
         <title>{[node.op,node.id,...node.values].join(' · ')}</title>
         <rect className="graph-node-body" width="130" height={node.height} rx="7"/>
         <path className="graph-node-accent" data-source={node.values.length>0} d="M 2 10 V 27"/>
         <svg x="11" y="0" width="108" height={node.height} overflow="hidden">
          <text className="graph-node-operation" y="22">{node.op}</text>
          <text className="graph-node-id" y="41">{node.id}</text>
          {node.values.length>0&&<g>
           {node.values.length>=3&&<rect x="0" y="51" width="12" height="12" rx="2"
             fill={'rgb('+node.values.slice(0,3).map(v=>100*Math.min(1,Math.max(0,v))+'%').join(' ')+')'}>
             <title>RGB components</title></rect>}
           <text className="graph-node-value" x={node.values.length>=3?17:0} y="61">
             {node.values.map(v=>Number(v.toPrecision(3))).join('  ')}
           </text>
          </g>}
         </svg>
         {node.inputs.map((input,index)=><circle key={index} className="graph-socket" cx="1"
           cy={node.height/2+(index-(node.inputs.length-1)/2)*9} r="3"><title>{input}</title></circle>)}
         <circle className="graph-socket graph-socket--out" cx="129" cy="25" r="3"/>
        </g>)}
       </svg>
      </div>
      {draft&&<div className="graph-bindings">
       {inspected&&<fieldset><legend>{inspected.id} · {inspected.op}</legend>
        {(inspected.op==='constant'||inspected.op==='parameter')&&
          ['R','G','B','A'].map((channel,index)=><label key={channel}>{channel}
           <ShaderComponentInput key={inspected.id} channel={channel}
             value={Array.isArray(selectedValue)?selectedValue[index]??0:selectedValue??0}
             onEdit={value=>{setDraft(setShaderComponent(draft,inspected.id,index,value));setError('');setMessage('');}}/></label>)}
        {inspected.op==='parameter'&&<label>Parameter name<input value={inspected.name??inspected.id}
          maxLength={64} onChange={event=>editField('name',event.target.value)}/></label>}
        {inspected.op==='swizzle'&&<label>RGBA mask<input value={inspected.mask??'rgba'}
          maxLength={4} onChange={event=>editField('mask',event.target.value)}/></label>}
        {(inspected.op==='sampleTexture'||inspected.op==='sampleTextureLod')&&<>
          <label>Project texture<input value={inspected.texture??''} onChange={event=>editField('texture',event.target.value)}/></label>
          <label>sRGB<input type="checkbox" checked={inspected.srgb??false} onChange={event=>editField('srgb',event.target.checked)}/></label>
        </>}

        {inspected.inputs?.map((id,slot)=><label key={slot}>Input {slot+1}
         <select value={id} onChange={event=>{
          const next=setShaderInput(draft,inspected.id,slot,event.target.value);
          if(next===draft){setError('This connection would create a cycle.');return;}
          setDraft(next);setMessage('');setError('');
         }}>{sourceOptions}</select></label>)}
       </fieldset>}
       <details><summary>Graph outputs</summary><fieldset>
        {shaderOutputNames(draft.domain).map(channel=><label key={channel}>{channel}
         <select value={draft.outputs?.[channel]??''} onChange={event=>{
          setDraft(setShaderOutput(draft,channel,event.target.value));setMessage('');setError('');
         }}><option value="" disabled={channel==='color'}>Unassigned</option>{sourceOptions}</select>
        </label>)}
       </fieldset></details>
      </div>}
     </> : <div className="graph-empty">No graph loaded.</div>}
   </main>
  </div>
  <footer className="panel-footer">Existing project assets only · generated GLSL/MSL is not exposed through this UI</footer>
 </section>;
}

function ShaderComponentInput({channel,value,onEdit}:{channel:string;value:number;onEdit:(v:number)=>void}){
 const [text,setText]=useState(String(value));
 const focused=useRef(false);
 useEffect(()=>{if(!focused.current)setText(String(value));},[value]);
 return <input type="number" aria-label={'Value '+channel} min="-1000000" max="1000000"
   step="any" value={text} onFocus={()=>{focused.current=true;}}
   onChange={event=>{
    const input=event.currentTarget;
    setText(input.value);
    if(input.validity.valid&&Number.isFinite(input.valueAsNumber))onEdit(input.valueAsNumber);
   }}
   onBlur={()=>{focused.current=false;setText(String(value));}}/>;
}
