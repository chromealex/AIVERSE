import {useState, type ReactNode} from 'react';
import {invoke} from '../bridge';
import type {StudioResult,StudioSnapshot} from '../types';
import {StudioAlert,StudioButton,StudioInput,StudioSection,StudioSelect} from '../ui';
import {readAgentDraft} from './agentDraft';

type Report=StudioResult['studio.projectPort'];
function Counts({values}:{values?:Record<string,number>}) {
  return values && <dl className="port-counts">{Object.entries(values).map(([name,value])=>
    <div key={name}><dt>{name}</dt><dd>{value}</dd></div>)}</dl>;
}

export default function ProjectImportPanel({commitSnapshot,showAgent,tools}:{
  commitSnapshot:(snapshot:StudioSnapshot)=>void;showAgent:()=>void;tools?:ReactNode;
}) {
  const [source,setSource]=useState(''),[out,setOut]=useState('');
  const [mode,setMode]=useState('auto'),[scale,setScale]=useState('1');
  const [scan,setScan]=useState<Report>(),[result,setResult]=useState<Report>();
  const [busy,setBusy]=useState(''),[error,setError]=useState('');
  function sourceChanged(value:string){setSource(value);setScan(undefined);setResult(undefined);setError('');}
  function destinationChanged(value:string){setOut(value);setResult(undefined);setError('');}
  async function browse(sourceFolder:boolean){
    setBusy('browse');setError('');
    try{const path=await invoke('studio.portFolder',{});if(path)(sourceFolder?sourceChanged:destinationChanged)(path);}
    catch(e){setError(String(e));}finally{setBusy('');}
  }
  async function run(action:'scan'|'import'){
    setBusy(action);setError('');setResult(undefined);if(action==='scan')setScan(undefined);
    try{
      const report=await invoke('studio.projectPort',{action,path:source.trim(),out:out.trim(),mode,scale:Number(scale)});
      if(report.ok===false||report.error)throw Error(report.error||report.errors?.join('\n')||'Source project could not be read.');
      if(action==='scan')setScan(report);else setResult(report);
    }catch(e){setError(e instanceof Error?e.message:String(e));}finally{setBusy('');}
  }
  async function open(agent:boolean){
    if(!result?.destination)return;
    setBusy('open');setError('');
    try{
      const opened=await invoke('studio.openImported',{dir:result.destination});
      if(!opened.snapshot||opened.cancelled)return;
      commitSnapshot(opened.snapshot);
      if(agent){
        const project=opened.snapshot.project.path;
        const key='studio.agentDraft:'+project;
        const brief='Review port-report.json in '+project+'. Inspect the missing and approximate items and propose the next migration steps.';
        const previous=readAgentDraft(key);
        if(!previous.text.includes(brief)){
          localStorage.setItem(key,JSON.stringify({...previous,text:[previous.text,brief].filter(Boolean).join('\n\n')}));
          window.dispatchEvent(new StorageEvent('storage',{key}));
        }
        showAgent();
      }
    }catch(e){setError(String(e));}finally{setBusy('');}
  }
  return <div className="project-port">
    <StudioSection title="Source project">
      <label>Unity, Godot or Unreal folder<StudioInput value={source} disabled={!!busy} onChange={e=>sourceChanged(e.target.value)}/></label>
      <div className="au-row"><StudioButton disabled={!!busy} onClick={()=>void browse(true)}>Browse source</StudioButton>
        <StudioButton disabled={!!busy||!source.trim()} onClick={()=>void run('scan')}>{busy==='scan'?'Scanning…':'Scan project'}</StudioButton></div>
      {scan && <><p>{[scan.engine,scan.version].filter(Boolean).join(' ')}</p><Counts values={scan.counts}/></>}
    </StudioSection>
    <StudioSection title="Import settings">
      <label>Destination folder<StudioInput value={out} disabled={!!busy} onChange={e=>destinationChanged(e.target.value)}/></label>
      <StudioButton size="sm" disabled={!!busy} onClick={()=>void browse(false)}>Browse destination</StudioButton>
      <div className="port-options"><label>Scene mode<StudioSelect value={mode} disabled={!!busy} onChange={e=>{setMode(e.target.value);setResult(undefined);}}>
        <option value="auto">Automatic</option><option value="2d">2D</option><option value="3d">3D</option></StudioSelect></label>
      <label>Scale (cells per metre)<StudioInput type="number" step="any" value={scale} disabled={!!busy} onChange={e=>{setScale(e.target.value);setResult(undefined);}}/></label></div>
      <StudioButton variant="primary" disabled={!!busy||!scan||!out.trim()||!Number.isFinite(Number(scale))||Number(scale)<=0} onClick={()=>void run('import')}>{busy==='import'?'Importing…':'Import project'}</StudioButton>
    </StudioSection>
    {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
    {result && <StudioSection title="Imported project"><p>{result.destination}</p><Counts values={result.counts}/><Counts values={result.summary}/>
      <div className="au-row"><StudioButton disabled={!!busy} onClick={()=>void open(false)}>Open project</StudioButton>
        <StudioButton disabled={!!busy} title="Open the imported project and prepare an editable Agent prompt" onClick={()=>void open(true)}>Open with Agent</StudioButton></div>
    </StudioSection>}
    {tools&&<details><summary>Report and resolution tools</summary>{tools}</details>}
  </div>;
}
