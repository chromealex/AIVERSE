import {useRef} from 'react';
import type {KeyboardEvent,PointerEvent} from 'react';
import './GraphCanvas.css';

export interface CanvasDiagram {
 width:number; height:number;
 nodes:readonly {id:string;op:string;x:number;y:number}[];
 edges:readonly {key:string;x1:number;y1:number;x2:number;y2:number}[];
}
export interface GraphCanvasProps {
 diagram:CanvasDiagram; selected:string; label:string; disabled?:boolean;
 onSelect:(id:string)=>void; onMove:(id:string,x:number,y:number)=>void;
}
export default function GraphCanvas({diagram,selected,label,disabled,onSelect,onMove}:GraphCanvasProps) {
 const svg=useRef<SVGSVGElement>(null);
 const drag=useRef<{id:string;pointer:number;x:number;y:number;originalX:number;originalY:number}|null>(null);
 const point=(event:PointerEvent<SVGElement>)=>{
  const ctm=svg.current?.getScreenCTM();
  return ctm?new DOMPoint(event.clientX,event.clientY).matrixTransform(ctm.inverse()):null;
 };
 const end=(event:PointerEvent<SVGElement>)=>{if(drag.current?.pointer===event.pointerId)drag.current=null;};
 const key=(event:KeyboardEvent<SVGGElement>,node:CanvasDiagram['nodes'][number])=>{
  if(disabled)return;
  const offsets:Record<string,[number,number]>={ArrowLeft:[-10,0],ArrowRight:[10,0],ArrowUp:[0,-10],ArrowDown:[0,10]};
  if(event.key==='Enter'||event.key===' '){event.preventDefault();onSelect(node.id);return;}
  const delta=offsets[event.key];if(!delta)return;
  event.preventDefault();onSelect(node.id);onMove(node.id,node.x+delta[0],node.y+delta[1]);
 };
 return <div className="au-graph-canvas">
  <svg ref={svg} width={diagram.width} height={diagram.height} viewBox={`0 0 ${diagram.width} ${diagram.height}`}
   role="group" aria-label={label}
   onPointerMove={event=>{
    const d=drag.current;if(disabled||!d||event.pointerId!==d.pointer)return;
    const p=point(event);if(p)onMove(d.id,d.originalX+p.x-d.x,d.originalY+p.y-d.y);
   }} onPointerUp={end} onPointerCancel={end} onLostPointerCapture={end}>
   {diagram.edges.map(edge=><path key={edge.key}
    d={`M ${edge.x1} ${edge.y1} C ${edge.x1+55} ${edge.y1}, ${edge.x2-55} ${edge.y2}, ${edge.x2} ${edge.y2}`}
    fill="none" stroke="var(--au-accent)" strokeWidth="2"/>)}
   {diagram.nodes.map(node=><g key={node.id} className="au-graph-node"
    transform={`translate(${node.x} ${node.y})`} tabIndex={disabled?-1:0}
    role="button" aria-label={node.id+' '+node.op} aria-pressed={selected===node.id}
    onKeyDown={event=>key(event,node)}
    onPointerDown={event=>{
     if(disabled||event.button!==0||drag.current)return;const p=point(event);if(!p)return;
     event.preventDefault();event.currentTarget.focus();onSelect(node.id);
     drag.current={id:node.id,pointer:event.pointerId,x:p.x,y:p.y,originalX:node.x,originalY:node.y};
     event.currentTarget.setPointerCapture(event.pointerId);
    }}>
    <rect width="130" height="54" rx="7" fill={selected===node.id?'var(--au-accent-strong)':'var(--au-panel-raised)'}
     stroke={selected===node.id?'var(--au-accent-ink)':'var(--au-accent)'}/>
    <text x="10" y="20" fill="var(--au-text)" fontSize="11">{node.id}</text>
    <text x="10" y="39" fill="var(--au-text-soft)" fontSize="10">{node.op}</text>
   </g>)}
  </svg>
 </div>;
}
