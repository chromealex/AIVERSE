/** Console panel: filtering and clickable references (pure, tested without React). */
import type { StudioLog } from '../types';

export type ConsoleLevel = 'all' | 'info' | 'warning' | 'error';
export interface ConsoleFilter { level: ConsoleLevel; source: string; query: string }
export const logSource = (log: StudioLog) => log.source ?? log.time ?? '';

export function filterLogs(logs: StudioLog[], f: ConsoleFilter): StudioLog[] {
  const q = f.query.trim().toLowerCase();
  return logs.filter(log => (f.level === 'all' || log.level === f.level) &&
    (!f.source || logSource(log) === f.source) &&
    (!q || log.text.toLowerCase().includes(q) || logSource(log).toLowerCase().includes(q)));
}
export function levelCounts(logs: StudioLog[]): Record<'info' | 'warning' | 'error', number> {
  const out = { info: 0, warning: 0, error: 0 };
  for (const log of logs) out[log.level] = (out[log.level] ?? 0) + 1;
  return out;
}
export const logSources = (logs: StudioLog[]) => [...new Set(logs.map(logSource).filter(Boolean))].sort();

/** Parts of a log line: plain text, Studio entity ids (select) and project file paths (reveal). */
export type LogPart = { kind: 'text'; text: string } | { kind: 'entity'; text: string; id: string } | { kind: 'file'; text: string; path: string } | { kind: 'script'; text:string; path:string; line:number };
const REF = /(entity:@?[A-Za-z0-9_.:-]+|(?:maps|content|scripts|ui|materials|lang|fx|rules)\/[^\s'"`,;:()]+\.[A-Za-z0-9]{1,6}|rules\.json|game\.json)/g;
export function logParts(text: string): LogPart[] {
  const out: LogPart[] = [];
  let last = 0;
  for (const m of text.matchAll(REF)) {
    const at = m.index ?? 0;
    if (at > last) out.push({ kind: 'text', text: text.slice(last, at) });
    const ref = m[0];
    if (ref.startsWith('entity:')) out.push({ kind: 'entity', text: ref, id: ref });
    else if (ref.startsWith('scripts/') && ref.endsWith('.lua')) {
      const suffix = text.slice(at + ref.length).match(/^:(\d+)/);
      const label = ref + (suffix?.[0] ?? '');
      out.push({kind:'script', text:label, path:ref, line:Number(suffix?.[1]) || 1});
      last = at + label.length;
      continue;
    } else out.push({ kind: 'file', text: ref, path: ref });
    last = at + ref.length;
  }
  if (last < text.length) out.push({ kind: 'text', text: text.slice(last) });
  return out;
}

export function buildLineLevel(text: string) {
  if (/\berror\s*:|fatal error|\bfailed\b|AIVERSE_MISSING_TOOLCHAIN|ошибка|не удал/iu.test(text)) return 'Errors';
  if (/\bwarning\s*:|предупреждение|внимание/iu.test(text)) return 'Warnings';
  if (/=== OK ===|готово|сборка завершена|установка завершена/iu.test(text)) return 'Success';
  return 'Info';
}
