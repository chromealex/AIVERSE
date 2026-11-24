import { useEffect, useState } from 'react';
import { History, RotateCcw } from 'lucide-react';
import { invoke } from '../bridge';
import type { StudioHistory, StudioSnapshot } from '../types';
import './HistoryPanel.css';

/** Undo history: every map edit as a step; click a step to go back (or forward) to it. */
export default function HistoryPanel({ snapshot, commitSnapshot }: {
  snapshot: StudioSnapshot | null; commitSnapshot: (next: StudioSnapshot) => void;
}) {
  const [history, setHistory] = useState<StudioHistory | null>(null);
  const [error, setError] = useState('');
  const [pending,setPending]=useState(false);
  // Any snapshot change (edit, undo, redo, map switch) re-reads the list.
  useEffect(() => {
    if (!snapshot?.project.path) { setHistory(null); return; }
    let live = true;
    void invoke('studio.history', {}).then(h => { if (live) { setHistory(h); setError(''); } })
      .catch(e => { if (live) setError(e instanceof Error ? e.message : String(e)); });
    return () => { live = false; };
  }, [snapshot]);
  const jump = async (position: number) => {
    if(pending||!history)return;
    setPending(true);setError('');
    try { const r = await invoke('studio.historyJump', { position,expectedVersion:history.version }); setHistory(r); commitSnapshot(r.snapshot); }
    catch (e) { setError(e instanceof Error ? e.message : String(e));try{setHistory(await invoke('studio.history',{}));}catch{setHistory(null);} }
    finally{setPending(false);}
  };
  if (!snapshot?.project.path) return <div className="history__empty">Open a project to see its edit history.</div>;
  const steps = history?.steps ?? [];
  return <div className="history">
    {error && <div className="history__error" role="alert">{error}</div>}
    <div className="history__empty">Map history · rules, files and live runtime edits are not covered.</div>
    <div role="listbox" aria-label="Edit history" className="history__list">
      <button type="button" role="option" aria-selected={history?.position === 0}
        className={'history__step history__step--origin' + (history?.position === 0 ? ' is-current' : '')} disabled={!history||pending} onClick={() => void jump(0)}>
        <RotateCcw size={12}/><span>Oldest retained · {snapshot.scene.name || 'map'}</span></button>
      {steps.map((step, i) => <button key={i} type="button" role="option" aria-selected={history?.position === i + 1}
        className={'history__step' + (step.done ? '' : ' is-redo') + (history?.position === i + 1 ? ' is-current' : '')}
        disabled={pending} onClick={() => void jump(i + 1)}>
        <b>{i + 1}</b><span>{step.label}</span></button>)}
      {!steps.length && <div className="history__empty"><History size={14}/>No edits yet. Changes to the map appear here.</div>}
    </div>
  </div>;
}
