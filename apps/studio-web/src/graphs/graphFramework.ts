/** Shared authoring projection only. Domain adapters own storage and evaluation. */
export interface Port { id: string; type: string; multiple?: boolean }
export interface NodeView { id: string; label: string; x: number; y: number; inputs: readonly Port[]; outputs: readonly Port[] }
export interface Link { id: string; from: string; output: string; to: string; input: string }
export interface GraphView { nodes: readonly NodeView[]; links: readonly Link[] }
export interface GraphPolicy { cycles: 'allow' | 'reject'; compatible?: (output: Port, input: Port) => boolean }
export interface Diagnostic { code: string; subject: string }
export interface GraphAdapter<T> {
 readonly domain: string;
 readonly policy: GraphPolicy;
 project(document: T): GraphView;
 move(document: T, node: string, x: number, y: number): T;
 /** Native validation and mutation must be supplied by the domain host. */
}
export class GraphRegistry<T> {
 private readonly adapters = new Map<string, GraphAdapter<T>>();
 register(adapter: GraphAdapter<T>): void {
  if (!adapter.domain.trim() || this.adapters.has(adapter.domain)) throw new Error('Duplicate or empty graph domain');
  this.adapters.set(adapter.domain, adapter);
 }
 get(domain: string): GraphAdapter<T> | undefined { return this.adapters.get(domain); }
 domains(): string[] { return [...this.adapters.keys()].sort(); }
}

export function validateTopology(view: GraphView, policy: GraphPolicy): Diagnostic[] {
 const issues: Diagnostic[] = [];
 const nodes = new Map<string, NodeView>();
 const ports = (list: readonly Port[], node: string) => {
  const seen = new Set<string>();
  for (const port of list) {
   if (!port.id || !port.type || seen.has(port.id)) issues.push({code:'invalid-port',subject:node+':'+port.id});
   seen.add(port.id);
  }
 };
 for (const n of view.nodes) {
  if (!n.id || nodes.has(n.id)) issues.push({code:'duplicate-node',subject:n.id});
  else nodes.set(n.id,n);
  if (!Number.isFinite(n.x)||!Number.isFinite(n.y)) issues.push({code:'invalid-position',subject:n.id});
  ports(n.inputs,n.id); ports(n.outputs,n.id);
 }
 const used = new Map<string, Set<string>>();
 const links = new Set<string>();
 const indegree = new Map([...nodes.keys()].map(id=>[id,0]));
 const next = new Map([...nodes.keys()].map(id=>[id,[] as string[]]));
 for (const e of view.links) {
  if (!e.id || links.has(e.id)) issues.push({code:'duplicate-link',subject:e.id});
  links.add(e.id);
  const from = nodes.get(e.from)?.outputs.find(p=>p.id===e.output);
  const to = nodes.get(e.to)?.inputs.find(p=>p.id===e.input);
  if (!from || !to) { issues.push({code:'dangling-link',subject:e.id}); continue; }
  if (!(policy.compatible ? policy.compatible(from,to) : from.type===to.type)) issues.push({code:'port-type',subject:e.id});
  const connected=used.get(e.to)??new Set<string>();
  if (!to.multiple && connected.has(e.input)) issues.push({code:'input-cardinality',subject:e.id});
  connected.add(e.input); used.set(e.to,connected);
  next.get(e.from)!.push(e.to); indegree.set(e.to,indegree.get(e.to)!+1);
 }
 if (policy.cycles==='reject') {
  const queue=[...indegree].filter(([,n])=>n===0).map(([id])=>id);
  for(let i=0;i<queue.length;i++) for(const id of next.get(queue[i])??[]) {
   const n=indegree.get(id)!-1; indegree.set(id,n); if(n===0)queue.push(id);
  }
  if(queue.length!==nodes.size)issues.push({code:'cycle',subject:'graph'});
 }
 return issues;
}

/** Bounded immutable draft history; native commit/history stays with the adapter. */
export class DraftHistory<T> {
 private past:T[]=[]; private future:T[]=[];
 private value:T; private readonly limit:number;
 constructor(value:T,limit=64) {
  if(!Number.isSafeInteger(limit)||limit<1)throw new Error('Invalid history limit');
  this.value=value;this.limit=limit;
 }
 current():T{return this.value;}
 canUndo():boolean{return this.past.length>0;}
 canRedo():boolean{return this.future.length>0;}
 push(next:T):T {
  if(Object.is(next,this.value))return this.value;
  this.past.push(this.value);
  if(this.past.length>this.limit)this.past.shift();
  this.value=next;this.future=[];return this.value;
 }
 undo():T {
  if(this.past.length){this.future.push(this.value);this.value=this.past.pop()!;}
  return this.value;
 }
 redo():T {
  if(this.future.length){this.past.push(this.value);this.value=this.future.pop()!;}
  return this.value;
 }
}
