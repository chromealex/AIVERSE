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
type CaveIdentity=number|string;
type NativeIdentity={revision:string;seed:string;generator:string;stamps:string[];entrances:string[]};
const decimal=(v:unknown,label:string,positive=true):string=>{
 if(typeof v!=='string'||!/^(0|[1-9][0-9]{0,19})$/.test(v)||(positive&&v==='0')||
    (v.length>19||(v.length===19&&v>'9223372036854775807')))throw new Error('Invalid '+label);
 return v;
};
const exact=(value:unknown,label:string,positive=true,wire?:string):CaveIdentity=>{
 if(wire===undefined)return int(value,label,positive?1:0);
 const id=decimal(wire,label,positive);
 // The text field is authoritative. Validate agreement with the numeric JSON
 // mirror, but never round an unsafe identity back into a native identifier.
 if(typeof value!=='number'||!Number.isFinite(value)||!Number.isInteger(value)||Number(id)!==value)
  throw new Error('Mismatched '+label);
 return id;
};
const compareIdentity=(a:CaveIdentity,b:CaveIdentity)=>{
 const x=String(a),y=String(b);return x.length-y.length||(x<y?-1:x>y?1:0);
};
export type CavePlanetAnchor={version:1;body:string;face:number;aQ16:number;bQ16:number;halfExtentCm:number};
export type CavePlanetRegion={id:string;anchor:CavePlanetAnchor;revision:string};
export type CavePlanetContext={activeRegionId:string;anchor:CavePlanetAnchor;regions:readonly CavePlanetRegion[]};
const parseAnchor=(value:unknown):CavePlanetAnchor=>{
 const a=obj(value,'planet anchor');
 if(a.version!==1||typeof a.body!=='string'||!a.body||a.body.length>128)throw new Error('Invalid planet anchor');
 return {version:1,body:a.body,face:int(a.face,'cube face',0,5),aQ16:int(a.aQ16,'chart A',0,512*65536),
  bQ16:int(a.bQ16,'chart B',0,512*65536),halfExtentCm:int(a.halfExtentCm,'chart extent',100,15000)};
};
export type CaveStamp={id:CaveIdentity;kind:'stamp';mode:'carve'|'fill';xCm:number;yCm:number;zCm:number;radiusCm:number};
export type CaveEntrance={id:CaveIdentity;kind:'entrance';xCm:number;zCm:number;surfaceYcm:number;caveYcm:number;radiusCm:number};
export type CaveEntry=CaveStamp|CaveEntrance;
export interface CaveInspection {
 generatorVersion:CaveIdentity;revision:CaveIdentity;planet?:CavePlanetContext;
 settings:{seed:CaveIdentity;cellCm:number;tunnelRadiusCm:number;chamberRadiusCm:number;tunnelChance:number;chamberChance:number;minYcm:number;maxYcm:number};
 stamps:readonly CaveStamp[];entrances:readonly CaveEntrance[];
}
/** Project only the complete canonical archive v1 emitted by the native owner.
 * Unsafe JSON integers fail closed; this is not a replacement native validator. */
export function parseCaveSnapshot(value:unknown,identity?:NativeIdentity):CaveInspection{
 const s=obj(value,'snapshot');
 if(s.version!==1)throw new Error('Unsupported Caverns snapshot version');
 const cfg=obj(s.settings,'settings');
 const cellCm=int(cfg.cellCm,'cellCm',100,10000);
 const settings={seed:exact(cfg.seed,'seed',false,identity?.seed),cellCm,
  tunnelRadiusCm:int(cfg.tunnelRadiusCm,'tunnelRadiusCm',1,Math.floor(cellCm/3)),
  chamberRadiusCm:int(cfg.chamberRadiusCm,'chamberRadiusCm',1,Math.floor(cellCm/2)),
  tunnelChance:int(cfg.tunnelChance,'tunnelChance',0,1000),chamberChance:int(cfg.chamberChance,'chamberChance',0,1000),
  minYcm:cm(cfg.minYcm,'minYcm'),maxYcm:cm(cfg.maxYcm,'maxYcm')};
 if(settings.minYcm>=settings.maxYcm)throw new Error('Invalid Caverns depth band');
 const stampRows=array(s.stamps,'stamps',16384),entranceRows=array(s.entrances,'entrances',4096);
 if(identity&&(identity.stamps.length!==stampRows.length||identity.entrances.length!==entranceRows.length))throw new Error('Caverns identity count mismatch');
 const stamps=stampRows.map((v,i):CaveStamp=>{
  const a=obj(v,'stamp');
  if(a.mode!=='carve'&&a.mode!=='fill')throw new Error('Invalid stamp mode');
  return {kind:'stamp',id:exact(a.id,'stamp id',true,identity?.stamps[i]),mode:a.mode,xCm:cm(a.xCm,'stamp xCm'),yCm:cm(a.yCm,'stamp yCm'),zCm:cm(a.zCm,'stamp zCm'),radiusCm:radius(a.radiusCm)};
 });
 const entrances=entranceRows.map((v,i):CaveEntrance=>{
  const a=obj(v,'entrance');
  // The archive checks ordering and radius, not the stamp world extent for entrances.
  // Only safely representable coordinates may enter this presentation.
  const surfaceYcm=int(a.surfaceYcm,'surfaceYcm',Number.MIN_SAFE_INTEGER),caveYcm=int(a.caveYcm,'caveYcm',Number.MIN_SAFE_INTEGER);
  if(surfaceYcm<caveYcm)throw new Error('Invalid entrance depth');
  return {kind:'entrance',id:exact(a.id,'entrance id',true,identity?.entrances[i]),xCm:int(a.xCm,'entrance xCm',Number.MIN_SAFE_INTEGER),zCm:int(a.zCm,'entrance zCm',Number.MIN_SAFE_INTEGER),surfaceYcm,caveYcm,radiusCm:radius(a.radiusCm)};
 });
 for(const rows of [stamps,entrances]){
  const seen=new Set<string>();
  for(const row of rows){const key=String(row.id);if(seen.has(key))throw new Error('Duplicate '+row.kind+' id');seen.add(key);}
  rows.sort((a,b)=>compareIdentity(a.id,b.id));
 }
 return {generatorVersion:exact(s.generatorVersion,'generatorVersion',true,identity?.generator),
  revision:exact(s.sourceRevision,'sourceRevision',true,identity?.revision),settings,stamps,entrances};
}
export function inspectCaveReply(value:unknown):CaveInspection{
 const r=obj(value,'Caverns get reply');
 if(r.ok!==true||r.error||('changed' in r&&r.changed!==false))throw new Error('Caverns get failed or was not read-only');
 const hasIdentity=['revisionText','seedText','generatorVersionText','stampIds','entranceIds'].some(k=>k in r);
 const identity:NativeIdentity|undefined=hasIdentity?{
  revision:decimal(r.revisionText,'revisionText'),seed:decimal(r.seedText,'seedText',false),
  generator:decimal(r.generatorVersionText,'generatorVersionText'),
  stamps:array(r.stampIds,'stampIds',16384).map(v=>decimal(v,'stamp ID')),
  entrances:array(r.entranceIds,'entranceIds',4096).map(v=>decimal(v,'entrance ID'))
 }:undefined;
 const data=parseCaveSnapshot(r.snapshot,identity);
 if(exact(r.revision,'reply revision',true,identity?.revision)!==data.revision||int(r.stampCount,'stampCount',0,16384)!==data.stamps.length)
  throw new Error('Caverns revision or stamp count mismatch');
 if(r.planetary===true){
  if(!identity)throw new Error('Planetary Caverns requires exact native identity');
  const activeRegionId=decimal(r.activeRegionId,'active region ID'),anchor=parseAnchor(r.planetAnchor);
  const regions=array(r.planetRegions,'planet regions',32).map((v):CavePlanetRegion=>{
   const region=obj(v,'planet region');
   return {id:decimal(region.id,'region ID'),anchor:parseAnchor(region.anchor),revision:decimal(region.revisionText,'region revision')};
  });
  if(new Set(regions.map(region=>region.id)).size!==regions.length)throw new Error('Duplicate planetary region');
  const active=regions.find(region=>region.id===activeRegionId);
  if(!active||active.revision!==data.revision||JSON.stringify(active.anchor)!==JSON.stringify(anchor))throw new Error('Active planetary region identity mismatch');
  // Match the native chart's geometric limits. A region-local centre is
  // insufficient: the complete sphere and both shaft ends must fit.
  const cap=anchor.halfExtentCm;
  const inside=(x:number,y:number,z:number,radius:number)=>radius<=cap&&
   Math.abs(x)<=cap-radius&&Math.abs(y)<=cap-radius&&Math.abs(z)<=cap-radius;
  if(data.settings.minYcm< -cap||data.settings.maxYcm>cap||
     data.stamps.some(s=>!inside(s.xCm,s.yCm,s.zCm,s.radiusCm))||
     data.entrances.some(e=>!inside(e.xCm,e.caveYcm,e.zCm,e.radiusCm)||!inside(e.xCm,e.surfaceYcm,e.zCm,e.radiusCm)))
   throw new Error('Caverns planetary snapshot exceeds chart bounds');
  data.planet={activeRegionId,anchor,regions};
 }else if(r.planetAnchor!==undefined||r.activeRegionId!==undefined||r.planetRegions!==undefined)throw new Error('Unexpected planetary metadata');
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
  node('volume',(data.planet?'Region '+data.planet.activeRegionId+' · ':'')+'Authored SDF · revision '+data.revision,660,40,true,false)];
 const links=[link('settings','volume')];
 for(const [i,e] of visible.entries()){
  const id=caveEntryId(e);
  nodes.push(node(id,(e.kind==='stamp'?e.mode:'Entrance')+' #'+e.id+' · '+e.radiusCm+' cm',340,40+i*100,false,true));
  links.push(link(id,'volume'));
 }
 const view:GraphView={nodes,links};
 return {view,visible,total:entries.length,pages,page:current};
}
