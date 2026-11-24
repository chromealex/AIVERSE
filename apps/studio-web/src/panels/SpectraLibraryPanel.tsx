import {useEffect,useMemo,useState} from 'react';
import {invoke,nativeConnected,onNativeEvent} from '../bridge';
import {StudioPanel,StudioSection,StudioInput,StudioSelect,StudioButton,StudioAlert} from '../ui';
import type {StudioPanel as PanelId} from '../nestedDock';
import {graphV2,safeGraphName} from './spectraDataInterfaceModel';
import {loadLibraryFrame,requestLibraryGraph} from './spectraLibraryStore';
import './SpectraLibraryPanel.css';

type Entry={name:string;draft:boolean;valid:boolean};
type PackageEntry={name:string;bytes:number};
type ReplayEntry={name:string;tick?:number;ok:boolean};
type Preference={favorite?:boolean;tags?:string[]};
const id=(entry:Entry)=>(entry.draft?'draft:':'published:')+entry.name;
const readPrefs=(project:string):Record<string,Preference>=>{
  try{return JSON.parse(localStorage.getItem('aiverse.spectrafx.library:'+project)||'{}')||{};}catch{return {};}
};
const op=async(name:string,params:Record<string,unknown>={})=>
  (await invoke('studio.op',{op:name,params})).result as Record<string,unknown>;
const errorText=(e:unknown)=>e instanceof Error?e.message:String(e);

export default function SpectraLibraryPanel({projectPath,showPanel}:{projectPath:string;showPanel:(panel:PanelId)=>void}){
  const [entries,setEntries]=useState<Entry[]>([]);
  const [packages,setPackages]=useState<PackageEntry[]>([]);
  const [presets,setPresets]=useState<ReplayEntry[]>([]);
  const [prefs,setPrefs]=useState(()=>readPrefs(projectPath));
  const [query,setQuery]=useState('');
  const [category,setCategory]=useState('all');
  const [selected,setSelected]=useState<Entry|null>(null);
  const [signature,setSignature]=useState('');
  const [tagText,setTagText]=useState('');
  const [revision,setRevision]=useState(0);
  const [thumbRevision,setThumbRevision]=useState(0);
  const [error,setError]=useState('');
  const [loading,setLoading]=useState(false);
  const native=nativeConnected();
  useEffect(()=>onNativeEvent(event=>{
    if(event==='workspace.changed'||event==='spectrafx.packageStaged')setRevision(v=>v+1);
  }),[]);
  useEffect(()=>{
    const changed=()=>setThumbRevision(v=>v+1);
    window.addEventListener('spectrafx:library-frame',changed);
    return()=>window.removeEventListener('spectrafx:library-frame',changed);
  },[]);
  useEffect(()=>{
    if(!projectPath||!native)return;
    let live=true;
    setLoading(true);setError('');
    void Promise.all([op('fx_graph_list'),op('fx_graph_draft_list'),
      op('fx_replay_preset_list',{expectedProjectPath:projectPath}),op('fx_replay_package_list',{expectedProjectPath:projectPath})])
      .then(([published,drafts,replays,archives])=>{
        if(!live)return;
        const result:Entry[]=[];
        for(const item of Array.isArray(published.graphs)?published.graphs:[])
          if(item&&typeof item.name==='string'&&safeGraphName(item.name))
            result.push({name:item.name,draft:false,valid:item.ok!==false});
        for(const item of Array.isArray(drafts.drafts)?drafts.drafts:Array.isArray(drafts.graphs)?drafts.graphs:[]){
          const name=typeof item==='string'?item:item?.name;
          if(typeof name==='string'&&safeGraphName(name))result.push({name,draft:true,valid:true});
        }
        setEntries(result.sort((a,b)=>a.name.localeCompare(b.name)||Number(a.draft)-Number(b.draft)));
        setPresets(Array.isArray(replays.presets)?replays.presets:[]);
        setPackages(Array.isArray(archives.packages)?archives.packages:[]);
      }).catch(e=>{if(live)setError(errorText(e));}).finally(()=>{if(live)setLoading(false);});
    return()=>{live=false;};
  },[projectPath,native,revision]);
  useEffect(()=>{
    setSignature('');
    if(!selected)return;
    let live=true;
    setTagText((prefs[id(selected)]?.tags??[]).join(', '));
    void op(selected.draft?'fx_graph_draft_get':'fx_graph_get',{name:selected.name})
      .then(r=>{if(live){const g=graphV2(r.graph);if(g)setSignature(JSON.stringify(g));}})
      .catch(e=>{if(live)setError(errorText(e));});
    return()=>{live=false;};
  },[selected,revision]);
  const update=(entry:Entry,patch:Preference)=>{
    const next={...prefs,[id(entry)]:{...prefs[id(entry)],...patch}};
    setPrefs(next);
    try{localStorage.setItem('aiverse.spectrafx.library:'+projectPath,JSON.stringify(next));}catch{/* session only */}
  };
  const filtered=useMemo(()=>entries.filter(e=>{
    const p=prefs[id(e)];
    if(category==='favorites'&&!p?.favorite||category==='drafts'&&!e.draft||category==='published'&&e.draft)return false;
    return [e.name,...(p?.tags??[])].join(' ').toLowerCase().includes(query.trim().toLowerCase());
  }),[entries,prefs,query,category]);
  const [thumb,setThumb]=useState('');
  useEffect(()=>{
    let live=true;
    setThumb('');
    if(selected&&signature)void loadLibraryFrame(projectPath,selected.name,selected.draft,signature)
      .then(image=>{if(live)setThumb(image);});
    return()=>{live=false;};
  },[selected,projectPath,signature,thumbRevision]);
  const open=(entry:Entry)=>{requestLibraryGraph({projectPath,name:entry.name,draft:entry.draft});showPanel('fx_graph');};
  return <StudioPanel title="SpectraFX Library" eyebrow="VFX · Project assets">
    <StudioSection title="Effects">
      {!native||!projectPath?<small>Open a project in native Studio.</small>:<>
        <div className="spectra-library__toolbar">
          <StudioInput placeholder="Search effects or tags" value={query} onChange={e=>setQuery(e.target.value)}/>
          <StudioSelect value={category} onChange={e=>setCategory(e.target.value)}>
            <option value="all">All effects</option><option value="published">Published</option>
            <option value="drafts">Drafts</option><option value="favorites">Favorites</option>
          </StudioSelect>
          <StudioButton size="sm" disabled={loading} onClick={()=>setRevision(v=>v+1)}>Refresh</StudioButton>
        </div>
        {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
        <small>{loading?'Loading…':filtered.length+' effects'}</small>
        <div className="spectra-library__grid">
          {filtered.map(entry=><div className="spectra-library__tile" key={id(entry)}>
            <StudioButton variant="ghost" onClick={()=>setSelected(entry)}>{entry.name}</StudioButton>
            <small>{entry.draft?'Draft':'Published'} · {entry.valid?'Valid':'Check graph'}</small>
            <small>{(prefs[id(entry)]?.tags??[]).join(' · ')}</small>
            <div className="spectra-library__toolbar">
              <StudioButton size="sm" onClick={()=>update(entry,{favorite:!prefs[id(entry)]?.favorite})}>
                {prefs[id(entry)]?.favorite?'★':'☆'}
              </StudioButton>
              <StudioButton size="sm" onClick={()=>open(entry)}>Open graph</StudioButton>
            </div>
          </div>)}
        </div>
        {selected&&<div className="spectra-library__detail">
          <strong>{selected.name}</strong>
          {thumb?<img src={thumb} alt="Native SpectraFX captured frame"/>:
            <small>No matching cached frame. Open the saved graph and generate a thumbnail.</small>}
          <StudioInput placeholder="Tags, comma separated" value={tagText} onChange={e=>setTagText(e.target.value)}/>
          <StudioButton size="sm" onClick={()=>update(selected,{tags:[...new Set(tagText.split(',').map(s=>s.trim()).filter(Boolean).map(s=>s.slice(0,40)))].slice(0,12)})}>Save tags</StudioButton>
          <StudioButton size="sm" onClick={()=>open(selected)}>Open live preview</StudioButton>
        </div>}
      </>}
    </StudioSection>
    <StudioSection title="Replay Presets">
      {presets.filter(p=>p.name.toLowerCase().includes(query.toLowerCase())).map(p=><div key={p.name}>{p.name} · {p.ok?'tick '+p.tick:'invalid'}</div>)}
      <StudioButton size="sm" onClick={()=>showPanel('fx_graph')}>Manage presets in Graph</StudioButton>
    </StudioSection>
    <StudioSection title="VFX Packages">
      {packages.filter(p=>p.name.toLowerCase().includes(query.toLowerCase())).map(p=><div key={p.name}>{p.name} · {Math.ceil(p.bytes/1024)} KiB</div>)}
      <StudioButton size="sm" disabled={!projectPath||!native} onClick={()=>void invoke('studio.spectraPackageChoose',{}).catch(e=>setError(errorText(e)))}>Import package</StudioButton>
      <StudioButton size="sm" onClick={()=>showPanel('catalog')}>AIVERSE-PACKAGES catalog</StudioButton>
    </StudioSection>
  </StudioPanel>;
}
