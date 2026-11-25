import {useEffect, useMemo, useState} from 'react';
import GraphCanvas from '../graphs/GraphCanvas';
import type {GraphView, NodeView} from '../graphs/graphFramework';
import type {PortRef} from '../graphs/graphGeometry';

type Pass = {id:string; op:string; inputs:string[]; target:string; amount?:number; graph?:unknown};
interface Document {sources?: {id:string; cameraInput:number}[]; targets:{id:string; divisor?:number}[]; passes:Pass[]; output:string}
const example:Document = {
  targets:[{id:'bright',divisor:2},{id:'glow',divisor:2},{id:'output'}],
  passes:[
    {id:'extract',op:'bright',inputs:['scene'],target:'bright',amount:0.55},
    {id:'spread',op:'blurX',inputs:['bright'],target:'glow',amount:2},
    {id:'combine',op:'add',inputs:['glow','scene'],target:'output',amount:0.8}
  ],output:'output'
};
const format = (v:unknown) => JSON.stringify(v,null,2);
function parse(text:string):Document|null {
  try {
    const d=JSON.parse(text);
    if (!d || !Array.isArray(d.targets) || !Array.isArray(d.passes) || d.passes.length>12 ||
        !d.passes.every((p:Pass)=>p && typeof p.id==='string' && typeof p.target==='string' &&
          typeof p.op==='string' && Array.isArray(p.inputs) && p.inputs.every(i=>typeof i==='string')) ||
        (d.sources!==undefined && (!Array.isArray(d.sources) || !d.sources.every((s:{id:string})=>s && typeof s.id==='string'))))
      return null;
    return d;
  } catch { return null; }
}
function project(d:Document|null):GraphView {
  if (!d) return {nodes:[], links:[]};
  const sources=['scene',...(d.sources??[]).map(s=>s.id)];
  const nodes:NodeView[]=sources.map((id,i)=>({id:'source:'+id,label:id,x:24,y:20+100*i,inputs:[],outputs:[{id,type:'texture'}]}));
  const owners = new Map(nodes.map(n=>[n.outputs[0].id,n.id]));
  d.passes.forEach((p,i)=>{
    const node='pass:'+p.id;
    owners.set(p.target,node);
    nodes.push({id:node,label:p.id+' · '+p.op,x:200+(i%3)*180,y:20+Math.floor(i/3)*100,
      inputs:p.inputs.map((_,j)=>({id:'in'+j,type:'texture'})),outputs:[{id:p.target,type:'texture'}]});
  });
  return {nodes,links:d.passes.flatMap(p=>p.inputs.flatMap((id,i)=>{
    const from=owners.get(id);return from?[{id:p.id+':'+i,from,output:id,to:'pass:'+p.id,input:'in'+i}]:[];
  }))};
}
interface Props {
  stored:unknown;
  scope:'game'|'map';
  save:(params:Record<string,unknown>)=>Promise<void>;
}
export default function PrismaCompositorEditor({stored,scope,save}:Props) {
  const latest=format(stored??null);
  const [base,setBase]=useState(latest);
  const [draft,setDraft]=useState(latest);
  const [selected,setSelected]=useState('');
  const [pending,setPending]=useState<PortRef|null>(null);
  const [busy,setBusy]=useState(false);
  const [error,setError]=useState('');
  const [positions,setPositions]=useState<Record<string,{x:number;y:number}>>({});
  useEffect(()=>{
    if (draft===base && latest!==base) {setBase(latest);setDraft(latest);}
  },[base,draft,latest]);
  const doc=useMemo(()=>parse(draft),[draft]);
  const graph=useMemo(()=>project(doc),[doc]);
  const view={...graph,nodes:graph.nodes.map(n=>({...n,...positions[n.id]}))};
  const apply=async(mode:'save'|'inherit'|'disable')=>{
    setBusy(true);setError('');
    try {
      const value=mode==='save'?JSON.parse(draft):null;
      await save({...(mode==='inherit'?{inherit:true}:{graph:value}),expectedGraph:JSON.parse(base)});
      setBase(format(value));setDraft(format(value));setPending(null);
    } catch(e){setError(e instanceof Error?e.message:String(e));}
    finally{setBusy(false);}
  };
  return <details><summary>Frame compositor</summary>
    {doc && <GraphCanvas view={view} label="Compositor passes" selected={selected}
      disabled={busy} pendingOutput={pending} onSelect={setSelected} onOutput={setPending} onCancel={()=>setPending(null)}
      onMove={(id,x,y)=>setPositions(p=>({...p,[id]:{x,y}}))}
      onInput={input=>{
        if (!pending) return;
        setDraft(format({...doc,passes:doc.passes.map(p=>('pass:'+p.id===input.node)?
          {...p,inputs:p.inputs.map((v,i)=>('in'+i===input.port)?pending.port:v)}:p)}));
        setPending(null);
      }}/>}
    <label>Targets and passes<textarea rows={10} value={draft} spellCheck={false} disabled={busy}
      onChange={e=>setDraft(e.target.value)} /></label>
    {error && <div className="tool__error" role="alert">{error}</div>}
    <div className="tbrush__chips">
      <button type="button" disabled={busy} onClick={()=>setDraft(format(example))}>Load glow example</button>
      <button type="button" disabled={busy || !doc || base===draft || base!==latest} onClick={()=>void apply('save')}>Save graph</button>
      <button type="button" disabled={busy} onClick={()=>{setBase(latest);setDraft(latest);setError('');}}>Reload saved graph</button>
      <button type="button" disabled={busy} onClick={()=>void apply('disable')}>Disable</button>
      {scope==='map' && <button type="button" disabled={busy} onClick={()=>void apply('inherit')}>Use game graph</button>}
    </div>
    {latest!==base && <p role="status">The saved graph changed. Reload it before saving.</p>}
  </details>;
}
