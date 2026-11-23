import {useRef,useState} from 'react';
import type {KeyboardEvent,PointerEvent} from 'react';
import {StudioButton} from '../ui';
import {moveGroup,selectInRect,selectionRect,selectNode} from './graphSelection';
import type {Point,Positioned,Rect} from './graphSelection';
import './GraphCanvas.css';

export interface CanvasDiagram {
 width:number; height:number;
 nodes:readonly {id:string;op:string;x:number;y:number}[];
 edges:readonly {key:string;x1:number;y1:number;x2:number;y2:number}[];
}
export interface CanvasPorts { inputs:number; output:boolean }
export interface GraphCanvasProps {
 diagram:CanvasDiagram; selected:string; label:string; disabled?:boolean;
 ports?:Readonly<Record<string,CanvasPorts>>; pendingOutput?:string;
 onOutput?:(id:string)=>void; onInput?:(id:string,index:number)=>void;
 selection?:readonly string[]; onSelection?:(ids:string[])=>void; onMoveGroup?:(positions:Positioned[])=>void;
 onSelect:(id:string)=>void; onMove:(id:string,x:number,y:number)=>void;
}
export default function GraphCanvas({diagram,selected,label,disabled,ports,pendingOutput,onOutput,onInput,onSelect,onMove,selection,onSelection,onMoveGroup}:GraphCanvasProps) {
 const svg=useRef<SVGSVGElement>(null);
 const scroll=useRef<HTMLDivElement>(null);
 const drag=useRef<{pointer:number;origin:Point;nodes:Positioned[]}|null>(null);
 const box=useRef<{pointer:number;origin:Point;extend:boolean}|null>(null);
 const pan=useRef<{pointer:number;x:number;y:number;left:number;top:number}|null>(null);
 const [preview,setPreview]=useState<Positioned[]>([]);
 const [rect,setRect]=useState<Rect|null>(null);
 const ids=selection??(selected?[selected]:[]);
 const heightOf=(id:string)=>Math.max(54,34+(ports?.[id]?.inputs??0)*10);
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
    const found=selectInRect(diagram.nodes.map(n=>({...n,width:130,height:heightOf(n.id)})),selectionRect(b.origin,p));
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
 const key=(event:KeyboardEvent<SVGGElement>,node:CanvasDiagram['nodes'][number])=>{
  if(disabled||event.target!==event.currentTarget)return;
  const offsets:Record<string,[number,number]>={ArrowLeft:[-10,0],ArrowRight:[10,0],ArrowUp:[0,-10],ArrowDown:[0,10]};
  if(event.key==='Enter'||event.key===' '){event.preventDefault();onSelect(node.id);return;}
  const delta=offsets[event.key];if(!delta)return;
  event.preventDefault();onSelect(node.id);onMove(node.id,node.x+delta[0],node.y+delta[1]);
 };
 const maxHeight=Math.max(diagram.height,...diagram.nodes.map(n=>n.y+Math.max(54,34+(ports?.[n.id]?.inputs??0)*10)+36));
 return <div className="au-graph-workspace">
 <div className="au-row" aria-label="Graph zoom">
  <StudioButton size="sm" disabled={zoom<=.5} onClick={()=>setZoom(z=>Math.max(.5,z-.25))}>−</StudioButton>
  <StudioButton size="sm" onClick={()=>setZoom(1)}>{Math.round(zoom*100)}%</StudioButton>
  <StudioButton size="sm" disabled={zoom>=2} onClick={()=>setZoom(z=>Math.min(2,z+.25))}>+</StudioButton>
 </div>
 <div className="au-graph-canvas" ref={scroll}>
  <svg ref={svg} width={diagram.width*zoom} height={maxHeight*zoom} viewBox={`0 0 ${diagram.width} ${maxHeight}`}
   role="group" aria-label={label}
   onPointerDown={event=>{
    if(disabled||drag.current||box.current||pan.current)return;
    if(event.button===1||(event.button===0&&event.altKey)){
     const el=scroll.current;if(!el)return;event.preventDefault();
     pan.current={pointer:event.pointerId,x:event.clientX,y:event.clientY,left:el.scrollLeft,top:el.scrollTop};
     event.currentTarget.setPointerCapture(event.pointerId);return;
    }
    if(event.button!==0||event.target!==event.currentTarget)return;
    const p=point(event);if(!p)return;event.preventDefault();
    box.current={pointer:event.pointerId,origin:p,extend:event.shiftKey};
    setRect(selectionRect(p,p));event.currentTarget.setPointerCapture(event.pointerId);
   }}
   onPointerMove={event=>{
    const v=pan.current;
    if(v?.pointer===event.pointerId&&scroll.current){
     scroll.current.scrollLeft=v.left-(event.clientX-v.x);scroll.current.scrollTop=v.top-(event.clientY-v.y);return;
    }
    const p=point(event);if(!p||disabled)return;
    const b=box.current;if(b?.pointer===event.pointerId)setRect(selectionRect(b.origin,p));
    const d=drag.current;if(d?.pointer===event.pointerId)setPreview(moveGroup(d.nodes,p.x-d.origin.x,p.y-d.origin.y));
   }} onPointerUp={e=>end(e,true)} onPointerCancel={e=>end(e,false)} onLostPointerCapture={e=>end(e,false)}>
   {diagram.edges.map(edge=><path key={edge.key}
    d={`M ${edge.x1} ${edge.y1} C ${edge.x1+55} ${edge.y1}, ${edge.x2-55} ${edge.y2}, ${edge.x2} ${edge.y2}`}
    fill="none" stroke="var(--au-accent)" strokeWidth="2"/>)}
   {diagram.nodes.map(node=>{
    const p=ports?.[node.id];
    const pos=preview.find(p=>p.id===node.id)??node;
    const height=Math.max(54,34+(p?.inputs??0)*10);
    return <g key={node.id} className="au-graph-node"
    transform={`translate(${pos.x} ${pos.y})`} tabIndex={disabled?-1:0}
    role="group" aria-label={node.id+' '+node.op}
    onKeyDown={event=>key(event,node)}
    onPointerDown={event=>{
     if(disabled||event.button!==0||event.altKey||drag.current||box.current||pan.current)return;const p=point(event);if(!p)return;
     event.preventDefault();event.stopPropagation();event.currentTarget.focus();
     const next=selectNode(ids,node.id,event.shiftKey||event.metaKey||event.ctrlKey);
     updateSelection(next);if(!next.includes(node.id))return;
     drag.current={pointer:event.pointerId,origin:p,nodes:diagram.nodes.filter(n=>next.includes(n.id)).map(n=>({id:n.id,x:n.x,y:n.y}))};
     event.currentTarget.setPointerCapture(event.pointerId);
    }}>
    <rect width="130" height={height} rx="7" fill={ids.includes(node.id)?'var(--au-accent-strong)':'var(--au-panel-raised)'}
     stroke={ids.includes(node.id)?'var(--au-accent-ink)':'var(--au-accent)'}/>
    <text x="10" y="15" fill="var(--au-text)" fontSize="11">{node.id}</text>
    <text x="10" y={height-5} fill="var(--au-text-soft)" fontSize="10">{node.op}</text>
    {p&&Array.from({length:p.inputs},(_,i)=><circle key={i} cx="0" cy={25+i*9} r="4" tabIndex={disabled?-1:0}
     role="button" aria-label={node.id+' input '+(i+1)} fill="var(--au-accent)"
     onPointerDown={e=>e.stopPropagation()} onClick={()=>{if(!disabled)onInput?.(node.id,i)}}
     onKeyDown={e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();e.stopPropagation();if(!disabled)onInput?.(node.id,i)}}}/>)}
    {p?.output&&<circle cx="130" cy="25" r="5" role="button" tabIndex={disabled?-1:0} aria-label={node.id+' output'}
     aria-pressed={pendingOutput===node.id} fill={pendingOutput===node.id?'var(--au-accent-ink)':'var(--au-accent)'}
     onPointerDown={e=>e.stopPropagation()} onClick={()=>{if(!disabled)onOutput?.(node.id)}}
     onKeyDown={e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();e.stopPropagation();if(!disabled)onOutput?.(node.id)}}}/>}
   </g>})}
   {rect&&<rect x={rect.x} y={rect.y} width={rect.width} height={rect.height} fill="var(--au-accent)" fillOpacity="0.12" stroke="var(--au-accent)" strokeDasharray="4 3" pointerEvents="none"/>}
  </svg>
 </div></div>;
}
