import {useEffect, useId, useState} from 'react';
import {invoke} from '../bridge';

type Notify = {name:string; time:number};
type Section = {name:string; clip:string; begin:number; end:number; next:number; notifies:Notify[]};
type Montage = {model:string; slot:string; maskRoot:string; blendIn:number; blendOut:number; sections:Section[]};
type File = {document:Montage; revision:string};
type Model = {clips:{name:string; duration:number}[]; nodes:string[]};

async function operation<T>(project:string, op:string, params:Record<string,unknown>) {
  return (await invoke('studio.op',{op,expectedProject:project,params})).result as T;
}
function blank(model:string):Montage {
  return {model,slot:'UpperBody',maskRoot:'',blendIn:.1,blendOut:.1,sections:[]};
}
// Older documents may omit fields for which the native compiler supplies defaults.
function editable(doc:Montage):Montage {
  if(typeof doc?.model!=='string' || !Array.isArray(doc.sections)) throw Error('Invalid montage file.');
  return {...blank(doc.model),...doc,sections:doc.sections.map(section=>{
    const notifies=section?.notifies??[];
    if(typeof section?.name!=='string'||typeof section.clip!=='string'||!Array.isArray(notifies)||
       notifies.some(n=>!n||typeof n.name!=='string')) throw Error('Invalid montage section.');
    return {...section,begin:section.begin??0,next:section.next??-1,notifies};
  })};
}

export default function MontagePanel({project,models,active}:{project:string;models:string[];active:boolean}) {
  const id=useId();
  const [draft,setDraft]=useState<Montage>(),[original,setOriginal]=useState<File & {name:string}>();
  const [files,setFiles]=useState<string[]>([]),[name,setName]=useState('');
  const [index,setIndex]=useState(0),[info,setInfo]=useState<Model>();
  const [busy,setBusy]=useState(false),[message,setMessage]=useState(''),[error,setError]=useState('');
  const model=draft?.model??(models.length===1?models[0]:'');
  const doc=draft??blank(model), sections=doc.sections, section=sections[index];
  const dirty=!!draft && draft!==original?.document;
  useEffect(()=>{
    if(!active)return;
    let live=true;
    operation<{names:string[]}>(project,'animation_asset_list',{kind:'montage'})
      .then(r=>{if(live){setFiles(r.names);if(r.names.length===1)setName(current=>current||r.names[0]);}}).catch(e=>{if(live)setError(String(e));});
    return ()=>{live=false;};
  },[active,project]);
  useEffect(()=>{
    setInfo(undefined);
    if(!active||!model)return;
    let live=true;
    const timer=setTimeout(()=>{void operation<Model>(project,'animation_model_info',{model})
      .then(r=>{if(live)setInfo(r);}).catch(e=>{if(live)setError(String(e));});},200);
    return ()=>{live=false;clearTimeout(timer);};
  },[active,project,model]);
  function edit(values:Partial<Montage>){
    setDraft({...doc,...values});setError('');setMessage('');
  }
  function patchSection(values:Partial<Section>){
    edit({sections:sections.map((s,i)=>i===index?{...s,...values}:s)});
  }
  function patchNotify(n:number,values:Partial<Notify>){
    patchSection({notifies:section.notifies.map((item,i)=>i===n?{...item,...values}:item)});
  }
  function addSection(){
    const clip=info?.clips[0];
    let number=sections.length+1;
    while(sections.some(s=>s.name==='Section '+number))++number;
    edit({sections:[...sections,{name:'Section '+number,clip:clip?.name??'',begin:0,end:clip?.duration??1,next:-1,notifies:[]}]});
    setIndex(sections.length);
  }
  function removeSection(){
    edit({sections:sections.filter((_,i)=>i!==index).map(s=>({...s,next:s.next===index?-1:s.next>index?s.next-1:s.next}))});
    setIndex(Math.max(0,index-1));
  }
  async function run(action:()=>Promise<void>){
    setBusy(true);setError('');setMessage('');
    try{await action();}catch(e){setError(e instanceof Error?e.message:String(e));}finally{setBusy(false);}
  }
  function accept(file:File){
    const document=editable(file.document);
    setOriginal({...file,document,name});setDraft(document);
  }
  const fileParams={kind:'montage',name};
  if(!active)return null;
  return <fieldset className="montage-editor" disabled={busy}><div className="rig-layout">
    <section className="rig-view"><h3>Montage file</h3>
      <label>Asset name<input list={id+'files'} value={name} onChange={e=>setName(e.target.value)} placeholder="movement"/></label>
      <datalist id={id+'files'}>{files.map(f=><option key={f}>{f}</option>)}</datalist>
      <div className="rig-actions">
        <button disabled={!name||dirty} onClick={()=>void run(async()=>{
          accept(await operation<File>(project,'animation_asset_get',fileParams));setIndex(0);setMessage('Opened animations/montage/'+name+'.json');
        })}>Open montage</button>
        <button className="avatar-save" disabled={!name||!sections.length||(!dirty&&name===original?.name)} onClick={()=>void run(async()=>{
          accept(await operation<File>(project,'animation_asset_save',{...fileParams,document:doc,...(name===original?.name?{expectedRevision:original.revision}:{})}));
          setFiles([...new Set([...files,name])].sort());setMessage('Saved animations/montage/'+name+'.json');
        })}>Save montage</button>
        {original&&!dirty&&<button onClick={()=>{setOriginal(undefined);setDraft(undefined);setIndex(0);setName('');setMessage('');setError('');}}>New montage</button>}
        {dirty&&<button onClick={()=>{setDraft(original?.document);setIndex(0);setError('');setMessage('');}}>Discard edits</button>}
      </div>
      <h3>Playback</h3>
      <label>Model path<input list={id+'models'} value={model} onChange={e=>edit({model:e.target.value})}/></label>
      <datalist id={id+'models'}>{models.map(m=><option key={m}>{m}</option>)}</datalist>
      <label>Slot<input value={doc.slot} onChange={e=>edit({slot:e.target.value})}/></label>
      <label>Bone mask<input value={doc.maskRoot} list={id+'nodes'} placeholder="Full body" onChange={e=>edit({maskRoot:e.target.value})}/></label>
      <datalist id={id+'nodes'}>{info?.nodes.map((n,i)=><option key={i}>{n}</option>)}</datalist>
      <div className="motion-fields">{(['blendIn','blendOut'] as const).map(k=><label key={k}>{k==='blendIn'?'Blend in (s)':'Blend out (s)'}<input type="number" min="0" step=".01" value={doc[k]} onChange={e=>edit({[k]:Number(e.target.value)})}/></label>)}</div>
      <button disabled={!sections.length} onClick={()=>void run(async()=>{
        await operation(project,'animation_montage_validate',{document:doc});setMessage('Validated '+sections.length+' sections.');
      })}>Validate montage</button>
      {busy&&<p role="status">Working…</p>}{message&&<p role="status">{message}</p>}{error&&<p role="alert">{error}</p>}
    </section>
    <section className="rig-view"><div className="avatar-title"><h3>Sections</h3><button disabled={sections.length>=128} onClick={addSection}>Add section</button></div>
      <nav aria-label="Montage sections">{sections.map((s,i)=><button key={i} aria-pressed={index===i} onClick={()=>setIndex(i)}>{s.name||'Unnamed section'}</button>)}</nav>
      {!section&&<p>Add a section to set its clip, transitions and events.</p>}
      {section&&<>
        <label>Section name<input value={section.name} onChange={e=>patchSection({name:e.target.value})}/></label>
        <label>Source clip<input list={id+'clips'} value={section.clip} onChange={e=>patchSection({clip:e.target.value})}/></label>
        <datalist id={id+'clips'}>{info?.clips.map((c,i)=><option key={i} value={c.name}>{c.duration.toFixed(2)} s</option>)}</datalist>
        <div className="motion-fields">{(['begin','end'] as const).map(k=><label key={k}>{k==='begin'?'Start (s)':'End (s)'}<input type="number" min="0" step=".01" value={section[k]} onChange={e=>patchSection({[k]:Number(e.target.value)})}/></label>)}</div>
        <label>Next section<select aria-label="Next section" value={section.next} onChange={e=>patchSection({next:Number(e.target.value)})}><option value={-1}>Stop</option>{sections.map((s,i)=><option key={i} value={i}>{s.name}</option>)}</select></label>
        <div className="avatar-title"><h3>Notify events</h3><button onClick={()=>patchSection({notifies:[...section.notifies,{name:'event',time:section.begin}]})}>Add notify</button></div>
        <div className="montage-timeline" role="img" aria-label={'Event timeline from '+section.begin+' to '+section.end+' seconds'}>{section.notifies.map((n,i)=>{
          const position=(n.time-section.begin)/(section.end-section.begin);
          return position>=0&&position<=1&&<i key={i} style={{left:(position*100)+'%'}} title={n.name+' · '+n.time+' s'}/>;
        })}</div>
        <div className="motion-scale"><small>{section.begin} s</small><small>{section.end} s</small></div>
        {section.notifies.map((n,i)=><div className="montage-notify" key={i}><label>Event name<input value={n.name} onChange={e=>patchNotify(i,{name:e.target.value})}/></label><label>Clip time (s)<input type="number" step=".01" value={n.time} onChange={e=>patchNotify(i,{time:Number(e.target.value)})}/></label><button aria-label={'Remove event '+(i+1)} onClick={()=>patchSection({notifies:section.notifies.filter((_,j)=>j!==i)})}>Remove</button></div>)}
        <button onClick={removeSection}>Remove section</button>
      </>}
    </section>
  </div></fieldset>;
}
