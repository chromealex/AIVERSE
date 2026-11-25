import { useEffect, useId, useState } from 'react';
import { invoke } from '../bridge';

type Profile = {model:string; bones:Record<string,string>};
type Body = {id:string; shape:string; position:number[]; radius:number; halfHeight?:number; axis?:number[]; angle?:number; density?:number; friction?:number};
type Joint = {a:string; b:string; kind:string; limits?:number[]};
type Rig = {bodies:Body[]; joints:Joint[]; tickRate?:number; substeps?:number};
type File = {document:Rig;revision:string};
const controls = [
  {key:'radiusRatio',label:'Radius / bone length',min:.05,max:.5,step:.01},
  {key:'density',label:'Density',min:50,max:1800,step:10},
  {key:'friction',label:'Friction',min:0,max:2,step:.05},
  {key:'jointLimit',label:'Hinge limit (rad)',min:.1,max:2.5,step:.05},
] as const;
async function op<T>(project:string,name:string,params:Record<string,unknown>) {
  return (await invoke('studio.op',{op:name,params,expectedProject:project})).result as T;
}

// Orthographic projection of Box3D's local-Y capsules using their saved axis-angle rotation.
function segment(body:Body,side:boolean) {
  if (!body || !Array.isArray(body.position) || body.position.length!==3 || !body.position.every(Number.isFinite) ||
      !Number.isFinite(body.radius) || body.radius<=0 || !['capsule','sphere'].includes(body.shape)) return [];
  const axis=body.axis??[0,1,0];
  if(!Array.isArray(axis)||axis.length!==3||!axis.every(Number.isFinite))return [];
  const [ax,ay,az]=axis, angle=body.angle??0, h=body.shape==='capsule'?(body.halfHeight??0):0;
  const s=Math.sin(angle), c=Math.cos(angle), k=1-c;
  const horizontal=side ? ax*s+az*ay*k : -az*s+ax*ay*k;
  const x=body.position[side?2:0], y=-body.position[1], dx=h*horizontal, dy=-h*(c+ay*ay*k);
  const result={body,x1:x-dx,y1:y-dy,x2:x+dx,y2:y+dy};
  return [result].filter(v=>[v.x1,v.x2,v.y1,v.y2].every(Number.isFinite));
}

function RigView({rig,roles}:{rig:Rig;roles:string[]}) {
  const [side,setSide]=useState(false);
  const [selected,select]=useState('');
  const name=(id:string)=>/^bone_\d+$/.test(id)?roles[Number(id.slice(5))]??id:id;
  const shapes=rig.bodies.flatMap(b=>segment(b,side));
  let left=Infinity,right=-Infinity,top=Infinity,bottom=-Infinity;
  for(const {body,x1,x2,y1,y2} of shapes){
    left=Math.min(left,x1-body.radius,x2-body.radius);right=Math.max(right,x1+body.radius,x2+body.radius);
    top=Math.min(top,y1-body.radius,y2-body.radius);bottom=Math.max(bottom,y1+body.radius,y2+body.radius);
  }
  const pad=.08*Math.max(right-left,bottom-top);
  const body=rig.bodies.find(b=>b.id===selected)??rig.bodies[0];
  return <section className="rig-view"><div className="avatar-title"><h3>Collision shapes</h3><small>{rig.bodies.length} bodies · {rig.joints.length} joints</small></div>
    <nav aria-label="Projection"><button aria-pressed={!side} onClick={()=>setSide(false)}>Front</button><button aria-pressed={side} onClick={()=>setSide(true)}>Side</button></nav>
    {shapes.length>0?<svg className="rig-frame" aria-label="Projected collision bodies" role="img" viewBox={[left-pad,top-pad,right-left+2*pad,bottom-top+2*pad].join(' ')}>
      {shapes.map(({body:b,x1,y1,x2,y2})=><g key={b.id} className={b.id===body?.id?'selected':''} onClick={()=>select(b.id)}>
        <title>{name(b.id)}</title>{x1===x2&&y1===y2?<circle cx={x1} cy={y1} r={b.radius}/>:<line x1={x1} y1={y1} x2={x2} y2={y2} strokeWidth={2*b.radius} strokeLinecap="round"/>}</g>)}
    </svg>:<p>No capsule or sphere geometry in this rig.</p>}
    {body&&<><label>Inspect body<select aria-label="Inspect body" value={body.id} onChange={e=>select(e.target.value)}>{rig.bodies.map(b=><option key={b.id} value={b.id}>{name(b.id)}</option>)}</select></label>
      <p>{body.shape} · radius {body.radius} · density {body.density} · friction {body.friction}</p>
      {rig.joints.filter(j=>j.a===body.id||j.b===body.id).map((j,i)=><p key={i}>{name(j.a)} → {name(j.b)} · {j.kind} {Array.isArray(j.limits)?j.limits.join(' … ')+' rad':''}</p>)}</>}
  </section>;
}

export default function RagdollPanel({project,avatar,avatarName,active}:{project:string;avatar?:Profile;avatarName:string;active:boolean}) {
  const [avatars,setAvatars]=useState<string[]>([]),[files,setFiles]=useState<string[]>([]),[roles,setRoles]=useState<string[]>([]);
  const [source,setSource]=useState(''),[name,setName]=useState('');
  const [options,setOptions]=useState({radiusRatio:.17,density:750,friction:.65,jointLimit:.85});
  const [rig,setRig]=useState<Rig>(),[original,setOriginal]=useState<(File&{name:string})>();
  const [dirty,setDirty]=useState(false),[busy,setBusy]=useState(false),[message,setMessage]=useState(''),[error,setError]=useState('');
  const id=useId();
  useEffect(()=>{
    if(!active)return;
    let live=true;
    void Promise.all(['avatar','ragdoll'].map(kind=>op<{names:string[];roles:string[]}>(project,'animation_asset_list',{kind})))
      .then(([a,r])=>{if(live){setRoles(a.roles);setAvatars(a.names);setFiles(r.names);if(r.names.length===1)setName(old=>old||r.names[0]);}})
      .catch(e=>{if(live)setError(String(e));});
    return ()=>{live=false;};
  },[active,project]);
  const chosen=source==='@draft'&&!avatar ? avatars[0]??'' : source || (avatar?'@draft':avatars[0]??'');
  const inputs=JSON.stringify([chosen,options,chosen==='@draft'?avatar:null]);
  const [generatedFrom,setGeneratedFrom]=useState('');
  const needsGeneration=dirty && generatedFrom!==inputs;
  async function run(action:()=>Promise<void>){
    setBusy(true);setError('');setMessage('');
    try{await action();}catch(e){setError(e instanceof Error?e.message:String(e));}finally{setBusy(false);}
  }
  async function generate(){
    const profile=chosen==='@draft'?avatar:(await op<{document:Profile}>(project,'animation_asset_get',{kind:'avatar',name:chosen})).document;
    if(!profile)throw Error('Choose an avatar first.');
    const result=await op<{document:Rig}>(project,'animation_ragdoll_generate',{profile,options});
    setRig(result.document);setGeneratedFrom(inputs);setDirty(true);setName(old=>old||(chosen==='@draft'?avatarName:chosen)||'ragdoll');setMessage('Generated from '+profile.model);
  }
  if(!active)return null;
  return <div className="rig-layout"><section><fieldset disabled={busy}><h3>Generate physics rig</h3>
    <label>Source avatar<select aria-label="Source avatar" value={chosen} onChange={e=>setSource(e.target.value)}>
      {!chosen&&<option value="">Open or create an avatar</option>}
      {avatar&&<option value="@draft">Current mapping · {avatar.model}</option>}
      {avatars.map(a=><option key={a}>{a}</option>)}</select></label>
    {controls.map(({key,label,min,max,step})=><label key={key}>{label}<span className="rig-range"><input aria-label={label} type="range" min={min} max={max} step={step} value={options[key]} onChange={e=>setOptions({...options,[key]:Number(e.target.value)})}/><output>{options[key]}</output></span></label>)}
    <button disabled={!chosen} onClick={()=>void run(generate)}>Generate rig</button>
    <h3>Ragdoll file</h3><label>Rig name<input list={id} placeholder="ragdoll" value={name} onChange={e=>setName(e.target.value)}/></label>
    <datalist id={id}>{files.map(n=><option key={n} value={n}/>)}</datalist>
    <div className="rig-actions"><button disabled={!name||dirty} onClick={()=>void run(async()=>{
      const file=await op<File>(project,'animation_asset_get',{kind:'ragdoll',name});
      if(!Array.isArray(file.document?.bodies)||!Array.isArray(file.document?.joints)||
         !file.document.bodies.every(b=>b && typeof b.id==='string')||
         !file.document.joints.every(j=>j && typeof j.a==='string'&&typeof j.b==='string'))
        throw Error('This file does not contain a Box3D body and joint list.');
      setRig(file.document);setOriginal({...file,name});setMessage('Opened animations/ragdoll/'+name+'.json');
    })}>Open rig</button>
    <button className="avatar-save" disabled={!name||!rig||needsGeneration||(!dirty&&name===original?.name)} onClick={()=>void run(async()=>{
      const file=await op<File>(project,'animation_asset_save',{kind:'ragdoll',name,document:rig,...(name===original?.name?{expectedRevision:original.revision}:{})});
      setOriginal({...file,name});setRig(file.document);setFiles(prev=>[...new Set([...prev,name])].sort());setDirty(false);setMessage('Saved animations/ragdoll/'+name+'.json');
    })}>Save rig</button>
    {dirty&&<button onClick={()=>{setRig(original?.document);if(original)setName(original.name);setDirty(false);setMessage('');setError('');}}>Discard generated rig</button>}</div>
  </fieldset>{needsGeneration&&<p>Generate the rig to apply the changed settings.</p>}{busy&&<p role="status">Working…</p>}{error&&<p role="alert">{error}</p>}{message&&<p role="status">{message}</p>}</section>
  {rig&&<RigView rig={rig} roles={roles}/>}</div>;
}
