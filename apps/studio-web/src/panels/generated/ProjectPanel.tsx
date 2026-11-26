import { useEffect, useRef, useState } from 'react';
import { Check, FilePlus2, FolderOpen, Map as MapIcon, Plus, RefreshCw, Save, Search } from 'lucide-react';
import { invoke } from '../../bridge';
import type { ProjectRuleGroup, StudioProjectState, StudioSnapshot } from '../../types';
import type { StudioPanelContext } from '../StudioPanelRenderer';
import { StudioPanel, StudioSection, StudioAlert, StudioEmptyState, StudioButton, StudioField, StudioInput, StudioSelect } from '../../ui';
import typeBlocks from '../../../../editor/core/type-blocks.json';
import './ProjectPanel.css';

const blockGroups = [...new Set(Object.values(typeBlocks).map(block=>block.group))];
const singular = {types:'type',resources:'resource',techs:'technology',factions:'faction'};
const sections = {maps:'Maps',types:'Types',resources:'Resources',techs:'Technologies',factions:'Factions'} as const;
type Section = keyof typeof sections;
type Draft = { id:string; original:string|null; text:string; initial:string };

export default function ProjectPanel({projectRecord,snapshot,busy,connected,openProject,updateSnapshot,setBusy,showPanel}:StudioPanelContext) {
  const path=snapshot?.project.path ?? '';
  const [project,setProject]=useState<StudioProjectState|null>(null);
  const [section,setSection]=useState<Section>('maps');
  const [query,setQuery]=useState('');
  const [draft,setDraft]=useState<Draft|null>(null);
  const [newMap,setNewMap]=useState(false);
  const [name,setName]=useState('New map');
  const [width,setWidth]=useState('64'),[height,setHeight]=useState('48');
  const [error,setError]=useState('');
  const [revision,setRevision]=useState(0);
  const request=useRef(0);
  const blockDetails=useRef<HTMLDetailsElement>(null);
  useEffect(()=>{
    let live=true;
    if(connected && path) void invoke('studio.project',{}).then(state=>{if(live)setProject(state);})
      .catch(e=>{if(live)setError(String(e));});
    return ()=>{live=false;};
  },[path,connected,snapshot,revision]);
  const run=async(operation:()=>Promise<StudioSnapshot>)=>{
    if(busy)return;
    setBusy(true);setError('');
    try {updateSnapshot(await operation());setNewMap(false);}
    catch(e){setError(e instanceof Error?e.message:String(e));}
    finally {setBusy(false);}
  };
  const leave=()=>!draft || (draft.text===draft.initial && (draft.original!==null || !draft.id)) || window.confirm('Discard your unsaved rule changes?');
  const changeSection=(next:Section)=>{
    if(next===section || !leave())return;
    ++request.current;setSection(next);setQuery('');setDraft(null);setNewMap(false);setError('');
  };
  const edit=async(group:ProjectRuleGroup,id:string)=>{
    if(!leave())return;
    const version=++request.current;
    setError('');
    try {
      const {text:original}=await invoke('studio.projectRule',{project:path,group,id});
      if(version!==request.current)return;
      const text=JSON.stringify(JSON.parse(original),null,2);
      setSection(group);setQuery('');setNewMap(false);
      setDraft({id,original,text,initial:text});
    }catch(e){if(version===request.current)setError(String(e));}
  };
  useEffect(()=>{
    if(projectRecord && path)void edit(projectRecord.group,projectRecord.key);
  },[projectRecord,path]);
  const createRule=()=>{
    if(!leave())return;
    ++request.current;
    const text=section==='types'?'{"health":{"max":100}}':'{}';
    setError('');setDraft({id:'',original:null,text,initial:text});
  };
  const saveRule=async()=>{
    if(section==='maps' || !draft || busy)return;
    setBusy(true);setError('');
    try {
      const id=draft.id.trim(),value=JSON.parse(draft.text);
      if(!id || !value || Array.isArray(value) || typeof value!=='object')throw new Error('Enter an ID and a JSON object.');
      if(draft.original===null && !Object.hasOwn(value,'name'))value.name=id;
      const {text:original}=await invoke('studio.projectRule',{project:path,group:section,id,original:draft.original,value});
      const text=JSON.stringify(JSON.parse(original),null,2);
      setDraft({id,original,text,initial:text});setRevision(n=>n+1);
      updateSnapshot(await invoke('studio.snapshot',{}));
    }catch(e){setError(e instanceof Error?e.message:String(e));}
    finally{setBusy(false);}
  };
  let properties:Record<string,unknown>|null=null;
  if(draft){try{const value=JSON.parse(draft.text);if(value && typeof value==='object' && !Array.isArray(value))properties=value;}catch{/* The JSON editor retains incomplete input. */}}
  const term=query.trim().toLowerCase();
  const matches=(...parts:string[])=>parts.some(part=>part.toLowerCase().includes(term));
  const maps=project?.maps.filter(m=>matches(m.title,m.file)) ?? [];
  const items=section==='maps'?[]:project?.groups?.[section]?.filter(item=>matches(item.id,item.name)) ?? [];
  return <StudioPanel className="studio-panel-project" title={snapshot?.project.name || 'Project'} subtitle={path}
    actions={<StudioButton disabled={!connected || busy} onClick={()=>void openProject()}><FolderOpen size={14}/>Open project</StudioButton>}>
    {!path ? <StudioEmptyState title="Open a project" description="Choose a project to browse its maps and rules."/> :
    <div className="project-page">
      {error && <StudioAlert tone="danger">{error}</StudioAlert>}
      {project?.rulesError && <StudioAlert tone="danger" title="Rules">{project.rulesError}</StudioAlert>}
      <div className="project-current"><MapIcon size={19}/><div><small>Current map</small><strong>{project?.currentMap || 'Unsaved map'}</strong></div>
        <StudioButton disabled={busy || !snapshot?.project.dirty} onClick={()=>void run(()=>invoke('studio.save',{}))}><Save size={13}/>Save</StudioButton>
        <StudioButton disabled={busy} onClick={()=>void run(()=>invoke('studio.saveAs',{}))}>Save as…</StudioButton></div>
      <nav className="project-sections" aria-label="Project catalog">{(Object.keys(sections) as Section[]).map(key=>
        <button type="button" key={key} aria-label={sections[key]} disabled={busy} aria-pressed={section===key} onClick={()=>changeSection(key)}>{sections[key]}
          <small>{key==='maps'?project?.maps.length:project?.groups?.[key]?.length}</small></button>)}</nav>
      <div className="project-tools"><label><Search size={14}/><input aria-label="Search project catalog" placeholder={'Search '+sections[section].toLowerCase()} value={query} onChange={e=>setQuery(e.target.value)}/></label>
        <StudioButton title="Refresh catalog" aria-label="Refresh catalog" disabled={busy} onClick={()=>setRevision(n=>n+1)}><RefreshCw size={14}/></StudioButton>
        {section==='maps'?project?.canCreateMap && <StudioButton disabled={busy} onClick={()=>setNewMap(!newMap)}><FilePlus2 size={14}/>New map</StudioButton>
          :<StudioButton disabled={busy} onClick={createRule}><Plus size={14}/>New item</StudioButton>}</div>
      {section==='maps' && newMap && <StudioSection title="New map"><form className="project-form" onSubmit={e=>{e.preventDefault();void run(()=>invoke('studio.newMap',{name,width:Number(width),height:Number(height)}));}}>
        <StudioField label="Name" htmlFor="new-map-name"><StudioInput id="new-map-name" value={name} required maxLength={256} onChange={e=>setName(e.target.value)}/></StudioField>
        <div className="project-dimensions">
          <StudioField label="Width" htmlFor="map-width"><StudioInput id="map-width" type="number" min={8} max={project?.maxMapSize} required step={1} value={width} onChange={e=>setWidth(e.target.value)}/></StudioField>
          <StudioField label="Height" htmlFor="map-height"><StudioInput id="map-height" type="number" min={8} max={project?.maxMapSize} required step={1} value={height} onChange={e=>setHeight(e.target.value)}/></StudioField>
        </div>
        <StudioButton type="submit" disabled={busy}>Create map</StudioButton></form></StudioSection>}
      {draft && section!=='maps' && <StudioSection title={draft.original===null?'New '+singular[section]:draft.id}>
        <form className="project-form" onSubmit={e=>{e.preventDefault();void saveRule();}}>
          {draft.original===null && <StudioField label="Item ID" htmlFor="project-rule-id"><StudioInput id="project-rule-id" autoFocus required value={draft.id} onChange={e=>setDraft({...draft,id:e.target.value})}/></StudioField>}
          <StudioField label="Display name" htmlFor="project-rule-name"><StudioInput id="project-rule-name" disabled={!properties || busy} placeholder={draft.id}
            value={typeof properties?.name==='string'?properties.name:''} onChange={e=>setDraft({...draft,text:JSON.stringify({...properties,name:e.target.value},null,2)})}/></StudioField>
          {section==='types' && <StudioField label="Add property block" htmlFor="project-type-block">
            <StudioSelect id="project-type-block" value="" disabled={busy || !properties} onChange={e=>{
              const key=e.target.value as keyof typeof typeBlocks;
              if(!properties || !Object.hasOwn(typeBlocks,key) || Object.hasOwn(properties,key))return;
              setDraft({...draft,text:JSON.stringify({...properties,[key]:typeBlocks[key].value},null,2)});
              if(blockDetails.current)blockDetails.current.open=true;
            }}>
              <option value="">Choose a block…</option>
              {blockGroups.map(group=>{
                const available=Object.entries(typeBlocks).filter(([key,block])=>block.group===group && (!properties || !Object.hasOwn(properties,key)));
                return available.length>0 && <optgroup key={group} label={group}>
                  {available.map(([key,block])=><option key={key} value={key}>{block.label}</option>)}
                </optgroup>;
              })}
            </StudioSelect>
          </StudioField>}
          <details ref={blockDetails}><summary>All properties</summary><textarea aria-label="Rule properties" rows={6} spellCheck={false} value={draft.text} onChange={e=>setDraft({...draft,text:e.target.value})}/></details>
          <div><StudioButton type="submit" disabled={busy || (draft.original===null?!draft.id.trim():draft.text===draft.initial)}>{draft.original===null?'Create item':'Save item'}</StudioButton>
          <StudioButton disabled={busy} onClick={()=>{if(leave())setDraft(null);}}>Close</StudioButton></div>
        </form></StudioSection>}
      <div className="project-items">{section==='maps'?maps.map(map=><button type="button" disabled={busy} key={map.file} aria-label={'Open '+map.file} aria-current={map.file===project?.currentMap?'page':undefined}
        onClick={()=>void run(()=>invoke('studio.openMap',{file:map.file}))}><strong>{map.title}</strong>{map.file===project?.currentMap && <Check size={13}/>}<small>{map.file}</small></button>)
        :items.map(item=><button type="button" key={item.id} aria-label={'Edit '+item.id} disabled={busy} aria-pressed={draft?.id===item.id} onClick={()=>void edit(section,item.id)}><strong>{item.name}</strong><small>{item.id}</small></button>)}</div>
      {project && !(section==='maps'?maps.length:items.length) && <p className="project-empty">{term?'No matches.':'No '+sections[section].toLowerCase()+' yet.'}</p>}
      <div className="project-links"><StudioButton onClick={()=>showPanel('rules')}>Game rules</StudioButton><StudioButton onClick={()=>showPanel('branding')}>Branding</StudioButton>
        {project && !project.canCreateMap && <StudioButton onClick={()=>showPanel('actor')}>Actors &amp; cameras</StudioButton>}</div>
    </div>}
  </StudioPanel>;
}
