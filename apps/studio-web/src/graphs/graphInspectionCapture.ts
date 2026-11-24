import type {StudioGraphDocumentScope,StudioCaveGraphCapture} from '../types';

type Scope=StudioGraphDocumentScope;
const record=(value:unknown):Record<string,unknown>|null=>
 value!==null&&typeof value==='object'&&!Array.isArray(value)?value as Record<string,unknown>:null;
const decimal=(value:unknown,positive:boolean):value is string=>
 typeof value==='string'&&/^(0|[1-9][0-9]{0,19})$/.test(value)&&
 (!positive||value!=='0')&&(value.length<20||value<='18446744073709551615');
export function parseGraphDocumentScope(value:unknown):Scope{
 const s=record(value);
 if(!s||typeof s.projectPath!=='string'||!s.projectPath||typeof s.mapPath!=='string'||
    !decimal(s.epoch,true)||!decimal(s.revision,false))throw new Error('Native graph document identity unavailable');
 return {projectPath:s.projectPath,mapPath:s.mapPath,epoch:s.epoch,revision:s.revision};
}
export const graphScopeKey=(scope:Scope)=>JSON.stringify([scope.projectPath,scope.mapPath,scope.epoch,scope.revision]);
export type GraphCaptureTicket={readonly scope:Readonly<Scope>};
/** One in-flight publication ticket. A later request, document event or unmount retires it. */
export class GraphInspectionGate{
 private pending:GraphCaptureTicket|null=null;
 begin(scope:Scope):GraphCaptureTicket{
  const ticket=Object.freeze({scope:Object.freeze(parseGraphDocumentScope(scope))});
  this.pending=ticket;return ticket;
 }
 isCurrent(ticket:GraphCaptureTicket){return this.pending===ticket;}
 finish(ticket:GraphCaptureTicket){if(this.isCurrent(ticket))this.pending=null;}
 invalidate(){this.pending=null;}
}
export function validateCaveCapture(expected:Scope,value:unknown):StudioCaveGraphCapture{
 const r=record(value);
 if(!r)throw new Error('Invalid native Caverns response');
 const scope=parseGraphDocumentScope(r.scope);
 if(graphScopeKey(expected)!==graphScopeKey(scope))throw new Error('Caverns response belongs to a different document revision');
 if(r.status==='ready'){
  if(!record(r.report))throw new Error('Missing native Caverns report');
  return {scope,status:r.status,report:r.report};
 }
 if(r.status==='unavailable'||r.status==='unsupported'||r.status==='absent'){
  if('report' in r)throw new Error('Unexpected Caverns report');
  return {scope,status:r.status};
 }
 throw new Error('Unknown Caverns capture status');
}
