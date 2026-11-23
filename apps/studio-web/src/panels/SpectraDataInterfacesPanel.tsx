import { useCallback, useEffect, useMemo, useState } from 'react';
import { CheckCircle2, FilePlus2, GitBranch, GripVertical, Lock, LockOpen, RefreshCw, Save, Trash2 } from 'lucide-react';
import { invoke, onNativeEvent } from '../bridge';
import { StudioAlert, StudioBadge, StudioButton, StudioCheckbox, StudioEmptyState, StudioField, StudioInput, StudioPanel, StudioSection, StudioSelect } from '../ui';
import {
  graphV2, providerNames, scalarParameters, validateInterfaces, putInterface,
  dropInterface, putParameter, dropParameter, putNode, dropNode, safeGraphName,
  type SpectraGraphV2, type FxDataInterface, type FxInstanceScalar, type GraphEdit
} from './spectraDataInterfaceModel';
import SpectraGraphCanvas from './SpectraGraphCanvas';
import SpectraCurveEditor from './SpectraCurveEditor';
import SpectraNativePreview from './SpectraNativePreview';
import SpectraIndexedGraphAssetInspector from './SpectraIndexedGraphAssetInspector';
import { createBlankGraph, createBurstStarter, nextUnusedGraphName } from './spectraStarterModel';
import './SpectraDataInterfacesPanel.css';

type AssetEntry = { name: string; draft: boolean; valid: boolean };
type NativeResponse = Record<string, unknown>;
const asRecord = (v: unknown): NativeResponse | null =>
  v !== null && typeof v === 'object' && !Array.isArray(v) ? v as NativeResponse : null;
const errorText = (e:unknown):string => e instanceof Error ? e.message : String(e);
const nativeOp = async(op:string,params:Record<string,unknown>={}):Promise<NativeResponse> => {
  const response = await invoke('studio.op',{op,params});
  const result=asRecord(response.result);
  if(!result)throw new Error(op+' returned no usable result.');
  return result;
};
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
  const [assets,setAssets]=useState<AssetEntry[]>([]);
  const [selected,setSelected]=useState<AssetEntry|null>(null);
  const [saved,setSaved]=useState<SpectraGraphV2|null>(null);
  const [graph,setGraph]=useState<SpectraGraphV2|null>(null);
  const [selectedName,setSelectedName]=useState('');
  const [pinned,setPinned]=useState(false);
  const [search,setSearch]=useState('');
  const [providerSearch,setProviderSearch]=useState('');
  const [selectedOutput,setSelectedOutput]=useState('');
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
  const dirty=!!graph && (!saved || JSON.stringify(saved)!==JSON.stringify(graph));
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
  },[projectPath]);

  useEffect(()=>{
    let alive=true;
    setSelected(null);setSaved(null);setGraph(null);setError('');setValidation(null);
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
      const r=await nativeOp(item.draft?'fx_graph_draft_get':'fx_graph_get',{name:item.name});
      const parsed=graphV2(r.graph);
      if(!parsed)throw new Error('This asset is not a Graph v2. Upgrade it using the graph tools first.');
      setSaved(parsed);setGraph(parsed);setSelected(item);
      setSelectedOutput(parsed.nodes.find(n=>n.type==='SpriteOutput')?.id??'');
      const first=Object.keys(parsed.instanceParameters??{})[0]??'';
      if(!pinned || !parsed.instanceParameters?.[selectedName])chooseParameter(first,parsed);
      else chooseParameter(selectedName,parsed);
      setValidation(asRecord(r.validation));
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
      setSelected({name,draft:true,valid:true});setSaved(null);setGraph(next);
      setSelectedOutput(next.nodes.find(n=>n.type==='SpriteOutput')?.id??'');
      setSelectedName('');setValidation(verified);
      setNewName(nextUnusedGraphName([...assets,{name}], 'preview_fx'));
      setSearch('');
      setStatus(starter
        ? 'Starter graph compiled by native SpectraFX. Open Scene, then Load graph and Play. Save Draft or Publish when ready.'
        : 'New blank graph created locally. Add Spawn, Initialize and SpriteOutput nodes to make a playable effect.');
    }catch(e){setError(errorText(e));}
    finally{setBusy(false);}
  };
  const apply=(result:GraphEdit)=>{
    if(result.error){setError(result.error);return;}
    setGraph(result.graph);setValidation(null);setError('');setStatus('Local changes — Validate and Save Draft or Publish.');
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
      const result=await nativeOp(publish?'fx_graph_set':'fx_graph_draft_set',{name:selected.name,graph});
      if(result.ok!==true)throw new Error(diagnostics(result));
      setSaved(graph);setSelected({...selected,draft:!publish,valid:true});
      setStatus(publish?'Published through native fx_graph_set.':'Draft saved separately; published graph unchanged.');
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
    className="spectra-di"
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
          Load it in the native Scene to preview actual particles. Nothing is written until Save Draft or Publish.</p>
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
        {!graph||!selected ? <div className="spectra-di__welcome">
          <GitBranch size={26}/>
          <strong>{assets.length===0?'Create your first VFX effect':'Choose a Graph v2'}</strong>
          <p>{assets.length===0
            ?'Start with a compilable particle burst: Spawn → Initialize → SpriteOutput. Preview it in the actual Metal Scene viewport.'
            :'Select an existing asset on the left or start a new particle effect. Changes stay local until you Save Draft or Publish.'}</p>
          <StudioButton size="sm" variant="primary" disabled={busy||dirty||!safeGraphName(newName.trim())}
            onClick={()=>void makeGraph(true)}><FilePlus2 size={14}/> Create Starter FX</StudioButton>
        </div> : <>
          <div className="spectra-di__toolbar">
            <strong>{selected.name}</strong><StudioBadge tone={dirty?'warning':'neutral'}>{dirty?'Unsaved':selected.draft?'Saved draft':'Saved'}</StudioBadge>
            <StudioButton size="sm" disabled={busy} onClick={()=>void validate()}><CheckCircle2 size={14}/> Validate</StudioButton>
            <StudioButton size="sm" disabled={busy||!dirty&&!!saved} onClick={()=>{setGraph(saved);setError('');setValidation(null);}}>Discard</StudioButton>
            <StudioButton size="sm" disabled={busy} onClick={()=>void save(false)}><Save size={14}/> Save Draft</StudioButton>
            <StudioButton size="sm" variant="primary" disabled={busy} onClick={()=>void save(true)}><Save size={14}/> Publish</StudioButton>
          </div>
          {validation&&<StudioAlert tone={validation.ok===true?'success':'warning'} title="Compiler diagnostics">
            {diagnostics(validation)} {validation.emitters!==undefined&&' · '+String(validation.emitters)+' emitter(s)'}
          </StudioAlert>}
          <SpectraNativePreview graph={graph} projectPath={projectPath}
            disabled={busy}/>
          <StudioSection title="Graph Nodes & Links">
            <SpectraGraphCanvas graph={graph} disabled={busy}
              onError={setError}
              onEdit={next=>{setGraph(next);setValidation(null);setError('');
                setStatus('Graph topology changed locally. Validate and save before publishing.');}}/>
          </StudioSection>
          <SpectraCurveEditor graph={graph} disabled={busy} onError={setError}
            onEdit={next=>{setGraph(next);setValidation(null);setError('');
              setStatus('Curve/Gradient changed in local draft. Validate and save before publishing.');}}/>
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
            <p className="spectra-di__hint">Select SpriteOutput, TrailModule, MaterialModule or MeshOutput. Textures use a string property; MeshOutput uses the meshes array. Resources resolve through the native project GUID index.</p>
            <StudioField label="Resource node" htmlFor="spectra-resource-node">
              <StudioSelect id="spectra-resource-node" value={selectedResourceNode}
                onChange={e=>setSelectedResourceNode(e.target.value)}>
                <option value="">Choose resource node…</option>
                {graph.nodes.filter(n=>['SpriteOutput','TrailModule','MaterialModule','MeshOutput'].includes(n.type))
                  .map(node=><option key={node.id} value={node.id}>{node.id} · {node.type}</option>)}
              </StudioSelect>
            </StudioField>
            {graph.nodes.filter(n=>n.id===selectedResourceNode).map(node=>
              <SpectraIndexedGraphAssetInspector key={node.id+'-'+node.type}
                graph={graph} projectPath={projectPath} selectedNodeId={node.id}
                property={node.type==='MeshOutput'?'meshes':'texture'}
                kinds={node.type==='MeshOutput'?['model']:['texture']}
                disabled={busy} onGraphChange={next=>apply({graph:next,error:''})}/>)}
          </StudioSection>
          <StudioSection title="DataInterface nodes">
            <div className="spectra-di__items">
              {graph.nodes.filter(n=>n.type==='DataInterface').map(n=><div key={n.id} className="spectra-di__node">
                <span>{n.id} → {String(n.properties?.name??'')}</span>
                <StudioButton variant="danger" size="sm" onClick={()=>apply(dropNode(graph,n.id))}><Trash2 size={13}/></StudioButton>
              </div>)}
            </div>
            <p className="spectra-di__hint">Connect a node to ParticleProgramModule using the graph editing operations. Full visual wiring and GPU playback preview are not yet integrated into Studio.</p>
          </StudioSection>
        </>}
      </main>
    </div>
  </StudioPanel>;
}
