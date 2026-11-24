import { useEffect, useMemo, useRef, useState, type PointerEvent } from 'react';
import { GitBranch, Maximize2, Plus, Trash2, ZoomIn, ZoomOut } from 'lucide-react';
import { invoke } from '../bridge';
import { StudioButton, StudioField, StudioSelect, StudioInput } from '../ui';
import { graphV2, type SpectraGraphV2 } from './spectraDataInterfaceModel';
import { connectionParams, moveGraphNode, nodePoint, visualEdges } from './spectraGraphCanvasModel';
import './SpectraGraphCanvas.css';

interface CatalogNode {type:string;group:string;ports:string[]}
const volumeFields:Record<string,Record<string,unknown>>={
  VolumeDomain:{dimension:32,origin:[-2,-1,-2],cellSize:0.125,gravity:[0,-9.81,0],wind:[0,0,0],obstacle:''},
  VolumeSource:{mode:'continuous',position:[0,0,0],radius:0.5,smoke:3,temperature:8,fuel:4,acceleration:[0,2,0],radialAcceleration:0,duration:1},
  VolumeAdvection:{buoyancy:1,smokeWeight:0.1,velocityDecay:0.15,smokeDecay:0.25,temperatureDecay:0.6,fuelDecay:0.05,maxVelocity:20},
  VolumeCombustion:{ignition:0.5,burnRate:3,heatYield:2,smokeYield:0.5},
  VolumePressure:{pressureIterations:12},
  VolumeOutput:{smokeColor:[0.45,0.48,0.52],fireColor:[4,1.2,0.2],extinction:2,emission:1,emissionThreshold:0.5},
};
interface Props {
  graph:SpectraGraphV2;
  disabled?:boolean;
  onEdit:(next:SpectraGraphV2)=>void;
  onError:(error:string)=>void;
}
const nativeFxEdit=async(params:Record<string,unknown>):Promise<SpectraGraphV2>=>{
  const response=await invoke('studio.op',{op:'fx_graph_edit',params});
  const result=response.result as {ok?:boolean;graph?:unknown;error?:string}|undefined;
  const graph=graphV2(result?.graph);
  if(result?.ok!==true || !graph)
    throw new Error(result?.error||'Native SpectraFX graph edit rejected.');
  return graph;
};
/** Graph canvas is layout/authoring only. Real emitter preview stays in
 * native RHI, never in this browser component.
 */
export default function SpectraGraphCanvas({graph,disabled=false,onEdit,onError}:Props) {
  const [catalog,setCatalog]=useState<CatalogNode[]>([]);
  const [nodeType,setNodeType]=useState('DataInterface');
  const [volumePreset,setVolumePreset]=useState('fire');
  const [volumeProperty,setVolumeProperty]=useState('');
  const [volumeValue,setVolumeValue]=useState('');
  const [volumeVector,setVolumeVector]=useState<string[]>(['0','0','0']);
  const [selectedNode,setSelectedNode]=useState('');
  const [from,setFrom]=useState('');
  const [to,setTo]=useState('');
  const [toPort,setToPort]=useState('');
  const [zoom,setZoom]=useState(1);
  const [working,setWorking]=useState(false);
  const drag=useRef<{id:string;clientX:number;clientY:number;x:number;y:number}|null>(null);
  const edges=useMemo(()=>visualEdges(graph),[graph]);
  const nodes=graph.nodes;
  const chosen=nodes.find(n=>n.id===selectedNode);
  const isVolumeNode=chosen?.type.startsWith('Volume')??false;
  const fields=chosen?volumeFields[chosen.type]??{}:{};
  const fieldDefault=fields[volumeProperty];
  const hasVolume=nodes.some(n=>n.type==='VolumeOutput');
  useEffect(()=>{setVolumeProperty('');setVolumeValue('');},[selectedNode]);
  const dest=nodes.find(n=>n.id===to);
  const ports=catalog.find(x=>x.type===dest?.type)?.ports??[];
  const width=Math.max(1200,...nodes.map((n,i)=>nodePoint(n,i).x+240));
  const height=Math.max(540,...nodes.map((n,i)=>nodePoint(n,i).y+100));

  useEffect(()=>{
    let active=true;
    void invoke('studio.op',{op:'fx_graph_node_catalog',params:{}})
      .then(response=>{
        if(!active)return;
        const r=response.result as {nodes?:unknown[]} | undefined;
        const items=(r?.nodes??[]).flatMap(raw=>{
          if(!raw || typeof raw!=='object')return [];
          const v=raw as {type?:unknown;group?:unknown;ports?:unknown};
          return typeof v.type==='string'?[{type:v.type,group:typeof v.group==='string'?v.group:'',
            ports:Array.isArray(v.ports)?v.ports.filter((p):p is string=>typeof p==='string'):[]}]:[];
        });
        setCatalog(items);
      }).catch(e=>{if(active)onError(e instanceof Error?e.message:String(e));});
    return()=>{active=false;};
  },[onError]);

  const execute=async(params:Record<string,unknown>)=>{
    if(working || disabled)return;
    setWorking(true);
    try{onEdit(await nativeFxEdit({...params,graph}));}
    catch(e){onError(e instanceof Error?e.message:String(e));}
    finally{setWorking(false);}
  };
  const connect=()=>{
    const params=connectionParams('connect',graph,from,to,toPort);
    if(!params){onError('Select two different existing graph nodes.');return;}
    void execute(params);
  };
  const finishDrag=(e:PointerEvent<HTMLDivElement>)=>{
    if(!drag.current)return;
    if(e.currentTarget.hasPointerCapture(e.pointerId))
      e.currentTarget.releasePointerCapture(e.pointerId);
    drag.current=null;
  };
  return <div className="spectra-canvas">
    <div className="spectra-canvas__toolbar">
      <StudioField label="New node" htmlFor="spectra-canvas-node-type">
        <StudioSelect id="spectra-canvas-node-type" value={nodeType}
          onChange={e=>setNodeType(e.target.value)}>
          {catalog.map(c=><option key={c.type} value={c.type}>{c.group} / {c.type}</option>)}
        </StudioSelect>
      </StudioField>
      <StudioButton size="sm" disabled={disabled||working||!catalog.length}
        onClick={()=>void execute({action:'add',type:nodeType})}><Plus size={14}/> Add node</StudioButton>
      <StudioButton variant="danger" size="sm" disabled={disabled||working||!selectedNode}
        onClick={()=>void execute({action:'deleteNode',node:selectedNode})}><Trash2 size={14}/> Delete selected</StudioButton>
      <StudioField label="Volume preset">
        <StudioSelect value={volumePreset} onChange={e=>setVolumePreset(e.target.value)}>
          <option value="fire">Fire</option><option value="smoke">Smoke</option>
          <option value="magic">Magic</option><option value="fog">Fog</option><option value="explosion">Explosion</option>
        </StudioSelect>
      </StudioField>
      <StudioButton size="sm" disabled={disabled||working||hasVolume}
        onClick={()=>void execute({action:'addVolume',preset:volumePreset})}>Add volume branch</StudioButton>
      <StudioButton size="sm" disabled={disabled||working}
        onClick={()=>{if(window.confirm('Replace the current unsaved graph with a volume-only preset?'))void execute({action:'newVolume',preset:volumePreset});}}>New volume graph</StudioButton>
      <span className="spectra-canvas__spacer"/>
      <StudioButton size="sm" variant="ghost" onClick={()=>setZoom(z=>Math.max(0.5,z-0.1))}><ZoomOut size={14}/></StudioButton>
      <span className="spectra-canvas__zoom">{Math.round(zoom*100)}%</span>
      <StudioButton size="sm" variant="ghost" onClick={()=>setZoom(z=>Math.min(2,z+0.1))}><ZoomIn size={14}/></StudioButton>
      <StudioButton size="sm" variant="ghost" onClick={()=>setZoom(1)} title="Reset zoom"><Maximize2 size={14}/></StudioButton>
    </div>
    <div className="spectra-canvas__viewport" role="region" aria-label="SpectraFX graph topology and node arrangement">
      <div className="spectra-canvas__extent" style={{width:width*zoom,height:height*zoom}}>
        <div className="spectra-canvas__world" style={{width,height,transform:`scale(${zoom})`}}>
          <svg viewBox={`0 0 ${width} ${height}`} width={width} height={height} className="spectra-canvas__edges" aria-hidden="true">
            {edges.map(edge=><path key={edge.id} d={edge.path} fill="none"
              stroke={edge.toPort?'var(--au-accent-secondary)':'var(--au-text-muted)'}
              strokeWidth={edge.toPort?2:1.5} strokeLinecap="round"/>)}
          </svg>
          {nodes.map((node,i)=>{
            const pos=nodePoint(node,i);
            return <div key={node.id}
              className={'spectra-canvas__node '+(node.id===selectedNode?'is-selected':'')}
              style={{left:pos.x,top:pos.y}}
              role="button" tabIndex={0}
              aria-label={node.type+' node '+node.id}
              onKeyDown={e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();setSelectedNode(node.id);}}}
              onClick={()=>setSelectedNode(node.id)}
              onPointerDown={e=>{
                if(disabled||e.button!==0)return;
                setSelectedNode(node.id);
                drag.current={id:node.id,clientX:e.clientX,clientY:e.clientY,x:pos.x,y:pos.y};
                e.currentTarget.setPointerCapture(e.pointerId);
              }}
              onPointerMove={e=>{
                if(!drag.current||drag.current.id!==node.id)return;
                const d=drag.current;
                const point={x:Math.max(0,d.x+(e.clientX-d.clientX)/zoom),
                             y:Math.max(0,d.y+(e.clientY-d.clientY)/zoom)};
                const edited=moveGraphNode(graph,node.id,point);
                if(!edited.error)onEdit(edited.graph);
              }}
              onPointerUp={finishDrag} onPointerCancel={finishDrag}>
              <div className="spectra-canvas__node-head"><GitBranch size={13}/>{node.type}</div>
              <div className="spectra-canvas__node-id">{node.id}</div>
              {node.type==='DataInterface' && <div className="spectra-canvas__node-param">{String(node.properties?.name??'Unbound')}</div>}
            </div>;
          })}
        </div>
      </div>
    </div>
    <div className="spectra-canvas__connect">
      <StudioField label="From" htmlFor="spectra-from">
        <StudioSelect id="spectra-from" value={from} onChange={e=>setFrom(e.target.value)}>
          <option value="">Source node…</option>{nodes.map(n=><option key={n.id} value={n.id}>{n.id} · {n.type}</option>)}
        </StudioSelect>
      </StudioField>
      <StudioField label="To" htmlFor="spectra-to">
        <StudioSelect id="spectra-to" value={to} onChange={e=>{setTo(e.target.value);setToPort('');}}>
          <option value="">Target node…</option>{nodes.map(n=><option key={n.id} value={n.id}>{n.id} · {n.type}</option>)}
        </StudioSelect>
      </StudioField>
      <StudioField label="Connection" htmlFor="spectra-port">
        <StudioSelect id="spectra-port" value={toPort} onChange={e=>setToPort(e.target.value)}>
          <option value="">Execution</option>{ports.map(p=><option key={p} value={p}>Value → {p}</option>)}
        </StudioSelect>
      </StudioField>
      <StudioButton disabled={working||disabled||!from||!to||from===to}
        onClick={connect}>Connect</StudioButton>
    </div>
    <div className="spectra-canvas__links">
      {edges.map(edge=><div className="spectra-canvas__link" key={edge.id}>
        <span>{edge.from} → {edge.to}{edge.toPort?' · '+edge.toPort:''}</span>
        <StudioButton size="sm" variant="ghost" disabled={working||disabled} aria-label={'Disconnect '+edge.from+' from '+edge.to}
          onClick={()=>{const params=connectionParams('disconnect',graph,edge.from,edge.to,edge.toPort);
            if(params)void execute(params);}}>Disconnect</StudioButton>
      </div>)}
    </div>
    {chosen&&<div className="spectra-canvas__selected">
      <strong>{chosen.type}: {chosen.id}</strong>
      {isVolumeNode?<div>
        <StudioField label="Volume property">
          <StudioSelect value={volumeProperty} onChange={e=>{
            const k=e.target.value;setVolumeProperty(k);
            const v=chosen.properties?.[k]??fields[k];
            setVolumeValue(v===undefined?'':String(v));
            if(Array.isArray(v))setVolumeVector(v.map(String));
          }}>
            <option value="">Choose a property…</option>
            {Object.keys(fields).filter(k=>k!=='duration'||chosen.properties?.mode==='impulse').map(k=><option key={k} value={k}>{k}</option>)}
          </StudioSelect>
        </StudioField>
        {Array.isArray(fieldDefault)?<div className="spectra-canvas__connect">
          {volumeVector.map((v,i)=><StudioField key={i} label={['X / R','Y / G','Z / B'][i]}><StudioInput type="number" step="any" value={v} disabled={disabled||working} onChange={e=>setVolumeVector(old=>old.map((x,j)=>j===i?e.target.value:x))}/></StudioField>)}
        </div>:volumeProperty==='mode'?<StudioField label="Emission mode"><StudioSelect value={volumeValue} onChange={e=>setVolumeValue(e.target.value)}><option value="continuous">Continuous</option><option value="impulse">Impulse</option></StudioSelect></StudioField>:
        <StudioField label={volumeProperty==='obstacle'?'Project SDF path (content/*.sdfbin)':'Value'}>
          <StudioInput type={typeof fieldDefault==='number'?'number':'text'} step={typeof fieldDefault==='number'?'any':undefined} value={volumeValue} onChange={e=>setVolumeValue(e.target.value)} disabled={disabled||working||!volumeProperty}/>
        </StudioField>}
        <StudioButton size="sm" disabled={disabled||working||!volumeProperty} onClick={()=>{
          const value=Array.isArray(fieldDefault)?volumeVector.map(Number):typeof fieldDefault==='number'?Number(volumeValue):volumeValue;
          if((Array.isArray(value)&&value.some((x,i)=>!Number.isFinite(x)||volumeVector[i].trim()===''))||(typeof value==='number'&&(!Number.isFinite(value)||volumeValue.trim()===''))){onError('Invalid volume value');return;}
          void execute({action:'setProperty',node:chosen.id,property:volumeProperty,value});
        }}>Apply volume property</StudioButton>
        <small>Native validation on save/preview. Reload to apply authored changes.</small>
      </div>:<code>{JSON.stringify(chosen.properties??{})}</code>}
    </div>}
    <p className="spectra-canvas__hint">Drag nodes to arrange them. Connections and new nodes use the native SpectraFX graph editor; save changes in the parent Graph panel. This canvas does not simulate or draw GPU particles.</p>
  </div>;
}
