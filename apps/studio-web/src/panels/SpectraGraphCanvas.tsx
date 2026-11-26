import {createPortal} from 'react-dom';
import { Fragment, useEffect, useMemo, useRef, useState, type PointerEvent } from 'react';
import { GitBranch, Maximize2, Plus, Trash2, ZoomIn, ZoomOut } from 'lucide-react';
import { invoke } from '../bridge';
import { StudioButton, StudioField, StudioSelect, StudioInput, StudioContextMenu } from '../ui';
import { graphV2, type SpectraGraphV2 } from './spectraDataInterfaceModel';
import { connectionParams, moveGraphNode, nodePoint, visualEdges } from './spectraGraphCanvasModel';
import SpectraNodeProperties,{type ModuleField} from './SpectraNodeProperties';
import './graphSurface.css';
import './SpectraGraphCanvas.css';

interface CatalogNode {type:string;group:string;ports:string[];fields:ModuleField[];outputKind:'none'|'execution'|'value'}
const volumeFields:Record<string,Record<string,unknown>>={
  VolumetricParticleOutput:{shape:'sphere',axes:[1,1,1],density:2,emission:0,steps:12,noiseFrequency:3,noiseStrength:0,noiseOctaves:3,shadowSteps:4,shadowStrength:1,anisotropy:0,ambient:0.35,directIntensity:0.65,depthAware:false,sortBackToFront:true,adaptiveLod:true,minimumSteps:4,lodSmallPixels:8,lodFullPixels:96,sparseTexture:true,densityTexture:''},
  VolumeDomain:{dimension:32,origin:[-2,-1,-2],cellSize:0.125,gravity:[0,-9.81,0],wind:[0,0,0],obstacle:'',particleDrag:0,particleDeposit:[0,0,0],physisWindResponse:0,physisGravityBlend:0,attachment:'world'},
  VolumeSource:{mode:'continuous',position:[0,0,0],radius:0.5,smoke:3,temperature:8,fuel:4,acceleration:[0,2,0],radialAcceleration:0,duration:1},
  VolumeAdvection:{buoyancy:1,smokeWeight:0.1,velocityDecay:0.15,smokeDecay:0.25,temperatureDecay:0.6,fuelDecay:0.05,maxVelocity:20},
  VolumeCombustion:{ignition:0.5,burnRate:3,heatYield:2,smokeYield:0.5},
  VolumePressure:{pressureIterations:12},
  VolumeOutput:{smokeColor:[0.45,0.48,0.52],fireColor:[4,1.2,0.2],extinction:2,emission:1,emissionThreshold:0.5,smokeDetail:0,emberIntensity:0},
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
  const [wire,setWire]=useState<{id:string;value:boolean;graph:SpectraGraphV2}|null>(null);
  const [fieldMenu,setFieldMenu]=useState<{to:string;x:number;y:number;ports:string[]}|null>(null);
  const pending=wire?.graph===graph?wire:null;
  const cancelWire=()=>{setWire(null);setFieldMenu(null);};
  const [from,setFrom]=useState('');
  const [to,setTo]=useState('');
  const [toPort,setToPort]=useState('');
  const [zoom,setZoom]=useState(1);
  const [working,setWorking]=useState(false);
  const drag=useRef<{base:SpectraGraphV2;pointerId:number;id:string;clientX:number;clientY:number;x:number;y:number}|null>(null);
  const [move,setMove]=useState<{base:SpectraGraphV2;next:SpectraGraphV2}|null>(null);
  const display=move?.base===graph?move.next:graph;
  const edges=useMemo(()=>visualEdges(display),[display]);
  const nodes=display.nodes;
  const chosen=nodes.find(n=>n.id===selectedNode);
  const isVolumeNode=(chosen?.type.startsWith('Volume')??false)||chosen?.type==='VolumetricParticleOutput';
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
          const v=raw as {type?:unknown;group?:unknown;ports?:unknown;fields?:ModuleField[];outputKind?:unknown};
          return typeof v.type==='string'?[{type:v.type,group:typeof v.group==='string'?v.group:'',
            fields:Array.isArray(v.fields)?v.fields:[],
            outputKind:v.outputKind==='value'?'value' as const:v.outputKind==='none'?'none' as const:'execution' as const,
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
  const finishWire=(target:string,port='')=>{
    if(!pending)return;
    const params=connectionParams('connect',graph,pending.id,target,port);
    cancelWire();
    if(params)void execute(params);
  };
  const connect=()=>{
    const params=connectionParams('connect',graph,from,to,toPort);
    if(!params){onError('Select two different existing graph nodes.');return;}
    void execute(params);
  };
  const finishDrag=(e:PointerEvent<HTMLDivElement>)=>{
    const d=drag.current;
    if(!d||d.pointerId!==e.pointerId)return;
    drag.current=null;
    if(e.currentTarget.hasPointerCapture(e.pointerId))
      e.currentTarget.releasePointerCapture(e.pointerId);
    if(e.type==='pointerup'&&d.base===graph&&(e.clientX!==d.clientX||e.clientY!==d.clientY)){
      const edited=moveGraphNode(graph,d.id,{x:Math.max(0,d.x+(e.clientX-d.clientX)/zoom),
        y:Math.max(0,d.y+(e.clientY-d.clientY)/zoom)});
      if(!edited.error)onEdit(edited.graph);
    }
    setMove(null);
  };
  return <div className="spectra-canvas" onKeyDown={e=>{
    if(pending&&e.key==='Escape'){e.preventDefault();e.stopPropagation();cancelWire();}
  }}>
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
          <option value="trail">Moving smoke trail</option>
          <option value="magic">Magic</option><option value="fog">Fog</option><option value="explosion">Explosion</option>
        </StudioSelect>
      </StudioField>
      <StudioButton size="sm" disabled={disabled||working||hasVolume}
        onClick={()=>void execute({action:'addVolume',preset:volumePreset})}>Add volume branch</StudioButton>
      <StudioButton size="sm" disabled={disabled||working}
        onClick={()=>{if(window.confirm('Replace the current unsaved graph with a volume-only preset?'))void execute({action:'newVolume',preset:volumePreset});}}>New volume graph</StudioButton>
      <div className="spectra-canvas__zoom-tools" role="group" aria-label="Graph zoom">
        <StudioButton size="sm" variant="ghost" aria-label="Zoom out" onClick={()=>setZoom(z=>Math.max(0.5,z-0.1))}><ZoomOut size={14}/></StudioButton>
        <span className="spectra-canvas__zoom">{Math.round(zoom*100)}%</span>
        <StudioButton size="sm" variant="ghost" aria-label="Zoom in" onClick={()=>setZoom(z=>Math.min(2,z+0.1))}><ZoomIn size={14}/></StudioButton>
        <StudioButton size="sm" variant="ghost" onClick={()=>setZoom(1)} title="Reset zoom"><Maximize2 size={14}/></StudioButton>
      </div>
    </div>
    <div className="spectra-canvas__viewport graph-surface" role="region" aria-label="SpectraFX graph topology and node arrangement">
      <div className="spectra-canvas__extent" style={{width:width*zoom,height:height*zoom}}>
        <div className="spectra-canvas__world" style={{width,height,transform:`scale(${zoom})`}}>
          <svg viewBox={`0 0 ${width} ${height}`} width={width} height={height} className="spectra-canvas__edges" aria-hidden="true">
            {edges.map(edge=><path key={edge.id} d={edge.path} fill="none"
              className="graph-wire" data-kind={edge.toPort?'value':'execution'}
              data-emphasis={chosen?(edge.from===chosen.id||edge.to===chosen.id?'active':'quiet'):undefined}/>)}
          </svg>
          {nodes.map((node,i)=>{
            const pos=nodePoint(node,i);
            const info=catalog.find(item=>item.type===node.type);
            const target=!!pending&&pending.id!==node.id&&(pending.value?!!info?.ports.length:
              node.type!=='Spawn'&&info?.outputKind!=='value');
            return <Fragment key={node.id}><div
              className={'spectra-canvas__node '+(node.id===selectedNode?'is-selected':'')}
              style={{left:pos.x,top:pos.y}}
              role="button" tabIndex={0} aria-pressed={node.id===selectedNode}
              aria-label={node.type+' node '+node.id}
              onKeyDown={e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();setSelectedNode(node.id);}}}
              onClick={()=>setSelectedNode(node.id)}
              onPointerDown={e=>{
                if(disabled||working||e.button!==0||!e.isPrimary)return;
                setSelectedNode(node.id);
                drag.current={base:graph,pointerId:e.pointerId,id:node.id,clientX:e.clientX,clientY:e.clientY,x:pos.x,y:pos.y};
                e.currentTarget.setPointerCapture(e.pointerId);
              }}
              onPointerMove={e=>{
                if(!drag.current||drag.current.base!==graph||drag.current.pointerId!==e.pointerId||drag.current.id!==node.id)return;
                const d=drag.current;
                const point={x:Math.max(0,d.x+(e.clientX-d.clientX)/zoom),
                             y:Math.max(0,d.y+(e.clientY-d.clientY)/zoom)};
                const edited=moveGraphNode(graph,node.id,point);
                if(!edited.error)setMove({base:graph,next:edited.graph});
              }}
              onPointerUp={finishDrag} onPointerCancel={finishDrag} onLostPointerCapture={finishDrag}>
              <div className="spectra-canvas__node-head"><GitBranch size={13}/><span title={node.type}>{node.type.replace(/([a-z0-9])([A-Z])/g,'$1 $2')}</span></div>
              <div className="spectra-canvas__node-id" title={node.id}>{node.id}</div>
              {node.type==='DataInterface' && <div className="spectra-canvas__node-param">{String(node.properties?.name??'Unbound')}</div>}
            </div>
              <button type="button" className="spectra-canvas__socket" data-side="in"
                data-kind={pending?.value?'value':'execution'} data-target={target||undefined}
                aria-label={'Input of '+node.id} title={pending?'Connect '+pending.id+' to '+node.id:'Input of '+node.id}
                style={{left:pos.x,top:pos.y+26}} disabled={!target||disabled||working}
                onClick={e=>{
                  if(!pending||!info)return;
                  if(!pending.value){finishWire(node.id);return;}
                  const rect=e.currentTarget.getBoundingClientRect();
                  setFieldMenu({to:node.id,x:rect.left,y:rect.bottom,ports:info.ports});
                }}/>
              {info&&info.outputKind!=='none'&&<button type="button" className="spectra-canvas__socket" data-side="out"
                data-kind={info.outputKind} aria-pressed={pending?.id===node.id}
                aria-label={'Output of '+node.id} title={'Connect '+info.outputKind+' output of '+node.id}
                style={{left:pos.x+170,top:pos.y+26}} disabled={disabled||working}
                onClick={()=>{setFieldMenu(null);setWire(pending?.id===node.id?null:{id:node.id,value:info.outputKind==='value',graph});}}/>}
            </Fragment>;
          })}
        </div>
      </div>
    </div>
    {pending&&<div className="spectra-canvas__pending" role="status">
      <span>Select an input for <strong>{pending.id}</strong>.</span>
      <StudioButton size="sm" variant="ghost" onClick={cancelWire}>Cancel connection</StudioButton>
    </div>}
    {pending&&fieldMenu&&createPortal(<StudioContextMenu x={fieldMenu.x} y={fieldMenu.y}
      label={'Input field on '+fieldMenu.to} onClose={cancelWire}
      items={fieldMenu.ports.map(port=>({id:port,label:port,onSelect:()=>finishWire(fieldMenu.to,port)}))}/>,document.body)}
    {edges.some(edge=>edge.toPort)&&<div className="graph-wire-key" aria-label="Connection styles">
      <span>Execution</span><span data-kind="value">Value</span>
    </div>}
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
        </div>:typeof fieldDefault==='boolean'?<StudioField label="Enabled"><StudioSelect value={volumeValue} onChange={e=>setVolumeValue(e.target.value)}><option value="true">Yes</option><option value="false">No</option></StudioSelect></StudioField>:volumeProperty==='shape'?<StudioField label="Volume shape"><StudioSelect value={volumeValue} onChange={e=>setVolumeValue(e.target.value)}>{['sphere','ellipsoid','box','capsule'].map(x=><option key={x} value={x}>{x}</option>)}</StudioSelect></StudioField>:volumeProperty==='attachment'?<StudioField label="Volume attachment"><StudioSelect value={volumeValue} onChange={e=>setVolumeValue(e.target.value)}><option value="world">World fixed</option><option value="follow">Follow source</option><option value="trail">Leave a trail</option></StudioSelect></StudioField>:volumeProperty==='mode'?<StudioField label="Emission mode"><StudioSelect value={volumeValue} onChange={e=>setVolumeValue(e.target.value)}><option value="continuous">Continuous</option><option value="impulse">Impulse</option></StudioSelect></StudioField>:
        <StudioField label={volumeProperty==='obstacle'?'Project SDF path (content/*.sdfbin)':'Value'}>
          <StudioInput type={typeof fieldDefault==='number'?'number':'text'} step={typeof fieldDefault==='number'?'any':undefined} value={volumeValue} onChange={e=>setVolumeValue(e.target.value)} disabled={disabled||working||!volumeProperty}/>
        </StudioField>}
        <StudioButton size="sm" disabled={disabled||working||!volumeProperty} onClick={()=>{
          const value=Array.isArray(fieldDefault)?volumeVector.map(Number):typeof fieldDefault==='number'?Number(volumeValue):typeof fieldDefault==='boolean'?volumeValue==='true':volumeValue;
          if((Array.isArray(value)&&value.some((x,i)=>!Number.isFinite(x)||volumeVector[i].trim()===''))||(typeof value==='number'&&(!Number.isFinite(value)||volumeValue.trim()===''))){onError('Invalid volume value');return;}
          void execute({action:'setProperty',node:chosen.id,property:volumeProperty,value});
        }}>Apply volume property</StudioButton>
        <small>Native validation on save/preview. Reload to apply authored changes.</small>
      </div>:null}
      <SpectraNodeProperties key={chosen.id} node={chosen} fields={catalog.find(item=>item.type===chosen.type)?.fields??[]}
        functions={graph.functions} disabled={disabled||working} edit={params=>void execute(params)}
        replace={properties=>onEdit({...graph,nodes:graph.nodes.map(node=>node.id===chosen.id?{...node,properties}:node)})}/>
    </div>}

  </div>;
}
