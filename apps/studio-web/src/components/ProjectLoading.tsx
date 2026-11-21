import { useEffect, useRef, useState } from 'react';
import { onNativeEvent } from '../bridge';
import './ProjectLoading.css';

export interface ProjectLoadState { phase: 'project' | 'scene' | 'models' | 'error' | 'done'; message: string; done: number; total: number; active: boolean }

function parse(payload: unknown): ProjectLoadState | null {
  if (!payload || typeof payload !== 'object') return null;
  const p = payload as Record<string, unknown>;
  const phase = String(p.phase);
  if (!['project', 'scene', 'models', 'error', 'done'].includes(phase)) return null;
  return { phase: phase as ProjectLoadState['phase'], message: String(p.message ?? ''), done: Number(p.done) || 0,
    total: Number(p.total) || 0, active: p.active !== false };
}

/** Native `project.loading` events → state; "scene" without follow-up fades after 5 s. */
export function useProjectLoading(): [ProjectLoadState | null, () => void] {
  const [state, setState] = useState<ProjectLoadState | null>(null);
  const timer = useRef(0);
  useEffect(() => onNativeEvent((event, payload) => {
    if (event !== 'project.loading') return;
    const next = parse(payload);
    if (!next) return;
    window.clearTimeout(timer.current);
    if (next.phase === 'done') { setState(null); return; }
    setState(next);
    if (next.phase === 'scene') timer.current = window.setTimeout(() => setState(s => s?.phase === 'scene' ? null : s), 5000);
  }), []);
  return [state, () => setState(null)];
}

/** Unity-style blocking dialog while the project itself opens (or fails). */
export function ProjectLoadingDialog({ state, onClose }: { state: ProjectLoadState | null; onClose: () => void }) {
  const [elapsed, setElapsed] = useState(0);
  const blocking = state?.phase === 'project' || state?.phase === 'error';
  useEffect(() => {
    if (state?.phase !== 'project') return;
    const started = performance.now();
    setElapsed(0);
    const t = window.setInterval(() => setElapsed((performance.now() - started) / 1000), 200);
    return () => window.clearInterval(t);
  }, [state?.phase, state?.message]);
  if (!blocking || !state) return null;
  return <div className="project-loading__backdrop" data-native-overlay>
    <section className="project-loading" role="dialog" aria-modal="true" aria-label="Opening project">
      <h2>{state.phase === 'error' ? 'Could not open project' : 'Opening Project'}</h2>
      <p className="project-loading__message">{state.message}</p>
      {state.phase === 'project' && <>
        <div className="project-loading__bar is-indeterminate" role="progressbar" aria-label="Loading project"><span/></div>
        <p className="project-loading__hint">Reading game.json, rules and the map… {elapsed.toFixed(1)} s</p>
      </>}
      {state.phase === 'error' && <button type="button" onClick={onClose}>Close</button>}
    </section>
  </div>;
}

/** Unreal-style non-blocking progress under the title bar while scene models stream in. */
export function SceneLoadingStrip({ state }: { state: ProjectLoadState | null }) {
  if (!state || (state.phase !== 'models' && state.phase !== 'scene')) return null;
  const ratio = state.total > 0 ? Math.min(1, state.done / state.total) : 0;
  return <div className={'scene-loading-strip' + (state.total ? '' : ' is-indeterminate')} role="progressbar"
    aria-label="Loading scene" aria-valuemin={0} aria-valuemax={state.total || undefined} aria-valuenow={state.total ? state.done : undefined}>
    <span style={{ width: state.total ? (ratio * 100).toFixed(1) + '%' : undefined }}/>
  </div>;
}
export function sceneLoadingText(state: ProjectLoadState | null): string {
  if (!state) return '';
  if (state.phase === 'models') return `Loading scene models ${state.done}/${state.total}` + (state.message ? ` · ${state.message.split('/').pop()}` : '');
  if (state.phase === 'scene') return 'Preparing scene…';
  return '';
}
