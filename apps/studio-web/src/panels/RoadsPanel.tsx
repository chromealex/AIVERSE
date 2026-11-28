import { useEffect, useMemo, useRef, useState, type PointerEvent, type ReactNode } from 'react';
import { invoke, onNativeEvent } from '../bridge';
import type { StudioGraphDocumentScope } from '../types';
import { StudioAlert, StudioButton, StudioField, StudioInput, StudioPanel, StudioSection, StudioSelect } from '../ui';
import './RoadsPanel.css';

type Point={x:number; z:number};
type Node=Point & {id:number; y?:number};
type Edge={id:number; a:number; b:number; type:string; c1?:number[]; c2?:number[]};
type Network={nodes:Node[];edges:Edge[];edgeInfo:{id:number;lanes:string}[]};
type RoadEnd=Point & {edge?:number;dir:number;first:number;last:number};
type Segment={points:RoadEnd[];revision:string};
type Draft={id:number;x:string;z:string;y:string;revision:string};
const fields=['x','z','y'] as const;
const toDraft=(n:Node,revision:string):Draft=>({id:n.id,x:String(n.x),z:String(n.z),y:String(n.y??0),revision});

function projection(net:Network) {
  let minX=Infinity,maxX=-Infinity,minZ=Infinity,maxZ=-Infinity;
  const include=(x:number,z:number)=>{minX=Math.min(minX,x);maxX=Math.max(maxX,x);minZ=Math.min(minZ,z);maxZ=Math.max(maxZ,z);};
  for(const n of net.nodes)include(n.x,n.z);
  for(const e of net.edges)for(const c of [e.c1,e.c2])if(c)include(c[0],c[1]);
  const scale=net.nodes.length?Math.min(544/Math.max(12,maxX-minX),200/Math.max(12,maxZ-minZ)):8;
  const cx=net.nodes.length?(minX+maxX)/2:24, cz=net.nodes.length?(minZ+maxZ)/2:16;
  return {screen:(x:number,z:number)=>[320+(x-cx)*scale,140+(z-cz)*scale],
    world:(x:number,z:number)=>({x:cx+(x-320)/scale,z:cz+(z-140)/scale})};
}

export default function RoadsPanel({scope,advanced}:{scope?:StudioGraphDocumentScope;advanced:ReactNode}) {
  const [tools,setTools]=useState(false);
  const [data,setData]=useState<{net:Network;revision:string}>();
  const [picked,setPicked]=useState<number>();
  const [draft,setDraft]=useState<Draft>();
  const [error,setError]=useState('');
  const [busy,setBusy]=useState(false);
  const [reload,setReload]=useState(0);
  const [segment,setSegment]=useState<Segment>();
  const [types,setTypes]=useState<{id:string;name?:string}[]>([]);
  const [roadType,setRoadType]=useState('street_2');
  const [moving,setMoving]=useState<Node>();
  const drag=useRef<{id:number;pointer:number;start:Point;origin:Node;revision:string}|undefined>(undefined);
  const svg=useRef<SVGSVGElement>(null);
  const {projectPath,epoch,revision}=scope??{};
  useEffect(()=>onNativeEvent(event=>{
    if(event==='document.changed'||event==='workspace.changed')setReload(n=>n+1);
  }),[]);
  useEffect(()=>{
    if(!projectPath || !epoch)return;
    let current=true;
    void (async()=>{
      const snap=await invoke('studio.snapshot',{});
      const doc=snap.graphDocument;
      if(doc?.epoch!==epoch || doc.projectPath!==projectPath)return;
      const r=await invoke('studio.op',{op:'road_network',params:{},expectedProject:projectPath,expectedDocumentEpoch:epoch,expectedDocumentRevision:doc.revision});
      if(current)setData({net:r.result as Network,revision:doc.revision});
    })().catch(e=>{if(current)setError(String(e));});
    return ()=>{current=false;};
  },[projectPath,epoch,revision,reload]);
  useEffect(()=>{
    if(!projectPath || !epoch)return;
    let current=true;
    void invoke('studio.op',{op:'road_types',params:{},expectedProject:projectPath,expectedDocumentEpoch:epoch})
      .then(r=>{if(current)setTypes((r.result as {types:{id:string;name?:string}[]}).types);})
      .catch(e=>{if(current)setError(String(e));});
    return ()=>{current=false;};
  },[projectPath,epoch,revision]);
  const net=data?.net;
  const view=useMemo(()=>projection(net??{nodes:[],edges:[],edgeInfo:[]}),[net]);
  const nodes=useMemo(()=>new Map(net?.nodes.map(n=>[n.id,n])),[net]);
  const node=nodes.get(picked??-1)??net?.nodes[0];
  const values=node && data && (draft?.id===node.id?draft:toDraft(node,data.revision));
  const valid=values && fields.every(k=>values[k].trim()!=='' && Number.isFinite(Number(values[k])));
  const changed=valid && node && fields.some(k=>Number(values[k])!==(node[k]??0));
  const select=(id:number)=>{setPicked(id);setDraft(undefined);setError('');};
  async function move(id:number,params:Record<string,number>,expected:string){
    if(busy||!epoch)return;
    setBusy(true);setError('');
    try{
      await invoke('studio.op',{op:'road_node_set',params:{node:id,...params},expectedProject:projectPath,expectedDocumentEpoch:epoch,expectedDocumentRevision:expected});
    }catch(e){setError(String(e));}
    finally{setDraft(undefined);setMoving(undefined);setBusy(false);setReload(n=>n+1);}
  }
  function point(e:PointerEvent):Point|undefined{
    const matrix=svg.current?.getScreenCTM();
    if(!matrix)return;
    const p=new DOMPoint(e.clientX,e.clientY).matrixTransform(matrix.inverse());
    return view.world(p.x,p.y);
  }
  function preview(e:PointerEvent){
    const d=drag.current,p=point(e);
    if(!d || d.pointer!==e.pointerId || !p)return;
    const round=(n:number)=>Math.round(n*100)/100;
    return {...d.origin,x:round(d.origin.x+p.x-d.start.x),z:round(d.origin.z+p.z-d.start.z)};
  }
  function finish(e:PointerEvent){
    const d=drag.current,position=preview(e);
    if(!d || d.pointer!==e.pointerId)return;
    drag.current=undefined;setMoving(undefined);
    if(position && (position.x!==d.origin.x || position.z!==d.origin.z))void move(d.id,{x:position.x,z:position.z},d.revision);
  }
  function addPoint(e:PointerEvent<SVGSVGElement>){
    if(!segment || segment.points.length===2 || busy || e.button!==0)return;
    const p=point(e);if(!p)return;
    const hit=(e.target as Element).closest('[data-edge]')?.getAttribute('data-edge');
    const edge=hit==null?undefined:Number(hit);
    const forward=net?.edgeInfo.find(item=>item.id===edge)?.lanes.split('+')[0];
    setSegment({...segment,points:[...segment.points,{...p,edge,dir:forward==='0'?-1:1,first:0,last:0}]});
  }
  function changeEnd(index:number,changes:Partial<RoadEnd>){
    if(segment)setSegment({...segment,points:segment.points.map((p,i)=>i===index?{...p,...changes}:p)});
  }
  const type=types.some(t=>t.id===roadType)?roadType:types[0]?.id;
  async function create(){
    if(!segment || segment.points.length!==2 || !type || busy)return;
    const params:Record<string,unknown>={type,points:segment.points.map(p=>[p.x,p.z])};
    segment.points.forEach((p,i)=>{if(p.edge!==undefined)params[i?'toLanes':'fromLanes']={edge:p.edge,dir:p.dir,lanes:[p.first,p.last]};});
    setBusy(true);setError('');
    try{
      await invoke('studio.op',{op:'road_add',params,expectedProject:projectPath,expectedDocumentEpoch:epoch,expectedDocumentRevision:segment.revision});
      setSegment(undefined);
    }catch(e){setError(String(e));}
    finally{setBusy(false);setReload(n=>n+1);}
  }
  const displayed=(n:Node)=>moving?.id===n.id?moving:n;
  return <StudioPanel title="Roads" className="road-panel" actions={<StudioButton size="sm" aria-pressed={tools} onClick={()=>setTools(!tools)}>{tools?'Network editor':'Road tools'}</StudioButton>}>
    {tools?advanced:!scope?<StudioAlert>Open a map to edit its roads.</StudioAlert>:<>
      {error && <StudioAlert tone="danger">{error} <StudioButton size="sm" onClick={()=>setReload(n=>n+1)}>Refresh</StudioButton></StudioAlert>}
      <StudioSection title="Road network" actions={data && !segment && <StudioButton size="sm" onClick={()=>{setError('');setSegment({points:[],revision:data.revision});}}>New segment</StudioButton>}>
        {!net?<p>Loading network…</p>:!net.nodes.length && !segment?<div className="road-empty"><p>This map has no road nodes.</p><StudioButton onClick={()=>setTools(true)}>Open road drawing tools</StudioButton></div>:<>
          <svg ref={svg} viewBox="0 0 640 280" className="road-map" aria-label="Road network plan"
            onPointerDown={addPoint} onPointerMove={e=>{const n=preview(e);if(n)setMoving(n);}} onPointerUp={finish}
            onLostPointerCapture={()=>{drag.current=undefined;setMoving(undefined);}}>
            {net.edges.map(e=>{
              const a=nodes.get(e.a),b=nodes.get(e.b);if(!a||!b)return null;
              const source=displayed(a),target=displayed(b);
              const control=(c:number[],from:Node,to:Node)=>view.screen(c[0]+to.x-from.x,c[1]+to.z-from.z);
              const curve=e.c1 && e.c2?'C '+control(e.c1,a,source)+' '+control(e.c2,b,target):'L';
              return <g key={e.id} data-edge={e.id}><path d={'M '+view.screen(source.x,source.z)+' '+curve+' '+view.screen(target.x,target.z)}><title>{e.type} · {e.id}</title></path><path className="road-edge-hit" d={'M '+view.screen(source.x,source.z)+' '+curve+' '+view.screen(target.x,target.z)}/></g>;
            })}
            {net.nodes.map(n=>{
              const pos=displayed(n),[x,y]=view.screen(pos.x,pos.z);
              return <g key={n.id} transform={'translate('+x+' '+y+')'} className={node?.id===n.id?'selected':''}
                onPointerDown={e=>{
                  if(segment || busy || !data || e.button!==0)return;
                  const p=point(e);if(!p)return;
                  select(n.id);drag.current={id:n.id,pointer:e.pointerId,start:p,origin:n,revision:data.revision};
                  svg.current?.setPointerCapture(e.pointerId);e.preventDefault();
                }}>
                <title>Node {n.id}</title><circle className="road-hit" r="16"/><circle r="7"/><text x="12" y="-10">{n.id}</text>
              </g>;
            })}
            {segment && <g className="road-new"><polyline points={segment.points.map(p=>view.screen(p.x,p.z).join(',')).join(' ')}/>
              {segment.points.map((p,i)=>{const [cx,cy]=view.screen(p.x,p.z);return <circle key={i} cx={cx} cy={cy} r="6"/>;})}</g>}
          </svg>
          <p className="road-caption">{net.nodes.length} nodes · {net.edges.length} segments</p>
        </>}
      </StudioSection>
      {segment && <StudioSection title="New segment">
        <p>{segment.points.length<2?('Click the '+(segment.points.length?'end':'start')+' on the plan. Click a road to attach to its lanes.'):'Review the endpoints and create the segment.'}</p>
        <StudioField label="Road type" htmlFor="road-type"><StudioSelect id="road-type" value={type??''} onChange={e=>setRoadType(e.target.value)}>
          {types.map(t=><option key={t.id} value={t.id}>{t.name||t.id}</option>)}
        </StudioSelect></StudioField>
        {segment.points.map((p,i)=>{
          const info=net?.edgeInfo.find(item=>item.id===p.edge);
          const counts=info?.lanes.split('+').map(Number)??[];
          const count=counts[p.dir===1?0:1]??0;
          return <fieldset className="road-end" key={i}><legend>{i?'End':'Start'} · {p.x.toFixed(1)}, {p.z.toFixed(1)}</legend>
            {info?<><span>Road {p.edge}</span>
              <label>Direction<select className="au-input" value={p.dir} onChange={e=>changeEnd(i,{dir:Number(e.target.value),first:0,last:0})}>
                {counts.map((n,j)=>n>0 && <option key={j} value={j?-1:1}>{j?'Backward':'Forward'}</option>)}</select></label>
              {(['first','last'] as const).map(k=><label key={k}>{k==='first'?'First lane':'Last lane'}<select className="au-input" value={p[k]}
                onChange={e=>{const v=Number(e.target.value);changeEnd(i,k==='first'?{first:v,last:Math.max(v,p.last)}:{last:v,first:Math.min(v,p.first)});}}>
                {Array.from({length:count},(_,n)=><option key={n} value={n}>{n+1}</option>)}</select></label>)}
            </>:<span>Free endpoint</span>}</fieldset>;
        })}
        <div className="road-actions"><StudioButton variant="primary" disabled={busy || !type || segment.points.length!==2 || segment.revision!==data?.revision} onClick={()=>void create()}>Create segment</StudioButton>
          <StudioButton disabled={busy} onClick={()=>setSegment(undefined)}>Cancel</StudioButton></div>
        {segment.revision!==data?.revision && <p role="status">The map changed. Cancel and select the endpoints again.</p>}
      </StudioSection>}
      {!segment && values && node && data && <StudioSection title="Node position">
        <StudioField label="Road node" htmlFor="road-node"><StudioSelect id="road-node" disabled={busy} value={node.id} onChange={e=>select(Number(e.target.value))}>
          {net?.nodes.map(n=><option key={n.id} value={n.id}>Node {n.id}</option>)}
        </StudioSelect></StudioField>
        <form onSubmit={e=>{e.preventDefault();if(changed)void move(node.id,{x:Number(values.x),z:Number(values.z),y:Number(values.y)},values.revision);}}>
          <div className="road-coordinates">{fields.map(k=><StudioField key={k} label={k==='y'?'Height (m)':k.toUpperCase()} htmlFor={'road-'+k}>
            <StudioInput id={'road-'+k} type="number" step="any" required disabled={busy} value={values[k]} onChange={e=>setDraft({...values,[k]:e.target.value})}/>
          </StudioField>)}</div>
          <StudioButton type="submit" variant="primary" disabled={busy||!changed}>Apply position</StudioButton>
        </form>
      </StudioSection>}
    </>}
  </StudioPanel>;
}
