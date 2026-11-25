/** Hierarchy search: free text plus Unity-like filters t:<kind> owner:<n> is:hidden|locked|selected. */
import type { StudioEntity } from './types.ts';

export interface HierarchyFilter { text: string[]; kinds: string[]; owners: number[]; is: string[] }
export function parseHierarchyFilter(query: string): HierarchyFilter {
  const out: HierarchyFilter = { text: [], kinds: [], owners: [], is: [] };
  for (const token of query.trim().toLowerCase().split(/\s+/).filter(Boolean)) {
    const m = /^(t|type|owner|is):(.*)$/.exec(token);
    if (!m || !m[2]) { out.text.push(token); continue; }
    if (m[1] === 't' || m[1] === 'type') out.kinds.push(m[2]);
    else if (m[1] === 'owner') { const n = Number(m[2]); if (Number.isInteger(n)) out.owners.push(n); else out.text.push(token); }
    else out.is.push(m[2]);
  }
  return out;
}
export function matchesHierarchy(node: StudioEntity, filter: HierarchyFilter, selected: ReadonlySet<string> = new Set()): boolean {
  const name = (node.name + ' ' + node.kind).toLowerCase();
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
type ObjectRow = {node:StudioEntity; depth:number};
type GroupRow = {key:string; label:string; depth:number; members:string[]; collapsed:boolean};
export function groupedHierarchy(rows:ObjectRow[], mode:HierarchyGrouping, collapsed:ReadonlySet<string>, paths?:Record<string,string[]>):(ObjectRow|GroupRow)[] {
  if(mode==='scene') return rows;
  const display:(ObjectRow|GroupRow)[]=[];
  function append(members:ObjectRow[], path:string[], byOwner:boolean) {
    const buckets=new Map<string,ObjectRow[]>();
    const projectGroup=mode.startsWith('project:');
    for(const row of members) {
      const owner=row.node.owner;
      const label=projectGroup ? paths?.[row.node.id]?.[path.length] || 'Unassigned' : byOwner ? owner===undefined?'No owner':owner===0?'Neutral':owner===-1?'Hostile':'Player '+owner
        : row.node.kind || 'Other';
      const bucket=buckets.get(label);
      if(bucket) bucket.push(row); else buckets.set(label,[row]);
    }
    for(const [label,items] of buckets) {
      const keys=[...path,label],key=JSON.stringify(keys);
      display.push({key,label,depth:path.length,members:items.map(r=>r.node.id),collapsed:collapsed.has(key)});
      if(collapsed.has(key)) continue;
      if(byOwner || (projectGroup && items.some(r=>(paths?.[r.node.id]?.length??0)>keys.length))) append(items,keys,false);
      else for(const {node} of items) display.push({node,depth:keys.length});
    }
  }
  append(rows,[],mode==='owner-type');
  return display;
}
