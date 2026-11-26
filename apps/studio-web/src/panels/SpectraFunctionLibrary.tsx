import {useEffect,useId,useState} from 'react';
import {RefreshCw} from 'lucide-react';
import {StudioAlert,StudioButton,StudioField,StudioInput,StudioSelect} from '../ui';
import {graphV2,type SpectraGraphV2} from './spectraDataInterfaceModel';

interface Props{
  graph:SpectraGraphV2;disabled:boolean;
  op:(name:string,params?:Record<string,unknown>)=>Promise<Record<string,unknown>>;
  onEdit:(next:SpectraGraphV2)=>void;
}
export default function SpectraFunctionLibrary({graph,disabled,op,onEdit}:Props){
  const id=useId();
  const [names,setNames]=useState<string[]>([]);
  const [selected,select]=useState('');
  const [newName,setName]=useState('');
  const [revision,setRevision]=useState<{name:string;source:string}|null>(null);
  const [refresh,reload]=useState(0);
  const [busy,setBusy]=useState(false);
  const [error,setError]=useState('');
  const [status,setStatus]=useState('');
  const name=selected||newName.trim();
  useEffect(()=>{
    let live=true;
    op('fx_function_list').then(r=>{if(live&&Array.isArray(r.libraries))setNames(r.libraries.filter((v):v is string=>typeof v==='string'));})
      .catch(e=>{if(live)setError(String(e));});
    return ()=>{live=false;};
  },[op,refresh]);
  useEffect(()=>{
    let live=true;setRevision(null);
    if(selected)op('fx_function_get',{library:selected}).then(r=>{
      if(live&&typeof r.source==='string')setRevision({name:selected,source:r.source});
    }).catch(e=>{if(live)setError(String(e));});
    return ()=>{live=false;};
  },[op,selected,refresh]);
  async function transfer(write:boolean){
    setError('');setStatus('');setBusy(true);
    try{
      const result=await op(write?'fx_function_export':'fx_function_import',{
        library:name,graph,...(write?{expectedSource:selected?revision?.source:null}:{})
      });
      if(result.ok!==true)throw Error(Array.isArray(result.errors)?result.errors.join('\n'):'Function library validation failed.');
      if(write){
        setStatus('Exported fx/functions/'+name+'.vfxfn.json');select(name);setRevision(null);reload(n=>n+1);
      }else{
        const next=graphV2(result.graph);if(!next)throw Error('Library did not return a graph.');
        onEdit(next);setStatus('Imported '+name+' into the graph draft.');
      }
    }catch(e){setError(e instanceof Error?e.message:String(e));}
    finally{setBusy(false);}
  }
  const ready=selected?revision?.name===selected:/^[a-z0-9_]+$/.test(name)&&name!=='fx'&&!names.includes(name);
  return <div className="spectra-function-library">
    <StudioField label="Function library" htmlFor={id}>
      <StudioSelect id={id} value={selected} disabled={disabled||busy} onChange={e=>{select(e.target.value);setError('');setStatus('');}}>
        <option value="">New library…</option>
        {[...new Set([...names,...(selected?[selected]:[])])].map(n=><option key={n}>{n}</option>)}
      </StudioSelect>
    </StudioField>
    {!selected&&<StudioInput aria-label="New function library name" placeholder="Library name" value={newName}
      disabled={disabled||busy} onChange={e=>setName(e.target.value)}/>}
    <div className="au-row">
      {selected&&<StudioButton size="sm" disabled={!ready||disabled||busy} onClick={()=>void transfer(false)}>Import functions</StudioButton>}
      <StudioButton size="sm" disabled={!ready||disabled||busy||!graph.functions} onClick={()=>void transfer(true)}>
        {selected?'Replace library':'Export library'}
      </StudioButton>
      <StudioButton size="sm" variant="ghost" aria-label="Reload function libraries" title="Reload function libraries"
        disabled={disabled||busy} onClick={()=>{setRevision(null);setError('');setStatus('');reload(n=>n+1);}}><RefreshCw size={14}/></StudioButton>
    </div>
    {status&&<StudioAlert role="status">{status}</StudioAlert>}{error&&<StudioAlert tone="danger">{error}</StudioAlert>}
  </div>;
}
