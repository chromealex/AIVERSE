/** Request provenance only; native Building Generator owns evaluation. */
type SeedRequest={seed:number};
const cloneWire=<R extends SeedRequest>(request:R):R=>{
 const copy=JSON.parse(JSON.stringify(request)) as R;
 if(!copy||!Number.isSafeInteger(copy.seed)||copy.seed<0||copy.seed>2147483647)
  throw new Error('Invalid building candidate seed');
 const freeze=(value:unknown):void=>{
  if(value!==null&&typeof value==='object'){
   for(const child of Object.values(value))freeze(child);
   Object.freeze(value);
  }
 };
 freeze(copy);return copy;
};
export const buildingDraftKey=(project:string,request:SeedRequest):string=>JSON.stringify([project,request]);
export interface BuildingCandidateTicket<R extends SeedRequest>{
 readonly id:number;readonly scope:string;readonly requests:readonly R[];
}
/** One pending batch; invalidation never grants an old promise permission to publish. */
export class BuildingCandidateGate<R extends SeedRequest>{
 private serial=0;
 private pending:BuildingCandidateTicket<R>|null=null;
 begin(scope:string,requests:readonly R[]):BuildingCandidateTicket<R>{
  this.invalidate();
  if(!scope||requests.length<1||requests.length>3)throw new Error('Invalid building candidate batch');
  const copies=requests.map(cloneWire);
  if(new Set(copies.map(r=>r.seed)).size!==copies.length)throw new Error('Duplicate candidate seed');
  const ticket=Object.freeze({id:++this.serial,scope,requests:Object.freeze(copies)});
  this.pending=ticket;return ticket;
 }
 isCurrent(ticket:BuildingCandidateTicket<R>){return this.pending===ticket;}
 finish(ticket:BuildingCandidateTicket<R>){
  if(!this.isCurrent(ticket))return false;
  this.pending=null;return true;
 }
 invalidate(){const hadPending=this.pending!==null;this.pending=null;return hadPending;}
}
