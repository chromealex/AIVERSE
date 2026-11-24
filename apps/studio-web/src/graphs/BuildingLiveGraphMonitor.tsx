import {useEffect,useMemo,useState} from 'react';
import {invoke,nativeConnected} from '../bridge';
import {StudioAlert,StudioButton,StudioInput,StudioSection,StudioSelect} from '../ui';
import GraphCanvas from './GraphCanvas';
import type {GraphView} from './graphFramework';
import {buildingGraphOwners} from './buildingGeometryGraph';

type Obj=Record<string,unknown>;
const object=(v:unknown):v is Obj=>!!v&&typeof v==='object'&&!Array.isArray(v);
const mask=(v:unknown):v is number=>typeof v==='number'&&Number.isInteger(v)&&v>=0&&v<=255;
const uint=(v:unknown):v is string=>typeof v==='string'&&/^(0|[1-9][0-9]{0,19})$/.test(v);
const point=(v:unknown):v is number[]=>Array.isArray(v)&&v.length===3&&v.every(n=>typeof n==='number'&&Number.isFinite(n));
const kinds=['Created','Removed','Geometry changed','Aperture changed','Access changed','Moved','Structure changed'];
function parse(value:unknown,id:string):Obj{
 if(!object(value)||value.version!==1||value.sceneId!==id||!uint(value.physicalRevision)||!uint(value.attempts))throw Error('Invalid native graph snapshot');
 for(const key of ['registeredDomains','requestedDomains','pendingDomains','acknowledgedDomains','missingOwners','refusedDomains','configuredDomains'])if(!mask(value[key]))throw Error('Invalid domain mask');
 const requested=Number(value.requestedDomains),pending=Number(value.pendingDomains),acked=Number(value.acknowledgedDomains);
 if((pending&acked)!==0||(pending|acked)!==requested)throw Error('Inconsistent ACK state');
 if(value.lastChange!==undefined){
  const c=value.lastChange;
  if(!object(c)||c.sceneId!==id||!uint(c.revision)||!point(c.min)||!point(c.max)||c.min.some((n,i)=>n>(c.max as number[])[i]))throw Error('Invalid committed bounds');
 }
 return value;
}
export function BuildingLiveGraphMonitor(){
 const [scene,setScene]=useState('1'),[active,setActive]=useState(''),[snapshot,setSnapshot]=useState<Obj|null>(null),[error,setError]=useState(''),[selected,setSelected]=useState('');
 const [domainFilter,setDomainFilter]=useState('all'),[kindFilter,setKindFilter]=useState('all'),[overlay,setOverlay]=useState(false);
 useEffect(()=>{
  if(!active)return;
  let alive=true,timer:ReturnType<typeof setTimeout>|undefined;
  const poll=async()=>{
   try{
    const reply=await invoke('studio.op',{op:'building_graph_diagnostics',params:{sceneId:active}});
    if(!alive)return;
    const result=reply.result;
    if(!object(result)||result.available!==true){setSnapshot(null);setError(object(result)?String(result.reason??'No runtime data'):'No runtime data');}
    else{setSnapshot(parse(result.snapshot,active));setError('');}
   }catch(e){if(alive){setSnapshot(null);setError(String(e));}}
   finally{if(alive)timer=setTimeout(()=>void poll(),1000);}
  };
  void poll();return()=>{alive=false;if(timer!==undefined)clearTimeout(timer)};
 },[active]);
 const view=useMemo<GraphView>(()=>{
  if(!snapshot)return {nodes:[],links:[]};
  const state=(i:number)=>{const bit=1<<i;
   if(Number(snapshot.missingOwners)&bit)return 'missing';
   if(Number(snapshot.refusedDomains)&bit)return 'refused';
   if(Number(snapshot.pendingDomains)&bit)return 'pending';
   if(Number(snapshot.acknowledgedDomains)&bit)return 'ACK';
   return Number(snapshot.registeredDomains)&bit?'registered':'inactive';
  };
  return {nodes:[{id:'router',label:'Committed invalidation',x:25,y:35,inputs:[],outputs:[{id:'out',type:'domain',multiple:true}]},...buildingGraphOwners.map((o,i)=>({id:o.id,label:o.label+' · '+state(i),x:250,y:35+i*90,inputs:[{id:'in',type:'domain'}],outputs:[]}))],links:buildingGraphOwners.flatMap((o,i)=>Number(snapshot.requestedDomains)&(1<<i)?[{id:o.id,from:'router',output:'out',to:o.id,input:'in'}]:[])};
 },[snapshot]);
 const change=object(snapshot?.lastChange)?snapshot.lastChange:null;
 const history=Array.isArray(snapshot?.history)?snapshot.history.slice(-32).filter(object).filter(h=>{
  const c=h.lastChange;
  return (domainFilter==='all'||(Number(h.requestedDomains)&Number(domainFilter))!==0)&&
    (kindFilter==='all'||(object(c)&&String(c.kind)===kindFilter));
 }):[];
 const timings=Array.isArray(snapshot?.callbackTimings)?snapshot.callbackTimings.filter(object).filter(t=>mask(t.domain)&&uint(t.calls)&&uint(t.lastNs)&&uint(t.totalNs)&&uint(t.maxNs)):[];
 const millis=(v:unknown)=>(Number(v)/1e6).toFixed(3)+' ms';
 const toggleOverlay=async()=>{
  try{const r=await invoke('studio.buildingGraphOverlay',{sceneId:overlay?'':active});setOverlay(r.active);}
  catch(e){setError(String(e))}
 };
 useEffect(()=>{if(!overlay)return;return()=>{void invoke('studio.buildingGraphOverlay',{sceneId:''}).catch(()=>{})}},[overlay]);
 const selectedOwner=buildingGraphOwners.find(o=>o.id===selected);
 return <StudioSection title="Live graph monitor">
  <div className="au-row">
   <StudioInput aria-label="Runtime scene ID" value={scene} onChange={e=>setScene(e.target.value)} disabled={!!active}/>
   <StudioButton size="sm" disabled={!active&&(!nativeConnected()||!(/^[1-9][0-9]{0,18}$/.test(scene)))} onClick={()=>{setSnapshot(null);setError('');setOverlay(false);setActive(active?'':scene)}}>{active?'Stop monitor':'Start monitor'}</StudioButton>
  </div>
  <small>Reads published owner snapshots once per second. No simulation commands or project changes.</small>
  {error&&<StudioAlert tone="warning">{error}</StudioAlert>}
  {snapshot&&<>
   <small>Scene {active} · physical revision {String(snapshot.physicalRevision)} · attempts {String(snapshot.attempts)}</small>
   <div style={{height:320,display:'flex',flexDirection:'column',minWidth:0}}><GraphCanvas view={view} label="Live graph owner ACK states" selected={selected} readOnly onSelect={setSelected} onMove={()=>{}}/></div>
   {selectedOwner&&<small>{selectedOwner.detail}</small>}
   {timings.length>0&&<div className="au-stack"><strong>Measured invalidation callback duration</strong>{timings.map(t=><small key={String(t.domain)}>{buildingGraphOwners.find((_,i)=>(1<<i)===t.domain)?.label??String(t.domain)} · calls {String(t.calls)} · last {millis(t.lastNs)} · total {millis(t.totalNs)} · max {millis(t.maxNs)}</small>)}</div>}
   {change&&<div className="au-stack"><strong>Last committed change · revision {String(change.revision)}</strong><small>World bounds: {String(change.min)} → {String(change.max)}</small><small>Kind {kinds[Number(change.kind)]??String(change.kind)} · feature {String(change.featureIndex)} · open {String(change.open)}</small><StudioButton size="sm" onClick={()=>void toggleOverlay()}>{overlay?'Hide bounds in Scene':'Show bounds in Scene'}</StudioButton><small>World-space AABB in authoring Scene; runtime and authoring coordinates must correspond.</small></div>}
   <div className="au-row">
    <StudioSelect aria-label="History domain filter" value={domainFilter} onChange={e=>setDomainFilter(e.target.value)}><option value="all">All domains</option>{buildingGraphOwners.map((o,i)=><option key={o.id} value={String(1<<i)}>{o.label}</option>)}</StudioSelect>
    <StudioSelect aria-label="History change filter" value={kindFilter} onChange={e=>setKindFilter(e.target.value)}><option value="all">All changes</option>{kinds.map((k,i)=><option key={k} value={String(i)}>{k}</option>)}</StudioSelect>
   </div>
   <details><summary>Published history ({history.length})</summary>{history.map((h,i)=><div key={i}><small>Revision {String(h.physicalRevision)} · pending {String(h.pendingDomains)} · ACK {String(h.acknowledgedDomains)} · attempts {String(h.attempts)}</small></div>)}</details>
   <StudioAlert>ACK means invalidation accepted. Measured callback time is not asynchronous rebuild duration. Actual affected cells, clusters and traversal are not reported.</StudioAlert>
  </>}
 </StudioSection>;
}
