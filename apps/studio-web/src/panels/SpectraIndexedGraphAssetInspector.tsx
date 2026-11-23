import { useEffect, useState } from 'react';
import { invoke, nativeConnected } from '../bridge';
import { StudioButton } from '../ui';
import SpectraGraphAssetInspector from './SpectraGraphAssetInspector';
import { indexedSpectraAssets } from './spectraStudioAssetIndex';
import type { SpectraIndexedAsset } from './SpectraAssetPicker';
import type { SpectraGraphV2 } from './spectraDataInterfaceModel';

interface Props {
  graph:SpectraGraphV2;
  selectedNodeId:string|null;
  property:string;
  kinds?:readonly string[];
  onGraphChange:(graph:SpectraGraphV2)=>void;
  disabled?:boolean;
}
/** Native Studio GUID catalog bridge + lockable asset field.
 * Only invoked with a running project; no browser filesystem access.
 */
export default function SpectraIndexedGraphAssetInspector(props:Props){
  const [assets,setAssets]=useState<SpectraIndexedAsset[]>([]);
  const [error,setError]=useState('');
  const [loading,setLoading]=useState(false);
  const [revision,setRevision]=useState(0);
  useEffect(()=>{
    if(!nativeConnected())return;
    let cancelled=false;
    setLoading(true);
    void invoke('studio.assets',{}).then(result=>{
      if(cancelled)return;
      setAssets(indexedSpectraAssets(result));
      setError('');
    }).catch(e=>{
      if(!cancelled){setAssets([]);setError(e instanceof Error?e.message:String(e));}
    }).finally(()=>{if(!cancelled)setLoading(false);});
    return()=>{cancelled=true;};
  },[revision]);
  return <div>
    <StudioButton size="sm" variant="ghost" disabled={loading||!nativeConnected()}
      onClick={()=>setRevision(n=>n+1)}>{loading?'Loading assets…':'Refresh assets'}</StudioButton>
    {error&&<small role="alert">{error}</small>}
    <SpectraGraphAssetInspector {...props} assets={assets}
      disabled={props.disabled||loading||!nativeConnected()}/>
  </div>;
}
