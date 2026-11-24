import {useEffect,useRef,useState} from 'react';
import {invoke,nativeConnected} from '../bridge';
import {StudioAlert,StudioButton,StudioInput,StudioSection} from '../ui';
import AssetReferencePicker from '../components/AssetReferencePicker';
import GraphCanvas from './GraphCanvas';
import {parseStrataAnalysis,strataReportGraph} from './strataAnalysis';
import type {StrataAnalysis} from './strataAnalysis';

const object=(v:unknown):v is Record<string,unknown>=>!!v&&typeof v==='object'&&!Array.isArray(v);
const op=async(name:string,params:Record<string,unknown>={})=>(await invoke('studio.op',{op:name,params})).result;
export default function StrataAnalysisPanel({projectPath,suggestedModel='',disabled=false}:{projectPath:string;suggestedModel?:string;disabled?:boolean}){
 const [guid,setGuid]=useState(''),[path,setPath]=useState(suggestedModel);
 const [cap,setCap]=useState<Record<string,unknown>|null>(null),[report,setReport]=useState<StrataAnalysis|null>(null);
 const [busy,setBusy]=useState(false),[error,setError]=useState(''),[elapsed,setElapsed]=useState<number|null>(null),[analyzed,setAnalyzed]=useState('');
 const epoch=useRef(0),lock=useRef(false);
 useEffect(()=>{
  const token=++epoch.current;setGuid('');setPath(suggestedModel);setReport(null);setCap(null);setError('');setBusy(false);setElapsed(null);setAnalyzed('');lock.current=false;
  if(projectPath&&nativeConnected())void op('strata_capabilities').then(r=>{
   if(epoch.current!==token)return;
   if(!object(r)||r.ok!==true||typeof r.builderAvailable!=='boolean')throw new Error('Native STRATA capability response unavailable');
   setCap(r);
  }).catch(e=>{if(epoch.current===token)setError(String(e))});
  return ()=>{if(epoch.current===token)epoch.current++};
 },[projectPath,suggestedModel]);
 const clear=()=>{setReport(null);setElapsed(null);setAnalyzed('');setError('')};
 const analyze=async()=>{
  if(lock.current||disabled||cap?.builderAvailable!==true||(!guid&&!path.trim()))return;
  lock.current=true;setBusy(true);clear();
  const token=epoch.current;
  try{
   let model=path.trim();
   if(guid){
    const asset=await invoke('studio.assetByGuid',{guid});
    if(epoch.current!==token)return;
    if(asset.directory)throw new Error('Select a model file');
    model='content/'+asset.path;
   }
   const start=performance.now(),r=await op('strata_model_analyze',{model});
   if(epoch.current!==token)return;
   setReport(parseStrataAnalysis(r));setElapsed(performance.now()-start);setAnalyzed(model);
  }catch(e){if(epoch.current===token)setError(e instanceof Error?e.message:String(e))}
  finally{if(epoch.current===token){setBusy(false);lock.current=false}}
 };
 return <StudioSection title="STRATA CPU import analysis">
  <StudioAlert>Analyzes an existing exported model, not unsaved model-source edits. No cook, GPU upload, rendering change or GPU profiling.</StudioAlert>
  {cap&&<small>{cap.builderAvailable===true?'Native builder available':'Native builder unavailable'} · read budget {String(cap.maxReadBytes)} bytes</small>}
  <AssetReferencePicker label="Model asset" value={guid} kinds={['model']} disabled={disabled||busy} onChange={value=>{setGuid(value);clear()}}/>
  <StudioInput aria-label="Existing project-relative model path" placeholder="models/example.glb" value={path} disabled={disabled||busy||!!guid} onChange={e=>{setPath(e.target.value);clear()}}/>
  <StudioButton size="sm" disabled={disabled||busy||cap?.builderAvailable!==true||(!guid&&!path.trim())} onClick={()=>void analyze()}>Analyze existing model</StudioButton>
  {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
  {report&&<>
   <div style={{height:280,display:'flex',flexDirection:'column'}}><GraphCanvas view={strataReportGraph(report)} selected="" label="STRATA CPU report dependencies" disabled onSelect={()=>{}} onMove={()=>{}}/></div>
   <div className="au-stack"><small>{analyzed}</small>
    <small>Eligible primitives: {report.eligiblePrimitives} · Cluster nodes: {report.clusterNodes} · Meshlets: {report.meshlets}</small>
    <small>Auxiliary CPU index bytes: {report.auxiliaryIndexBytes} · Certified primitives: {report.seamCertifiedPrimitives}</small>
    <small>Seam rejected parents: {report.seamRejectedParents} · Shared boundary segments: {report.sharedBoundarySegments}</small>
    <small>Source seams certified: {report.sourceSeamsCertified?'yes':'no'} · Editor request round trip: {elapsed?.toFixed(1)} ms (not GPU time)</small>
    {report.skipped.map((s,i)=><small key={i}>Skipped mesh {s.mesh}, primitive {s.primitive}: {s.reason}</small>)}
    <small>{report.note}</small>
   </div>
  </>}
 </StudioSection>;
}
