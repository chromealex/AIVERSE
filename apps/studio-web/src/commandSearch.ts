/** Command palette search: fuzzy subsequence match with word-start / prefix bonuses. */
export type CommandKind = 'command' | 'window' | 'workspace' | 'entity' | 'asset';
export interface CommandEntry {
  id: string; kind: CommandKind; title: string; hint?: string; keywords?: string;
  shortcut?: string; disabled?: boolean; legacy?: boolean; run: () => void;
}
export const KIND_ORDER: CommandKind[] = ['command', 'workspace', 'window', 'entity', 'asset'];
export const KIND_TITLES: Record<CommandKind, string> = {
  command: 'Commands', workspace: 'Workspaces', window: 'Windows', entity: 'Scene', asset: 'Assets'
};
/** Prefix filters like VS Code / Unity search: ">" commands, "@" entities, "#" assets. */
export function parseQuery(raw: string): { text: string; only: CommandKind[] | null } {
  const q = raw.trimStart();
  if (q.startsWith('>')) return { text: q.slice(1).trim(), only: ['command', 'workspace', 'window'] };
  if (q.startsWith('@')) return { text: q.slice(1).trim(), only: ['entity'] };
  if (q.startsWith('#')) return { text: q.slice(1).trim(), only: ['asset'] };
  return { text: q.trim(), only: null };
}
/** Score of `query` in `text`; -1 when not all characters appear in order. */
export function fuzzyScore(query: string, text: string): number {
  const q = query.toLowerCase(), t = text.toLowerCase();
  if (!q) return 0;
  const direct = t.indexOf(q);
  if (direct >= 0) {
    const wordStart = direct === 0 || /[\s_./:\-›(]/.test(t[direct - 1]);
    return 1000 - direct + (wordStart ? 300 : 0) + (t.length === q.length ? 200 : 0);
  }
  let score = 0, ti = 0, streak = 0;
  for (const ch of q) {
    const found = t.indexOf(ch, ti);
    if (found < 0) return -1;
    const wordStart = found === 0 || /[\s_./:\-›(]/.test(t[found - 1]);
    streak = found === ti ? streak + 1 : 0;
    score += 10 + streak * 5 + (wordStart ? 15 : 0) - Math.min(found - ti, 10);
    ti = found + 1;
  }
  return score;
}
export interface CommandGroup { kind: CommandKind; title: string; items: CommandEntry[] }
/** Groups ranked matches by kind; `recent` ids float to the top when the query is empty. */
export function searchCommands(entries: CommandEntry[], raw: string, recent: string[] = [], perGroup = 8): CommandGroup[] {
  const { text, only } = parseQuery(raw);
  const scored = entries
    .filter(e => !only || only.includes(e.kind))
    .map(e => {
      const s = Math.max(fuzzyScore(text, e.title), fuzzyScore(text, e.keywords ?? '') - 50, fuzzyScore(text, e.hint ?? '') - 200);
      const r = recent.indexOf(e.id);
      return { e, ok: s >= 0, s: s + (r >= 0 ? 400 - r * 10 : 0) - (e.disabled ? 150 : 0) };
    })
    .filter(x => x.ok && (text || x.e.kind !== 'entity' && x.e.kind !== 'asset' || recent.includes(x.e.id)));
  return KIND_ORDER.map(kind => ({ kind, title: KIND_TITLES[kind],
    items: scored.filter(x => x.e.kind === kind).sort((a, b) => b.s - a.s || a.e.title.localeCompare(b.e.title))
      .slice(0, perGroup).map(x => x.e) })).filter(g => g.items.length > 0);
}
