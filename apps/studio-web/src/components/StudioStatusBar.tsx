import { AlertTriangle, CircleX, Dot } from 'lucide-react';
import type { StudioGameState, StudioSnapshot } from '../types';

/** Status bar: problems (opens Console), unsaved map, selection, simulation state, host. */
export default function StudioStatusBar({ snapshot, connected, game, action = '', onOpenConsole }: {
  snapshot: StudioSnapshot | null; connected: boolean; game: StudioGameState | null; action?: string; onOpenConsole: () => void;
}) {
  const logs = snapshot?.logs ?? [];
  const errors = logs.filter(l => l.level === 'error').length;
  const warnings = logs.filter(l => l.level === 'warning').length;
  const map = snapshot?.scene.name || snapshot?.project.name || '';
  return <footer className="statusbar studio-statusbar">
    <button type="button" className={'studio-statusbar__problems' + (errors ? ' has-errors' : '')}
      onClick={onOpenConsole} title="Open Console">
      <CircleX size={12}/> {errors} {errors === 1 ? 'error' : 'errors'}
      <AlertTriangle size={12} className="studio-statusbar__warn"/> {warnings}
    </button>
    {map && <span className="studio-statusbar__map">{map}{snapshot?.project.dirty
      ? <b className="studio-statusbar__dirty"><Dot size={16}/>unsaved</b> : <span className="studio-statusbar__saved"> · saved</span>}</span>}
    {action && <span className="studio-statusbar__action" aria-live="polite">{action}</span>}
    <span className="studio-statusbar__spacer"/>
    {snapshot?.selectedId && <span>Selected: {snapshot.scene.entities.find(e => e.id === snapshot.selectedId)?.name ?? snapshot.selectedId}</span>}
    {game?.active
      ? <span className="studio-statusbar__mono">{game.paused ? 'paused' : 'playing'} · tick {game.tick}</span>
      : <span className="studio-statusbar__mono">edit mode</span>}
    <span className={connected ? 'studio-statusbar__host is-on' : 'studio-statusbar__host'}>● {connected ? 'Native host' : 'Preview'}</span>
  </footer>;
}
