import { useCallback, useId, useEffect, useMemo, useRef, useState } from 'react';
import { CheckCircle2, FilePlus2, GitBranch, GripVertical, Lock, LockOpen, RefreshCw, Save, Trash2 } from 'lucide-react';
import { invoke, onNativeEvent } from '../bridge';
import { StudioAlert, StudioBadge, StudioButton, StudioCheckbox, StudioEmptyState, StudioField, StudioInput, StudioPanel, StudioSection, StudioSelect } from '../ui';
import {
  graphV2, providerNames, scalarParameters, validateInterfaces, putInterface,
  dropInterface, putParameter, dropParameter, putNode, dropNode, safeGraphName,
  type SpectraGraphV2, type FxDataInterface, type FxInstanceScalar, type GraphEdit
} from './spectraDataInterfaceModel';
import {DraftHistory} from '../graphs/graphFramework';
import SpectraFunctionLibrary from './SpectraFunctionLibrary';
import {GraphObjectEditor} from './SpectraNodeProperties';
import SpectraGraphCanvas from './SpectraGraphCanvas';
import SpectraCurveEditor from './SpectraCurveEditor';
import SpectraNativePreview from './SpectraNativePreview';
import { takeLibraryGraph } from './spectraLibraryStore';
import SpectraIndexedGraphAssetInspector from './SpectraIndexedGraphAssetInspector';
import { createBlankGraph, createBurstStarter, nextUnusedGraphName } from './spectraStarterModel';
import './SpectraDataInterfacesPanel.css';

type AssetEntry = { name: string; draft: boolean; valid: boolean };
type NativeResponse = Record<string, unknown>;
const asRecord = (v: unknown): NativeResponse | null =>
  v !== null && typeof v === 'object' && !Array.isArray(v) ? v as NativeResponse : null;
const errorText = (e:unknown):string => e instanceof Error ? e.message : String(e);
const diagnostics = (value:unknown):string => {
  const r=asRecord(value);
  if(!r)return 'Validation unavailable.';
  const errors=Array.isArray(r.errors)?r.errors.filter((v):v is string=>typeof v==='string'):[];
  return errors.length?errors.join(' · '):'Graph compiled successfully.';
};
const blankBinding = ():FxDataInterface => ({provider:'weather',channel:'vertical_acceleration',scale:1,offset:0,fallback:0,required:false});
interface Props {
  projectPath:string;
  onSaved?:()=>void;
}

/** Real project authoring through Workspace fx_graph_* operations.
 * Never mutates files directly, and never pretends to render a GPU preview.
 */
export default function SpectraDataInterfacesPanel({projectPath,onSaved}:Props) {
  const nodeIdPrefix=useId();
  const nativeOp = useCallback(async(op:string,params:Record<string,unknown>={}):Promise<NativeResponse> => {
    const response = await invoke('studio.op',{op,params,expectedProject:projectPath});
    const result=asRecord(response.result);
    if(!result)throw new Error(op+' returned no usable result.');
    return result;
  },[projectPath]);
  const [assets,setAssets]=useState<AssetEntry[]>([]);
  const [selected,setSelected]=useState<AssetEntry|null>(null);
  const history=useRef(new DraftHistory<SpectraGraphV2|null>(null));
  const originals=useRef<{published:string|null;draft:string|null}>({published:null,draft:null});
  const [legacy,setLegacy]=useState<string|null>(null);
  const [saved,setSaved]=useState<SpectraGraphV2|null>(null);
  const [graph,setGraph]=useState<SpectraGraphV2|null>(null);
  const [selectedName,setSelectedName]=useState('');
  const [pinned,setPinned]=useState(false);
  const [search,setSearch]=useState('');
  const [providerSearch,setProviderSearch]=useState('');
  const [selectedResourceNode,setSelectedResourceNode]=useState('');
  const [binding,setBinding]=useState<FxDataInterface>(blankBinding);
  const [paramName,setParamName]=useState('');
  const [param,setParam]=useState<FxInstanceScalar>({slot:0,default:0,min:-100,max:100});
  const [newName,setNewName]=useState('preview_fx');
  const [busy,setBusy]=useState(false);
  const [error,setError]=useState('');
  const [status,setStatus]=useState('');
  const [pending,setPending]=useState<AssetEntry|null>(null);
  const [validation,setValidation]=useState<NativeResponse|null>(null);
  const dirty=graph ? !saved || JSON.stringify(saved)!==JSON.stringify(graph) : !!legacy && !!saved;
  const names=useMemo(()=>graph?scalarParameters(graph):[],[graph]);
  const providers=useMemo(()=>graph?providerNames(graph,providerSearch):[],[graph,providerSearch]);

  const listAssets=useCallback(async()=>{
    if(!projectPath){setAssets([]);return;}
    const [published,drafts]=await Promise.all([nativeOp('fx_graph_list'),nativeOp('fx_graph_draft_list')]);
    const items:AssetEntry[]=[];
    if(Array.isArray(published.graphs)) for(const item of published.graphs) {
      const r=asRecord(item);
      if(r && typeof r.name==='string' && safeGraphName(r.name))
        items.push({name:r.name,draft:false,valid:r.ok!==false});
    }
    const rawDrafts=Array.isArray(drafts.drafts)?drafts.drafts: Array.isArray(drafts.graphs)?drafts.graphs:[];
    for(const item of rawDrafts){
      const name=typeof item==='string'?item:asRecord(item)?.name;
      if(typeof name==='string' && safeGraphName(name))
        items.push({name,draft:true,valid:true});
    }
    setAssets(items.sort((a,b)=>a.name.localeCompare(b.name)||Number(a.draft)-Number(b.draft)));
  },[projectPath,nativeOp]);

  useEffect(()=>{
    let alive=true;
    history.current=new DraftHistory<SpectraGraphV2|null>(null);
    setSelected(null);setLegacy(null);setSaved(null);setGraph(null);setError('');setValidation(null);
    if(!projectPath){setAssets([]);return;}
    void listAssets().catch(e=>{if(alive)setError(errorText(e));});
    return()=>{alive=false;};
  },[projectPath,listAssets]);
  useEffect(()=>onNativeEvent(event=>{
    if(event==='workspace.changed') void listAssets().catch(()=>{});
  }),[listAssets]);

  const chooseParameter=(name:string,source=graph)=>{
    setSelectedName(name);
    if(!source)return;
    const value=source.dataInterfaces?.[name];
    setBinding(value?{...value}:{...blankBinding(),fallback:source.instanceParameters?.[name]?.default??0});
    const p=source.instanceParameters?.[name];
    if(p){setParam({...p});setParamName(name);}
  };
  const openAsset=async(item:AssetEntry,force=false)=>{
    if(busy)return;
    if(dirty&&!force){setPending(item);return;}
    setBusy(true);setError('');setStatus('');setPending(null);setValidation(null);
    try{
      const read=(draft:boolean)=>nativeOp(draft?'fx_graph_draft_get':'fx_graph_get',{name:item.name});
      const sibling=assets.some(asset=>asset.name===item.name && asset.draft!==item.draft);
      const [r,other]=await Promise.all([read(item.draft),sibling?read(!item.draft):null]);
      const revision=typeof r.originalJson==='string'?r.originalJson:null;
      if(!revision)throw new Error(String(r.parseError??'Could not read graph revision.'));
      const parsed=graphV2(r.graph);
      const isLegacy=asRecord(r.graph)?.version===1;
      if(!parsed&&!isLegacy)throw new Error('Unsupported SpectraFX graph format.');
      const counterpart=typeof other?.originalJson==='string'?other.originalJson:null;
      originals.current=item.draft?{draft:revision,published:counterpart}:{published:revision,draft:counterpart};
      history.current=new DraftHistory<SpectraGraphV2|null>(parsed);
      setLegacy(isLegacy?revision:null);setSaved(parsed);setGraph(parsed);setSelected(item);
      if(!parsed)return;
      const first=Object.keys(parsed.instanceParameters??{})[0]??'';
      if(!pinned || !parsed.instanceParameters?.[selectedName])chooseParameter(first,parsed);
      else chooseParameter(selectedName,parsed);
      setValidation(asRecord(r.validation));
    }catch(e){setError(errorText(e));}
    finally{setBusy(false);}
  };
  useEffect(()=>{
    if(busy||!projectPath)return;
    const receive=()=>{
      const request=takeLibraryGraph(projectPath);
      if(request)void openAsset({name:request.name,draft:request.draft,valid:true});
    };
    receive();
    window.addEventListener('spectrafx:library-open',receive);
    return()=>window.removeEventListener('spectrafx:library-open',receive);
  },[projectPath,busy,dirty,selectedName,pinned]);
  const upgrade=async()=>{
    if(!legacy||busy)return;
    setBusy(true);setError('');
    try{
      const result=await nativeOp('fx_graph_edit',{action:'upgrade',text:legacy,...(selected&&!selected.draft?{name:selected.name}:{})});
      const next=graphV2(result.graph);
      if(!next||result.ok===false)throw new Error(String(result.error??'Could not upgrade this graph.'));
      setGraph(history.current.push(next));setSelectedName('');setValidation(asRecord(result.validation));
      setStatus('Upgraded locally. Save Draft or Publish to write this version.');
    }catch(e){setError(errorText(e));}
    finally{setBusy(false);}
  };
  const makeGraph=async(starter:boolean)=>{
    const name=newName.trim();
    if(!safeGraphName(name)){setError('Graph name: 1–48 lowercase letters, digits, underscore.');return;}
    if(assets.some(item=>item.name===name) || selected?.name===name){
      setError('A graph or draft with this name already exists. Choose a different name.');return;
    }
    if(dirty){setError('Save or discard the current draft before creating another graph.');return;}
    setBusy(true);setError('');setStatus('');
    try{
      const r=await nativeOp('fx_graph_edit',{action:'new'});
      const base=graphV2(r.graph);
      if(!base||r.ok===false)throw new Error('Native editor did not return a Graph v2.');
      const next=starter?createBurstStarter(base):createBlankGraph(base);
      if(!next)throw new Error('Native new Graph v2 did not contain a playable starter chain. Nothing was changed.');
      let verified:NativeResponse|null=asRecord(r.validation);
      if(starter){
        verified=await nativeOp('fx_graph_validate',{graph:next});
        if(verified.ok!==true)throw new Error('Native starter validation failed: '+diagnostics(verified));
      }
      history.current=new DraftHistory<SpectraGraphV2|null>(next);
      originals.current={draft:null,published:null};setLegacy(null);
      setSelected({name,draft:true,valid:true});setSaved(null);setGraph(next);
      setSelectedName('');setValidation(verified);
      setNewName(nextUnusedGraphName([...assets,{name}], 'preview_fx'));
      setSearch('');
      setStatus(starter
        ? 'Starter graph compiled by native SpectraFX. Load the graph in the preview and press Play. Save Draft or Publish when ready.'
        : 'New blank graph created locally. Add Spawn, Initialize and SpriteOutput nodes to make a playable effect.');
    }catch(e){setError(errorText(e));}
    finally{setBusy(false);}
  };
  const editGraph=(next:SpectraGraphV2)=>{
    if(history.current.current()!==graph)return; // Ignore native edits completed after changing the draft.
    if(JSON.stringify(next)===JSON.stringify(graph))return;
    setGraph(history.current.push(next));setValidation(null);setError('');setStatus('');
  };
  const restore=(next:SpectraGraphV2|null)=>{
    setGraph(next);setValidation(null);setStatus('');setError('');
    const name=next?.instanceParameters?.[selectedName]?selectedName:Object.keys(next?.instanceParameters??{})[0]??'';
    chooseParameter(name,next);
    if(!name){setParamName('');setParam({slot:0,default:0,min:-100,max:100});}
    if(!next?.nodes.some(node=>node.id===selectedResourceNode))setSelectedResourceNode('');
  };
  const undoRedo=(redo:boolean)=>{
    if(busy)return;
    const edits=history.current;
    if(redo?edits.canRedo():edits.canUndo())restore(redo?edits.redo():edits.undo());
  };
  const historyButtons=<>
    <StudioButton size="sm" disabled={busy||!history.current.canUndo()} onClick={()=>undoRedo(false)}>Undo</StudioButton>
    <StudioButton size="sm" disabled={busy||!history.current.canRedo()} onClick={()=>undoRedo(true)}>Redo</StudioButton>
  </>;
  const apply=(result:GraphEdit)=>{
    if(result.error){setError(result.error);return;}
    editGraph(result.graph);
  };
  const applySource=async(source:Record<string,unknown>)=>{
    const next=graphV2(source);
    if(!next)throw Error('Expected a SpectraFX v2 graph with nodes, links and outputs.');
    const local=validateInterfaces(next);
    if(local)throw Error(local);
    setBusy(true);
    try{
      const result=await nativeOp('fx_graph_validate',{graph:next});
      if(result.ok!==true)throw Error(diagnostics(result));
      editGraph(next);
    }finally{setBusy(false);}
  };
  const validate=async()=>{
    if(!graph)return;
    const local=validateInterfaces(graph);
    if(local){setError(local);return;}
    setBusy(true);setError('');setStatus('');
    try{
      const r=await nativeOp('fx_graph_validate',{graph});
      setValidation(r);
      if(r.ok!==true)throw new Error(diagnostics(r));
      setStatus('Validated by the native SpectraFX compiler. No file written.');
    }catch(e){setError(errorText(e));}
    finally{setBusy(false);}
  };
  const save=async(publish:boolean)=>{
    if(!graph||!selected||busy)return;
    const local=validateInterfaces(graph);
    if(local){setError(local);return;}
    setBusy(true);setError('');setStatus('');
    try{
      if(publish){
        const verified=await nativeOp('fx_graph_validate',{graph});
        setValidation(verified);
        if(verified.ok!==true)throw new Error(diagnostics(verified));
      }
      const target=publish?'published':'draft';
      const result=await nativeOp(publish?'fx_graph_set':'fx_graph_draft_set',{name:selected.name,graph,expected:originals.current[target]});
      if(result.ok!==true)throw new Error(diagnostics(result));
      originals.current[target]=typeof result.originalJson==='string'?result.originalJson:JSON.stringify(graph);
      setSaved(graph);setSelected({...selected,draft:!publish,valid:true});
      setStatus(publish?'Published '+selected.name+'.':'Saved draft '+selected.name+'.');
      await listAssets();
      onSaved?.();
    }catch(e){setError(errorText(e));}
    finally{setBusy(false);}
  };
  const addParam=()=>{if(graph)apply(putParameter(graph,paramName.trim(),param));};
  const addBinding=()=>{if(graph)apply(putInterface(graph,selectedName,binding));};
  const available=assets.filter(a=>a.name.toLowerCase().includes(search.trim().toLowerCase()));

  if(!projectPath)return <StudioPanel title="SpectraFX Graph" eyebrow="VFX · Graph v2">
    <StudioEmptyState title="Open a project" description="Graph assets and runtime Data Interfaces belong to a project."/>
  </StudioPanel>;
  return <StudioPanel title="SpectraFX Graph" eyebrow="VFX · Data Interfaces"
    subtitle="Typed, presentation-only parameters · native compiler validation"
    className="spectra-di" onKeyDown={e=>{
      if(!selected||!(e.metaKey||e.ctrlKey)||e.altKey||e.target instanceof Element&&e.target.closest('input,textarea,[contenteditable="true"]'))return;
      const key=e.key.toLowerCase();
      if(key==='z'||key==='y'){e.preventDefault();e.stopPropagation();undoRedo(key==='y'||e.shiftKey);}
    }}
    actions={<div className="spectra-di__top-actions">
      <StudioButton size="sm" variant="primary" disabled={busy||dirty}
        onClick={()=>void makeGraph(true)}><FilePlus2 size={14}/> Starter FX</StudioButton>
      <StudioButton size="sm" variant="ghost" disabled={busy}
        onClick={()=>void listAssets().catch(e=>setError(errorText(e)))}><RefreshCw size={14}/> Refresh</StudioButton>
    </div>}>
    {error&&<StudioAlert tone="danger" title="SpectraFX error">{error}</StudioAlert>}
    {status&&<StudioAlert tone="success">{status}</StudioAlert>}
    {pending&&<StudioAlert tone="warning" title="Unsaved changes">
      <p>Discard the changes in {selected?.name} and open {pending.name}?</p>
      <div className="spectra-di__actions">
        <StudioButton size="sm" variant="danger" onClick={()=>void openAsset(pending,true)}>Discard and open</StudioButton>
        <StudioButton size="sm" onClick={()=>setPending(null)}>Cancel</StudioButton>
      </div>
    </StudioAlert>}
    <div className="spectra-di__layout">
      <aside className="spectra-di__library">
        <StudioSection title="New Graph">
          <StudioField label="Asset name" htmlFor="spectra-new-name">
            <StudioInput id="spectra-new-name" value={newName} placeholder="preview_fx"
              onChange={e=>setNewName(e.target.value)}/>
          </StudioField>
          <div className="spectra-di__actions">
            <StudioButton size="sm" variant="primary" disabled={busy||dirty||!safeGraphName(newName.trim())}
              onClick={()=>void makeGraph(true)}><FilePlus2 size={14}/> Starter FX</StudioButton>
            <StudioButton size="sm" disabled={busy||dirty||!safeGraphName(newName.trim())}
              onClick={()=>void makeGraph(false)}>Blank V2</StudioButton>
          </div>
          <p className="spectra-di__hint">Starter FX creates a working Spawn → Initialize → SpriteOutput chain.
          Use the preview in this window to play the effect.</p>
        </StudioSection>
        <StudioSection title="Graph Assets">
          <StudioField label="Search graphs" htmlFor="spectra-search">
            <StudioInput id="spectra-search" value={search} placeholder="Search fx/graphs…" onChange={e=>setSearch(e.target.value)}/>
          </StudioField>
          <div className="spectra-di__assets" role="list" aria-label="SpectraFX graph assets">
            {available.map(asset=><button key={asset.name+String(asset.draft)}
              className={'spectra-di__asset '+(selected!==null&&selected.name===asset.name&&selected.draft===asset.draft?'is-selected':'')}
              disabled={busy} type="button" draggable
              onDragStart={e=>e.dataTransfer.setData('application/x-aiverse-spectra-graph',JSON.stringify(asset))}
              onClick={()=>void openAsset(asset)}>
              <GitBranch size={14}/><span>{asset.name}</span>
              <StudioBadge tone={asset.draft?'warning':asset.valid?'success':'danger'}>{asset.draft?'Draft':asset.valid?'Live':'Error'}</StudioBadge>
            </button>)}
            {available.length===0&&<p className="spectra-di__hint spectra-di__empty-list">
              {assets.length===0?'No VFX graphs in this project. Use Starter FX above to create the first one.':'No graphs match the search. Clear the filter or create a new asset.'}
            </p>}
          </div>
        </StudioSection>
      </aside>
      <main className="spectra-di__editor"
        onDragOver={e=>{if(e.dataTransfer.types.includes('application/x-aiverse-spectra-graph'))e.preventDefault();}}
        onDrop={e=>{const raw=e.dataTransfer.getData('application/x-aiverse-spectra-graph');if(!raw)return;e.preventDefault();
          try{const value=JSON.parse(raw) as AssetEntry;if(safeGraphName(value.name))void openAsset(value);}catch{setError('Invalid dropped graph.');}}}>
        {legacy&&!graph&&selected?<StudioSection title={selected.name}>
          <p>This file uses SpectraFX Graph v1. Upgrade a local copy to edit its nodes and links.</p>
          <StudioButton variant="primary" disabled={busy} onClick={()=>void upgrade()}>Upgrade to v2</StudioButton>
          {historyButtons}
        </StudioSection>:!graph||!selected ? <div className="spectra-di__welcome">
          <GitBranch size={26}/>
          <strong>{assets.length===0?'Create your first VFX effect':'Choose a Graph v2'}</strong>
          <p>{assets.length===0
            ?'Start with a compilable particle burst: Spawn → Initialize → SpriteOutput. Preview it in the graph window.'
            :'Select an existing asset on the left or start a new particle effect. Changes stay local until you Save Draft or Publish.'}</p>
          <StudioButton size="sm" variant="primary" disabled={busy||dirty||!safeGraphName(newName.trim())}
            onClick={()=>void makeGraph(true)}><FilePlus2 size={14}/> Create Starter FX</StudioButton>
        </div> : <>
          <div className="spectra-di__toolbar">
            <strong>{selected.name}</strong><StudioBadge tone={dirty?'warning':'neutral'}>{dirty?'Unsaved':selected.draft?'Saved draft':'Saved'}</StudioBadge>
            <StudioButton size="sm" disabled={busy} onClick={()=>void validate()}><CheckCircle2 size={14}/> Validate</StudioButton>
            {historyButtons}
            <StudioButton size="sm" disabled={busy||!dirty&&!!saved} onClick={()=>{history.current=new DraftHistory<SpectraGraphV2|null>(saved);restore(saved);}}>Discard</StudioButton>
            <StudioButton size="sm" disabled={busy} onClick={()=>void save(false)}><Save size={14}/> Save Draft</StudioButton>
            <StudioButton size="sm" variant="primary" disabled={busy} onClick={()=>void save(true)}><Save size={14}/> Publish</StudioButton>
          </div>
          {validation&&<StudioAlert tone={validation.ok===true?'success':'warning'} title="Compiler diagnostics">
            {diagnostics(validation)} {validation.emitters!==undefined&&' · '+String(validation.emitters)+' emitter(s)'}
          </StudioAlert>}
          <StudioSection title="Graph Nodes & Links">
            <SpectraGraphCanvas nodeIdPrefix={nodeIdPrefix} graph={graph} disabled={busy}
              onError={setError}
              onEdit={editGraph}/>
          </StudioSection>
          <StudioSection title="Graph definitions">
            {(['parameters','functions'] as const).map(field=><GraphObjectEditor key={field+JSON.stringify(graph[field])}
              label={field==='parameters'?'Shared parameters':'Local functions'} value={graph[field]} disabled={busy}
              apply={value=>editGraph({...graph,[field]:value})}/>)}
            <GraphObjectEditor key={selected.name+JSON.stringify(graph)} label="Full graph JSON"
              value={graph} disabled={busy} apply={applySource}/>
            <SpectraFunctionLibrary key={selected.name} graph={graph} disabled={busy} op={nativeOp} onEdit={editGraph}/>
          </StudioSection>
          <SpectraCurveEditor graph={graph} disabled={busy} onError={setError} onEdit={editGraph}/>
          <SpectraNativePreview key={projectPath} graph={graph} projectPath={projectPath}
            libraryAsset={selected&&!dirty?{name:selected.name,draft:selected.draft}:undefined}
            disabled={busy}/>
          <StudioSection title="Instance parameters">
            <div className="spectra-di__items">
              {names.map(([name,p])=><button key={name} type="button" disabled={busy}
                className={'spectra-di__item '+(selectedName===name?'is-selected':'')}
                onClick={()=>chooseParameter(name)}>
                <span>{name}</span><small>param{p.slot} · {p.min}…{p.max}</small>
                {graph.dataInterfaces?.[name]&&<StudioBadge tone="accent">Bound</StudioBadge>}
              </button>)}
              {names.length===0&&<p className="spectra-di__hint">Create a scalar parameter to bind a Data Interface.</p>}
            </div>
            <div className="spectra-di__form">
              <StudioField label="Parameter name" htmlFor="spectra-param-name">
                <StudioInput id="spectra-param-name" value={paramName} onChange={e=>setParamName(e.target.value)} placeholder="wind_lift"/>
              </StudioField>
              {(['slot','default','min','max'] as const).map(key=><StudioField key={key} label={key} htmlFor={'spectra-param-'+key}>
                <StudioInput id={'spectra-param-'+key} type="number" step={key==='slot'?1:'any'} value={param[key]}
                  onChange={e=>setParam(old=>({...old,[key]:e.target.value===''?NaN:Number(e.target.value)}))}/>
              </StudioField>)}
              <div className="spectra-di__actions">
                <StudioButton size="sm" disabled={busy} onClick={addParam}>Add / Update parameter</StudioButton>
                <StudioButton size="sm" variant="danger" disabled={!selectedName||busy}
                  onClick={()=>graph&&apply(dropParameter(graph,selectedName))}><Trash2 size={13}/> Remove</StudioButton>
              </div>
            </div>
          </StudioSection>
          <StudioSection title="Data Interface binding"
            actions={<StudioButton size="sm" variant="ghost" title={pinned?'Unpin inspector':'Pin inspector'}
              onClick={()=>setPinned(x=>!x)}>{pinned?<Lock size={14}/>:<LockOpen size={14}/>} {pinned?'Pinned':'Follow'}</StudioButton>}>
            {!selectedName?<p className="spectra-di__hint">Select an existing scalar parameter above.</p>:<>
              <p className="spectra-di__hint">Bound to <strong>{selectedName}</strong>. Provider IDs are supplied by the native host, not asset file paths.</p>
              <div className="spectra-di__form spectra-di__form--binding"
                onDragOver={e=>{if(e.dataTransfer.types.includes('text/x-aiverse-spectra-provider'))e.preventDefault();}}
                onDrop={e=>{const value=e.dataTransfer.getData('text/x-aiverse-spectra-provider');if(value){e.preventDefault();setBinding(b=>({...b,provider:value}));}}}>
                <StudioField label="Provider" htmlFor="spectra-provider">
                  <StudioInput id="spectra-provider" value={binding.provider}
                    onChange={e=>setBinding(b=>({...b,provider:e.target.value}))}/>
                </StudioField>
                <StudioField label="Channel" htmlFor="spectra-channel">
                  <StudioInput id="spectra-channel" value={binding.channel}
                    onChange={e=>setBinding(b=>({...b,channel:e.target.value}))}/>
                </StudioField>
                {(['scale','offset','fallback'] as const).map(key=><StudioField label={key} htmlFor={'spectra-binding-'+key} key={key}>
                  <StudioInput type="number" step="any" id={'spectra-binding-'+key} value={binding[key]??(key==='scale'?1:0)}
                    onChange={e=>setBinding(b=>({...b,[key]:e.target.value===''?NaN:Number(e.target.value)}))}/>
                </StudioField>)}
                <StudioCheckbox label="Required (reject burst if unavailable)" checked={binding.required??false}
                  onChange={e=>setBinding(b=>({...b,required:e.target.checked}))}/>
                <div className="spectra-di__actions">
                  <StudioButton size="sm" variant="primary" disabled={busy} onClick={addBinding}>Apply binding</StudioButton>
                  <StudioButton size="sm" disabled={busy||!graph.dataInterfaces?.[selectedName]}
                    onClick={()=>apply(putNode(graph,selectedName))}>+ DataInterface node</StudioButton>
                  <StudioButton size="sm" variant="danger" disabled={busy||!graph.dataInterfaces?.[selectedName]}
                    onClick={()=>apply(dropInterface(graph,selectedName))}>Remove binding</StudioButton>
                </div>
              </div>
            </>}
          </StudioSection>
          <StudioSection title="Providers in this graph">
            <StudioField label="Filter providers" htmlFor="spectra-provider-search">
              <StudioInput id="spectra-provider-search" value={providerSearch} onChange={e=>setProviderSearch(e.target.value)} placeholder="Search provider IDs"/>
            </StudioField>
            <div className="spectra-di__providers">
              {providers.map(name=><button key={name} type="button" draggable
                onDragStart={e=>e.dataTransfer.setData('text/x-aiverse-spectra-provider',name)}
                onClick={()=>setBinding(b=>({...b,provider:name}))}>
                <GripVertical size={13}/> {name}
              </button>)}
            </div>
            <p className="spectra-di__hint">Drag a provider onto the binding form. Actual runtime availability is resolved by the host.</p>
          </StudioSection>
          <StudioSection title="Graph resource assets">
            <p className="spectra-di__hint">Select an output or CollisionModule. MeshOutput references models; CollisionModule bakes a closed model into a static SDF. Assets resolve through the native project index.</p>
            <StudioField label="Resource node" htmlFor="spectra-resource-node">
              <StudioSelect id="spectra-resource-node" value={selectedResourceNode}
                onChange={e=>setSelectedResourceNode(e.target.value)}>
                <option value="">Choose resource node…</option>
                {graph.nodes.filter(n=>['SpriteOutput','TrailModule','MaterialModule','MeshOutput','CollisionModule'].includes(n.type))
                  .map(node=><option key={node.id} value={node.id}>{node.id} · {node.type}</option>)}
              </StudioSelect>
            </StudioField>
            {graph.nodes.filter(n=>n.id===selectedResourceNode).map(node=>
              <SpectraIndexedGraphAssetInspector key={node.id+'-'+node.type}
                graph={graph} projectPath={projectPath} selectedNodeId={node.id}
                property={node.type==='CollisionModule'?'staticSdf':node.type==='MeshOutput'?'meshes':'texture'}
                kinds={node.type==='MeshOutput'||node.type==='CollisionModule'?['model']:['texture']}
                disabled={busy} onGraphChange={next=>apply({graph:next,error:''})}/>)}
          </StudioSection>
          <StudioSection title="DataInterface nodes">
            <div className="spectra-di__items">
              {graph.nodes.filter(n=>n.type==='DataInterface').map(n=><div key={n.id} className="spectra-di__node">
                <StudioButton variant="ghost" size="sm" aria-label={'Show '+n.id+' in graph'}
                  onClick={()=>{
                    const node=document.getElementById(nodeIdPrefix+n.id);
                    node?.click();node?.focus();node?.scrollIntoView({block:'center',inline:'center'});
                  }}>{n.id} → {String(n.properties?.name??'')}</StudioButton>
                <StudioButton variant="danger" size="sm" disabled={busy} aria-label={'Remove '+n.id} onClick={()=>apply(dropNode(graph,n.id))}><Trash2 size={13}/></StudioButton>
              </div>)}
            </div>
            {graph.nodes.every(n=>n.type!=='DataInterface')&&<p className="spectra-di__hint">No DataInterface nodes in this graph.</p>}
          </StudioSection>
        </>}
      </main>
    </div>
  </StudioPanel>;
}
