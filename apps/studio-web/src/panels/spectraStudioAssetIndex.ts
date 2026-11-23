/** Adapter for the authoritative StudioSession "studio.assets" response.
 * StudioAssetCatalog is backed by .meta GUIDs and disposable SQLite index.
 * No filesystem paths or identifiers are synthesized in the browser.
 */
import type { SpectraIndexedAsset } from './SpectraAssetPicker';

interface AssetRow {path?:unknown;guid?:unknown;importer?:unknown;bytes?:unknown}
const isObject=(v:unknown):v is Record<string,unknown> =>
  v!==null&&typeof v==='object'&&!Array.isArray(v);
const guidValid=(v:string):boolean => /^[a-f0-9]{32}$/i.test(v);
const pathValid=(v:string):boolean =>
  v.length>0&&v.length<=512&&!v.startsWith('/')&&!v.includes('\\')&&
  !v.split('/').includes('..')&&!/[\x00-\x1f]/.test(v);
/** Accepts the result payload of invoke('studio.assets',{}), not an RPC envelope. */
export function indexedSpectraAssets(payload:unknown):SpectraIndexedAsset[]{
  if(!isObject(payload)||payload.root!=='content'||!Array.isArray(payload.files))
    return [];
  const out:SpectraIndexedAsset[]=[];
  const seen=new Set<string>();
  for(const raw of payload.files){
    if(!isObject(raw))continue;
    const row=raw as AssetRow;
    if(typeof row.guid!=='string'||!guidValid(row.guid)||
       typeof row.path!=='string'||!pathValid(row.path))continue;
    const guid=row.guid.toLowerCase();
    if(seen.has(guid))continue;
    seen.add(guid);
    out.push({guid,path:row.path,name:row.path.split('/').pop()??row.path,
      kind:typeof row.importer==='string'?row.importer:'data'});
    if(out.length>=5000)break;
  }
  return out;
}
