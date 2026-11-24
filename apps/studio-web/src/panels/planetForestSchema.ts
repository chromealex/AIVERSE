// Pure immutable draft helpers. Native parser is the authoritative validator.
export type ForestRecord=Record<string,unknown>;
export const object=(x:unknown):ForestRecord=>x&&typeof x==='object'&&!Array.isArray(x)?x as ForestRecord:{};
export const rows=(x:unknown):ForestRecord[]=>Array.isArray(x)?x.map(object):[];
export function patchRow(doc:ForestRecord,key:string,index:number,patch:ForestRecord):ForestRecord{
  const list=rows(doc[key]);return {...doc,[key]:list.map((r,i)=>i===index?{...r,...patch}:r)};
}
export function removeRow(doc:ForestRecord,key:string,index:number):ForestRecord{
  return {...doc,[key]:rows(doc[key]).filter((_,i)=>i!==index)};
}
export function nextId(items:ForestRecord[],key:string):number{
  const used=new Set(items.map(r=>Number(r[key])));let n=1;while(used.has(n))n++;return n;
}
export function collisionMode(row:ForestRecord,mode:string):ForestRecord{
  const next={...row};
  if(mode==='none'){delete next.naturalFallBody;return next;}
  const old=object(row.naturalFallBody);
  const material={density:old.density??600,frictionPermille:old.frictionPermille??500};
  next.naturalFallBody=mode==='arbor'?{...material,fromArbor:true}:{...material,radiusCm:old.radiusCm??15,halfHeightCm:old.halfHeightCm??200};
  return next;
}
