/** Hierarchy search: free text plus Unity-like filters t:<kind> owner:<n> is:hidden|locked|selected. */
import type { StudioEntity, StudioHierarchyPaths } from './types.ts';

export interface HierarchyFilter { text: string[]; kinds: string[]; owners: number[]; playerTerms?: string[]; ids?: string[]; is: string[] }
export function parseHierarchyFilter(query: string): HierarchyFilter {
  const out: HierarchyFilter = { text: [], kinds: [], owners: [], is: [] };
  for (const token of query.trim().toLowerCase().split(/\s+/).filter(Boolean)) {
    if (token.startsWith('#') && token.length > 1) { (out.ids ??= []).push(token.slice(1)); continue; }
    const m = /^(t|type|owner|p|id|is):(.*)$/.exec(token);
    if (!m || !m[2]) { out.text.push(token); continue; }
    if (m[1] === 't' || m[1] === 'type') out.kinds.push(m[2]);
    else if (m[1] === 'p') (out.playerTerms ??= []).push(m[2]);
    else if (m[1] === 'id') (out.ids ??= []).push(m[2]);
    else if (m[1] === 'owner') { const n = Number(m[2]); if (Number.isInteger(n)) out.owners.push(n); else out.text.push(token); }
    else out.is.push(m[2]);
  }
  return out;
}
export function matchesHierarchy(node: StudioEntity, filter: HierarchyFilter, selected: ReadonlySet<string> = new Set(), playerNames: readonly string[] = []): boolean {
  const labelPrefix = node.kind + ' #';
  const mapId = (node.id.startsWith('entity:@') ? node.id.slice(8)
    : node.id.startsWith('entity:') && node.name.startsWith(labelPrefix) ? node.name.slice(labelPrefix.length) : '').toLowerCase();
  const name = (node.name + ' ' + node.kind + ' ' + mapId).toLowerCase();
  if (filter.ids && !filter.ids.every(id => mapId.includes(id))) return false;
  if (filter.playerTerms) {
    if (node.owner === undefined) return false;
    const ownerName = node.owner === 0 ? 'neutral' : node.owner === -1 ? 'hostile' : playerNames[node.owner - 1] ?? '';
    if (!filter.playerTerms.every(term => term === String(node.owner) || ownerName.toLowerCase().includes(term))) return false;
  }
  if (!filter.text.every(t => name.includes(t))) return false;
  if (filter.kinds.length && !filter.kinds.some(k => node.kind.toLowerCase().includes(k))) return false;
  if (filter.owners.length && !filter.owners.includes(node.owner ?? Number.NaN)) return false;
  for (const flag of filter.is) {
    if (flag === 'hidden' && node.visible !== false) return false;
    if (flag === 'locked' && !node.locked) return false;
    if (flag === 'selected' && !selected.has(node.id)) return false;
  }
  return true;
}
/** Ids between two rows (inclusive) in display order, for Shift-click ranges. */
export function rangeIds(order: string[], anchor: string, target: string): string[] {
  const a = order.indexOf(anchor), b = order.indexOf(target);
  if (a < 0 || b < 0) return [target];
  const [lo, hi] = a < b ? [a, b] : [b, a];
  const ids = order.slice(lo, hi + 1);
  // keep the clicked row last so it becomes the primary selection
  return [...ids.filter(id => id !== target), target];
}

export type HierarchyGrouping = 'scene' | 'type' | 'owner-type' | `project:${number}`;
type ObjectRow = {node:StudioEntity; depth:number; occurrence?:string; children?:number};
type GroupRow = {key:string; label:string; depth:number; members:string[]; collapsed:boolean};
export function groupedHierarchy(rows:ObjectRow[], mode:HierarchyGrouping, toggled:ReadonlySet<string>, paths?:StudioHierarchyPaths):(ObjectRow|GroupRow)[] {
  if(mode==='scene') return rows;
  const display:(ObjectRow|GroupRow)[]=[];
  function append(members:ObjectRow[], path:string[], byOwner:boolean) {
    const buckets=new Map<string,ObjectRow[]>();
    const projectGroup=mode.startsWith('project:');
    let componentLevel=false;
    for(const row of members) {
      const owner=row.node.owner;
      const label=projectGroup ? paths?.[row.node.id]?.[path.length] || 'Unassigned' : byOwner ? owner===undefined?'No owner':owner===0?'Neutral':owner===-1?'Hostile':'Player '+owner
        : row.node.kind || 'Other';
      if(Array.isArray(label))componentLevel=true;
      for(const membership of Array.isArray(label) ? label : [label]){
        const bucket=buckets.get(membership);
        if(bucket) bucket.push(row); else buckets.set(membership,[row]);
      }
    }
    for(const [label,items] of buckets) {
      const keys=[...path,label],key=JSON.stringify(keys);
      const collapsed=componentLevel !== toggled.has(key);
      display.push({key,label,depth:path.length,members:items.map(r=>r.node.id),collapsed});
      if(collapsed) continue;
      if(byOwner || (projectGroup && items.some(r=>(paths?.[r.node.id]?.length??0)>keys.length))) append(items,keys,false);
      else for(const {node} of items) display.push({node,depth:keys.length,occurrence:JSON.stringify([...keys,node.id])});
    }
  }
  append(rows,[],mode==='owner-type');
  return display;
}

// The native snapshot supplies cycle-free document links. Filtering promotes a
// child when its parent is absent, so a matching object is always reachable.
export function nestedDocumentRows(rows:ObjectRow[], parents:Record<string,string>, collapsed:ReadonlySet<string>):ObjectRow[] {
  const present=new Set(rows.map(row=>row.node.id));
  const children=new Map<string,ObjectRow[]>();
  for(const row of rows){
    const parent=present.has(parents[row.node.id]) ? parents[row.node.id] : '';
    const siblings=children.get(parent);
    if(siblings)siblings.push(row); else children.set(parent,[row]);
  }
  const stack=(children.get('')??[]).map(row=>({...row,depth:0})).reverse();
  const visible:ObjectRow[]=[];
  while(stack.length){
    const row=stack.pop()!;
    const nested=children.get(row.node.id)??[];
    visible.push({...row,children:nested.length});
    if(!collapsed.has(row.node.id))
      for(let i=nested.length-1;i>=0;i--)stack.push({...nested[i],depth:row.depth+1});
  }
  return visible;
}
