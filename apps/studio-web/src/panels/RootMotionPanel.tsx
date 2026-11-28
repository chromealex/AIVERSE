import {useEffect,useId,useMemo,useState} from 'react';
import {invoke} from '../bridge';

type Track={id:number;rate:number;loop:boolean;frames:number[][]};
type Document={format:string;tracks:Track[]};
type File={document:Document;revision:string};
type ModelInfo={clips:{name:string;duration:number}[];nodes:string[]};
async function op<T>(project:string,name:string,params:Record<string,unknown>){
  return (await invoke('studio.op',{op:name,params,expectedProject:project})).result as T;
}
const axes=['X','Y','Z','Yaw'];
const colours=['#faad72','#87d5aa','#81baff','#dcb2ff'];
const fixedOne=65536;

function TrackView({doc}:{doc:Document}){
  const [index,choose]=useState(0),[frame,scrub]=useState(0),[axis,selectAxis]=useState(0);
  const track=doc.tracks[index]??doc.tracks[0];
  const last=track.frames.length-1, tick=Math.min(frame,last);
  const x=(i:number)=>8+304*i/Math.max(last,1);
  const curve=useMemo(()=>{
    const values=track.frames.map(row=>row[axis]/fixedOne);
    let peak=0;for(const n of values)peak=Math.max(peak,Math.abs(n));
    const y=(n:number)=>55-45*n/(peak||1);
    // Preserve spikes when a long track spans many samples per display column.
    const stride=Math.max(1,Math.ceil(values.length/304)),points:string[]=[];
    for(let i=0;i<values.length;i+=stride){
      let lo=i,hi=i;
      for(let j=i+1;j<Math.min(i+stride,values.length);++j){
        if(values[j]<values[lo])lo=j;
        if(values[j]>values[hi])hi=j;
      }
      for(const j of [Math.min(lo,hi),Math.max(lo,hi)])points.push(x(j)+','+y(values[j]));
    }
    return {points:points.join(' '),values,peak};
  },[track,axis,last]);
  const y=(n:number)=>55-45*n/(curve.peak||1);
  return <section className="rig-view"><div className="avatar-title"><h3>Baked motion</h3><small>{track.frames.length} frames · {track.rate} Hz</small></div>
    {doc.tracks.length>1&&<label>Track<select value={index} aria-label="Track" onChange={e=>{choose(Number(e.target.value));scrub(0);}}>{doc.tracks.map((t,i)=><option key={t.id} value={i}>{t.id}</option>)}</select></label>}
    <nav aria-label="Motion channel">{axes.map((a,i)=><button key={a} aria-pressed={axis===i} onClick={()=>selectAxis(i)}>{a}</button>)}</nav>
    <svg role="img" aria-label={axes[axis]+' delta by tick'} className="motion-plot" viewBox="0 0 320 110">
      <path d="M8 55H312" stroke="#4b5c70"/><polyline points={curve.points} fill="none" stroke={colours[axis]} strokeWidth="2"/>
      <line x1={x(tick)} x2={x(tick)} y1="6" y2="104" stroke="#c5cfda" strokeDasharray="2 3"/><circle cx={x(tick)} cy={y(curve.values[tick])} r="3.5" fill={colours[axis]}/>
    </svg>
    <div className="motion-scale"><small>0 s</small><small>Peak |Δ{axes[axis]}| {curve.peak.toFixed(5)}{axis===3?' turns':''}</small><small>{(track.frames.length/track.rate).toFixed(2)} s</small></div>
    <label>Frame {tick+1} / {last+1}<input aria-label="Inspect frame" type="range" min="0" max={last} value={tick} onChange={e=>scrub(Number(e.target.value))}/></label>
    <div className="motion-values">{axes.map((a,i)=><div key={a}><small>{a}{i===3?' (turns)':''}</small><output>{(track.frames[tick][i]/fixedOne).toFixed(5)}</output></div>)}</div>
    <p>Track {track.id} · {(track.frames.length/track.rate).toFixed(2)} s · {track.loop?'Looping':'Single pass'}</p>
  </section>;
}
function valid(doc:Document){
  return doc?.format==='aiverse-root-motion-v1'&&Array.isArray(doc.tracks)&&doc.tracks.length>0&&doc.tracks.every(t=>t&&Number.isInteger(t.id)&&t.rate>0&&Array.isArray(t.frames)&&t.frames.length>0&&t.frames.every(r=>Array.isArray(r)&&r.length===4&&r.every(Number.isSafeInteger)));
}
export default function RootMotionPanel({project,models,initialModel,active}:{project:string;models:string[];initialModel:string;active:boolean}){
  const [model,setModel]=useState<string>(),[inspected,setInspected]=useState(''),[info,setInfo]=useState<ModelInfo>();
  const [clip,setClip]=useState(''),[root,setRoot]=useState(''),[trackId,setTrackId]=useState('1'),[rate,setRate]=useState('60'),[loop,setLoop]=useState(false);
  const [name,setName]=useState(''),[files,setFiles]=useState<string[]>([]),[doc,setDoc]=useState<Document>();
  const [original,setOriginal]=useState<(File&{name:string})>(),[baked,setBaked]=useState('');
  const [busy,setBusy]=useState(false),[message,setMessage]=useState(''),[error,setError]=useState('');
  const list=useId(),path=model??(initialModel||(models.length===1?models[0]:''));
  const inputs=JSON.stringify([path,clip,root,trackId,rate,loop]),dirty=!!baked,stale=dirty&&inputs!==baked;
  useEffect(()=>{
    if(!active)return;
    let live=true;
    void op<{names:string[]}>(project,'animation_asset_list',{kind:'root_motion'}).then(r=>{if(live){setFiles(r.names);if(r.names.length===1)setName(old=>old||r.names[0]);}}).catch(e=>{if(live)setError(String(e));});
    return ()=>{live=false;};
  },[active,project]);
  async function run(action:()=>Promise<void>){setBusy(true);setError('');setMessage('');try{await action();}catch(e){setError(e instanceof Error?e.message:String(e));}finally{setBusy(false);}}
  const canBake=inspected===path&&!!clip&&!!root&&Number.isInteger(Number(trackId))&&Number(trackId)>0&&Number(trackId)<=4294967295&&Number.isInteger(Number(rate))&&Number(rate)>=10&&Number(rate)<=240;
  if(!active)return null;
  return <div className="rig-layout"><section><fieldset disabled={busy}><h3>Bake root motion</h3>
    <label>Model path<input list={list+'models'} value={path} onChange={e=>setModel(e.target.value)} placeholder="content/character.gltf"/></label>
    <datalist id={list+'models'}>{models.map(m=><option key={m} value={m}/>)}</datalist>
    <button disabled={!path} onClick={()=>void run(async()=>{
      const r=await op<ModelInfo>(project,'animation_model_info',{model:path});setInspected(path);setInfo(r);setClip(r.clips[0]?.name??'');setRoot(r.nodes[0]??'');
      if(!r.clips.length)setMessage('This model has no animation clips.');
    })}>Read model</button>
    {info&&path===inspected&&<><label>Animation clip<select aria-label="Animation clip" value={clip} onChange={e=>setClip(e.target.value)}>{info.clips.map((c,i)=><option key={i} value={c.name}>{c.name} · {c.duration.toFixed(2)} s</option>)}</select></label>
    <label>Root bone<select aria-label="Root bone" value={root} onChange={e=>setRoot(e.target.value)}>{info.nodes.map((n,i)=><option key={i}>{n}</option>)}</select></label></>}
    <div className="motion-fields"><label>Track ID<input type="number" min="1" max="4294967295" value={trackId} onChange={e=>setTrackId(e.target.value)}/></label>
    <label>Ticks per second<input type="number" min="10" max="240" value={rate} onChange={e=>setRate(e.target.value)}/></label></div>
    <label className="motion-check"><input type="checkbox" checked={loop} onChange={e=>setLoop(e.target.checked)}/>Loop track</label>
    <button disabled={!canBake} onClick={()=>void run(async()=>{
      const r=await op<{document:Document}>(project,'animation_root_motion_generate',{model:path,clip,root,id:Number(trackId),rate:Number(rate),loop});
      if(!valid(r.document))throw Error('Invalid baked track data.');
      setDoc(r.document);setBaked(inputs);setName(old=>old||clip.replace(/[^a-zA-Z0-9_-]/g,'_').slice(0,64));setMessage('Baked '+clip+' from '+root);
    })}>Bake track</button>
    <h3>Track file</h3><label>Asset name<input list={list+'files'} value={name} onChange={e=>setName(e.target.value)} placeholder="walk"/></label>
    <datalist id={list+'files'}>{files.map(f=><option key={f} value={f}/>)}</datalist>
    <div className="rig-actions"><button disabled={!name||dirty} onClick={()=>void run(async()=>{
      const r=await op<File>(project,'animation_asset_get',{kind:'root_motion',name});if(!valid(r.document))throw Error('This file contains no valid baked tracks.');
      setDoc(r.document);setOriginal({...r,name});setMessage('Opened animations/root_motion/'+name+'.json');
    })}>Open track</button>
    <button className="avatar-save" disabled={!doc||!name||stale||(!dirty&&name===original?.name)} onClick={()=>void run(async()=>{
      const r=await op<File>(project,'animation_asset_save',{kind:'root_motion',name,document:doc,...(name===original?.name?{expectedRevision:original.revision}:{})});
      setOriginal({...r,name});setDoc(r.document);setBaked('');setFiles(list=>[...new Set([...list,name])].sort());setMessage('Saved animations/root_motion/'+name+'.json');
    })}>Save track</button>{dirty&&<button onClick={()=>{setDoc(original?.document);if(original)setName(original.name);setBaked('');setError('');setMessage('');}}>Discard bake</button>}</div>
    </fieldset>{stale&&<p>Bake the track to apply these settings.</p>}{busy&&<p role="status">Working…</p>}{message&&<p role="status">{message}</p>}{error&&<p role="alert">{error}</p>}</section>
    {doc&&<TrackView doc={doc}/>}</div>;
}
