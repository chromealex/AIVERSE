// Disposable persistent cache. Never stores project/GPU runtime state.
export interface LibraryThumbnail {id:string;signature:string;image:string;updated:number}
export const LIBRARY_DISK_LIMIT=64;
export const LIBRARY_IMAGE_LIMIT=900000;
export function validThumbnail(t:LibraryThumbnail):boolean {
  return typeof t.id==='string'&&t.id.length<=8192&&typeof t.signature==='string'&&
    t.signature.length>0&&t.signature.length<=524288&&typeof t.updated==='number'&&Number.isFinite(t.updated)&&
    typeof t.image==='string'&&t.image.startsWith('data:image/jpeg;base64,')&&t.image.length<=LIBRARY_IMAGE_LIMIT;
}
export function evictThumbnailIds(rows:readonly Pick<LibraryThumbnail,'id'|'updated'>[],limit=LIBRARY_DISK_LIMIT):string[]{
  return [...rows].sort((a,b)=>b.updated-a.updated||a.id.localeCompare(b.id)).slice(Math.max(0,limit)).map(r=>r.id);
}
let dbPromise:Promise<IDBDatabase>|undefined;
function database():Promise<IDBDatabase>{
  if(!dbPromise)dbPromise=new Promise((resolve,reject)=>{
    if(typeof indexedDB==='undefined'){reject(new Error('IndexedDB unavailable'));return;}
    const req=indexedDB.open('aiverse-spectrafx-thumbnails',1);
    req.onupgradeneeded=()=>{req.result.createObjectStore('frames',{keyPath:'id'});};
    req.onsuccess=()=>resolve(req.result);
    req.onerror=()=>reject(req.error);
    req.onblocked=()=>reject(new Error('Thumbnail cache blocked'));
  });
  return dbPromise;
}
export async function persistThumbnail(row:LibraryThumbnail):Promise<void>{
  if(!validThumbnail(row))return;
  const db=await database();
  await new Promise<void>((resolve,reject)=>{
    const tx=db.transaction('frames','readwrite');
    const frames=tx.objectStore('frames');
    frames.put(row);
    // Evict in the same transaction, including the row just added.
    const request=frames.getAll();
    request.onsuccess=()=>{
      for(const id of evictThumbnailIds(request.result as LibraryThumbnail[]))frames.delete(id);
    };
    tx.oncomplete=()=>resolve();
    tx.onerror=()=>reject(tx.error);
    tx.onabort=()=>reject(tx.error);
  });
}
export async function readThumbnail(id:string,signature:string):Promise<string>{
  const db=await database();
  return new Promise((resolve,reject)=>{
    const request=db.transaction('frames','readonly').objectStore('frames').get(id);
    request.onsuccess=()=>{
      const row=request.result as LibraryThumbnail|undefined;
      resolve(row&&validThumbnail(row)&&row.signature===signature?row.image:'');
    };
    request.onerror=()=>reject(request.error);
  });
}
