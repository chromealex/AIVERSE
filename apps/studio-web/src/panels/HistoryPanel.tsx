import { useEffect, useRef, useState } from 'react';
import { History, RotateCcw, Undo2, Redo2, RefreshCw } from 'lucide-react';
import { invoke } from '../bridge';
import type { StudioHistory, StudioProjectHistory, StudioSnapshot } from '../types';
import { StudioAlert, StudioButton, StudioIconButton, StudioEmptyState, StudioSkeleton } from '../ui';
import { historyStepParams, type HistoryDirection, type HistoryScope } from './historyLogic';
import './HistoryPanel.css';

const message = (e: unknown) => e instanceof Error ? e.message : String(e);
type HistoryRead = { source: StudioSnapshot; scope: HistoryScope; value: StudioHistory | StudioProjectHistory };
type HistoryError = { project: string; scope: HistoryScope; message: string };
const read = (scope: HistoryScope) => scope === 'map' ? invoke('studio.history', {}) : invoke('studio.projectHistory', {});

/** Independent map timeline and saved project document history. Never replay a project jump. */
export default function HistoryPanel({ snapshot, commitSnapshot }: {
  snapshot: StudioSnapshot | null; commitSnapshot: (next: StudioSnapshot) => void;
}) {
  const [scope, setScope] = useState<HistoryScope>('map');
  const [loaded, setLoaded] = useState<HistoryRead | null>(null);
  const [failure, setFailure] = useState<HistoryError | null>(null);
  const [pending, setPending] = useState(false);
  const [refresh, setRefresh] = useState(0);
  const request = useRef(false);
  const latest = useRef({ snapshot, scope });
  latest.current = { snapshot, scope };
  const project = snapshot?.project.path ?? '';
  const history = loaded?.source === snapshot && loaded.scope === scope ? loaded.value : null;
  const error = failure?.project === project && failure.scope === scope ? failure.message : '';
  const projectHistory = history?.scope === 'project' ? history as StudioProjectHistory : null;

  useEffect(() => {
    if (!project || !snapshot || pending) return;
    let live = true;
    void read(scope).then(value => {
      if (live) setLoaded({ source: snapshot, scope, value });
    }).catch(e => {
      if (live) { setLoaded(null); setFailure({ project, scope, message: message(e) }); }
    });
    return () => { live = false; };
  }, [snapshot, project, scope, refresh, pending]);

  const reload = () => { setLoaded(null); setFailure(null); setRefresh(n => n + 1); };
  const switchScope = (next: HistoryScope) => {
    if (request.current || next === scope) return;
    setScope(next); setLoaded(null); setFailure(null);
  };
  const navigate = async (position: number | HistoryDirection) => {
    if (request.current || !snapshot || !history || history.strokeActive) return;
    const source = snapshot;
    const params = scope === 'project' && typeof position !== 'number'
      ? historyStepParams(projectHistory, position) : null;
    if (scope === 'project' && !params) return;
    if (scope === 'map' && (typeof position !== 'number' || !history.version)) return;
    request.current = true; setPending(true); setFailure(null);
    try {
      const result = scope === 'project' && params
        ? await invoke('studio.projectHistoryStep', params)
        : await invoke('studio.historyJump', { position: position as number, expectedVersion: history.version });
      // Do not replace a newer snapshot delivered by a native event or a project switch.
      // Native document.changed refreshes other windows even when this reply is discarded.
      if (latest.current.snapshot === source && latest.current.scope === scope) {
        commitSnapshot(result.snapshot);
      }
    } catch (e) {
      if (latest.current.snapshot?.project.path === project && latest.current.scope === scope)
        setFailure({ project, scope, message: message(e) });
    } finally {
      request.current = false; setLoaded(null); setPending(false); setRefresh(n => n + 1);
    }
  };

  if (!project || !snapshot) return <StudioEmptyState title="No project open" description="Open a project to see its edit history." icon={<History size={20}/>}/>;
  const steps = history?.steps ?? [];
  const disabled = pending || !history || !!history.strokeActive;
  const loading = !history && !error;
  return <div className="history" aria-busy={pending || loading}>
    <div className="history__toolbar">
      <div className="history__scopes" role="group" aria-label="History scope">
        <StudioButton size="sm" variant={scope === 'map' ? 'primary' : 'ghost'} aria-pressed={scope === 'map'} disabled={pending} onClick={() => switchScope('map')}>Map</StudioButton>
        <StudioButton size="sm" variant={scope === 'project' ? 'primary' : 'ghost'} aria-pressed={scope === 'project'} disabled={pending} onClick={() => switchScope('project')}>Project</StudioButton>
      </div>
      <StudioIconButton size="sm" variant="ghost" aria-label="Refresh history" title="Refresh history" disabled={pending} icon={<RefreshCw size={14}/>} onClick={reload}/>
    </div>
    <div className="history__description">
      {scope === 'map' ? 'Scene edits only. Global Undo / Redo controls the map.'
        : 'Saved Rules and compatible Manifest edits. Assets and map edits are separate.'}
    </div>
    {scope === 'project' && <div className="history__actions">
      <StudioButton size="sm" disabled={disabled || !historyStepParams(projectHistory, 'undo')} onClick={() => void navigate('undo')}><Undo2 size={14}/>Undo</StudioButton>
      <StudioButton size="sm" disabled={disabled || !historyStepParams(projectHistory, 'redo')} onClick={() => void navigate('redo')}><Redo2 size={14}/>Redo</StudioButton>
    </div>}
    {error && <StudioAlert tone="danger" className="history__alert" title="History unavailable or edit refused">{error}</StudioAlert>}
    {history?.strokeActive && <StudioAlert tone="warning" className="history__alert">Finish or cancel the active Scene stroke first.</StudioAlert>}
    {projectHistory?.blockedReason && <StudioAlert tone="warning" className="history__alert">{projectHistory.blockedReason}</StudioAlert>}
    {loading && <div className="history__loading"><StudioSkeleton/><StudioSkeleton width="65%"/><span role="status">Reading history…</span></div>}
    {pending && <div className="history__description" role="status">Applying history…</div>}
    {history && scope === 'map' && <div role="listbox" aria-label="Map edit history" className="history__list">
      <button type="button" role="option" aria-selected={history.position === 0}
        className={'history__step history__step--origin' + (history.position === 0 ? ' is-current' : '')} disabled={disabled || !history.version || history.position === 0} onClick={() => void navigate(0)}>
        <RotateCcw size={12}/><span>Oldest retained · {snapshot.scene.name || 'map'}</span></button>
      {steps.map((step, i) => <button key={i} type="button" role="option" aria-selected={history.position === i + 1}
        className={'history__step' + (step.done ? '' : ' is-redo') + (history.position === i + 1 ? ' is-current' : '')}
        disabled={disabled || !history.version || history.position === i + 1} onClick={() => void navigate(i + 1)}>
        <b>{i + 1}</b><span title={step.label}>{step.label}</span></button>)}
      {!steps.length && <div className="history__empty"><History size={14}/>No retained map edits.</div>}
    </div>}
    {projectHistory && <>
      <ol className="history__list history__project-list" aria-label="Project edit history">
        <li className={'history__step history__step--origin' + (projectHistory.position === 0 ? ' is-current' : '')} aria-current={projectHistory.position === 0 ? 'step' : undefined}>
          <RotateCcw size={12}/><span>Oldest retained project state</span></li>
        {steps.map((step, i) => <li key={i} aria-current={projectHistory.position === i + 1 ? 'step' : undefined}
          className={'history__step' + (step.done ? '' : ' is-redo') + (projectHistory.position === i + 1 ? ' is-current' : '')}>
          <b>{i + 1}</b><span title={step.label}>{step.label}</span><small>{step.done ? 'Applied' : 'Redo'}</small></li>)}
        {!steps.length && <li className="history__empty"><History size={14}/>No retained project edits.</li>}
      </ol>
      <div className="history__footer" title="Estimated retained payload; not total process memory">
        {steps.length} / {projectHistory.maxSteps} steps · {(projectHistory.bytes / 1048576).toFixed(2)} / {(projectHistory.maxBytes / 1048576).toFixed(0)} MiB
      </div>
    </>}
  </div>;
}
