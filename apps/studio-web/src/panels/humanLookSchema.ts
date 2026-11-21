import type { HumanLook, HumanLookField } from '../types';
export function validateHumanLook(value:unknown,schema:HumanLookField[]):value is HumanLook {
 if(!value||typeof value!=='object'||Array.isArray(value))return false;
 const doc=value as Record<string,unknown>;
 if(doc.version!==1||Object.keys(doc).some(k=>!['version','values','materials'].includes(k)))return false;
 if(!doc.values||typeof doc.values!=='object'||Array.isArray(doc.values)||
    !doc.materials||typeof doc.materials!=='object'||Array.isArray(doc.materials))return false;
 const fields=new Map(schema.map(f=>[f.key,f]));
 if(Object.entries(doc.values).some(([key,v])=>{
  const f=fields.get(key);return !f||typeof v!=='number'||!Number.isFinite(v)||v<f.min||v>f.max;
 }))return false;
 return Object.keys(doc.materials).length<=128&&Object.entries(doc.materials).every(([key,v])=>
  key.length>0&&key.length<=256&&typeof v==='string'&&['skin','hair','lashes','eyes','cloth'].includes(v));
}
