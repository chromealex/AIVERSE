import { useEffect, useId, useRef, useState } from 'react';
import { Plus, RotateCcw, Save, Trash2 } from 'lucide-react';
import { invoke, onNativeEvent } from '../bridge';
import type { StudioAttachmentDoc, StudioAttachmentItem, StudioSnapshot } from '../types';
import SpectraDockViewport from './SpectraDockViewport';
import { fromTrs, toTrs, IDENTITY, editAffine } from './attachMath';
import './ToolPanel.css';
import './ProjectToolPanels.css';

const message = (e: unknown) => e instanceof Error ? e.message : String(e);

function Num({ label, value, onChange, step = 0.1 }: { label: string; value: number; onChange: (v: number) => void; step?: number }) {
  const [text,setText]=useState(String(value));
  const focused=useRef(false);
  useEffect(()=>{if(!focused.current)setText(String(+value.toFixed(6)));},[value]);
  return <label className="att__num"><span>{label}</span>
    <input className="au-input" type="number" step={step} value={text} onFocus={()=>{focused.current=true;}} onBlur={()=>{focused.current=false;setText(String(+value.toFixed(6)));}}
      onChange={e => { setText(e.target.value);const v = e.target.valueAsNumber; if (Number.isFinite(v)) onChange(v); }}/></label>;
}

function Transform({title,value,onChange}:{title:string;value?:number[];onChange:(m:number[])=>void}){
 const matrix=value??IDENTITY,trs=toTrs(matrix);
 return <fieldset className="att-transform"><legend>{title}</legend>
  <div className="att-triple">{['X','Y','Z'].map((axis,i)=><Num key={axis} label={'Offset '+axis} value={matrix[12+i]} onChange={v=>onChange(editAffine(matrix,3,i,v))}/>)}</div>
  {!trs.custom&&<div className="att-triple"><Num label="Yaw °" value={trs.yaw} step={5} onChange={v=>onChange(fromTrs(trs.t,v,trs.scale))}/>
   <Num label="Scale" value={trs.scale} step={.05} onChange={v=>{if(v>0)onChange(fromTrs(trs.t,trs.yaw,v));}}/></div>}
  <details><summary>Orientation and scale matrix</summary>{['X','Y','Z'].map((axis,col)=><div key={axis} className="att-triple">{['X','Y','Z'].map((component,row)=>
   <Num key={component} label={axis+' axis · '+component} value={matrix[col*4+row]} onChange={v=>onChange(editAffine(matrix,col,row,v))}/>)}</div>)}</details>
 </fieldset>;
}

const empty:StudioAttachmentDoc={parent:'',attachments:[]};
export default function AttachmentsPanel({ snapshot }: { snapshot: StudioSnapshot | null }) {
  const project=snapshot?.project.path??'';
  const [path,setPath]=useState('attachments.json'),[loaded,setLoaded]=useState('');
  const [doc,setDoc]=useState<StudioAttachmentDoc>(empty),[original,setOriginal]=useState<StudioAttachmentDoc|null>(null);
  const [dirty,setDirty]=useState(false),[busy,setBusy]=useState(false);
  const [status,setStatus]=useState<{ok:boolean;text:string}|null>(null);
  const [preview,setPreview]=useState<{children?:number;duration?:number;sockets?:string[]}|null>(null);
  const [playing,setPlaying]=useState(true),[time,setTime]=useState(0),[yaw,setYaw]=useState(.6),[pitch,setPitch]=useState(.4);
  const socketList=useId(),generation=useRef(0);
  const fail=(e:unknown)=>setStatus({ok:false,text:message(e)});
  async function load(p:string){
    const id=++generation.current;setBusy(true);
    try {
      const r=await invoke('studio.attachmentsGet',{path:p,project});
      if(id!==generation.current)return;
      if(r.error)throw new Error(r.error);
      if(p !== loaded && preview) {
        await invoke('studio.attachmentsPreview',{project,close:true});
        if(id!==generation.current)return;
        setPreview(null);
        setTime(0);
      }
      setLoaded(p);setDoc(r.document);setOriginal(r.exists?r.document:null);setDirty(false);setStatus(r.exists?null:{ok:true,text:'New file · '+r.path});
    }catch(e){if(id===generation.current)fail(e);}finally{if(id===generation.current)setBusy(false);}
  }
  useEffect(()=>{
    setPath('attachments.json');setLoaded('');setDoc(empty);setOriginal(null);setDirty(false);setStatus(null);setPreview(null);
    if(project)void load('attachments.json');
    const stop=onNativeEvent((event,payload)=>{if(event==='attachments.previewError'){setPreview(null);fail(payload&&typeof payload==='object'&&'message' in payload?payload.message:payload);}});
    return ()=>{++generation.current;stop();if(project)void invoke('studio.attachmentsPreview',{project,close:true}).catch(()=>{});};
  },[project]);
  useEffect(()=>{
    if(!preview?.duration||!playing)return;
    let active=true,timer=0;
    async function poll(){
      try{const r=await invoke('studio.attachmentsPreview',{project,status:true});if(active&&r.time!==undefined)setTime(r.time);}
      catch(e){if(active){fail(e);return;}}
      if(active)timer=window.setTimeout(poll,250);
    }
    void poll();return ()=>{active=false;window.clearTimeout(timer);};
  },[preview,playing,project]);
  const edit=(next:StudioAttachmentDoc)=>{setDoc(next);setDirty(true);};
  const setItem=(i:number,patch:Partial<StudioAttachmentItem>)=>edit({...doc,attachments:doc.attachments.map((a,n)=>n===i?{...a,...patch}:a)});
  async function save(){
    const id=generation.current;setBusy(true);
    try{const r=await invoke('studio.attachmentsSet',{path,project,document:doc,expected:original});
     if(id===generation.current){if(r.error)throw new Error(r.error);setOriginal(r.document);setDoc(r.document);setDirty(false);setStatus({ok:true,text:'Saved '+r.path});}
    }catch(e){if(id===generation.current)fail(e);}finally{if(id===generation.current)setBusy(false);}
  }
  async function publish(){
    const id=generation.current;setBusy(true);
    try{const r=await invoke('studio.attachmentsPreview',{project,document:doc,playing,yaw,pitch});
     if(id===generation.current){setPreview(r);setTime(0);setStatus(null);}
    }catch(e){if(id===generation.current)fail(e);}finally{if(id===generation.current)setBusy(false);}
  }
  function control(params:{time?:number;playing?:boolean;yaw?:number;pitch?:number}){
    const id=generation.current;
    void invoke('studio.attachmentsPreview',{project,...params}).catch(e=>{if(id===generation.current)fail(e);});
  }
  if (!project) return <div className="tool__empty">Open a project to author attachments.</div>;
  return <div className="ptp attachment-page">
    <div className="ptp__edhead">
      <input className="au-input att__path" value={path} aria-label="Attachment layout file" disabled={busy||dirty} onChange={e => setPath(e.target.value)} onKeyDown={e => e.key === 'Enter' && !dirty && void load(path)} title="Layout file (Enter to open)"/>
      <button type="button" className="au-button au-button--sm" disabled={busy||dirty} onClick={()=>void load(path)}>Open</button>
      <button type="button" className="au-button au-button--sm au-button--primary" aria-label="Save attachment layout" disabled={busy||!dirty||loaded!==path} onClick={() => void save()}><Save size={13}/>Save</button>
      <button type="button" className="au-button au-button--sm" disabled={busy||!dirty} onClick={() => void load(loaded)}><RotateCcw size={13}/>Revert</button>
    </div>
    {status && <div className={'ptp__status ' + (status.ok ? 'is-ok' : 'is-error')} role={status.ok ? 'status' : 'alert'}>{status.text}</div>}
    <div className="ptp__scroll attachment-body">
      <section className="attachment-view"><header><h4>Model preview</h4>
      <button className="au-button au-button--sm" disabled={busy||!doc.parent} onClick={()=>void publish()}>{preview?'Reimport preview':'Preview layout'}</button></header>
      <SpectraDockViewport id="attachments" label="Attachment model preview"/>
      {preview&&<><div className="att-controls">
       <button className="au-button au-button--sm" onClick={()=>{setPlaying(!playing);control({playing:!playing});}}>{playing?'Pause preview':'Play preview'}</button>
       <span>{preview.children} active {preview.children===1?'attachment':'attachments'}</span></div>
       {!!preview.duration&&<label>Parent clip time<input type="range" min={0} max={preview.duration} step=".01" value={time} onChange={e=>{const t=e.target.valueAsNumber;setTime(t);setPlaying(false);control({time:t,playing:false});}}/></label>}
       <label>Orbit<input type="range" min={-3.14} max={3.14} step=".01" value={yaw} onChange={e=>{setYaw(e.target.valueAsNumber);control({yaw:e.target.valueAsNumber});}}/></label>
       <label>Elevation<input type="range" min={-.9} max={1.4} step=".01" value={pitch} onChange={e=>{setPitch(e.target.valueAsNumber);control({pitch:e.target.valueAsNumber});}}/></label></>}
      </section>
      <fieldset className="attachment-form" disabled={busy||path!==loaded}><datalist id={socketList}>{preview?.sockets?.map((n,i)=><option value={n} key={i}/>)}</datalist>
      <label className="ptp__row"><span>Parent model</span>
        <input className="au-input att__wide" value={doc.parent} placeholder="content/hero.glb" onChange={e => edit({ ...doc, parent: e.target.value })}/></label>
      <h4>Attachments · {doc.attachments.length}
        <button type="button" className="au-button au-button--ghost au-button--sm" onClick={() => edit({ ...doc, attachments: [...doc.attachments, { model: '', socket: '', enabled: true }] })}><Plus size={12}/>Attachment</button></h4>
      {doc.attachments.map((a, i) => {
        return <div key={i} className="ptp__ctx">
          <div className="ptp__ctxhead">
            <label className="ptp__check"><input type="checkbox" aria-label="Enable attachment" checked={a.enabled !== false} onChange={e => setItem(i, { enabled: e.target.checked })}/></label>
            <input className="au-input" aria-label="Child model" value={a.model} placeholder="content/sword.glb" onChange={e => setItem(i, { model: e.target.value })}/>
            <button type="button" className="au-button au-button--icon au-button--ghost au-button--sm" title="Remove" onClick={() => edit({ ...doc, attachments: doc.attachments.filter((_, n) => n !== i) })}><Trash2 size={12}/></button>
          </div>
          <div className="att__grid">
            <label className="att__num"><span>Socket</span><input className="au-input" list={socketList} value={a.socket} placeholder="hand_r" onChange={e => setItem(i, { socket: e.target.value })}/></label>
            <label className="att__num"><span>Stable id</span><input className="au-input" value={a.id ?? ''} onChange={e => setItem(i, { id: e.target.value || undefined })}/></label>
          </div>
          <Transform title="Socket transform" value={a.local} onChange={local=>setItem(i,{local})}/>
          <details><summary>Child model transform</summary><Transform title="Model space" value={a.childWorld} onChange={childWorld=>setItem(i,{childWorld})}/></details>
          <label className="ptp__check"><input type="checkbox" checked={!!a.animated} onChange={e => setItem(i, { animated: e.target.checked })}/>Animated (own MotionWeave graph)</label>
          {a.animated && <label className="ptp__row"><span>Graph asset</span><input className="au-input att__wide" value={a.animationGraph ?? ''} onChange={e => setItem(i, { animationGraph: e.target.value })}/></label>}
        </div>;
      })}
      </fieldset>
    </div>
  </div>;
}
