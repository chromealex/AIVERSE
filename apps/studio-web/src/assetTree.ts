// UI-only presentation model of paths RELATIVE TO project/content.
// Native StudioContentDirectory scans the directory safely; UI never reads
// arbitrary paths, invents thumbnails or modifies project assets.
import type { StudioAsset } from './types';

export type AssetKind = 'image' | 'model' | 'audio' | 'code' | 'document';
export interface AssetFolder { path: string; name: string; count: number; depth: number }

const IMAGE = new Set(['png','jpg','jpeg','webp','tga','dds','ktx','ktx2','bmp','hdr','exr']);
const MODEL = new Set(['obj','fbx','glb','gltf','mesh','usd','usdz']);
const AUDIO = new Set(['wav','ogg','mp3','flac','aiff']);
const CODE = new Set(['json','lua','ts','tsx','js','jsx','cpp','h','hpp','glsl','hlsl','metal','shader','css']);

export function safeAssetPath(path: string): string | null {
  if(typeof path !== 'string' || !path || path.length>1024 || path.startsWith('/') ||
     /^[a-zA-Z]:/.test(path))return null;
  const normalized=path.replaceAll('\\','/');
  const parts=normalized.split('/');
  if(parts.some(segment=>!segment || segment==='.' || segment==='..' ||
      /[\u0000-\u001f]/.test(segment)))return null;
  return parts.join('/');
}
export function assetKind(path: string): AssetKind {
  const ext=path.split('.').pop()?.toLowerCase()??'';
  if(IMAGE.has(ext))return 'image';
  if(MODEL.has(ext))return 'model';
  if(AUDIO.has(ext))return 'audio';
  if(CODE.has(ext))return 'code';
  return 'document';
}
export function assetFileName(path:string):string {return path.slice(path.lastIndexOf('/')+1);}
export function assetParent(path:string):string {
  const index=path.lastIndexOf('/');
  return index<0?'':path.slice(0,index);
}
export function assetFolders(files:readonly StudioAsset[]):AssetFolder[] {
  const counts=new Map<string,number>();
  for(const asset of files) {
    const path=safeAssetPath(asset.path);
    if(!path)continue;
    const segments=path.split('/');
    for(let depth=1;depth<segments.length;depth++){
      const key=segments.slice(0,depth).join('/');
      counts.set(key,(counts.get(key)??0)+1);
    }
  }
  return [...counts.entries()].map(([path,count])=>({
    path,name:assetFileName(path),count,depth:path.split('/').length-1
  })).sort((a,b)=>a.path.localeCompare(b.path));
}
export function childFolders(folders:readonly AssetFolder[],parent:string):AssetFolder[] {
  return folders.filter(folder=>assetParent(folder.path)===parent);
}
export function visibleAssetFiles(files:readonly StudioAsset[],parent:string,search:string):StudioAsset[]{
  const query=search.trim().toLowerCase();
  return files.flatMap(asset=>{
    const path=safeAssetPath(asset.path);
    if(!path)return [];
    if(query ? !path.toLowerCase().includes(query) : assetParent(path)!==parent)return [];
    return [{...asset,path}];
  }).sort((a,b)=>a.path.localeCompare(b.path));
}
export function assetBreadcrumbs(parent:string):Array<{name:string;path:string}> {
  const result=[{name:'content',path:''}];
  if(!parent)return result;
  const parts=parent.split('/');
  parts.forEach((name,index)=>result.push({name,path:parts.slice(0,index+1).join('/')}));
  return result;
}
