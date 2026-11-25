import {useEffect,useRef,useState,type PointerEvent} from 'react';
import {invoke} from '../bridge';
import SpectraDockViewport from './SpectraDockViewport';

type Point=[number,number,number];
type Inputs=Record<string,number|Point>;
type Bone={name:string;parent:number};
type Contact={ground:number;stable:boolean;supports:number;supportError:number;penetration:number;balanceError:number;
 attached?:boolean;stanceFeet?:number;steppingFeet?:number;footError?:number};
type Frame={contact?:Contact;points:Point[];state:string;time:number;token:number};
type Opened=Frame&{bones:Bone[];inputs:Inputs};
type Plane='Front'|'Side'|'Top';
function project(p:Point,plane:Plane){
 if(plane==='Side')return [p[2],-p[1]];
 if(plane==='Top')return [p[0],p[2]];
 return [p[0],-p[1]];
}
function moveInPlane(value:Point,plane:Plane,dx:number,dy:number):Point{
 if(plane==='Side')return [value[0],value[1]-dy,value[2]+dx];
 if(plane==='Top')return [value[0]+dx,value[1],value[2]+dy];
 return [value[0]+dx,value[1]-dy,value[2]];
}
function controlPoint(event:PointerEvent<SVGGElement>){
 const matrix=event.currentTarget.ownerSVGElement?.getScreenCTM();
 return matrix?new DOMPoint(event.clientX,event.clientY).matrixTransform(matrix.inverse()):null;
}
function poseAndControls(points:Point[],inputs:Inputs){
 return [...points,...Object.values(inputs).filter((value):value is Point=>Array.isArray(value))];
}
function bounds(points:Point[],plane:Plane){
 let left=Infinity,right=-Infinity,top=Infinity,bottom=-Infinity;
 for(const p of points){
  const [x,y]=project(p,plane);
  left=Math.min(left,x);right=Math.max(right,x);top=Math.min(top,y);bottom=Math.max(bottom,y);
 }
 if(!points.length)return {left:-1,top:-1,width:2,height:2};
 const span=Math.max(.1,right-left,bottom-top),pad=span*.15;
 return {left:(left+right-span)/2-pad,top:(top+bottom-span)/2-pad,width:span+2*pad,height:span+2*pad};
}

export default function AnimationSkeletonPreview({projectPath,graph,scope,panel,onPick,onKeep,busy}:{projectPath:string;graph:object;scope:string;panel:'rig'|'graph';onPick:(bone:string)=>void;onKeep:(controls:Inputs)=>void;busy:boolean}){
 const [bind,setBind]=useState(false),[restart,setRestart]=useState(0),[playing,setPlaying]=useState(false);
 const [opened,setOpened]=useState<Opened|null>(null),[frame,setFrame]=useState<Frame|null>(null);
 const [plane,setPlane]=useState<Plane>('Front'),[fit,setFit]=useState<Point[]>([]),[selected,select]=useState(-1);
 const [inputs,setInputs]=useState<Inputs>({}),[error,setError]=useState('');
 const drag=useRef<{pointer:number;name:string;start:DOMPoint;value:Point}|null>(null);
 const [showMesh,setShowMesh]=useState(false),[yaw,setYaw]=useState(.5);
 const pendingYaw=useRef<number|undefined>(undefined);
 const pendingGround=useRef<number|undefined>(undefined);
 const running=useRef(false),changes=useRef<Inputs>({}),steps=useRef(0);
 useEffect(()=>{
  let active=true,token=0,timer=0,last=performance.now();
  drag.current=null;pendingGround.current=undefined;pendingYaw.current=undefined;setYaw(.5);
  setOpened(null);setFrame(null);setError('');select(-1);setPlaying(false);running.current=false;changes.current={};steps.current=0;
  async function command<T>(params:Record<string,unknown>){
   const response=await invoke('studio.op',{op:'animation_graph_preview',expectedProject:projectPath,params:{panel,...params}});
   return response.result as T;
  }
  function release(id:number){void command({action:'close',token:id}).catch(()=>{});}
  async function tick(){
   if(!active)return;
   try{
    const now=performance.now(),dt=running.current?Math.min(.25,(now-last)/1000):steps.current?1/30:0;
    last=now;
    const edits=changes.current;changes.current={};if(steps.current)steps.current--;
    const height=pendingGround.current;pendingGround.current=undefined;
    const angle=pendingYaw.current;pendingYaw.current=undefined;
    if(dt||height!==undefined||angle!==undefined||Object.keys(edits).length){
     const next=await command<Frame>({action:'frame',token,dt,inputs:edits,...(angle===undefined?{}:{yaw:angle}),...(height===undefined?{}:{ground:height})});
     if(active)setFrame(next);
    }
    if(active)timer=window.setTimeout(tick,33);
   }catch(e){if(active){setError(String(e));running.current=false;setPlaying(false);setOpened(null);}}
  }
  void command<Opened>({action:'open',graph,scope,bindPose:bind}).then(next=>{
   if(!active){release(next.token);return;}
   token=next.token;setOpened(next);setFrame(next);setFit(poseAndControls(next.points,next.inputs));setInputs(next.inputs);last=performance.now();void tick();
  }).catch(e=>{if(active)setError(String(e));});
  return ()=>{active=false;window.clearTimeout(timer);if(token)release(token);};
 },[projectPath,graph,scope,panel,bind,restart]);
 function pick(index:number){
  select(index);if(opened?.bones[index])onPick(opened.bones[index].name);
 }
 function input(name:string,value:number|Point){
  setInputs(old=>({...old,[name]:value}));changes.current={...changes.current,[name]:value};
 }
 const edits=Object.fromEntries(Object.entries(inputs).filter(([name,value])=>
  JSON.stringify(value)!==JSON.stringify(opened?.inputs[name])));
 const area=bounds(fit,plane),points=frame?.points.map(p=>project(p,plane))??[],radius=area.width*.014;
 return <div className={showMesh&&opened?'motion-preview motion-preview--model':'motion-preview'}>
  <div className="motion-preview-toolbar">
   <label>Pose source<select value={bind?'bind':'graph'} onChange={e=>setBind(e.target.value==='bind')}><option value="graph">Graph output</option><option value="bind">Model bind pose</option></select></label>
   <label>View<select value={plane} onChange={e=>setPlane(e.target.value as Plane)}>{['Front','Side','Top'].map(x=><option key={x}>{x}</option>)}</select></label>
   <button aria-label={playing?'Pause skeleton preview':'Play skeleton preview'} disabled={!opened} onClick={()=>{running.current=!playing;setPlaying(!playing);}}>{playing?'Pause':'Play'}</button>
   <button aria-label="Step skeleton preview" disabled={!opened||playing} onClick={()=>steps.current++}>Step</button>
   <button onClick={()=>setRestart(n=>n+1)}>Restart</button>
   <button disabled={!frame} onClick={()=>setFit(poseAndControls(frame!.points,inputs))}>Fit pose</button>
   <button disabled={!opened} aria-pressed={showMesh} onClick={()=>setShowMesh(v=>!v)}>Model preview</button>
   {opened&&!bind&&Object.keys(inputs).length>0&&<button disabled={busy||!Object.keys(edits).length} onClick={()=>onKeep(edits)}>Keep controls</button>}
  </div>
  {showMesh&&opened&&<div className="motion-model-preview">
   <SpectraDockViewport id={panel==='rig'?'animationRig':'animationGraph'} label="Animated model preview"/>
   <label>Model rotation<input type="range" min={-Math.PI} max={Math.PI} step=".01" value={yaw} onChange={e=>{const angle=e.target.valueAsNumber;setYaw(angle);pendingYaw.current=angle;}}/></label>
  </div>}
  {error&&<p role="alert">{error}</p>}
  {opened&&frame?<div className="motion-preview-page">
   <div className="motion-preview-stage"><svg viewBox={[area.left,area.top,area.width,area.height].join(' ')} aria-label="Evaluated skeleton">
    {frame.contact&&plane!=='Top'&&<line className="motion-ground" x1={area.left} x2={area.left+area.width} y1={-frame.contact.ground} y2={-frame.contact.ground}/>}
    {opened.bones.map((bone,i)=>{const parent=points[bone.parent],p=points[i];return parent&&p?<line key={i} x1={parent[0]} y1={parent[1]} x2={p[0]} y2={p[1]} className={selected===i?'selected':''}/>:null;})}
    {points.map((p,i)=><g key={i} role="button" tabIndex={0} aria-pressed={selected===i} aria-label={'Select joint '+opened.bones[i].name} onClick={()=>pick(i)} onKeyDown={e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();pick(i);}}}>
     <title>{opened.bones[i].name}</title><circle cx={p[0]} cy={p[1]} r={radius*1.9} fill="transparent"/><circle className="motion-joint" cx={p[0]} cy={p[1]} r={radius}/></g>)}
    {Object.entries(inputs).map(([name,value])=>{
     if(!Array.isArray(value))return null;
     const [x,y]=project(value,plane);
     return <g key={name} transform={`translate(${x} ${y})`} className="motion-target"
      role="group" aria-label={'Move target '+name} tabIndex={0}
      onPointerDown={e=>{
       if(e.button!==0||drag.current)return;
       const start=controlPoint(e);if(!start)return;
       e.preventDefault();e.currentTarget.focus();e.currentTarget.setPointerCapture(e.pointerId);
       drag.current={pointer:e.pointerId,name,start,value};
      }}
      onPointerMove={e=>{
       const current=drag.current;if(current?.pointer!==e.pointerId||current.name!==name)return;
       const pt=controlPoint(e);if(pt)input(name,moveInPlane(current.value,plane,pt.x-current.start.x,pt.y-current.start.y));
      }}
      onLostPointerCapture={e=>{if(drag.current?.pointer===e.pointerId)drag.current=null;}}
      onKeyDown={e=>{
       const arrows:Record<string,[number,number]>={ArrowLeft:[-1,0],ArrowRight:[1,0],ArrowUp:[0,-1],ArrowDown:[0,1]};
       const delta=arrows[e.key];if(!delta)return;
       e.preventDefault();e.stopPropagation();const step=area.width*(e.shiftKey ? .05 : .005);
       input(name,moveInPlane(value,plane,delta[0]*step,delta[1]*step));
      }}>
      <title>{name} · Drag or use arrow keys</title>
      <circle r={radius*3} fill="transparent"/>
      <rect className="motion-target-mark" x={-radius} y={-radius} width={2*radius} height={2*radius} transform="rotate(45)"/>
      <text x={radius*2} y={radius} fontSize={radius*3.5}>{name}</text>
     </g>;
    })}
   </svg><div className="motion-clock">{frame.state&&<span>{frame.state}</span>} <output>{frame.time.toFixed(2)} s</output></div></div>
   <div className="motion-preview-fields"><label>Selected joint<select value={selected} onChange={e=>pick(Number(e.target.value))}><option disabled value={-1}>Pick a joint</option>{opened.bones.map((b,i)=><option value={i} key={i}>{b.name||'Node '+i}</option>)}</select></label>
    {selected>=0&&<output>{['X','Y','Z'].map((k,i)=>k+' '+frame.points[selected][i].toFixed(3)).join(' · ')}</output>}
    {frame.contact&&<fieldset className="motion-contact"><legend>Ground contact</legend>
     <label>Ground height<input type="number" step="0.01" key={frame.token} defaultValue={frame.contact.ground} onChange={e=>{
      const height=e.target.valueAsNumber;if(Number.isFinite(height))pendingGround.current=height;
     }}/></label>
     <strong>{frame.contact.attached!==undefined?(frame.contact.attached?'Attached':'Detached'):
      frame.contact.stable?'Stable':frame.contact.supports?'Unstable':'Unsupported'}</strong>
     <dl>{(frame.contact.attached===undefined?[
      ['Supports',frame.contact.supports],['Support error',frame.contact.supportError.toFixed(3)],
      ['Penetration',frame.contact.penetration.toFixed(3)],['Balance error',frame.contact.balanceError.toFixed(3)]
     ]:[['Stance feet',frame.contact.stanceFeet],['Stepping feet',frame.contact.steppingFeet],
      ['Foot error',frame.contact.footError?.toFixed(3)]]).map(([key,val])=><div key={key}><dt>{key}</dt><dd>{val}</dd></div>)}</dl>
    </fieldset>}
    {Object.entries(inputs).map(([key,val])=>typeof val==='number'?<label key={key}>{key}<input type="number" step="any" value={val} onChange={e=>{if(Number.isFinite(e.target.valueAsNumber))input(key,e.target.valueAsNumber);}}/></label>:
     <fieldset key={key}><legend>{key}</legend>{val.map((v,i)=><label key={i}>{['X','Y','Z'][i]}<input type="number" step="any" value={v} onChange={e=>{if(Number.isFinite(e.target.valueAsNumber)){const next:Point=[...val];next[i]=e.target.valueAsNumber;input(key,next);}}}/></label>)}</fieldset>)}
   </div></div>:!error&&<p role="status">Loading model and evaluating pose…</p>}
 </div>;
}
