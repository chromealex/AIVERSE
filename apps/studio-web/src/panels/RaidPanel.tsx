import {useState, type FormEvent} from 'react';
import {invoke} from '../bridge';
import type {StudioGraphDocumentScope} from '../types';
import {StudioButton, StudioInput} from '../ui';
import './RaidPanel.css';

export interface RaidInfo {
  enabled?: boolean; error?: string; mode: string; duration: number; raidersOnMap: number; interactablesOnMap: number;
  points: {id: string; pos: number[]; channel: number; radius: number; call?: boolean; requires: string[]}[];
  objectives: {id: string; text: string; required: boolean}[];
  warnings: string[];
}
interface Run {
  phase: string; seconds: number;
  receipts: {player: number; outcome: string; value: number}[];
}
export default function RaidPanel({scope, info, onChanged}: {
  scope: StudioGraphDocumentScope; info?: RaidInfo; onChanged: () => void;
}) {
  const [result, setResult] = useState<Run | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  if (!info) return <p role="status">Loading raid…</p>;
  if (info.error) return <p role="alert">{info.error}</p>;
  if (!info.enabled) return <p>Enable a raid in the project rules to author extraction points.</p>;
  async function submit(event: FormEvent<HTMLFormElement>, point: boolean) {
    event.preventDefault();
    const form = event.currentTarget;
    const data = new FormData(form);
    const id = String(data.get('id') ?? '').trim();
    setError('');
    if (point && (!id || info?.points.some(p => p.id === id))) {
      setError('Enter a unique extraction point ID.');
      return;
    }
    setBusy(true);
    try {
      const reply = await invoke('studio.op', {
        op: point ? 'raid_point' : 'raid_run',
        params: point ? {id, point: {pos: [Number(data.get('x')), Number(data.get('z'))]}} : {seed: Number(data.get('seed'))},
        expectedProject: scope.projectPath, expectedDocumentEpoch: scope.epoch, expectedDocumentRevision: scope.revision,
      });
      if (point) { form.reset(); onChanged(); }
      else setResult(reply.result as Run);
    } catch (cause) { setError(cause instanceof Error ? cause.message : String(cause)); }
    finally { setBusy(false); }
  }
  return <article className="raid-page">
    <header><h2>Extraction raid</h2><p>{info.mode.toUpperCase()} · {info.duration} s · {info.raidersOnMap} raiders · {info.interactablesOnMap} interactables</p></header>
    {info.warnings.map(w => <p key={w} role="status">{w}</p>)}
    <section><h3>Extraction points</h3>
      <ul>{info.points.map(p => <li key={p.id}><strong>{p.id}</strong><span>X {p.pos[0]} · Z {p.pos[1]} · radius {p.radius} · channel {p.channel} s{p.call && ' · call required'}{p.requires.length > 0 && ' · requires ' + p.requires.join(', ')}</span></li>)}</ul>
      <form onSubmit={e => void submit(e,true)}>
        <label>Point ID<StudioInput required name="id" placeholder="north-gate"/></label>
        {['x','z'].map(axis => <label key={axis}>{axis.toUpperCase()}<StudioInput required type="number" step="any" name={axis}/></label>)}
        <StudioButton type="submit" disabled={busy}>Add point</StudioButton>
      </form>
    </section>
    {info.objectives.length > 0 && <section><h3>Objectives</h3><ul>{info.objectives.map(o => <li key={o.id}><strong>{o.id}</strong><span>{o.text}{o.required && ' · required'}</span></li>)}</ul></section>}
    <section><h3>Bot playtest</h3>
      <form onSubmit={e => void submit(e,false)}><label>Seed<StudioInput name="seed" required type="number" min="0" max="4294967295" defaultValue="1"/></label>
        <StudioButton variant="primary" disabled={busy} type="submit">Run bot raid</StudioButton></form>
      {result && <div aria-live="polite"><h4>Receipts · {result.phase} · {result.seconds} s</h4>
        <ul>{result.receipts.map(r => <li key={r.player}><strong>Player {r.player}</strong><span>{r.outcome} · value {r.value}</span></li>)}</ul>
      </div>}
    </section>
    {error && <p role="alert">{error}</p>}
  </article>;
}
