import GpuEmitterPreview from './GpuEmitterPreview';
import SpectraGpuSettings from './SpectraGpuSettings';
import './SpectraGpuFiles.css';import {useEffect, useState, type ReactNode} from 'react';
import {applyEdits, modify, parse, type ParseError} from 'jsonc-parser';
import {invoke} from '../bridge';
import {StudioAlert, StudioButton, StudioField, StudioInput, StudioSection, StudioSelect, StudioTextarea} from '../ui';

const routesFile = '@routes';
const emptyRoutes = '{\n  "version": 1,\n  "routes": []\n}\n';
const emptyEmitter = JSON.stringify({version:1, preference:'auto', particles:256,
  spawn:{rate:20, burst:12, life:1.5}, appearance:{color:[1,0.75,0.25],radius:0.18,additive:true}}, null, 2)+'\n';

export default function SpectraGpuFiles({project,tools}:{project:string;tools:ReactNode}) {
  const [name,select] = useState(routesFile);
  const [newName,setNewName] = useState('');
  const [files,setFiles] = useState<string[]>([]);
  const [source,setSource] = useState<string|null>();
  const [draft,edit] = useState('');
  const [busy,setBusy] = useState(true);
  const [revision,reload] = useState(0);
  const [error,setError] = useState('');
  const [status,setStatus] = useState('');
  function op<T>(operation:string, params:Record<string,unknown> = {}) {
    return invoke('studio.op',{op:operation,expectedProject:project,params}).then(reply=>reply.result as T);
  }
  useEffect(()=>{
    if (!project) return;
    let live = true;
    setBusy(true); setSource(undefined); setStatus(''); setError('');
    Promise.all([op<string[]>('fx_gpu_list'),op<{source:string|null}>('fx_gpu_read',{name})])
      .then(([names,data])=>{
        if (!live) return;
        setFiles(names); setSource(data.source);
        edit(data.source ?? (name===routesFile ? emptyRoutes : emptyEmitter));
      }).catch(e=>{if(live)setError(String(e));}).finally(()=>{if(live)setBusy(false);});
    return ()=>{live=false;};
  },[name,revision,project]);
  function change(text:string) {edit(text);setStatus('');setError('');}
  const dirty = source!==undefined && draft!==(source ?? (name===routesFile?emptyRoutes:emptyEmitter));
  async function submit(save:boolean) {
    setBusy(true); setError(''); setStatus('');
    try {
      await op(save?'fx_gpu_save':'fx_gpu_validate',{name,text:draft,...(save?{expectedSource:source}:{})});
      if (save) {
        setSource(draft);
        if(name!==routesFile&&!files.includes(name))setFiles([...files,name].sort());
      }
      setStatus(save?'Saved '+(name===routesFile?'routes.json':name+'.gpu.json'):'Source is valid');
    } catch(e) {setError(String(e));}
    finally {setBusy(false);}
  }
  function patch(path:(string|number)[],value:unknown) {
    change(applyEdits(draft,modify(draft,path,value,{formattingOptions:{tabSize:2,insertSpaces:true}})));
  }
  const issues:ParseError[]=[];
  const doc = parse(draft,issues);
  const routes:Record<string,unknown>[]|undefined = name===routesFile && !issues.length && Array.isArray(doc?.routes)
    && doc.routes.every((r:unknown)=>r!==null && typeof r==='object' && !Array.isArray(r)) ? doc.routes : undefined;
  if(!project)return <StudioAlert>Open a project to edit GPU assets.</StudioAlert>;
  return <div className="effects-files"><StudioSection title="GPU emitters and event routes">
    <fieldset disabled={busy} className="gpu-file-fields">
      <StudioField label="Project file"><StudioSelect aria-label="Project file" value={name} disabled={dirty} onChange={e=>select(e.target.value)}>
        <option value={routesFile}>Event routes · fx/gpu/routes.json</option>
        {(name===routesFile||files.includes(name)?files:[...files,name]).map(id=><option key={id} value={id}>{id}.gpu.json</option>)}
      </StudioSelect></StudioField>
      <div className="au-row"><StudioInput aria-label="New emitter name" placeholder="Emitter name" value={newName} onChange={e=>setNewName(e.target.value)}/>
        <StudioButton disabled={dirty||!/^[A-Za-z0-9_-]{1,64}$/.test(newName)||files.includes(newName)} onClick={()=>{select(newName);setNewName('');}}>New emitter</StudioButton></div>
      {name!==routesFile&&source!==undefined&&<GpuEmitterPreview key={name} project={project} source={draft}/>}
      {source!==undefined&&!issues.length&&doc&&typeof doc==='object'&&!Array.isArray(doc)&&
        <SpectraGpuSettings routes={name===routesFile} doc={doc} change={(field,value)=>patch([field],value)}/>}
      {routes&&<StudioSection title="Event routes" actions={<StudioButton disabled={routes.length>=64} onClick={()=>{
        const ids=new Set(routes.map(r=>r.eventType));let id=1;while(ids.has(id))id++;
        patch(['routes',routes.length],{eventType:id,asset:files[0]??'new_effect',ownPlayerOnly:true,enabled:true,oneShotSeconds:3});
      }}>Add route</StudioButton>}>
        {routes.length===0&&<p>No events are routed to GPU emitters.</p>}
        {routes.map((r,i)=><div key={i} className="gpu-route">
          <StudioField label="Event type"><StudioInput aria-label="Event type" type="number" min={0} max={65535} value={String(r.eventType??0)} onChange={e=>{if(e.target.value)patch(['routes',i,'eventType'],Number(e.target.value));}}/></StudioField>
          <StudioField label="Emitter asset"><StudioSelect aria-label="Emitter asset" value={String(r.asset??'')} onChange={e=>patch(['routes',i,'asset'],e.target.value)}>
            {[...new Set([String(r.asset??''),...files])].map(id=><option key={id}>{id}</option>)}</StudioSelect></StudioField>
          <StudioField label="One-shot seconds"><StudioInput aria-label="One-shot seconds" type="number" min={0.05} max={60} step="any" value={String(r.oneShotSeconds??3)} onChange={e=>{if(e.target.value)patch(['routes',i,'oneShotSeconds'],Number(e.target.value));}}/></StudioField>
          <label><input type="checkbox" checked={r.enabled!==false} onChange={e=>patch(['routes',i,'enabled'],e.target.checked)}/> Enabled</label>
          <StudioButton onClick={()=>patch(['routes',i],undefined)}>Remove route</StudioButton>
        </div>)}
      </StudioSection>}
      {source!==undefined&&<details open={!!issues.length}><summary>GPU asset source</summary><StudioField label="JSON"><StudioTextarea aria-label="GPU asset source" rows={12} spellCheck={false} value={draft} onChange={e=>change(e.target.value)}/></StudioField></details>}
      <div className="au-row"><StudioButton variant="primary" disabled={source===undefined||(!dirty&&source!==null)} onClick={()=>void submit(true)}>Save file</StudioButton>
        <StudioButton disabled={source===undefined} onClick={()=>void submit(false)}>Validate</StudioButton>
        <StudioButton onClick={()=>reload(revision+1)}>{dirty?'Discard and reload':'Reload'}</StudioButton></div>
    </fieldset>
    {error&&<StudioAlert tone="danger">{error}</StudioAlert>}{status&&<StudioAlert role="status">{status}</StudioAlert>}
  </StudioSection><details><summary>GPU functions and imports</summary>{tools}</details></div>;
}
