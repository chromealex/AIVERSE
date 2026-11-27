import { useEffect, useState } from 'react';
import { invoke } from '../bridge';
import type { StudioPlayerSample } from '../types';

function Trend({ label, values, ticks }: { label: string; values: number[]; ticks: number[] }) {
  const min = Math.min(0, ...values), max = Math.max(1, ...values);
  const span = Math.max(1, ticks[ticks.length - 1] - ticks[0]);
  const points = values.map((value, i) =>
    ((ticks[i] - ticks[0]) * 400 / span) + ',' + (38 - (value - min) * 36 / (max - min))).join(' ');
  return <div><span>{label} · {values[values.length - 1].toLocaleString()}</span>
    <svg className="frames__chart" style={{height: 40}} viewBox="0 0 400 40" preserveAspectRatio="none" role="img" aria-label={label}>
      <polyline points={points} className="frames__line"/>
    </svg>
  </div>;
}

export default function PlayerHistory({ project }: { project: string }) {
  const [samples, setSamples] = useState<StudioPlayerSample[]>([]);
  const [error, setError] = useState('');
  useEffect(() => {
    let active = true, timer = 0, epoch = '', afterTick = -1;
    async function refresh() {
      try {
        const state = await invoke('studio.gameState', { statisticsProject: project, afterTick, epoch });
        if (!active) return;
        const history = state.playerHistory;
        if (!state.inMatch || !history) {
          afterTick = -1; epoch = ''; setSamples(previous => previous.length ? [] : previous);
        } else {
          const reset = epoch !== history.epoch;
          epoch = history.epoch;
          if (reset || history.samples.length) {
            const batch = history.samples;
            afterTick = batch.length ? batch[batch.length - 1].tick : -1;
            setSamples(previous => (reset ? batch : previous.concat(batch)).slice(-3600));
          }
        }
        setError('');
      } catch (e) {
        if (active) setError(e instanceof Error ? e.message : String(e));
      }
      if (active) timer = window.setTimeout(refresh, 1000);
    }
    void refresh();
    return () => { active = false; window.clearTimeout(timer); };
  }, [project]);
  const latest = samples[samples.length - 1];
  return <section aria-label="Player history"><h4>Player history</h4>
    {error && <p role="alert">{error}</p>}
    {!latest && !error && <p className="ptp__hint">Start a local match to track armies, structures and resources.</p>}
    {latest?.players.map(player => {
      const entries = samples.flatMap(sample => {
        const row = sample.players.find(item => item.slot === player.slot);
        return row ? [{tick:sample.tick, row}] : [];
      });
      const ticks = entries.map(e => e.tick);
      return <details key={player.slot}><summary>{player.name} · Player {player.slot} · {player.units} units · {player.structures} structures</summary>
        <Trend label="Army" values={entries.map(e => e.row.units)} ticks={ticks}/>
        <Trend label="Structures" values={entries.map(e => e.row.structures)} ticks={ticks}/>
        {Object.keys(player.resources).map(resource => <Trend key={resource} label={resource} ticks={ticks}
          values={entries.map(e => e.row.resources[resource] ?? 0)}/>)}
      </details>;
    })}
  </section>;
}
