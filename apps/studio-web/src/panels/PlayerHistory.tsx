import { useEffect, useState } from 'react';
import { invoke } from '../bridge';
import type { StudioAssetResidency, StudioGameState, StudioPlayerSample } from '../types';

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

function PlayerTrends({ player, samples, names, initiallyOpen }: {
  player: StudioPlayerSample['players'][number]; samples: StudioPlayerSample[]; names: Record<string,string>; initiallyOpen: boolean;
}) {
  const [open, setOpen] = useState(initiallyOpen);
  const entries = open ? samples.flatMap(sample => {
    const row = sample.players.find(item => item.slot === player.slot);
    return row ? [{tick: sample.tick, row}] : [];
  }) : [];
  const ticks = entries.map(entry => entry.tick);
  return <details open={open} onToggle={event => setOpen(event.currentTarget.open)}>
    <summary>{player.name} · Player {player.slot} · {player.units} units · {player.structures} structures</summary>
    {open && <>
      <Trend label="Army" values={entries.map(entry => entry.row.units)} ticks={ticks}/>
      <Trend label="Structures" values={entries.map(entry => entry.row.structures)} ticks={ticks}/>
      {Object.keys(player.resources).map(resource => <Trend key={resource} label={names[resource] || resource} ticks={ticks}
        values={entries.map(entry => entry.row.resources[resource] ?? 0)}/>)}
    </>}
  </details>;
}

export default function PlayerHistory({ project }: { project: string }) {
  const [samples, setSamples] = useState<StudioPlayerSample[]>([]);
  const [match, setMatch] = useState<{tick: number; seconds: number; units?: number; projectiles?: number; residency?: StudioAssetResidency | null; pool?: NonNullable<StudioGameState['playerHistory']>['geometryPool']} | null>(null);
  const [names, setNames] = useState<Record<string,string>>({});
  const [error, setError] = useState('');
  useEffect(() => {
    let active = true, timer = 0, epoch = '', afterTick = -1;
    async function refresh() {
      try {
        const state = await invoke('studio.gameState', { statisticsProject: project, afterTick, epoch });
        if (!active) return;
        const history = state.playerHistory;
        setMatch(state.inMatch && history?.tickRate ? {
          tick: state.tick, seconds: state.tick / history.tickRate, units: history.units, projectiles: history.projectiles, pool: history.geometryPool, residency: history.assetResidency,
        } : null);
        if (!state.inMatch || !history) {
          afterTick = -1; epoch = ''; setSamples(previous => previous.length ? [] : previous);
        } else {
          const reset = epoch !== history.epoch;
          epoch = history.epoch;
          if (history.resourceNames || reset) setNames(history.resourceNames ?? {});
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
    {match && <div className="stats__cards">
      <div><b>{match.tick}</b><span>simulation tick</span></div>
      <div><b>{match.seconds.toFixed(1)} s</b><span>game time</span></div>
      {match.units !== undefined && <div><b>{match.units}</b><span>unit entities</span></div>}
      {match.projectiles !== undefined && <div><b>{match.projectiles}</b><span>projectiles</span></div>}
    </div>}
    {match?.pool && <section aria-label="Game geometry pool">
      <h4>Game geometry pool</h4>
      <div className="stats__cards">
        <div><b>{(match.pool.occupiedBytes / 1048576).toFixed(2)} MiB</b><span>occupied</span></div>
        <div><b>{(match.pool.allocatedBytes / 1048576).toFixed(2)} MiB</b><span>allocated</span></div>
        <div><b>{match.pool.pages}</b><span>pages</span></div>
        <div><b>{match.pool.models}</b><span>pooled models</span></div>
      </div>
    </section>}
    {match?.residency && <section aria-label="Game model assets">
      <h4>Game model assets</h4>
      <div className="stats__cards">
        <div><b>{match.residency.loadedTypes}</b><span>loaded types</span></div>
        <div><b>{match.residency.cachedModels}</b><span>cached models</span></div>
        <div><b>{match.residency.gpuTextures}</b><span>model textures</span></div>
        <div><b>{(match.residency.decodedCpuBytes / 1048576).toFixed(2)} MiB</b><span>decoded image cache</span></div>
        <div><b>{(match.residency.residentModelKnownBytes / 1048576).toFixed(2)} MiB</b><span>resident model memory</span></div>
        <div><b>{match.residency.retiredModels}</b><span>models awaiting release</span></div>
        <div><b>{match.residency.retirementBatches}</b><span>pending batches</span></div>
        <div><b>{match.residency.retirementPeak}</b><span>peak queue depth</span></div>
        <div><b>{(match.residency.estimatedRetiredGpuBytes / 1048576).toFixed(2)} MiB</b><span>estimated memory awaiting release</span></div>
        <div><b>{match.residency.unresolvedFailedFences}</b><span>unresolved fences</span></div>
      </div>
      {match.residency.publicationBackpressured && <p role="status">New model uploads are waiting for GPU releases.</p>}
    </section>}
    {!latest && !match && !error && <p className="ptp__hint">Start a local match to track armies, structures and resources.</p>}
    {latest?.players.map((player, index) => <PlayerTrends key={player.slot} player={player}
      samples={samples} names={names} initiallyOpen={index < 2}/>)}
  </section>;
}
