import {useRef,useState} from 'react';
import type {KeyboardEvent,PointerEvent} from 'react';
import {StudioButton} from '../ui';
import {moveGroup,selectInRect,selectionRect,selectNode} from './graphSelection';
import type {Point,Positioned,Rect} from './graphSelection';
import type {GraphView} from './graphFramework';
import {graphGeometry,nodeWidth,samePort} from './graphGeometry';
import type {PortRef} from './graphGeometry';
import './GraphCanvas.css';

export interface GraphCommand {id:string;label:string;shortcut?:string;disabled?:boolean;run:()=>void}
export interface GraphCanvasProps {
 view:GraphView; selected:string; label:string; disabled?:boolean;
 pendingOutput?:PortRef|null; onOutput?:(port:PortRef)=>void; onInput?:(port:PortRef)=>void;
 commands?:readonly GraphCommand[]; onCancel?:()=>void;
 selection?:readonly string[]; onSelection?:(ids:string[])=>void; onMoveGroup?:(positions:Positioned[])=>void;
 onSelect:(id:string)=>void; onMove:(id:string,x:number,y:number)=>void;
}
export default function GraphCanvas({view,selected,label,disabled,pendingOutput,onOutput,onInput,onSelect,onMove,selection,onSelection,onMoveGroup,commands,onCancel}:GraphCanvasProps) {
 const svg=useRef<SVGSVGElement>(null);
 const scroll=useRef<HTMLDivElement>(null);
 const drag=useRef<{pointer:number;origin:Point;nodes:Positioned[]}|null>(null);
 const box=useRef<{pointer:number;origin:Point;extend:boolean}|null>(null);
 const pan=useRef<{pointer:number;x:number;y:number;left:number;top:number}|null>(null);
 const [preview,setPreview]=useState<Positioned[]>([]);
 const [rect,setRect]=useState<Rect|null>(null);
 const ids=selection??(selected?[selected]:[]);
 const geometry=graphGeometry(view,preview);
 const [cursor,setCursor]=useState<Point|null>(null);
 const [menu,setMenu]=useState(false);
 const pending=geometry.ports.find(p=>p.direction==='output'&&samePort(pendingOutput,p));
 const updateSelection=(next:string[])=>{if(onSelection)onSelection(next);else onSelect(next.at(-1)??'');};
 const [zoom,setZoom]=useState(1);
 const point=(event:PointerEvent<SVGElement>)=>{
  const ctm=svg.current?.getScreenCTM();
  return ctm?new DOMPoint(event.clientX,event.clientY).matrixTransform(ctm.inverse()):null;
 };
 const end=(event:PointerEvent<SVGElement>,commit:boolean)=>{
  const p=point(event);
  if(pan.current?.pointer===event.pointerId)pan.current=null;
  const b=box.current;
  if(b?.pointer===event.pointerId){
   box.current=null;setRect(null);
   if(commit&&!disabled&&p){
    const found=selectInRect(geometry.nodes.map(n=>({...n,width:nodeWidth,height:n.height})),selectionRect(b.origin,p));
    updateSelection(b.extend?[...new Set([...ids,...found])]:found);
   }
  }
  const d=drag.current;if(!d||d.pointer!==event.pointerId)return;
  drag.current=null;setPreview([]);
  if(commit&&!disabled&&p&&(p.x!==d.origin.x||p.y!==d.origin.y)){
   const next=moveGroup(d.nodes,p.x-d.origin.x,p.y-d.origin.y);
   if(onMoveGroup)onMoveGroup(next);else for(const n of next)onMove(n.id,n.x,n.y);
  }
 };
 const key=(event:KeyboardEvent<SVGGElement>,node:GraphView['nodes'][number])=>{
  if(disabled||event.target!==event.currentTarget)return;
  const offsets:Record<string,[number,number]>={ArrowLeft:[-10,0],ArrowRight:[10,0],ArrowUp:[0,-10],ArrowDown:[0,10]};
  if(event.key==='Enter'||event.key===' '){event.preventDefault();onSelect(node.id);return;}
  const delta=offsets[event.key];if(!delta)return;
  event.preventDefault();onSelect(node.id);onMove(node.id,node.x+delta[0],node.y+delta[1]);
 };
 const maxHeight=Math.max(320,...view.nodes.map(n=>n.y+(geometry.nodes.find(g=>g.id===n.id)?.height??54)+36));
 const maxWidth=Math.max(520,...view.nodes.map(n=>n.x+nodeWidth+36));
 const curve=(x1:number,y1:number,x2:number,y2:number)=>`M ${x1} ${y1} C ${x1+55} ${y1}, ${x2-55} ${y2}, ${x2} ${y2}`;
 return <div className="au-graph-workspace" onContextMenu={e=>{if(commands?.length&&!disabled){e.preventDefault();setMenu(v=>!v)}}} onKeyDown={e=>{if(e.key==='Escape'){setMenu(false);onCancel?.();return;}if(disabled||e.defaultPrevented)return;const target=e.target as HTMLElement;if(target.closest('input,textarea,select,[contenteditable=true]'))return;const chord=(e.metaKey||e.ctrlKey?'mod+':'')+(e.shiftKey?'shift+':'')+e.key.toLowerCase();const command=commands?.find(c=>c.shortcut===chord&&!c.disabled);if(command){e.preventDefault();e.stopPropagation();setMenu(false);command.run();}}}>
 <div className="au-row" aria-label="Graph zoom">
  <StudioButton size="sm" disabled={zoom<=.5} onClick={()=>setZoom(z=>Math.max(.5,z-.25))}>−</StudioButton>
  <StudioButton size="sm" onClick={()=>setZoom(1)}>{Math.round(zoom*100)}%</StudioButton>
  <StudioButton size="sm" disabled={zoom>=2} onClick={()=>setZoom(z=>Math.min(2,z+.25))}>+</StudioButton>
  {commands?.length&&<StudioButton size="sm" aria-expanded={menu} disabled={disabled} onClick={()=>setMenu(v=>!v)}>Actions</StudioButton>}
 </div>
 {menu&&<div className="au-row au-graph-actions" aria-label="Graph actions">{commands?.map(c=><StudioButton key={c.id} size="sm" disabled={disabled||c.disabled} onClick={()=>{setMenu(false);c.run()}}>{c.label}</StudioButton>)}</div>}
 <div className="au-graph-canvas" ref={scroll}>
  <svg ref={svg} width={maxWidth*zoom} height={maxHeight*zoom} viewBox={`0 0 ${maxWidth} ${maxHeight}`}
   role="group" tabIndex={0} aria-label={label}
   onPointerDown={event=>{
    if(disabled||drag.current||box.current||pan.current)return;
    if(event.button===1||(event.button===0&&event.altKey)){
     const el=scroll.current;if(!el)return;event.preventDefault();
     pan.current={pointer:event.pointerId,x:event.clientX,y:event.clientY,left:el.scrollLeft,top:el.scrollTop};
     event.currentTarget.setPointerCapture(event.pointerId);return;
    }
    if(event.button!==0||event.target!==event.currentTarget)return;
    const p=point(event);if(!p)return;event.preventDefault();event.currentTarget.focus();
    box.current={pointer:event.pointerId,origin:p,extend:event.shiftKey};
    setRect(selectionRect(p,p));event.currentTarget.setPointerCapture(event.pointerId);
   }}
   onPointerMove={event=>{
    const v=pan.current;
    if(v?.pointer===event.pointerId&&scroll.current){
     scroll.current.scrollLeft=v.left-(event.clientX-v.x);scroll.current.scrollTop=v.top-(event.clientY-v.y);return;
    }
    const p=point(event);if(!p||disabled)return;
    setCursor(p);
    const b=box.current;if(b?.pointer===event.pointerId)setRect(selectionRect(b.origin,p));
    const d=drag.current;if(d?.pointer===event.pointerId)setPreview(moveGroup(d.nodes,p.x-d.origin.x,p.y-d.origin.y));
   }} onPointerUp={e=>end(e,true)} onPointerCancel={e=>end(e,false)} onLostPointerCapture={e=>end(e,false)}>
   {geometry.wires.map(edge=><path key={edge.id} d={curve(edge.x1,edge.y1,edge.x2,edge.y2)} fill="none" stroke="var(--au-accent)" strokeWidth="2" pointerEvents="none"/>)}
   {pending&&cursor&&<path d={curve(pending.x,pending.y,cursor.x,cursor.y)} fill="none" stroke="var(--au-accent)" strokeDasharray="4 3" pointerEvents="none"/>}
   {geometry.nodes.map(node=>{
    const height=node.height;
    return <g key={node.id} className="au-graph-node"
    transform={`translate(${node.x} ${node.y})`} tabIndex={disabled?-1:0}
    role="group" aria-label={node.id+' '+node.label}
    onKeyDown={event=>key(event,node)}
    onContextMenu={()=>{if(!ids.includes(node.id))updateSelection([node.id]);}}
    onPointerDown={event=>{
     if(disabled||event.button!==0||event.altKey||drag.current||box.current||pan.current)return;const p=point(event);if(!p)return;
     event.preventDefault();event.stopPropagation();event.currentTarget.focus();
     const next=selectNode(ids,node.id,event.shiftKey||event.metaKey||event.ctrlKey);
     updateSelection(next);if(!next.includes(node.id))return;
     drag.current={pointer:event.pointerId,origin:p,nodes:view.nodes.filter(n=>next.includes(n.id)).map(n=>({id:n.id,x:n.x,y:n.y}))};
     event.currentTarget.setPointerCapture(event.pointerId);
    }}>
    <rect width="130" height={height} rx="7" fill={ids.includes(node.id)?'var(--au-accent-strong)':'var(--au-panel-raised)'}
     stroke={ids.includes(node.id)?'var(--au-accent-ink)':'var(--au-accent)'}/>
    <text x="10" y="15" fill="var(--au-text)" fontSize="11">{node.id}</text>
    <text x="10" y={height-5} fill="var(--au-text-soft)" fontSize="10">{node.label}</text>
    {geometry.ports.filter(p=>p.node===node.id).map(p=><circle key={p.direction+':'+p.port}
     cx={p.x-node.x} cy={p.y-node.y} r="5" role={p.direction==='output'?(onOutput?'button':undefined):(onInput?'button':undefined)} tabIndex={disabled||!(p.direction==='output'?onOutput:onInput)?-1:0}
     aria-label={node.id+' '+p.direction+' '+p.port+' '+p.type}
     aria-pressed={p.direction==='output'?samePort(pendingOutput,p):undefined}
     fill={p.direction==='output'&&samePort(pendingOutput,p)?'var(--au-accent-ink)':'var(--au-accent)'}
     onPointerDown={e=>e.stopPropagation()} onClick={()=>{if(!disabled)(p.direction==='output'?onOutput:onInput)?.({node:p.node,port:p.port})}}
     onKeyDown={e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();e.stopPropagation();if(!disabled)(p.direction==='output'?onOutput:onInput)?.({node:p.node,port:p.port})}}}><title>{p.port+' · '+p.type}</title></circle>)}
   </g>})}
   {rect&&<rect x={rect.x} y={rect.y} width={rect.width} height={rect.height} fill="var(--au-accent)" fillOpacity="0.12" stroke="var(--au-accent)" strokeDasharray="4 3" pointerEvents="none"/>}
  </svg>
 </div></div>;
}
