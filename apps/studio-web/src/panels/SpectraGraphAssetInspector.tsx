import { useEffect, useState } from 'react';
import SpectraAssetPicker from './SpectraAssetPicker';
import type { SpectraIndexedAsset } from './SpectraAssetPicker';
import { bindResolvedSpectraAsset, editSpectraMeshSlot } from './spectraAssetBinding';
import type { SpectraGraphV2 } from './spectraDataInterfaceModel';

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
}
export default function SpectraGraphAssetInspector({
  graph,selectedNodeId,property,assets,onGraphChange,kinds,disabled=false,
}:Props){
  const [locked,setLocked]=useState(false);
  const [targetNodeId,setTargetNodeId]=useState<string|null>(selectedNodeId);
  const [error,setError]=useState('');
  const [pendingInsertAfter,setPendingInsertAfter]=useState<number|null>(null);
  useEffect(()=>{
    if(!locked)setTargetNodeId(selectedNodeId);
  },[selectedNodeId,locked]);
  useEffect(()=>{setPendingInsertAfter(null);},[targetNodeId]);
  // A node deleted by a graph edit is never resurrected by a stale lock.
  const node=graph.nodes.find(n=>n.id===targetNodeId);
  const raw=node?.properties?.[property];
  const currentPath=typeof raw==='string'?raw:
    property==='meshes'&&Array.isArray(raw)&&typeof raw[0]==='string'?raw[0]:null;
  const selected=currentPath?assets.find(a=>a.path&&'content/'+a.path===currentPath):undefined;
  const current=selected?{guid:selected.guid,path:selected.path}:null;
  const meshSlots=property==='meshes'&&Array.isArray(raw)?raw.filter((v):v is string=>typeof v==='string'):[];
  const commit=(result:{graph:SpectraGraphV2;error:string})=>{
    if(result.error){setError(result.error);return;}
    setError('');onGraphChange(result.graph);
  };
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
    {locked&&<small>Selection locked to this graph node</small>}
    {error&&<small role="alert">{error}</small>}
  </section>;
}
