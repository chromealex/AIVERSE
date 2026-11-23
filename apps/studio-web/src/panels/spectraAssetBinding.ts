/** SpectraFX graph asset-reference editing, independent of UI and engine genre.
 * This helper never guesses asset GUIDs and never mutates the input graph.
 * The caller supplies a GUID resolved by the Studio Assets browser/index.
 */
import type { SpectraGraphV2 } from './spectraDataInterfaceModel';

export interface SpectraAssetReference {
  guid: string;
  path?: string;
}
export interface SpectraAssetEdit {
  graph: SpectraGraphV2;
  error: string;
}
const failure=(graph:SpectraGraphV2,error:string):SpectraAssetEdit=>({graph,error});
const validGuid=(guid:string):boolean =>
  /^[a-fA-F0-9]{32}$/.test(guid) ||
  /^[a-fA-F0-9]{8}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{12}$/.test(guid);
const validProperty=(key:string):boolean => /^[a-zA-Z][a-zA-Z0-9_]{0,63}$/.test(key);
const validPath=(path:string):boolean =>
  path.length>0 && path.length<=512 && !path.startsWith('/') &&
  !path.includes('\\') && !path.split('/').includes('..') && !/[\x00-\x1f]/.test(path);

/** Replace a single node property after the asset picker resolves its GUID.
 * Existing unrelated properties, links, outputs and unknown graph metadata survive.
 */
export function bindSpectraNodeAsset(
  graph:SpectraGraphV2,nodeId:string,property:string,asset:SpectraAssetReference,
):SpectraAssetEdit {
  if(!validProperty(property))return {graph,error:'Invalid asset property name.'};
  if(!validGuid(asset.guid))return {graph,error:'Asset must have a valid indexed GUID.'};
  if(asset.path!==undefined&&!validPath(asset.path))
    return {graph,error:'Asset path must be a safe project-relative path.'};
  const index=graph.nodes.findIndex(node=>node.id===nodeId);
  if(index<0)return {graph,error:'Target graph node not found.'};
  const nodes=graph.nodes.map((node,i)=>i===index?{
    ...node,
    properties:{...(node.properties??{}),[property]:{
      guid:asset.guid,...(asset.path===undefined?{}:{path:asset.path}),
    }},
  }:node);
  return {graph:{...graph,nodes},error:''};
}

/** Read the selected GUID without treating a plain filename as a resolved asset. */
export function readSpectraNodeAsset(
  graph:SpectraGraphV2,nodeId:string,property:string,
):SpectraAssetReference|null {
  const value=graph.nodes.find(node=>node.id===nodeId)?.properties?.[property];
  if(!value||typeof value!=='object'||Array.isArray(value))return null;
  const asset=value as Record<string,unknown>;
  if(typeof asset.guid!=='string'||!validGuid(asset.guid))return null;
  if(asset.path!==undefined&&(typeof asset.path!=='string'||!validPath(asset.path)))return null;
  return {guid:asset.guid,...(typeof asset.path==='string'?{path:asset.path}:{})};
}

/** Convert a picker-selected GUID into a compiler-compatible string property.
 * The native VFX graph compiler expects strings (e.g. SpriteOutput.texture),
 * not the browser-only {guid,path} authoring object. The authoritative index
 * must contain the GUID; callers decide the expected asset kind.
 */
export function bindResolvedSpectraAsset(
  graph:SpectraGraphV2,nodeId:string,property:string,
  asset:SpectraAssetReference,
  indexed:readonly {guid:string;path?:string;kind:string}[],
  expectedKinds?:readonly string[],
):SpectraAssetEdit {
  if(!validProperty(property))return failure(graph,'Invalid asset property name.');
  if(!validGuid(asset.guid))return failure(graph,'Invalid indexed asset GUID.');
  const resolved=indexed.find(item=>item.guid.toLowerCase()===asset.guid.toLowerCase());
  if(!resolved||!resolved.path||!validPath(resolved.path))
    return failure(graph,'Asset GUID is not resolvable to a safe content path.');
  if(expectedKinds&&!expectedKinds.includes(resolved.kind))
    return failure(graph,'Asset importer type is incompatible with this property.');
  if(property==='texture'&&!/\.(png|jpe?g|tga)$/i.test(resolved.path))
    return failure(graph,'Sprite textures require PNG, JPEG or TGA assets.');
  const index=graph.nodes.findIndex(node=>node.id===nodeId);
  if(index<0)return failure(graph,'Target graph node not found.');
  const node=graph.nodes[index];
  if(property==='meshes'&&node.type!=='MeshOutput')
    return failure(graph,'meshes is only supported on MeshOutput.');
  if(property==='meshes'&&resolved.kind!=='model')
    return failure(graph,'MeshOutput requires a model asset.');
  const previous=node.properties?.meshes;
  const meshList=Array.isArray(previous)?previous.filter(v=>typeof v==='string'):[];
  const value=property==='meshes'
    ? ['content/'+resolved.path,...meshList.slice(1)]
    : 'content/'+resolved.path;
  const nodes=graph.nodes.map((item,i)=>i===index?{
    ...item,properties:{...(item.properties??{}),[property]:value},
  }:item);
  return {graph:{...graph,nodes},error:''};
}

/** MeshOutput array operations preserve unknown graph fields and unrelated nodes.
 * Never leave an empty meshes array: native MeshOutput requires at least one mesh.
 */
export function editSpectraMeshSlot(
  graph:SpectraGraphV2,nodeId:string,action:'add'|'remove'|'up'|'down'|'replace',
  slot:number,asset?:SpectraAssetReference,
  indexed:readonly {guid:string;path?:string;kind:string}[]=[],
):SpectraAssetEdit {
  const node=graph.nodes.find(n=>n.id===nodeId&&n.type==='MeshOutput');
  if(!node)return failure(graph,'Select an existing MeshOutput node.');
  const raw=node.properties?.meshes;
  if(!Array.isArray(raw)||raw.length===0||raw.some(v=>typeof v!=='string'))
    return failure(graph,'MeshOutput meshes must be a nonempty array of strings.');
  if(!Number.isInteger(slot)||slot<0||slot>=raw.length)
    return failure(graph,'Invalid mesh slot.');
  const meshes=[...raw] as string[];
  if(action==='remove'){
    if(meshes.length===1)return failure(graph,'MeshOutput requires at least one mesh.');
    meshes.splice(slot,1);
  }else if(action==='up'||action==='down'){
    const target=slot+(action==='up'?-1:1);
    if(target<0||target>=meshes.length)return failure(graph,'Cannot move mesh beyond array bounds.');
    [meshes[slot],meshes[target]]=[meshes[target],meshes[slot]];
  }else if(action==='add'||action==='replace'){
    if(!asset||!validGuid(asset.guid))return failure(graph,'Select a valid indexed model GUID.');
    const resolved=indexed.find(a=>a.guid.toLowerCase()===asset.guid.toLowerCase());
    if(!resolved||resolved.kind!=='model'||!resolved.path||!validPath(resolved.path))
      return failure(graph,'Model GUID is not present in the current project catalog.');
    const path='content/'+resolved.path;
    if(action==='add'){
      if(meshes.length>=32)return failure(graph,'Maximum 32 mesh slots.');
      meshes.splice(slot+1,0,path);
    }else meshes[slot]=path;
  }else return failure(graph,'Unknown mesh edit action.');
  return {graph:{...graph,nodes:graph.nodes.map(n=>n.id===nodeId?
    {...n,properties:{...(n.properties??{}),meshes}}:n)},error:''};
}
