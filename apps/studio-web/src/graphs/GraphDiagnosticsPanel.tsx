import {useState} from 'react';
import {StudioButton,StudioAlert,StudioSection} from '../ui';
import type {GraphView} from './graphFramework';
import {inspectGraph} from './graphDiagnostics';
import type {DiagnosticPolicy,GraphDiagnosticReport} from './graphDiagnostics';

export default function GraphDiagnostics({view,policy}:{view:GraphView;policy:DiagnosticPolicy}){
 const [sample,setSample]=useState<{view:GraphView;report:GraphDiagnosticReport;ms:number}|null>(null);
 const stale=sample!==null&&sample.view!==view;
 const inspect=()=>{
  const start=performance.now(),report=inspectGraph(view,policy);
  setSample({view,report,ms:performance.now()-start});
 };
 return <StudioSection title="Graph diagnostics">
  <StudioButton size="sm" onClick={inspect}>Inspect topology</StudioButton>
  <small>Editor-side topology only. Native compilation remains authoritative. No runtime execution or GPU timings.</small>
  {stale&&<StudioAlert tone="warning">Graph changed; run inspection again.</StudioAlert>}
  {sample&&!stale&&<>
   <small>{sample.report.nodes} nodes · {sample.report.links} links · {sample.report.inputPorts} input ports · {sample.report.outputPorts} output ports</small>
   <small>Editor topology inspection: {sample.ms.toFixed(3)} ms</small>
   <StudioAlert tone={sample.report.issues.length?'warning':'neutral'}>{sample.report.issues.length?sample.report.issues.length+' topology issues':'No issues in this topology projection'}</StudioAlert>
   {sample.report.issues.slice(0,100).map((d,i)=><div key={i}>{d.code}: {d.subject}</div>)}
   {sample.report.issues.length>100&&<small>Only the first 100 issues are displayed.</small>}
  </>}
 </StudioSection>;
}
