import {useEffect,useRef,useState} from 'react';
import {invoke} from '../bridge';

type Point=[number,number,number];
type Inputs=Record<string,number|Point>;
type Bone={name:string;parent:number};
type Frame={points:Point[];state:string;time:number;token:number};
type Opened=Frame&{bones:Bone[];inputs:Inputs};
type Plane='Front'|'Side'|'Top';
function project(p:Point,plane:Plane){
 if(plane==='Side')return [p[2],-p[1]];
 if(plane==='Top')return [p[0],p[2]];
 return [p[0],-p[1]];
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

export default function AnimationSkeletonPreview({projectPath,graph,scope,panel,onPick}:{projectPath:string;graph:object;scope:string;panel:'rig'|'graph';onPick:(bone:string)=>void}){
 const [bind,setBind]=useState(false),[restart,setRestart]=useState(0),[playing,setPlaying]=useState(false);
 const [opened,setOpened]=useState<Opened|null>(null),[frame,setFrame]=useState<Frame|null>(null);
 const [plane,setPlane]=useState<Plane>('Front'),[fit,setFit]=useState<Point[]>([]),[selected,select]=useState(-1);
 const [inputs,setInputs]=useState<Inputs>({}),[error,setError]=useState('');
 const running=useRef(false),changes=useRef<Inputs>({}),steps=useRef(0);
 useEffect(()=>{
  let active=true,token=0,timer=0,last=performance.now();
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
    if(dt||Object.keys(edits).length){
     const next=await command<Frame>({action:'frame',token,dt,inputs:edits});
     if(active)setFrame(next);
    }
    if(active)timer=window.setTimeout(tick,33);
   }catch(e){if(active){setError(String(e));running.current=false;setPlaying(false);setOpened(null);}}
  }
  void command<Opened>({action:'open',graph,scope,bindPose:bind}).then(next=>{
   if(!active){release(next.token);return;}
   token=next.token;setOpened(next);setFrame(next);setFit(next.points);setInputs(next.inputs);last=performance.now();void tick();
  }).catch(e=>{if(active)setError(String(e));});
  return ()=>{active=false;window.clearTimeout(timer);if(token)release(token);};
 },[projectPath,graph,scope,panel,bind,restart]);
 function pick(index:number){
  select(index);if(opened?.bones[index])onPick(opened.bones[index].name);
 }
 function input(name:string,value:number|Point){
  setInputs(old=>({...old,[name]:value}));changes.current={...changes.current,[name]:value};
 }
 const area=bounds(fit,plane),points=frame?.points.map(p=>project(p,plane))??[],radius=area.width*.014;
 return <div className="motion-preview">
  <div className="motion-preview-toolbar">
   <label>Pose source<select value={bind?'bind':'graph'} onChange={e=>setBind(e.target.value==='bind')}><option value="graph">Graph output</option><option value="bind">Model bind pose</option></select></label>
   <label>View<select value={plane} onChange={e=>setPlane(e.target.value as Plane)}>{['Front','Side','Top'].map(x=><option key={x}>{x}</option>)}</select></label>
   <button aria-label={playing?'Pause skeleton preview':'Play skeleton preview'} disabled={!opened} onClick={()=>{running.current=!playing;setPlaying(!playing);}}>{playing?'Pause':'Play'}</button>
   <button aria-label="Step skeleton preview" disabled={!opened||playing} onClick={()=>steps.current++}>Step</button>
   <button onClick={()=>setRestart(n=>n+1)}>Restart</button>
   <button disabled={!frame} onClick={()=>setFit(frame!.points)}>Fit pose</button>
  </div>
  {error&&<p role="alert">{error}</p>}
  {opened&&frame?<div className="motion-preview-page">
   <div className="motion-preview-stage"><svg viewBox={[area.left,area.top,area.width,area.height].join(' ')} aria-label="Evaluated skeleton">
    {opened.bones.map((bone,i)=>{const parent=points[bone.parent],p=points[i];return parent&&p?<line key={i} x1={parent[0]} y1={parent[1]} x2={p[0]} y2={p[1]} className={selected===i?'selected':''}/>:null;})}
    {points.map((p,i)=><g key={i} role="button" tabIndex={0} aria-pressed={selected===i} aria-label={'Select joint '+opened.bones[i].name} onClick={()=>pick(i)} onKeyDown={e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();pick(i);}}}>
     <title>{opened.bones[i].name}</title><circle cx={p[0]} cy={p[1]} r={radius*1.9} fill="transparent"/><circle className="motion-joint" cx={p[0]} cy={p[1]} r={radius}/></g>)}
   </svg><div className="motion-clock">{frame.state&&<span>{frame.state}</span>} <output>{frame.time.toFixed(2)} s</output></div></div>
   <div className="motion-preview-fields"><label>Selected joint<select value={selected} onChange={e=>pick(Number(e.target.value))}><option disabled value={-1}>Pick a joint</option>{opened.bones.map((b,i)=><option value={i} key={i}>{b.name||'Node '+i}</option>)}</select></label>
    {selected>=0&&<output>{['X','Y','Z'].map((k,i)=>k+' '+frame.points[selected][i].toFixed(3)).join(' · ')}</output>}
    {Object.entries(inputs).map(([key,val])=>typeof val==='number'?<label key={key}>{key}<input type="number" step="any" value={val} onChange={e=>{if(Number.isFinite(e.target.valueAsNumber))input(key,e.target.valueAsNumber);}}/></label>:
     <fieldset key={key}><legend>{key}</legend>{val.map((v,i)=><label key={i}>{['X','Y','Z'][i]}<input type="number" step="any" value={v} onChange={e=>{if(Number.isFinite(e.target.valueAsNumber)){const next:Point=[...val];next[i]=e.target.valueAsNumber;input(key,next);}}}/></label>)}</fieldset>)}
   </div></div>:!error&&<p role="status">Loading model and evaluating pose…</p>}
 </div>;
}
