import { useCallback, useEffect, useMemo, useState } from 'react';
import { ArrowLeftRight, RefreshCw } from 'lucide-react';
import { invoke } from '../bridge';
import type { StudioGpuStats, StudioProjectState, StudioSnapshot, StudioStats } from '../types';
import { formatBytes, frameSummary, jsonDiff, previewValue, type JsonChange } from './jsonDiff';
import './ToolPanel.css';
import './ProjectToolPanels.css';
import { StudioLoader } from '../ui';

const message = (e: unknown) => e instanceof Error ? e.message : String(e);
type Ctx = { snapshot: StudioSnapshot | null };

/** Statistics: map contents (by type and owner) and Scene frame time; refreshes with the document and every 2 s. */
export function StatsPanel({ snapshot }: Ctx) {
  const path = snapshot?.project.path ?? '';
  const [stats, setStats] = useState<StudioStats | null>(null);
  const [error, setError] = useState('');
  const read = useCallback(() => void invoke('studio.stats', {}).then(s => { setStats(s); setError(''); }).catch(e => setError(message(e))), []);
  useEffect(() => { if (path) read(); }, [path, snapshot, read]);
  useEffect(() => { if (!path) return; const t = window.setInterval(read, 2000); return () => window.clearInterval(t); }, [path, read]);
  if (!path) return <div className="tool__empty">Open a project to see its statistics.</div>;
  const fps = stats && stats.sceneFrameMs > 0 ? 1000 / stats.sceneFrameMs : 0;
  const max = Math.max(1, ...(stats?.types ?? []).map(t => t.count));
  return <div className="ptp">
    {error && <div className="tool__error" role="alert">{error}</div>}
    {stats && <div className="ptp__scroll">
      <div className="stats__cards">
        <div><b>{stats.entities}</b><span>objects</span></div>
        <div><b>{stats.width}×{stats.height}</b><span>map cells</span></div>
        <div><b>{stats.players}</b><span>players</span></div>
        <div><b>{stats.roads}</b><span>roads</span></div>
        <div><b>{stats.ruleTypes}</b><span>rule types</span></div>
        <div><b>{fps ? fps.toFixed(0) : '—'}</b><span>Scene fps{stats.sceneFrameMs ? ` · ${stats.sceneFrameMs.toFixed(1)} ms` : ''}</span></div>
      </div>
      <h4>By owner</h4>
      <table className="ptp__table"><tbody>{stats.owners.map(o =>
        <tr key={o.owner}><td>{o.name}</td><td className="ptp__meta">{o.owner}</td><td className="stats__num">{o.count}</td></tr>)}</tbody></table>
      <h4>By type · {stats.types.length}</h4>
      <div className="stats__bars">{stats.types.map(t => <div key={t.type} className="stats__bar">
        <span title={t.type}>{t.type}</span><i style={{ width: `${(t.count / max) * 100}%` }}/><b>{t.count}</b></div>)}</div>
      <p className="ptp__hint">{stats.map}{stats.dirty ? ' · unsaved' : ''} · {stats.undoSteps} undo steps</p>
    </div>}
  </div>;
}

/** Snapshot Diff: structural comparison of two JSON files of the project (maps by default). Offline, read-only. */
export function SnapshotDiffPanel({ snapshot }: Ctx) {
  const path = snapshot?.project.path ?? '';
  const [maps, setMaps] = useState<string[]>([]);
  const [a, setA] = useState(''), [b, setB] = useState('');
  const [changes, setChanges] = useState<JsonChange[] | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const [filter, setFilter] = useState('');
  useEffect(() => {
    if (!path) return;
    void invoke('studio.project', {}).then((p: StudioProjectState) => {
      const files = p.maps.map(m => m.file);
      setMaps(files); setA(x => x || files[0] || ''); setB(x => x || files[1] || files[0] || '');
    }).catch(() => {});
  }, [path]);
  const read = async (file: string) => {
    const r = (await invoke('studio.op', { op: 'game_file_read', params: { path: file } })).result as { text?: string };
    try { return JSON.parse(r.text ?? ''); } catch { throw new Error(file + ' is not JSON'); }
  };
  const compare = async () => {
    setBusy(true); setChanges(null);
    try { setChanges(jsonDiff(await read(a.trim()), await read(b.trim()))); setError(''); }
    catch (e) { setError(message(e)); }
    finally { setBusy(false); }
  };
  const shown = useMemo(() => (changes ?? []).filter(c => !filter || c.path.toLowerCase().includes(filter.toLowerCase())), [changes, filter]);
  if (!path) return <div className="tool__empty">Open a project to compare its snapshots.</div>;
  return <div className="ptp">
    <div className="ptp__edhead diff__head">
      <input className="au-input" list="diff-maps" value={a} onChange={e => setA(e.target.value)} placeholder="maps/a.json"/>
      <button type="button" className="au-button au-button--icon au-button--ghost au-button--sm" title="Swap" onClick={() => { setA(b); setB(a); }}><ArrowLeftRight size={13}/></button>
      <input className="au-input" list="diff-maps" value={b} onChange={e => setB(e.target.value)} placeholder="maps/b.json"/>
      <datalist id="diff-maps">{maps.map(m => <option key={m} value={m}/>)}</datalist>
      <button type="button" className="au-button au-button--sm au-button--primary" disabled={busy || !a || !b} onClick={() => void compare()}>
        {busy ? <StudioLoader size={16}/> : <RefreshCw size={13}/>}Compare</button>
    </div>
    {error && <div className="tool__error" role="alert">{error}</div>}
    <div className="ptp__scroll">
      {changes && <div className="diff__summary">
        <span>{changes.length >= 2000 ? '2000+' : changes.length} differences</span>
        <span className="diff__added">+{changes.filter(c => c.kind === 'added').length}</span>
        <span className="diff__removed">−{changes.filter(c => c.kind === 'removed').length}</span>
        <span className="diff__changed">~{changes.filter(c => c.kind === 'changed').length}</span>
        <input className="au-input" placeholder="Filter paths" value={filter} onChange={e => setFilter(e.target.value)}/>
      </div>}
      {changes && !changes.length && <p className="ptp__ok">The files are the same.</p>}
      {!!shown.length && <table className="ptp__table ptp__audit"><tbody>{shown.slice(0, 500).map((c, i) =>
        <tr key={i} className={'diff__' + c.kind}><td>{c.kind === 'added' ? '+' : c.kind === 'removed' ? '−' : '~'} {c.path}</td>
          <td>{previewValue(c.before)}</td><td>{previewValue(c.after)}</td></tr>)}</tbody></table>}
      {!changes && <p className="ptp__hint">Compares two JSON files of the project path by path (numbers within 1e-6 count as equal). Read-only.</p>}
    </div>
  </div>;
}

/** Frame Capture: the Scene's last 240 frames (time between frames, draw calls), live or frozen with Capture. */
export function FrameCapturePanel({ snapshot }: Ctx) {
  const path = snapshot?.project.path ?? '';
  const [live, setLive] = useState<StudioGpuStats | null>(null);
  const [frozen, setFrozen] = useState<StudioGpuStats | null>(null);
  const [error, setError] = useState('');
  useEffect(() => {
    if (!path || frozen) return;
    const read = () => void invoke('studio.gpuStats', {}).then(s => { setLive(s); setError(s.error ?? ''); }).catch(e => setError(message(e)));
    read(); const t = window.setInterval(read, 500); return () => window.clearInterval(t);
  }, [path, frozen]);
  if (!path) return <div className="tool__empty">Open a project to capture Scene frames.</div>;
  const data = frozen ?? live;
  const ms = (data?.history ?? []).map(h => h[0]), draws = (data?.history ?? []).map(h => h[1]);
  const sum = frameSummary(ms);
  const W = 480, H = 90, top = Math.max(33.4, sum.max * 1.1);
  const line = ms.map((v, i) => `${(i / Math.max(1, ms.length - 1)) * W},${H - (v / top) * H}`).join(' ');
  const y16 = H - (16.7 / top) * H, y33 = H - (33.3 / top) * H;
  return <div className="ptp">
    <div className="ptp__edhead">
      <button type="button" className={'au-button au-button--sm' + (frozen ? '' : ' au-button--primary')} onClick={() => setFrozen(frozen ? null : live)}>
        {frozen ? 'Resume live' : 'Capture'}</button>
      <span className="ptp__meta">{frozen ? 'Captured' : 'Live'} · {sum.count} frames · draw calls {data?.drawCalls ?? '—'}</span>
    </div>
    {error && <div className="tool__error" role="alert">{error}</div>}
    <div className="ptp__scroll">
      <div className="stats__cards">
        <div><b>{sum.avg ? (1000 / sum.avg).toFixed(0) : '—'}</b><span>fps (average)</span></div>
        <div><b>{sum.avg.toFixed(1)}</b><span>ms average</span></div>
        <div><b>{sum.p95.toFixed(1)}</b><span>ms 95th percentile</span></div>
        <div><b>{sum.max.toFixed(1)}</b><span>ms worst</span></div>
        <div><b>{draws.length ? Math.max(...draws) : '—'}</b><span>draw calls (max)</span></div>
      </div>
      <svg className="frames__chart" viewBox={`0 0 ${W} ${H}`} preserveAspectRatio="none" role="img" aria-label="Frame times">
        <line x1={0} x2={W} y1={y16} y2={y16} className="frames__budget"/><line x1={0} x2={W} y1={y33} y2={y33} className="frames__budget"/>
        <polyline points={line} className="frames__line"/>
      </svg>
      <p className="ptp__hint">Time between Scene frames (CPU side, includes waiting for the display); lines at 60 and 30 fps. Draw calls of the Scene renderer. Not a GPU command capture.</p>
    </div>
  </div>;
}

/** Texture Resources: live GPU textures of the Scene renderer with a format-based size estimate. */
export function TextureResourcesPanel({ snapshot }: Ctx) {
  const path = snapshot?.project.path ?? '';
  const [gpu, setGpu] = useState<StudioGpuStats['gpu'] | null>(null);
  const [error, setError] = useState('');
  const [filter, setFilter] = useState('');
  const read = useCallback(() => void invoke('studio.gpuStats', { textures: true }).then(s => { setGpu(s.gpu ?? null); setError(s.error ?? ''); })
    .catch(e => setError(message(e))), []);
  useEffect(() => { if (path) read(); }, [path, read]);
  if (!path) return <div className="tool__empty">Open a project to list GPU textures.</div>;
  const rows = [...(gpu?.textures ?? [])].sort((a, b) => b.bytes - a.bytes)
    .filter(t => !filter || t.format.toLowerCase().includes(filter.toLowerCase()) || `${t.width}x${t.height}`.includes(filter));
  return <div className="ptp">
    <div className="ptp__edhead">
      <button type="button" className="au-button au-button--sm" onClick={read}><RefreshCw size={13}/>Refresh</button>
      <span className="ptp__meta">{gpu ? `${gpu.textures.length} textures · ${formatBytes(gpu.totalBytes)} estimated` : ''}</span>
      <input className="au-input" placeholder="Filter (format, 1024x1024)" value={filter} onChange={e => setFilter(e.target.value)}/>
    </div>
    {error && <div className="tool__error" role="alert">{error}</div>}
    <div className="ptp__scroll">
      <table className="ptp__table ptp__audit">
        <thead><tr><th>#</th><th>Size</th><th>Format</th><th>Mips</th><th>Kind</th><th>Estimate</th></tr></thead>
        <tbody>{rows.slice(0, 1000).map(t => <tr key={t.id}>
          <td>{t.id}</td><td>{t.width}×{t.height}{t.depth > 1 ? `×${t.depth}` : ''}{t.layers > 1 ? ` [${t.layers}]` : ''}</td>
          <td>{t.format}</td><td>{t.mips}</td><td>{t.renderTarget ? 'target' : 'sampled'}{t.samples > 1 ? ` ×${t.samples}` : ''}</td>
          <td className="stats__num">{formatBytes(t.bytes)}</td></tr>)}</tbody>
      </table>
      <p className="ptp__hint">Estimated from format and size (tightly packed); drivers may pad. Scene renderer only.</p>
    </div>
  </div>;
}
