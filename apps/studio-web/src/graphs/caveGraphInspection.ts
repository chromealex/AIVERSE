import type {GraphView, NodeView, Link} from './graphFramework';

type Obj=Record<string,unknown>;
const obj=(v:unknown,label:string):Obj=>{
 if(!v||typeof v!=='object'||Array.isArray(v))throw new Error('Invalid '+label);
 return v as Obj;
};
const int=(v:unknown,label:string,min:number,max=Number.MAX_SAFE_INTEGER):number=>{
 if(typeof v!=='number'||!Number.isSafeInteger(v)||v<min||v>max)throw new Error('Invalid or unsafe '+label);
 return v;
};
const array=(v:unknown,label:string,max:number):unknown[]=>{
 if(!Array.isArray(v)||v.length>max)throw new Error('Invalid '+label+' array or limit');
 return v;
};
const cm=(v:unknown,label:string)=>int(v,label,-1000000000,1000000000);
const radius=(v:unknown)=>int(v,'radiusCm',1,1000000);
export type CaveStamp={id:number;kind:'stamp';mode:'carve'|'fill';xCm:number;yCm:number;zCm:number;radiusCm:number};
export type CaveEntrance={id:number;kind:'entrance';xCm:number;zCm:number;surfaceYcm:number;caveYcm:number;radiusCm:number};
export type CaveEntry=CaveStamp|CaveEntrance;
export interface CaveInspection {
 generatorVersion:number;revision:number;
 settings:{seed:number;cellCm:number;tunnelRadiusCm:number;chamberRadiusCm:number;tunnelChance:number;chamberChance:number;minYcm:number;maxYcm:number};
 stamps:readonly CaveStamp[];entrances:readonly CaveEntrance[];
}
/** Project only the complete canonical archive v1 emitted by the native owner.
 * Unsafe JSON integers fail closed; this is not a replacement native validator. */
export function parseCaveSnapshot(value:unknown):CaveInspection{
 const s=obj(value,'snapshot');
 if(s.version!==1)throw new Error('Unsupported Caverns snapshot version');
 const cfg=obj(s.settings,'settings');
 const cellCm=int(cfg.cellCm,'cellCm',100,10000);
 const settings={seed:int(cfg.seed,'seed',0),cellCm,
  tunnelRadiusCm:int(cfg.tunnelRadiusCm,'tunnelRadiusCm',1,Math.floor(cellCm/3)),
  chamberRadiusCm:int(cfg.chamberRadiusCm,'chamberRadiusCm',1,Math.floor(cellCm/2)),
  tunnelChance:int(cfg.tunnelChance,'tunnelChance',0,1000),chamberChance:int(cfg.chamberChance,'chamberChance',0,1000),
  minYcm:cm(cfg.minYcm,'minYcm'),maxYcm:cm(cfg.maxYcm,'maxYcm')};
 if(settings.minYcm>=settings.maxYcm)throw new Error('Invalid Caverns depth band');
 const stamps=array(s.stamps,'stamps',16384).map((v):CaveStamp=>{
  const a=obj(v,'stamp');
  if(a.mode!=='carve'&&a.mode!=='fill')throw new Error('Invalid stamp mode');
  return {kind:'stamp',id:int(a.id,'stamp id',1),mode:a.mode,xCm:cm(a.xCm,'stamp xCm'),yCm:cm(a.yCm,'stamp yCm'),zCm:cm(a.zCm,'stamp zCm'),radiusCm:radius(a.radiusCm)};
 });
 const entrances=array(s.entrances,'entrances',4096).map((v):CaveEntrance=>{
  const a=obj(v,'entrance');
  // The archive checks ordering and radius, not the stamp world extent for entrances.
  // Only safely representable coordinates may enter this presentation.
  const surfaceYcm=int(a.surfaceYcm,'surfaceYcm',Number.MIN_SAFE_INTEGER),caveYcm=int(a.caveYcm,'caveYcm',Number.MIN_SAFE_INTEGER);
  if(surfaceYcm<caveYcm)throw new Error('Invalid entrance depth');
  return {kind:'entrance',id:int(a.id,'entrance id',1),xCm:int(a.xCm,'entrance xCm',Number.MIN_SAFE_INTEGER),zCm:int(a.zCm,'entrance zCm',Number.MIN_SAFE_INTEGER),surfaceYcm,caveYcm,radiusCm:radius(a.radiusCm)};
 });
 for(const rows of [stamps,entrances]){
  const seen=new Set<number>();
  for(const row of rows){if(seen.has(row.id))throw new Error('Duplicate '+row.kind+' id');seen.add(row.id);}
  rows.sort((a,b)=>a.id-b.id);
 }
 return {generatorVersion:int(s.generatorVersion,'generatorVersion',1),revision:int(s.sourceRevision,'sourceRevision',1),settings,stamps,entrances};
}
export function inspectCaveReply(value:unknown):CaveInspection{
 const r=obj(value,'Caverns get reply');
 if(r.ok!==true||r.error)throw new Error('Caverns get failed');
 const data=parseCaveSnapshot(r.snapshot);
 if(int(r.revision,'reply revision',1)!==data.revision||int(r.stampCount,'stampCount',0,16384)!==data.stamps.length)
  throw new Error('Caverns revision or stamp count mismatch');
 return data;
}
export const cavePageSize=24;
export type CaveFilter='all'|'carve'|'fill'|'entrance';
export const caveEntryId=(e:CaveEntry)=>e.kind+':'+e.id;
export function projectCavePage(data:CaveInspection,filter:CaveFilter='all',page=0){
 const entries:CaveEntry[]=filter==='entrance'?[...data.entrances]:
  filter==='all'?[...data.stamps,...data.entrances]:data.stamps.filter(e=>e.mode===filter);
 const pages=Math.max(1,Math.ceil(entries.length/cavePageSize));
 const current=int(page,'page',0,pages-1);
 const visible=entries.slice(current*cavePageSize,(current+1)*cavePageSize);
 const node=(id:string,label:string,x:number,y:number,input:boolean,output:boolean):NodeView=>({
  id,label,x,y,inputs:input?[{id:'in',type:'volume',multiple:true}]:[],outputs:output?[{id:'out',type:'volume',multiple:true}]:[]
 });
 const link=(from:string,to:string):Link=>({id:JSON.stringify([from,to]),from,to,input:'in',output:'out'});
 const nodes=[node('settings','Settings · seed '+data.settings.seed,40,40,false,true),
  node('volume','Authored SDF · revision '+data.revision,660,40,true,false)];
 const links=[link('settings','volume')];
 for(const [i,e] of visible.entries()){
  const id=caveEntryId(e);
  nodes.push(node(id,(e.kind==='stamp'?e.mode:'Entrance')+' #'+e.id+' · '+e.radiusCm+' cm',340,40+i*100,false,true));
  links.push(link(id,'volume'));
 }
 const view:GraphView={nodes,links};
 return {view,visible,total:entries.length,pages,page:current};
}
