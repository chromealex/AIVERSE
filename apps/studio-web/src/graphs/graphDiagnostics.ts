import {validateTopology} from './graphFramework.ts';
import type {GraphView,GraphPolicy,Diagnostic} from './graphFramework';

export interface DiagnosticPolicy extends GraphPolicy {
 /** Separate cycle domains, e.g. SpectraFX exec vs data. */
 cycleGroups?:readonly string[];
}
export interface GraphDiagnosticReport {nodes:number;links:number;inputPorts:number;outputPorts:number;issues:Diagnostic[]}
export function inspectGraph(view:GraphView,policy:DiagnosticPolicy):GraphDiagnosticReport {
 const issues=validateTopology(view,policy.cycleGroups?{...policy,cycles:'allow'}:policy);
 if(policy.cycleGroups&&policy.cycles==='reject'){
  const byId=new Map(view.nodes.map(n=>[n.id,n]));
  for(const type of policy.cycleGroups){
   const links=view.links.filter(l=>byId.get(l.from)?.outputs.find(p=>p.id===l.output)?.type===type&&byId.get(l.to)?.inputs.find(p=>p.id===l.input)?.type===type);
   if(validateTopology({...view,links},{...policy,cycles:'reject'}).some(d=>d.code==='cycle'))issues.push({code:'cycle',subject:type});
  }
 }
 return {nodes:view.nodes.length,links:view.links.length,inputPorts:view.nodes.reduce((a,n)=>a+n.inputs.length,0),
  outputPorts:view.nodes.reduce((a,n)=>a+n.outputs.length,0),issues};
}
