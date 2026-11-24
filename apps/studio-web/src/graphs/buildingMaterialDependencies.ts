import type {GraphView,NodeView,Link} from './graphFramework';
import type {MaterialLibrary} from '../panels/materialSchema';
import type {StudioAsset} from '../types';
import {safeAssetPath} from '../assetTree.ts';

export const buildingTextureChannels=['texture','normalMap','metallicRoughnessMap','occlusionMap','emissiveMap'] as const;
export type TextureReference={id:string;reference:string;asset?:StudioAsset};
export interface TextureProvenance {guid:string;path:string;manifestFound:boolean;manifestPath?:string;output?:string;graph?:unknown;overrides?:unknown}
export function projectBuildingMaterialDependencies(bindings:Readonly<Record<string,string>>,library:MaterialLibrary,assets:readonly StudioAsset[],provenance?:TextureProvenance){
 const nodes:NodeView[]=[],links:Link[]=[],textures=new Map<string,TextureReference>(),materials=new Map<string,string>();
 const add=(id:string,label:string,x:number,y:number)=>nodes.push({id,label,x,y,inputs:[{id:'in',type:'dependency',multiple:true}],outputs:[{id:'out',type:'dependency',multiple:true}]});
 const link=(from:string,to:string,channel:string)=>links.push({id:JSON.stringify([from,to,channel]),from,to,output:'out',input:'in'});
 const byGuid=new Map<string,StudioAsset>(),byPath=new Map<string,StudioAsset>();
 for(const a of assets){if(a.importer!=='texture'||!a.guid||!safeAssetPath(a.path))continue;byGuid.set(a.guid,a);byPath.set(a.path,a);}
 add('building','Building',20,30);
 Object.entries(bindings).sort(([a],[b])=>a.localeCompare(b)).slice(0,64).forEach(([slot,name],i)=>{
  if(!name)return;
  const slotId='slot:'+slot,matId='material:'+name;
  add(slotId,slot,210,30+i*100);link('building',slotId,'slot');
  if(!materials.has(matId)){
   const material=Object.prototype.hasOwnProperty.call(library.library,name)?library.library[name]:undefined;
   materials.set(matId,name);add(matId,material?name:'Missing · '+name,400,30+(materials.size-1)*100);
   if(material)for(const channel of buildingTextureChannels){
    const ref=material[channel];if(typeof ref!=='string'||!ref)continue;
    const path=safeAssetPath(ref.startsWith('content/')?ref.slice(8):ref);
    const asset=byGuid.get(ref)??(path?byPath.get(path):undefined);
    const id=asset?'asset:'+asset.guid:'unresolved:'+ref;
    if(!textures.has(id)){textures.set(id,{id,reference:ref,asset});add(id,asset?asset.path:'Unresolved · '+ref,590,30+(textures.size-1)*100);}
    link(matId,id,channel);
   }
  }
  link(slotId,matId,'material');
 });
 if(provenance?.manifestFound&&provenance.manifestPath&&provenance.output){
  const id='asset:'+provenance.guid,asset=textures.get(id)?.asset;
  if(asset?.path===provenance.path){
   add('bake:'+provenance.guid,'Bake · '+provenance.output,800,30);
   link(id,'bake:'+provenance.guid,'recorded provenance');
  }
 }
 const view:GraphView={nodes,links};return {view,textures,materials};
}
