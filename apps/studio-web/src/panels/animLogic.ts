/** Animation timeline helpers (authoring model documents, tools/modeling). No React. */
export interface AnimChannel { node: string; path?: string; times?: number[]; interpolation?: string; values?: number[][] }
export interface AnimClip { id: string; channels: AnimChannel[] }
export interface AnimDoc { nodes?: Array<{ id: string; parent?: string }>; clips?: AnimClip[] }
export interface SampledNode { id: string; worldPosition: [number, number, number] }

export function clipDuration(c: AnimClip): number {
  let d = 0;
  for (const ch of c.channels) for (const t of ch.times ?? []) d = Math.max(d, t);
  return d;
}
/** Distinct key times of a clip, sorted (within 1 ms counts as the same key). */
export function clipKeyTimes(c: AnimClip): number[] {
  const all = c.channels.flatMap(ch => ch.times ?? []).sort((a, b) => a - b);
  const out: number[] = [];
  for (const t of all) if (!out.length || t - out[out.length - 1] > 0.001) out.push(t);
  return out;
}
/** Orthographic skeleton drawing: bones between each node and its parent, fitted into width x height. */
export function projectSkeleton(doc: AnimDoc | null, pose: SampledNode[], view: 'front' | 'side', width: number, height: number) {
  const pos = new Map(pose.map(n => [n.id, n.worldPosition]));
  const pts = pose.map(n => n.worldPosition);
  if (!pts.length) return { bones: [] as number[][], joints: [] as Array<[number, number, string]>, ground: height - 10 };
  const hx = (p: [number, number, number]) => view === 'front' ? p[0] : p[2];
  const xs = pts.map(hx), ys = pts.map(p => p[1]);
  const minX = Math.min(...xs), maxX = Math.max(...xs), minY = Math.min(0, ...ys), maxY = Math.max(...ys);
  const span = Math.max(maxX - minX, maxY - minY, 0.001), pad = 16;
  const k = Math.min((width - 2 * pad) / span, (height - 2 * pad) / span);
  const cx = width / 2 - ((minX + maxX) / 2) * k, base = height - pad;
  const to = (p: [number, number, number]): [number, number] => [cx + hx(p) * k, base - (p[1] - minY) * k];
  const bones: number[][] = [];
  for (const n of doc?.nodes ?? []) {
    const a = pos.get(n.id), b = n.parent ? pos.get(n.parent) : undefined;
    if (a && b) { const [x0, y0] = to(a), [x1, y1] = to(b); bones.push([x0, y0, x1, y1]); }
  }
  const joints = pose.map(n => { const [x, y] = to(n.worldPosition); return [x, y, n.id] as [number, number, string]; });
  return { bones, joints, ground: base };
}
