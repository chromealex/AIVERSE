import {useEffect,useMemo,useState} from 'react';
import {ArrowUp,ChevronRight,Copy,FileText,Folder,RefreshCw,Search} from 'lucide-react';
import {invoke} from '../bridge';
import {copyText} from '../clipboard';
import type {StudioResult} from '../types';

export function absoluteChildPath(parent:string,child:string) {
  const separator=/^(?:[a-z]:\\|\\\\)/i.test(parent)?'\\':'/';
  return parent.replace(/[/\\]$/,'')+separator+child.split('/').join(separator);
}

export default function ProjectDirectory({project,onContent}:{project:string;onContent:()=>void}) {
  const [folder,setFolder]=useState('');
  const [search,setSearch]=useState('');
  const [page,setPage]=useState(0);
  const [refresh,setRefresh]=useState(0);
  const [listing,setListing]=useState<StudioResult['studio.projectDirectory']|null>(null);
  const [error,setError]=useState('');
  const [message,setMessage]=useState('');
  useEffect(()=>{
    let live=true;
    setError('');setListing(null);
    void invoke('studio.projectDirectory',{expectedProject:project,folder}).then(result=>{
      if(live)setListing(result);
    }).catch(reason=>{if(live)setError(String(reason));});
    return ()=>{live=false;};
  },[project,folder,refresh]);
  const entries=useMemo(()=>{
    const query=search.trim().toLocaleLowerCase();
    return (listing?.entries??[]).filter(row=>row.name.toLocaleLowerCase().includes(query))
      .sort((a,b)=>Number(b.directory)-Number(a.directory)||a.name.localeCompare(b.name));
  },[listing,search]);
  const perPage=100;
  const lastPage=Math.max(0,Math.ceil(entries.length/perPage)-1);
  const current=Math.min(lastPage,page);
  const parts=folder?folder.split('/'):[];
  function navigate(path:string){
    if(path==='content'){onContent();return;}
    setListing(null);setFolder(path);setSearch('');setPage(0);setMessage('');setError('');
    if(path===folder)setRefresh(n=>n+1);
  }
  async function copy(path:string){
    const ok=await copyText(path);
    setMessage(ok?'Copied '+path:'Clipboard unavailable');
  }
  return <section className="studio-assets project-directory" aria-label="Project files">
    <div className="studio-assets-toolbar">
      <nav className="studio-assets-breadcrumb" aria-label="Project location">
        <span className="studio-assets-crumb"><button type="button" aria-current={!folder?'location':undefined} onClick={()=>navigate('')}>Project</button></span>
        {parts.map((part,i)=><span key={i} className="studio-assets-crumb"><ChevronRight size={13}/>
          <button type="button" aria-current={i===parts.length-1?'location':undefined} onClick={()=>navigate(parts.slice(0,i+1).join('/'))}>{part}</button></span>)}
      </nav>
      <button className="studio-assets-tool-action" type="button" aria-label="Parent folder" title="Parent folder" disabled={!folder}
        onClick={()=>navigate(parts.slice(0,-1).join('/'))}><ArrowUp size={16}/></button>
      <button className="studio-assets-reload" type="button" aria-label="Reload project folder" title="Reload project folder" onClick={()=>setRefresh(n=>n+1)}><RefreshCw size={15}/></button>
      <button className="project-directory-content" type="button" onClick={onContent}>Content assets</button>
    </div>
    <label className="studio-assets-search"><Search size={14}/><input aria-label="Filter project files" placeholder="Find in folder…" value={search}
      onChange={e=>{setSearch(e.target.value);setPage(0);}}/></label>
    <div className="project-directory-list">
      {error?<p role="alert">{error}</p>:!listing?<p role="status">Loading folder…</p>:!entries.length?
        <p>{search?'No matching files.':'This folder is empty.'}</p>:
        <ul>{entries.slice(current*perPage,(current+1)*perPage).map(row=>{
          const path=folder?folder+'/'+row.name:row.name;
          const absolute=absoluteChildPath(listing.absolute,row.name);
          return <li key={row.name}>
            {row.directory?<button type="button" className="project-directory-entry" title={absolute} onClick={()=>navigate(path)}>
              <Folder size={16}/><span>{row.name}</span></button>:<span className="project-directory-entry" title={absolute}><FileText size={16}/><span>{row.name}</span></span>}
            <button type="button" className="studio-assets-tool-action" aria-label={'Copy absolute path for '+row.name} title="Copy absolute path"
              onClick={()=>void copy(absolute)}><Copy size={14}/></button>
          </li>;
        })}</ul>}
    </div>
    {listing&&<footer><span>{entries.length} {entries.length===1?'item':'items'}</span>
      {lastPage>0&&<><button disabled={!current} onClick={()=>setPage(current-1)}>Previous</button><span>{current+1} / {lastPage+1}</span>
        <button disabled={current===lastPage} onClick={()=>setPage(current+1)}>Next</button></>}
      <span role="status">{message}</span></footer>}
  </section>;
}
