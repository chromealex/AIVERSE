import {useCallback,useEffect,useRef,useState} from 'react';
import {invoke} from '../bridge';
import {onReferenceView,referenceView} from './referenceStore';
import {brushPixels,onSculptBrush,sculptBrush,strokeSpacing} from './sculptBrushStore';
import SpectraDockViewport from './SpectraDockViewport';

type Rect={x:number;y:number;width:number;height:number};
type Point={x:number;y:number};

export default function CharacterViewport({id,label}:{id:'character'|'appearance';label:string}){
 const [reference,setReference]=useState(referenceView);
 const [brush,setBrush]=useState(sculptBrush);
 const liveReference=useRef(reference);
 liveReference.current=reference;
 const viewport=useRef<Rect>({x:0,y:0,width:0,height:0});
 const setRect=useCallback((rect:Rect)=>{viewport.current=rect;},[]);
 const element=useRef<HTMLDivElement>(null);
 const drag=useRef<(Point&{paint:boolean;moved:boolean})|null>(null);
 const composite=document.documentElement.classList.contains('character-composite');
 useEffect(()=>onReferenceView(setReference),[]);
 useEffect(()=>onSculptBrush(setBrush),[]);
 function pick(x:number,y:number){
  const r=viewport.current;
  if(r.width&&r.height)void invoke('studio.viewportInput',{surface:id,kind:'pick',x:(x-r.x)/r.width,y:(y-r.y)/r.height}).catch(console.warn);
 }
 useEffect(()=>{
  if(!composite)return;
  const {src,...settings}=reference;
  void invoke('studio.viewportInput',{surface:id,kind:'reference',...settings}).catch(console.warn);
 },[id,composite,reference]);
 useEffect(()=>{
  if(!composite)return;
  let active=true;
  const publish=(image:string)=>{
   if(!active)return;
   const {src,...settings}=liveReference.current;
   void invoke('studio.viewportInput',{surface:id,kind:'reference',...settings,image}).catch(console.warn);
  };
  publish('');
  if(reference.src){
   const img=new Image();
   img.onload=()=>{
    if(!active||!img.naturalWidth||!img.naturalHeight)return;
    const ratio=Math.min(1,2048/Math.max(img.naturalWidth,img.naturalHeight));
    const canvas=document.createElement('canvas');
    canvas.width=Math.max(1,Math.round(img.naturalWidth*ratio));canvas.height=Math.max(1,Math.round(img.naturalHeight*ratio));
    const ctx=canvas.getContext('2d');if(!ctx)return;
    ctx.drawImage(img,0,0,canvas.width,canvas.height);publish(canvas.toDataURL('image/png'));
   };
   img.src=reference.src;
  }
  return ()=>{active=false;};
 },[id,composite,reference.src]);
 useEffect(()=>{
  const target=element.current;
  if(!target||!composite)return;
  const wheel=(event:WheelEvent)=>{
   event.preventDefault();
   void invoke('studio.viewportInput',{surface:id,kind:'zoom',amount:-Math.sign(event.deltaY)*2}).catch(console.warn);
  };
  target.addEventListener('wheel',wheel,{passive:false});
  return ()=>target.removeEventListener('wheel',wheel);
 },[id,composite]);
 useEffect(()=>{
  if(composite&&!brush.armed)void invoke('studio.viewportInput',{surface:id,kind:'brushCursor',x:0,y:0,radius:0}).catch(console.warn);
 },[id,composite,brush.armed]);
 const radius=brushPixels(brush.radius,viewport.current.height);
 return <div ref={element} className="character-viewport" onContextMenu={e=>e.preventDefault()}
  onPointerDown={e=>{
   if(!composite||drag.current)return;
   e.currentTarget.setPointerCapture(e.pointerId);
   const paint=e.button===0&&brush.armed;
   drag.current={x:e.clientX,y:e.clientY,paint,moved:false};
   if(paint)pick(e.clientX,e.clientY);
  }}
  onPointerMove={e=>{
   if(!composite)return;
   if(brush.armed){
    const r=viewport.current;
    if(r.width&&r.height)void invoke('studio.viewportInput',{surface:id,kind:'brushCursor',x:(e.clientX-r.x)/r.width,y:(e.clientY-r.y)/r.height,radius}).catch(console.warn);
   }
   const d=drag.current;if(!d)return;
   const dx=e.clientX-d.x,dy=e.clientY-d.y;
   if(Math.hypot(dx,dy)<(d.paint?strokeSpacing(radius):2))return;
   if(d.paint)pick(e.clientX,e.clientY);
   else void invoke('studio.viewportInput',{surface:id,kind:'orbit',dx,dy}).catch(console.warn);
   d.x=e.clientX;d.y=e.clientY;d.moved=true;
  }}
  onPointerUp={e=>{if(e.button===0&&drag.current&&!drag.current.paint&&!drag.current.moved)pick(e.clientX,e.clientY);drag.current=null;}}
  onLostPointerCapture={()=>{drag.current=null;}}
  onPointerLeave={()=>{if(composite&&brush.armed)void invoke('studio.viewportInput',{surface:id,kind:'brushCursor',x:0,y:0,radius:0}).catch(console.warn);}}
  >
  <SpectraDockViewport id={id} label={label} onRect={setRect}/>
 </div>;
}
