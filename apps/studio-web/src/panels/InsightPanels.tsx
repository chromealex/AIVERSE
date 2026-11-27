import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { ArrowLeftRight, FolderOpen, RefreshCw } from 'lucide-react';
import { invoke } from '../bridge';
import type { StudioGpuStats, StudioGpuTexture, StudioProjectState, StudioSnapshot, StudioStats } from '../types';
import { formatBytes, frameSummary, jsonDiff, previewValue, type JsonChange } from './jsonDiff';
import { copyText } from '../clipboard';
import PlayerHistory from './PlayerHistory';
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
      <StatsFrameHistory key={path} project={path}/>
      <PlayerHistory key={'players:' + path} project={path}/>
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
  const [firstFile, setFirstFile] = useState<File | null>(null), [secondFile, setSecondFile] = useState<File | null>(null);
  const [notice, setNotice] = useState('');
  const [page, setPage] = useState(0);
  const pageSize = 100;
  const [truncated, setTruncated] = useState(false);
  const [changes, setChanges] = useState<JsonChange[] | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const [filter, setFilter] = useState('');
  useEffect(() => {
    if (!path) return;
    void invoke('studio.project', {}).then((p: StudioProjectState) => {
      const files = p.maps.map(m => m.file);
      setMaps(files); setA(x => x || files[0] || ''); setB(x => x || files[1] || files[0] || '');
    }).catch(e => setError(message(e)));
  }, [path]);
  const read = async (file: string, external: File | null) => {
    if (external) {
      if (external.size > 16 * 1024 * 1024) throw new Error(external.name + ' exceeds 16 MiB');
      const text = await external.text();
      try { return JSON.parse(text); } catch { throw new Error(external.name + ' is not JSON'); }
    }
    const r = (await invoke('studio.op', { op: 'game_file_read', params: { path: file } })).result as { text?: string };
    try { return JSON.parse(r.text ?? ''); } catch { throw new Error(file + ' is not JSON'); }
  };
  const compare = async () => {
    setPage(0); setBusy(true); setChanges(null); setNotice(''); setError('');
    try {
      const [first, second] = await Promise.all([read(a.trim(), firstFile), read(b.trim(), secondFile)]);
      const rows = jsonDiff(first, second, 1e-6, 2001);
      setTruncated(rows.length > 2000);
      setChanges(rows.slice(0, 2000));
    }
    catch (e) { setError(message(e)); }
    finally { setBusy(false); }
  };
  const copyReport = async () => {
    if (!changes?.length) return;
    const lines = changes.map(c => [c.path.replaceAll('\t', '\\t').replaceAll('\n', '\\n').replaceAll('\r', '\\r'), JSON.stringify(c.before), JSON.stringify(c.after)].join('\t'));
    if (truncated) lines.push('[truncated]');
    setNotice(await copyText(['path\tfirst\tsecond', ...lines, ''].join('\n')) ? 'Report copied' : 'Clipboard unavailable');
  };
  const resetResult = () => { setChanges(null); setNotice(''); setError(''); };
  const shown = useMemo(() => (changes ?? []).filter(c => !filter || c.path.toLowerCase().includes(filter.toLowerCase())), [changes, filter]);
  if (!path) return <div className="tool__empty">Open a project to compare its snapshots.</div>;
  return <div className="ptp">
    <div className="ptp__edhead diff__head">
      <label className="au-button au-button--icon au-button--sm diff__browse" title="Choose first snapshot file">
        <FolderOpen size={15}/>
        <input type="file" accept=".json,application/json" disabled={busy} aria-label="Choose first snapshot file" onChange={event => {
          const file = event.target.files?.[0];
          if (file) { setFirstFile(file); setA(file.name); resetResult(); }
          event.target.value = '';
        }}/>
      </label>
      <input className="au-input" list="diff-maps" aria-label="First snapshot" disabled={busy} value={a} onChange={e => { setA(e.target.value); setFirstFile(null); resetResult(); }} placeholder="maps/a.json"/>
      <button type="button" className="au-button au-button--icon au-button--ghost au-button--sm" title="Swap" disabled={busy} onClick={() => { setA(b); setB(a); setFirstFile(secondFile); setSecondFile(firstFile); resetResult(); }}><ArrowLeftRight size={13}/></button>
      <label className="au-button au-button--icon au-button--sm diff__browse" title="Choose second snapshot file">
        <FolderOpen size={15}/>
        <input type="file" accept=".json,application/json" disabled={busy} aria-label="Choose second snapshot file" onChange={event => {
          const file = event.target.files?.[0];
          if (file) { setSecondFile(file); setB(file.name); resetResult(); }
          event.target.value = '';
        }}/>
      </label>
      <input className="au-input" list="diff-maps" aria-label="Second snapshot" disabled={busy} value={b} onChange={e => { setB(e.target.value); setSecondFile(null); resetResult(); }} placeholder="maps/b.json"/>
      <datalist id="diff-maps">{maps.map(m => <option key={m} value={m}/>)}</datalist>
      <button type="button" className="au-button au-button--sm au-button--primary" disabled={busy || !a || !b} onClick={() => void compare()}>
        {busy ? <StudioLoader size={16}/> : <RefreshCw size={13}/>}Compare</button>
    </div>
    {error && <div className="tool__error" role="alert">{error}</div>}
    <div className="ptp__scroll">
      {changes && <div className="diff__summary">
        <span>{truncated ? '2000+' : changes.length} differences</span>
        <span className="diff__added">+{changes.filter(c => c.kind === 'added').length}</span>
        <span className="diff__removed">−{changes.filter(c => c.kind === 'removed').length}</span>
        <span className="diff__changed">~{changes.filter(c => c.kind === 'changed').length}</span>
        <button type="button" className="au-button au-button--sm" disabled={!changes.length} onClick={() => void copyReport()}>Copy report</button>
        <span role="status">{notice}</span>
        <input className="au-input" placeholder="Filter paths" aria-label="Filter paths" value={filter} onChange={e => { setFilter(e.target.value); setPage(0); }}/>
      </div>}
      {changes && !changes.length && <p className="ptp__ok">The files are the same.</p>}
      {shown.length > pageSize && <nav className="diff__summary" aria-label="Difference pages">
        <button type="button" className="au-button au-button--sm" disabled={page === 0} onClick={() => setPage(page - 1)}>Previous</button>
        <span aria-live="polite">{page * pageSize + 1}–{Math.min((page + 1) * pageSize, shown.length)} of {shown.length}</span>
        <button type="button" className="au-button au-button--sm" disabled={(page + 1) * pageSize >= shown.length} onClick={() => setPage(page + 1)}>Next</button>
      </nav>}
      {changes && changes.length > 0 && !shown.length && <p className="ptp__hint">No paths match this filter.</p>}
      {!!shown.length && <table className="ptp__table ptp__audit"><thead><tr><th>Path</th><th>First</th><th>Second</th></tr></thead><tbody>{shown.slice(page * pageSize, (page + 1) * pageSize).map((c, i) =>
        <tr key={i} className={'diff__' + c.kind}><td>{c.kind === 'added' ? '+' : c.kind === 'removed' ? '−' : '~'} {c.path}</td>
          <td>{previewValue(c.before)}</td><td>{previewValue(c.after)}</td></tr>)}</tbody></table>}
      {!changes && <p className="ptp__hint">Compares two JSON files of the project path by path (numbers within 1e-6 count as equal). Read-only.</p>}
    </div>
  </div>;
}

function FrameChart({ values, label, budget = false, selected }: {
  values: number[]; label: string; budget?: boolean; selected?: number;
}) {
  const width = 480, height = 90;
  const top = Math.max(budget ? 33.4 : 0.0001, Math.max(0, ...values) * 1.1);
  const x = (i: number) => width * i / Math.max(1, values.length - 1);
  const y = (v: number) => height * (1 - v / top);
  const points = values.map((v, i) => x(i) + ',' + y(v)).join(' ');
  return <svg className="frames__chart" viewBox={`0 0 ${width} ${height}`} preserveAspectRatio="none" role="img" aria-label={label}>
    {budget && [16.7, 33.3].map(ms => <line key={ms} x1={0} x2={width} y1={y(ms)} y2={y(ms)} className="frames__budget"/>)}
    <polyline points={points} className="frames__line"/>
    {selected !== undefined && selected < values.length &&
      <line x1={x(selected)} x2={x(selected)} y1={0} y2={height} className="frames__cursor"/>}
  </svg>;
}

function StatsFrameHistory({ project }: { project: string }) {
  const [frames, setFrames] = useState<StudioGpuStats | null>(null);
  const [error, setError] = useState('');
  useEffect(() => {
    let active = true;
    let timer = 0;
    async function refresh() {
      try {
        const next = await invoke('studio.gpuStats', {});
        if (active) { setFrames(next); setError(next.error ?? ''); }
      } catch (e) {
        if (active) { setFrames(null); setError(message(e)); }
      }
      if (active) timer = window.setTimeout(refresh, 2000);
    }
    void refresh();
    return () => { active = false; window.clearTimeout(timer); };
  }, [project]);
  const ms = (frames?.history ?? []).map(sample => sample[0]);
  const sum = frameSummary(ms);
  return <section aria-label="Scene frame history">
    <h4>Scene frame time</h4>
    {sum.count > 0 && <>
      <div className="stats__cards">
        <div><b>{sum.avg.toFixed(1)} ms</b><span>average</span></div>
        <div><b>{sum.p95.toFixed(1)} ms</b><span>95th percentile</span></div>
        <div><b>{sum.max.toFixed(1)} ms</b><span>peak</span></div>
        <div><b>{frames?.history[ms.length - 1]?.[1]}</b><span>latest draw calls</span></div>
      </div>
      <FrameChart values={ms} label="Frame times" budget/>
    </>}
    <p className="ptp__hint">{error || (sum.count ? sum.count + ' Scene frames · reference lines at 60 and 30 fps' : 'Waiting for Scene frames')}</p>
  </section>;
}

/** Frame Capture: the Scene's last 240 frames (time between frames, draw calls), live or frozen with Capture. */
export function FrameCapturePanel({ snapshot }: Ctx) {
  const path = snapshot?.project.path ?? '';
  const [live, setLive] = useState<StudioGpuStats | null>(null);
  const [selected, setSelected] = useState(0);
  type Capture = StudioGpuStats & { frameIds?: number[] };
  const [recording, setRecording] = useState<{ after: number; previous: Capture | null } | null>(null);
  const [clearedThrough, setClearedThrough] = useState(0);
  const [capturing, setCapturing] = useState(false);
  const [copyStatus, setCopyStatus] = useState('');
  const [frozen, setFrozen] = useState<Capture | null>(null);
  const [error, setError] = useState('');
  useEffect(() => {
    if (!path || frozen) return;
    let active = true, timer = 0;
    const read = async () => {
      try {
        const next = await invoke('studio.gpuStats', {});
        if (active) { setLive({ ...next, history: next.history.slice(Math.max(0, clearedThrough - next.frames + next.history.length)) }); setError(next.error ?? ''); }
      } catch (e) { if (active) setError(message(e)); }
      if (active) timer = window.setTimeout(read, 500);
    };
    void read();
    return () => { active = false; window.clearTimeout(timer); };
  }, [path, frozen, clearedThrough]);
  if (!path) return <div className="tool__empty">Open a project to capture Scene frames.</div>;
  const data = frozen ?? live;
  const ms = (data?.history ?? []).map(h => h[0]), draws = (data?.history ?? []).map(h => h[1]);
  const sum = frameSummary(ms);
  const frameId = (capture: Capture, index: number) =>
    capture.frameIds?.[index] ?? capture.frames - capture.history.length + index + 1;
  const toggleRecording = async () => {
    setCapturing(true); setCopyStatus('');
    try {
      const state = await invoke('studio.gpuStats', recording ? { capture: true, project: path } : {});
      if (recording) {
        const first = Math.max(0, recording.after - state.frames + state.history.length);
        const batch = state.history.slice(first);
        const previous = recording.previous;
        const history = [...(previous?.history ?? []), ...batch].slice(-6000);
        const ids = previous?.history.map((_, i) => frameId(previous, i)) ?? [];
        const addedIds = batch.map((_, i) => state.frames - batch.length + i + 1);
        setFrozen({ ...state, history, frameIds: ids.concat(addedIds).slice(-6000) });
        setSelected(Math.max(0, history.length - 1)); setRecording(null);
      } else {
        setRecording({ after: state.frames, previous: frozen?.frameIds ? frozen : null });
        setClearedThrough(state.frames); setFrozen(null); setLive(null);
      }
      setError(state.error ?? '');
    } catch (e) { setError(message(e)); }
    finally { setCapturing(false); }
  };
  return <div className="ptp">
    <div className="ptp__edhead">
      <button type="button" className="au-button au-button--sm" disabled={capturing} onClick={() => void toggleRecording()}>
        {recording ? 'Pause recording' : frozen?.frameIds?.length ? 'Resume recording' : 'Start recording'}</button>
      {!recording && <button type="button" className={'au-button au-button--sm' + (frozen ? '' : ' au-button--primary')} disabled={capturing || (!frozen && !sum.count)} onClick={async () => {
        setCopyStatus('');
        if (frozen) { setFrozen(null); return; }
        setCapturing(true);
        try {
          const full = await invoke('studio.gpuStats', { capture: true, project: path });
          const history = full.history.slice(Math.max(0, clearedThrough - full.frames + full.history.length));
          setSelected(Math.max(0, history.length - 1));
          setFrozen({ ...full, history }); setError(full.error ?? '');
        } catch (e) { setError(message(e)); }
        finally { setCapturing(false); }
      }}>
        {frozen ? 'Resume live' : 'Capture'}</button>}
      {frozen && <>
        <button type="button" className="au-button au-button--sm" disabled={!sum.count} onClick={async () => {
          const rows = frozen.history.map(([cpu, calls, sim], i) => [frameId(frozen, i), cpu.toFixed(4), calls, sim?.toFixed(4) ?? ''].join(','));
          setCopyStatus(await copyText(['frame,cpu_ms,draw_calls,simulation_ms', ...rows, ''].join('\n')) ? 'CSV copied' : 'Clipboard unavailable');
        }}>Copy CSV</button>
        <span role="status">{copyStatus}</span>
      </>}
      {(recording || frozen) && <button type="button" className="au-button au-button--sm" disabled={capturing || (!recording && !sum.count)} onClick={async () => {
        setCapturing(true);
        try {
          const boundary = frozen?.frames ?? (await invoke('studio.gpuStats', {})).frames;
          setClearedThrough(boundary); setLive(null); setSelected(0); setCopyStatus('');
          if (recording) setRecording({ after: boundary, previous: null });
          if (frozen) setFrozen({ ...frozen, history: [], frameIds: [], drawCalls: undefined });
          setError('');
        } catch (e) { setError(message(e)); }
        finally { setCapturing(false); }
      }}>Clear</button>}
      <span className="ptp__meta">{recording ? 'Recording' : frozen ? 'Captured' : 'Live'} · {recording ? Math.min(6000, (recording.previous?.history.length ?? 0) + Math.max(0, (live?.frames ?? recording.after) - recording.after)) : sum.count} frames · draw calls {data?.drawCalls ?? '—'}</span>
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
      {frozen && sum.count > 0 && <label className="frame-capture__scrub">
        <span>Frame {frameId(frozen, selected)} · {ms[selected].toFixed(2)} ms · {draws[selected]} draw calls{frozen.history[selected][2] !== undefined && ` · Simulation ${frozen.history[selected][2].toFixed(4)} ms`}</span>
        <input type="range" aria-label="Captured frame" min={0} max={sum.count - 1} value={selected}
          onChange={event => setSelected(Number(event.target.value))}/>
      </label>}
      {sum.count > 0 && <div className="frame-capture__plots">
        <figure><figcaption>Scene frame time · ms</figcaption><FrameChart values={ms} label="Frame times" budget selected={frozen ? selected : undefined}/></figure>
        {frozen && frozen.history.every(sample => sample[2] !== undefined) && <figure>
          <figcaption>Simulation step time · ms</figcaption>
          <FrameChart values={frozen.history.map(sample => sample[2]!)} label="Simulation step times" selected={selected}/>
        </figure>}
        <figure><figcaption>Scene draw calls</figcaption><FrameChart values={draws} label="Draw calls" selected={frozen ? selected : undefined}/></figure>
      </div>}
      <p className="ptp__hint">Time between Scene frames (CPU side, includes waiting for the display); lines at 60 and 30 fps. Draw calls of the Scene renderer. Not a GPU command capture.</p>
    </div>
  </div>;
}

function TexturePixels({ project, texture, mip, layer }: { project: string; texture: StudioGpuTexture; mip: number; layer: number }) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const active = useRef(false);
  const [pending, setPending] = useState(false), [ready, setReady] = useState(false), [error, setError] = useState('');
  useEffect(() => { active.current = true; return () => { active.current = false; }; }, []);
  const readPixels = async () => {
    setPending(true); setReady(false); setError('');
    const request = { project, texture: texture.id, mip, layer, ticket: Date.now() + '-' + Math.random() };
    try {
      await invoke('studio.textureReadback', request);
      const deadline = Date.now() + 15000;
      while (active.current && Date.now() < deadline) {
        await new Promise(resolve => window.setTimeout(resolve, 120));
        if (!active.current) return;
        const { pixels, width, height } = await invoke('studio.textureReadback', { ...request, poll: true });
        if (!active.current || !pixels || !width || !height) continue;
        const element = canvas.current;
        const ctx = element?.getContext('2d');
        if (!element || !ctx) throw new Error('Pixel preview unavailable');
        element.width = width; element.height = height;
        const image = ctx.createImageData(width, height);
        pixels.forEach((pixel, i) => image.data.set([pixel >> 16 & 255, pixel >> 8 & 255, pixel & 255, 255], i * 4));
        ctx.putImageData(image, 0, 0); setReady(true); return;
      }
      if (active.current) throw new Error('Timed out waiting for the GPU');
    } catch (e) { if (active.current) setError(message(e)); }
    finally { if (active.current) setPending(false); }
  };
  return <div className="texture-resource__pixels">
    <button type="button" className="au-button au-button--sm" disabled={pending} onClick={() => void readPixels()}>{pending ? 'Reading pixels…' : 'Read GPU pixels'}</button>
    {error && <p role="alert">{error}</p>}
    <canvas ref={canvas} hidden={!ready} aria-label="GPU pixel preview"/>
    {ready && <p className="ptp__hint">Sampled preview up to 32 × 32 pixels.{texture.format === 'RGBA16F' && ' HDR colors use approximate tone mapping.'}</p>}
  </div>;
}

function TextureSubresource({ texture, project }: { texture: StudioGpuTexture; project: string }) {
  const [mip, setMip] = useState(0), [layer, setLayer] = useState(0);
  const extent = (side: number) => Math.max(1, Math.floor(side / 2 ** mip));
  const volume = texture.dimension === '3D';
  const cube = texture.dimension?.startsWith('Cube');
  const slices = volume ? extent(texture.depth) : Math.max(1, texture.layers);
  const sliceLabel = volume ? 'Depth slice' : cube ? 'Cube face' : 'Array layer';
  const canRead = texture.samples === 1 && texture.dimension !== '1D' &&
    ['RGBA8', 'RGBA8_sRGB', 'RGBA16F'].includes(texture.format) &&
    extent(texture.width) * extent(texture.height) * (texture.format === 'RGBA16F' ? 8 : 4) <= 16 * 1024 * 1024;
  const face = ['+X', '−X', '+Y', '−Y', '+Z', '−Z'][layer % 6];
  return <section className="texture-resource__detail" aria-label="Texture subresource">
    <h4>Texture #{texture.id} · {texture.dimension} · {texture.format}</h4>
    <div className="texture-resource__controls">
      <label>Mip level <output>{mip}</output>
        <input type="range" min={0} max={Math.max(0, texture.mips - 1)} value={mip} disabled={texture.mips <= 1}
          aria-label="Mip level" onChange={e => {
            const nextMip = Number(e.target.value);
            setMip(nextMip);
            if (volume) setLayer(current => Math.min(current, Math.max(1, Math.floor(texture.depth / 2 ** nextMip)) - 1));
          }}/>
      </label>
      {slices > 1 && <label>{sliceLabel} <output>{cube ? Math.floor(layer / 6) + ' · ' + face : layer}</output>
        <input type="range" min={0} max={slices - 1} value={layer} aria-label={sliceLabel} onChange={e => setLayer(Number(e.target.value))}/>
      </label>}
    </div>
    <p className="ptp__meta">{extent(texture.width)} × {extent(texture.height)} pixels · {texture.samples}× sampling</p>
    {canRead ? <TexturePixels key={mip + '/' + layer} project={project} texture={texture} mip={mip} layer={layer}/> :
      <p className="ptp__hint">Readback requires a single-sample RGBA8 or RGBA16F subresource up to 16 MiB.</p>}
  </section>;
}

/** Texture Resources: live GPU textures of the Scene renderer with a format-based size estimate. */
export function TextureResourcesPanel({ snapshot }: Ctx) {
  const path = snapshot?.project.path ?? '';
  const [selectedId, setSelectedId] = useState<number | null>(null);
  const [targetsOnly, setTargetsOnly] = useState(false);
  const [largestFirst, setLargestFirst] = useState(true);
  const [gpu, setGpu] = useState<StudioGpuStats['gpu'] | null>(null);
  const [error, setError] = useState('');
  const [filter, setFilter] = useState('');
  const read = useCallback(() => void invoke('studio.gpuStats', { textures: true }).then(s => { setGpu(s.gpu ?? null); setSelectedId(id => s.gpu?.textures.some(texture => texture.id === id) ? id : null); setError(s.error ?? ''); })
    .catch(e => setError(message(e))), []);
  useEffect(() => { if (path) read(); }, [path, read]);
  if (!path) return <div className="tool__empty">Open a project to list GPU textures.</div>;
  const selected = gpu?.textures.find(texture => texture.id === selectedId);
  const query = filter.trim().toLowerCase().replaceAll('×', 'x');
  const rows = (gpu?.textures ?? []).filter(t => (!targetsOnly || t.renderTarget) &&
    [String(t.id), t.dimension ?? '', t.format, `${t.width}x${t.height}`].some(field => field.toLowerCase().includes(query)))
    .sort((a, b) => (largestFirst ? b.bytes - a.bytes : 0) || a.id - b.id);
  return <div className="ptp">
    <div className="ptp__edhead">
      <button type="button" className="au-button au-button--sm" onClick={read}><RefreshCw size={13}/>Refresh</button>
      <span className="ptp__meta">{gpu ? `${gpu.textures.length} textures · ${gpu.textures.filter(t => t.renderTarget).length} targets · ${gpu.textures.filter(t => t.samples > 1).length} multisampled · ${formatBytes(gpu.totalBytes)} estimated` : ''}</span>
    </div>
    <div className="ptp__edhead texture-resources__filters">
      <label><input type="checkbox" checked={targetsOnly} onChange={e => setTargetsOnly(e.target.checked)}/> Render targets only</label>
      <label><input type="checkbox" checked={largestFirst} onChange={e => setLargestFirst(e.target.checked)}/> Largest first</label>
      <input className="au-input" aria-label="Filter textures" placeholder="ID, dimension, format or size" value={filter} onChange={e => setFilter(e.target.value)}/>
    </div>
    {error && <div className="tool__error" role="alert">{error}</div>}
    <div className="ptp__scroll">
      {selected && <TextureSubresource project={path} key={[selected.id, selected.dimension, selected.width, selected.height, selected.depth, selected.layers, selected.mips, selected.format].join('/')} texture={selected}/>}
      <table className="ptp__table ptp__audit">
        <thead><tr><th>ID</th><th>Dimension</th><th>Size</th><th>Format</th><th>Mips</th><th>Kind</th><th>Estimate</th></tr></thead>
        <tbody>{rows.map(t => <tr key={t.id}>
          <td><button type="button" className="au-button au-button--sm" aria-label={'Inspect texture '+t.id} aria-pressed={selectedId === t.id} onClick={() => setSelectedId(t.id)}>{t.id}</button></td><td>{t.dimension ?? '—'}</td><td>{t.width}×{t.height}{t.depth > 1 ? `×${t.depth}` : ''}{t.layers > 1 ? ` [${t.layers}]` : ''}</td>
          <td>{t.format}</td><td>{t.mips}</td><td>{t.renderTarget ? 'target' : 'sampled'}{t.samples > 1 ? ` ×${t.samples}` : ''}</td>
          <td className="stats__num">{formatBytes(t.bytes)}</td></tr>)}</tbody>
      </table>
      {gpu && !rows.length && <p className="ptp__hint">{gpu.textures.length ? 'No textures match these filters.' : 'No Scene textures allocated.'}</p>}
      <p className="ptp__hint">Estimated from format and size (tightly packed); drivers may pad. Scene renderer only.</p>
    </div>
  </div>;
}
