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
