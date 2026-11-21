/** Pure helpers of the Publish / Build panel (tested without React). */
export type BuildPhase = 'idle' | 'running' | 'ok' | 'failed' | 'missing-toolchain';

export function formatBytes(bytes: unknown): string {
  const n = typeof bytes === 'number' && Number.isFinite(bytes) ? bytes : 0;
  if (n >= 1024 * 1024 * 1024) return (n / 1024 / 1024 / 1024).toFixed(2) + ' GB';
  if (n >= 1024 * 1024) return (n / 1024 / 1024).toFixed(1) + ' MB';
  if (n >= 1024) return Math.round(n / 1024) + ' KB';
  return Math.round(n) + ' B';
}
/** aiversebuild prints `==> step` for every stage: the latest one is the current stage. */
export function buildSteps(log: string): string[] {
  const out: string[] = [];
  for (const line of log.split('\n')) if (line.startsWith('==> ')) out.push(line.slice(4).trim());
  return out;
}
/** `AIVERSE_MISSING_TOOLCHAIN <target>: id, id` (exit code 3): the tools a build stopped on. */
export function missingToolchain(log: string): { target: string; ids: string[] } | null {
  const m = /AIVERSE_MISSING_TOOLCHAIN ([a-z0-9-]+):\s*([^\n]*)/.exec(log);
  if (!m) return null;
  return { target: m[1], ids: m[2].split(/[,\s]+/).map(s => s.trim()).filter(Boolean) };
}
export function buildPhase(running: boolean, runs: number, exitCode: number, log: string): BuildPhase {
  if (running) return 'running';
  if (!runs && !log) return 'idle';
  if (exitCode === 0 && /=== OK ===/.test(log)) return 'ok';
  if (missingToolchain(log)) return 'missing-toolchain';
  return /=== (OK|FAILED) ===/.test(log) || runs ? (exitCode === 0 ? 'ok' : 'failed') : 'idle';
}
/** Error lines worth showing above the log (aiversebuild prints them in Russian: "ОШИБКА:"). */
export function buildErrors(log: string, limit = 6): string[] {
  const out: string[] = [];
  for (const line of log.split('\n')) {
    const t = line.trim();
    if (/^(ОШИБКА|ERROR|error)[:\s]/.test(t) || /\berror:/.test(t)) out.push(t);
  }
  return out.slice(-limit);
}
/** Label of the running/last run → which target and whether it was a size estimate. */
export function runTarget(label: string): { target: string; estimate: boolean } {
  if (label === 'estimate') return { target: '', estimate: true };
  return { target: label.startsWith('build-') ? label.slice(6) : '', estimate: false };
}
