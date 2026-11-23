import { useEffect, useState } from 'react';
import SpectraAssetPicker from './SpectraAssetPicker';
import type { SpectraIndexedAsset } from './SpectraAssetPicker';
import { bindResolvedSpectraAsset } from './spectraAssetBinding';
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
  useEffect(()=>{
    if(!locked)setTargetNodeId(selectedNodeId);
  },[selectedNodeId,locked]);
  // A node deleted by a graph edit is never resurrected by a stale lock.
  const node=graph.nodes.find(n=>n.id===targetNodeId);
  const raw=node?.properties?.[property];
  const currentPath=typeof raw==='string'?raw:
    property==='meshes'&&Array.isArray(raw)&&typeof raw[0]==='string'?raw[0]:null;
  const selected=currentPath?assets.find(a=>a.path&&'content/'+a.path===currentPath):undefined;
  const current=selected?{guid:selected.guid,path:selected.path}:null;
  return <section aria-label="SpectraFX node asset reference">
    <header style={{display:'flex',justifyContent:'space-between',alignItems:'center'}}>
      <strong>{property} asset</strong>
      <small>{node?node.id:'No node selected'}</small>
    </header>
    <SpectraAssetPicker assets={assets} value={current} kinds={kinds}
      disabled={disabled||!node} locked={locked} onLockChange={setLocked}
      onChange={asset=>{
        if(!node)return;
        const result=bindResolvedSpectraAsset(graph,node.id,property,asset,assets,kinds);
        if(result.error){setError(result.error);return;}
        setError('');
        onGraphChange(result.graph);
      }}/>
    {locked&&<small>Selection locked to this graph node</small>}
    {error&&<small role="alert">{error}</small>}
  </section>;
}
