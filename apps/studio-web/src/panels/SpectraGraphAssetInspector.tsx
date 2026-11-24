import { useEffect, useRef, useState } from 'react';
import { StudioButton, StudioField, StudioSelect } from '../ui';
import { invoke } from '../bridge';
import SpectraAssetPicker from './SpectraAssetPicker';
import type { SpectraIndexedAsset } from './SpectraAssetPicker';
import { bindResolvedSpectraAsset, editSpectraMeshSlot } from './spectraAssetBinding';
import { graphV2, type SpectraGraphV2 } from './spectraDataInterfaceModel';
import type { SpectraAssetReference } from './spectraAssetBinding';

/** Asset-reference sub-inspector for a selected graph node.
 * The host supplies an authoritative project asset index and selected node.
 * A locked inspector keeps its node target when selection changes outside.
 * This does not serialize the graph; caller must validate through native compiler.
 */
interface Props {
  graph:SpectraGraphV2;
  selectedNodeId:string|null;
  property:string;
  assets:readonly SpectraIndexedAsset[];
  onGraphChange:(graph:SpectraGraphV2)=>void;
  kinds?:readonly string[];
  disabled?:boolean;
  projectPath?:string;
}
export default function SpectraGraphAssetInspector({
  graph,selectedNodeId,property,assets,onGraphChange,kinds,disabled=false,projectPath,
}:Props){
  const [locked,setLocked]=useState(false);
  const [targetNodeId,setTargetNodeId]=useState<string|null>(selectedNodeId);
  const [error,setError]=useState('');
  const [checks,setChecks]=useState<Record<string,string>>({});
  const [checking,setChecking]=useState(false);
  const checkAsset=async(guid:string)=>{
    setChecking(true);
    try {
      const result=await invoke('studio.spectraAssetCheck',{guid});
      setChecks(old=>({...old,[guid]:(result.ok?'PASS: ':'FAIL: ')+result.code+' — '+result.detail+' (CPU only)'}));
    }catch(e){setChecks(old=>({...old,[guid]:String(e)}));}
    finally{setChecking(false);}
  };
  const [pendingInsertAfter,setPendingInsertAfter]=useState<number|null>(null);
  useEffect(()=>{
    if(!locked)setTargetNodeId(selectedNodeId);
  },[selectedNodeId,locked]);
  useEffect(()=>{setPendingInsertAfter(null);},[targetNodeId]);
  useEffect(()=>{setPendingInsertAfter(null);},[property]);
  // A node deleted by a graph edit is never resurrected by a stale lock.
  const node=graph.nodes.find(n=>n.id===targetNodeId);
  const [bakeModel,setBakeModel]=useState<SpectraAssetReference|null>(null);
  const [bakeDimension,setBakeDimension]=useState(16);
  const [sdfAsset,setSdfAsset]=useState<SpectraAssetReference|null>(null);
  const [baking,setBaking]=useState(false);
  const [bakeStatus,setBakeStatus]=useState('');
  const currentBake=useRef({graph,targetNodeId,projectPath,onGraphChange});
  currentBake.current={graph,targetNodeId,projectPath,onGraphChange};
  const mounted=useRef(true);
  useEffect(()=>{mounted.current=true;return()=>{mounted.current=false;};},[]);
  const bake=async(importAsset=false)=>{
    const request=currentBake.current;
    const model=assets.find(a=>importAsset?a.guid===sdfAsset?.guid&&a.kind==='sdf':a.guid===bakeModel?.guid&&a.kind==='model');
    if(disabled||baking||!request.projectPath||!request.targetNodeId||!model?.path)return;
    setBaking(true);setError('');setBakeStatus('');
    try{
      const response=await invoke('studio.op',{op:'fx_graph_edit',params:{
        action:importAsset?'importSdf':'bakeSdf',graph:request.graph,node:request.targetNodeId,
        expectedProjectPath:request.projectPath,...(importAsset?{sdfAsset:'content/'+model.path}:{model:'content/'+model.path,dimension:bakeDimension})}});
      if(!mounted.current)return;
      const latest=currentBake.current;
      if(latest.graph!==request.graph||latest.targetNodeId!==request.targetNodeId||
         latest.projectPath!==request.projectPath){setError('Graph or project changed; discarded stale bake.');return;}
      const result=response.result as {ok?:boolean;graph?:unknown;error?:string;validation?:{ok?:boolean}}|undefined;
      const next=graphV2(result?.graph);
      if(!result?.ok||!next)throw new Error(result?.error||'Native SDF bake rejected.');
      latest.onGraphChange(next);
      setBakeStatus((importAsset?'Imported SDF snapshot into local draft. ':'Baked '+bakeDimension+'³ samples into local draft. ')+ (result.validation?.ok?'Compiler accepted.':'Validate graph before publishing.'));
    }catch(e){if(mounted.current)setError(e instanceof Error?e.message:String(e));}
    finally{if(mounted.current)setBaking(false);}
  };
  const raw=node?.properties?.[property];
  const currentPath=typeof raw==='string'?raw:
    property==='meshes'&&Array.isArray(raw)&&typeof raw[0]==='string'?raw[0]:null;
  const selected=currentPath?assets.find(a=>a.path&&'content/'+a.path===currentPath):undefined;
  const current=selected?{guid:selected.guid,path:selected.path}:null;
  const meshSlots=property==='meshes'&&Array.isArray(raw)?raw.filter((v):v is string=>typeof v==='string'):[];
  const diagnostic=(path:string)=>{
    if(path.startsWith('builtin:'))return '';
    if(!path.startsWith('content/'))return 'Legacy path: '+path+' (not checked by the content index)';
    if(!assets.some(a=>a.path&&'content/'+a.path===path))return 'Not available in current asset picker: '+path+' (missing, unsupported or not indexed)';
    return '';
  };
  const commit=(result:{graph:SpectraGraphV2;error:string})=>{
    if(result.error){setError(result.error);return;}
    setError('');onGraphChange(result.graph);
  };
  if(property==='staticSdf')return <section aria-label="SpectraFX mesh SDF bake">
    <header><strong>Mesh SDF</strong> <small>{node?.id??'No node selected'}</small></header>
    <SpectraAssetPicker assets={assets} value={bakeModel} kinds={['model']}
      disabled={disabled||baking||!node} locked={locked} onLockChange={setLocked}
      onChange={asset=>{setBakeModel(asset);setBakeStatus('');}}/>
    <StudioField label="Lattice dimension" htmlFor="spectra-sdf-dimension">
      <StudioSelect id="spectra-sdf-dimension" value={String(bakeDimension)}
        disabled={disabled||baking} onChange={e=>setBakeDimension(Number(e.target.value))}>
        {[4,8,12,16,24,32].map(n=><option key={n} value={n}>{n} × {n} × {n} ({(n*n*n).toLocaleString()} samples)</option>)}
      </StudioSelect>
    </StudioField>
    <StudioButton size="sm" disabled={disabled||baking||!node||!bakeModel||!projectPath}
      onClick={()=>void bake()}>{baking?'Baking…':raw?'Rebake SDF':'Bake SDF'}</StudioButton>
    <small>Closed low-poly mesh, bind pose, model-space metres. At 32³ the bake budget allows up to 512 triangles. Replaces only the baked SDF in the local graph; save to publish.</small>
    <StudioField label="Import binary SDF snapshot">
      <SpectraAssetPicker assets={assets} value={sdfAsset} kinds={['sdf']}
        disabled={disabled||baking||!node} onChange={asset=>{setSdfAsset(asset);setBakeStatus('');}}/>
    </StudioField>
    <StudioButton size="sm" disabled={disabled||baking||!node||!sdfAsset||!projectPath}
      onClick={()=>void bake(true)}>Import SDF</StudioButton>
    <small>Imports a copy from an indexed .sdfbin file. The graph does not retain a live file reference.</small>
    {bakeStatus&&<small role="status">{bakeStatus}</small>
    {error&&<small role="alert">{error}</small>}
  </section>;
  return <section aria-label="SpectraFX node asset reference">
    <header style={{display:'flex',justifyContent:'space-between',alignItems:'center'}}>
      <strong>{property} asset</strong>
      <small>{node?node.id:'No node selected'}</small>
    </header>
    {property==='meshes' ? <>
      {meshSlots.length===0&&<small>MeshOutput has no valid mesh array; add a mesh using native graph node defaults before editing slots.</small>}
      {meshSlots.map((path,slot)=>{
        const entry=assets.find(a=>a.path&&'content/'+a.path===path);
        return <div key={slot} style={{display:'flex',alignItems:'center',gap:6,marginBlock:6,flexWrap:'wrap'}}>
          <small style={{minWidth:36}}>#{slot+1}</small>
          <small title={path}>{path}</small>
          {diagnostic(path)&&<small role="status">{diagnostic(path)}</small>}
          {entry&&<><button type="button" disabled={disabled||checking} onClick={()=>void checkAsset(entry.guid)}>Check asset</button><small>{checks[entry.guid]}</small></>}
          <div style={{flex:1}}>
            <SpectraAssetPicker assets={assets}
              value={entry?{guid:entry.guid,path:entry.path}:null} kinds={kinds}
              disabled={disabled||!node} locked={locked} onLockChange={setLocked}
              onChange={asset=>node&&commit(editSpectraMeshSlot(graph,node.id,'replace',slot,asset,assets))}/>
          </div>
          <button type="button" disabled={disabled||!node||slot===0}
            onClick={()=>node&&commit(editSpectraMeshSlot(graph,node.id,'up',slot))}>↑</button>
          <button type="button" disabled={disabled||!node||slot===meshSlots.length-1}
            onClick={()=>node&&commit(editSpectraMeshSlot(graph,node.id,'down',slot))}>↓</button>
          <button type="button" disabled={disabled||!node||meshSlots.length<=1}
            onClick={()=>node&&commit(editSpectraMeshSlot(graph,node.id,'remove',slot))}>Remove</button>
          <button type="button" disabled={disabled||!node||meshSlots.length>=32}
            onClick={()=>setPendingInsertAfter(pendingInsertAfter===slot?null:slot)}>Add</button>
          {pendingInsertAfter===slot&&<div style={{flexBasis:'100%',width:'100%'}}>
            <small>Choose a model for the new mesh slot</small>
            <SpectraAssetPicker assets={assets} value={null} kinds={['model']}
              disabled={disabled||!node} onChange={asset=>{
                if(!node)return;
                const result=editSpectraMeshSlot(graph,node.id,'add',slot,asset,assets);
                if(!result.error)setPendingInsertAfter(null);
                commit(result);
              }}/>
            <button type="button" onClick={()=>setPendingInsertAfter(null)}>Cancel</button>
          </div>}
        </div>;
      })}
    </> : <SpectraAssetPicker assets={assets} value={current} kinds={kinds}
      disabled={disabled||!node} locked={locked} onLockChange={setLocked}
      onChange={asset=>{
        if(!node)return;
        commit(bindResolvedSpectraAsset(graph,node.id,property,asset,assets,kinds));
      }}/>}
    {property!=='meshes'&&selected&&<><button type="button" disabled={disabled||checking} onClick={()=>void checkAsset(selected.guid)}>Check asset</button><small>{checks[selected.guid]}</small></>}
    {property!=='meshes'&&currentPath&&diagnostic(currentPath)&&<small role="status">{diagnostic(currentPath)}</small>}
    {locked&&<small>Selection locked to this graph node</small>}
    {error&&<small role="alert">{error}</small>}
  </section>;
}
