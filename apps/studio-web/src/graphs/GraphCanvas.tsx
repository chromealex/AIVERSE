import {useRef,useState} from 'react';
import type {KeyboardEvent,PointerEvent} from 'react';
import {StudioButton} from '../ui';
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
 onSelect:(id:string)=>void; onMove:(id:string,x:number,y:number)=>void;
}
export default function GraphCanvas({diagram,selected,label,disabled,ports,pendingOutput,onOutput,onInput,onSelect,onMove}:GraphCanvasProps) {
 const svg=useRef<SVGSVGElement>(null);
 const drag=useRef<{id:string;pointer:number;x:number;y:number;originalX:number;originalY:number}|null>(null);
 const [preview,setPreview]=useState<{id:string;x:number;y:number}|null>(null);
 const [zoom,setZoom]=useState(1);
 const point=(event:PointerEvent<SVGElement>)=>{
  const ctm=svg.current?.getScreenCTM();
  return ctm?new DOMPoint(event.clientX,event.clientY).matrixTransform(ctm.inverse()):null;
 };
 const end=(event:PointerEvent<SVGElement>,commit:boolean)=>{
  const d=drag.current;if(!d||d.pointer!==event.pointerId)return;
  drag.current=null;setPreview(null);
  const p=point(event);
  if(commit&&!disabled&&p&&(p.x!==d.x||p.y!==d.y))onMove(d.id,d.originalX+p.x-d.x,d.originalY+p.y-d.y);
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
 <div className="au-graph-canvas">
  <svg ref={svg} width={diagram.width*zoom} height={maxHeight*zoom} viewBox={`0 0 ${diagram.width} ${maxHeight}`}
   role="group" aria-label={label}
   onPointerMove={event=>{
    const d=drag.current;if(disabled||!d||event.pointerId!==d.pointer)return;
    const p=point(event);if(p)setPreview({id:d.id,x:d.originalX+p.x-d.x,y:d.originalY+p.y-d.y});
   }} onPointerUp={e=>end(e,true)} onPointerCancel={e=>end(e,false)} onLostPointerCapture={e=>end(e,false)}>
   {diagram.edges.map(edge=><path key={edge.key}
    d={`M ${edge.x1} ${edge.y1} C ${edge.x1+55} ${edge.y1}, ${edge.x2-55} ${edge.y2}, ${edge.x2} ${edge.y2}`}
    fill="none" stroke="var(--au-accent)" strokeWidth="2"/>)}
   {diagram.nodes.map(node=>{
    const p=ports?.[node.id];
    const pos=preview?.id===node.id?preview:node;
    const height=Math.max(54,34+(p?.inputs??0)*10);
    return <g key={node.id} className="au-graph-node"
    transform={`translate(${pos.x} ${pos.y})`} tabIndex={disabled?-1:0}
    role="group" aria-label={node.id+' '+node.op}
    onKeyDown={event=>key(event,node)}
    onPointerDown={event=>{
     if(disabled||event.button!==0||drag.current)return;const p=point(event);if(!p)return;
     event.preventDefault();event.currentTarget.focus();onSelect(node.id);
     drag.current={id:node.id,pointer:event.pointerId,x:p.x,y:p.y,originalX:node.x,originalY:node.y};
     event.currentTarget.setPointerCapture(event.pointerId);
    }}>
    <rect width="130" height={height} rx="7" fill={selected===node.id?'var(--au-accent-strong)':'var(--au-panel-raised)'}
     stroke={selected===node.id?'var(--au-accent-ink)':'var(--au-accent)'}/>
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
  </svg>
 </div></div>;
}
