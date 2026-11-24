import {useEffect,useMemo,useState} from 'react';
import {invoke,nativeConnected,onNativeEvent} from '../bridge';
import type {StudioAsset} from '../types';
import type {MaterialLibrary} from '../panels/materialSchema';
import {StudioAlert,StudioButton,StudioSection} from '../ui';
import GraphCanvas from './GraphCanvas';
import {projectBuildingMaterialDependencies} from './buildingMaterialDependencies';
import type {TextureProvenance} from './buildingMaterialDependencies';

export function BuildingMaterialDependenciesView({bindings,library,onRevealAsset,onFocusSlot}:{bindings:Readonly<Record<string,string>>;library:MaterialLibrary;onRevealAsset:(path:string)=>void;onFocusSlot:(slot:string)=>void}){
 const [assets,setAssets]=useState<StudioAsset[]>([]),[selected,setSelected]=useState(''),[revision,setRevision]=useState(0);
 const [catalogError,setCatalogError]=useState(''),[provenance,setProvenance]=useState<TextureProvenance|null>(null),[error,setError]=useState(''),[busy,setBusy]=useState(false);
 useEffect(()=>{
  const refresh=()=>setRevision(n=>n+1);
  const off=onNativeEvent(event=>{if(event==='ui.assetsChanged'||event==='document.changed'||event==='workspace.changed')refresh()});
  window.addEventListener('studio:assets-changed',refresh);
  return()=>{off();window.removeEventListener('studio:assets-changed',refresh)};
 },[]);
 useEffect(()=>{
  let alive=true;setAssets([]);setCatalogError('');setProvenance(null);
  void invoke('studio.assets',{}).then(r=>{if(alive)setAssets(r.files)}).catch(e=>{if(alive)setCatalogError(String(e))});
  return()=>{alive=false};
 },[revision]);
 const data=useMemo(()=>projectBuildingMaterialDependencies(bindings,library,assets,provenance??undefined),[bindings,library,assets,provenance]);
 const texture=data.textures.get(selected),guid=texture?.asset?.guid,path=texture?.asset?.path;
 useEffect(()=>{
  let alive=true;setError('');setBusy(false);
  if(!guid||!path||!nativeConnected())return;
  setBusy(true);
  void invoke('studio.buildingTextureProvenance',{guid}).then(r=>{
   if(!alive)return;
   if(r.guid!==guid||r.path!==path)throw Error('Texture provenance identity mismatch');
   setProvenance(r);
  }).catch(e=>{if(alive)setError(String(e))}).finally(()=>{if(alive)setBusy(false)});
  return()=>{alive=false};
 },[guid,path,revision]);
 const current=provenance&&((guid===provenance.guid&&path===provenance.path)||selected==='bake:'+provenance.guid)?provenance:null;
 return <StudioSection title="Building material and texture dependencies">
  <small>Explicit project bindings and catalog references. Inherited and bundled style defaults are not resolved here.</small>
  {catalogError&&<StudioAlert tone="warning">{catalogError}</StudioAlert>}
  <div style={{height:320,display:'flex',flexDirection:'column',minWidth:0}}>
   <GraphCanvas view={data.view} label="Building material dependencies" selected={data.view.nodes.some(n=>n.id===selected)?selected:''} readOnly onSelect={setSelected} onMove={()=>{}}/>
  </div>
  {selected.startsWith('slot:')&&<StudioButton size="sm" onClick={()=>onFocusSlot(selected.slice(5))}>Focus material binding</StudioButton>}
  {data.materials.has(selected)&&<small>Project material · {data.materials.get(selected)}</small>}
  {texture&&<div className="au-stack"><small>Reference: {texture.reference}</small>
   {path?<StudioButton size="sm" onClick={()=>onRevealAsset(path)}>Reveal texture in Assets</StudioButton>:<StudioAlert tone="warning">Texture reference not resolved in current catalog.</StudioAlert>}
   {busy&&<small>Inspecting native bake record…</small>}{error&&<StudioAlert tone="warning">{error}</StudioAlert>}
   {current&&!current.manifestFound&&<small>No matching recorded texture-set manifest. Origin unknown.</small>}
  </div>}
  {current?.manifestFound&&<div className="au-stack">
   <small>Recorded Texture Graph bake · {current.output} · {current.manifestPath}</small>
   {current.manifestPath&&<StudioButton size="sm" onClick={()=>onRevealAsset(current.manifestPath!)}>Reveal bake manifest</StudioButton>}
   <details><summary>Recorded graph and overrides</summary><pre style={{whiteSpace:'pre-wrap',overflowWrap:'anywhere'}}>{JSON.stringify({graph:current.graph,overrides:current.overrides},null,2)}</pre></details>
   <small>Recorded source snapshot only; current image bytes and current editable graph identity are not verified.</small>
  </div>}
 </StudioSection>;
}
