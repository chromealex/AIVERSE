import { useCallback, useEffect, useMemo, useState, type KeyboardEvent } from 'react';
import { AlertTriangle, CheckCircle2, FilePlus2, Play, Plus, RefreshCw, RotateCcw, Save, ScanSearch, Trash2 } from 'lucide-react';
import { invoke } from '../bridge';
import type { StudioInputAsset, StudioInputInfo, StudioSnapshot } from '../types';
import './ToolPanel.css';
import './ProjectToolPanels.css';
import { StudioLoader } from '../ui';

const message = (e: unknown) => e instanceof Error ? e.message : String(e);
const op = async (name: string, params: Record<string, unknown> = {}) => (await invoke('studio.op', { op: name, params })).result as Record<string, any>;
type Ctx = { snapshot: StudioSnapshot | null; commitSnapshot: (s: StudioSnapshot) => void };
const refreshSnapshot = (ctx: Ctx) => void invoke('studio.snapshot', {}).then(ctx.commitSnapshot).catch(() => {});

/* ---------------- Fog of war (rules.json fogOfWar through rules_merge) ---------------- */
function rgbHex(c: unknown, def: string): string {
  if (!Array.isArray(c) || c.length < 3) return def;
  return '#' + c.slice(0, 3).map(v => { const n = Number(v); return Math.round(Math.min(255, Math.max(0, n <= 1 && n > 0 && !Number.isInteger(n) ? n * 255 : n))).toString(16).padStart(2, '0'); }).join('');
}
const hexRgb = (h: string) => [1, 3, 5].map(i => parseInt(h.slice(i, i + 2), 16));

function Slider({ label, value, min, max, step = .01, onCommit }: { label: string; value: number; min: number; max: number; step?: number; onCommit: (v: number) => void }) {
  const [v, setV] = useState(value);
  useEffect(() => setV(value), [value]);
  return <label className="ptp__row"><span>{label}</span>
    <input type="range" min={min} max={max} step={step} value={v} onChange={e => setV(Number(e.target.value))}
      onPointerUp={() => v !== value && onCommit(v)} onKeyUp={() => v !== value && onCommit(v)}/>
    <output>{step >= 1 ? v : v.toFixed(2)}</output></label>;
}
function Segmented<T extends string>({ value, options, onChange }: { value: T; options: [T, string][]; onChange: (v: T) => void }) {
  return <div className="ptp__seg" role="radiogroup">{options.map(([id, label]) =>
    <button key={id} type="button" role="radio" aria-checked={value === id} className={value === id ? 'is-on' : ''} onClick={() => onChange(id)}>{label}</button>)}</div>;
}

export function FogPanel(ctx: Ctx) {
  const [fog, setFog] = useState<Record<string, any>>({});
  const [error, setError] = useState('');
  const path = ctx.snapshot?.project.path ?? '';
  const read = useCallback(async () => {
    // Whole rules (no path): a game without a fogOfWar block is not an error.
    try { const v = (await op('rules_get')).value; setFog(v && typeof v.fogOfWar === 'object' ? v.fogOfWar : {}); setError(''); }
    catch (e) { setError(message(e)); }
  }, []);
  useEffect(() => { if (path) void read(); }, [path, ctx.snapshot, read]);
  const set = async (key: string, value: unknown) => {
    try { await op('rules_merge', { path: 'fogOfWar', value: { [key]: value } }); await read(); refreshSnapshot(ctx); }
    catch (e) { setError(message(e)); }
  };
  if (!path) return <div className="tool__empty">Open a project to edit its fog of war.</div>;
  const mode = fog.mode === 'off' ? 'none' : (fog.mode ?? 'full');
  const grid = fog.grid && typeof fog.grid === 'object' ? fog.grid : null;
  const gridShow = !grid || Number(grid.alpha ?? .12) <= 0 ? 'none' : grid.show === 'always' ? 'all' : (grid.show ?? 'fog');
  const num = (k: string, d: number) => typeof fog[k] === 'number' ? fog[k] : d;
  return <div className="ptp">
    {error && <div className="tool__error" role="alert">{error}</div>}
    <div className="ptp__scroll">
      <h4>Mode <small>affects the game</small></h4>
      <Segmented value={mode} options={[['full', 'Full'], ['explored', 'Explored'], ['none', 'No fog']]} onChange={v => void set('mode', v)}/>
      <p className="ptp__hint">{mode === 'full' ? 'Black map; explored areas are darker.' : mode === 'explored' ? 'Map open; enemies only inside vision.' : 'Everyone sees everything.'}</p>
      <label className="ptp__check"><input type="checkbox" checked={fog.shareAllies ?? true} onChange={e => void set('shareAllies', e.target.checked)}/>Allies share vision</label>
      <label className="ptp__check"><input type="checkbox" checked={fog.lineOfSight ?? false} onChange={e => void set('lineOfSight', e.target.checked)}/>3D line of sight over heights</label>
      {fog.lineOfSight && <Slider label="Rock height" value={num('rockHeight', 2)} min={0} max={6} step={.1} onCommit={v => void set('rockHeight', Math.round(v * 10) / 10)}/>}
      <h4>Look</h4>
      <label className="ptp__check"><input type="checkbox" checked={fog.show ?? true} onChange={e => void set('show', e.target.checked)}/>Draw fog</label>
      <label className="ptp__row"><span>Color</span><input type="color" value={rgbHex(fog.color, '#080a0e')} onChange={e => void set('color', hexRgb(e.target.value))}/></label>
      <Slider label="Unexplored" value={num('unexplored', .92)} min={0} max={1} onCommit={v => void set('unexplored', +v.toFixed(2))}/>
      <Slider label="Explored" value={num('explored', .47)} min={0} max={1} onCommit={v => void set('explored', +v.toFixed(2))}/>
      <label className="ptp__row"><span>Style</span>
        <select className="au-input" value={fog.style ?? 'smooth'} onChange={e => void set('style', e.target.value)}>
          <option value="smooth">Soft edges</option><option value="cells">Cells</option><option value="grid">Cells with grid</option><option value="pattern">Pattern (classic)</option>
        </select></label>
      <Slider label="Softness, cells" value={num('softness', 2)} min={0} max={6} step={1} onCommit={v => void set('softness', v)}/>
      <Slider label="Reveal fade, s" value={num('fade', .3)} min={0} max={3} onCommit={v => void set('fade', +v.toFixed(2))}/>
      <Slider label="Cloudy edges" value={num('noise', 0)} min={0} max={1} onCommit={v => void set('noise', +v.toFixed(2))}/>
      <Slider label="Layer height" value={num('height', 1.3)} min={0} max={4} onCommit={v => void set('height', +v.toFixed(2))}/>
      <h4>Cell grid</h4>
      <Segmented value={gridShow} options={[['none', 'None'], ['fog', 'In fog'], ['all', 'Everywhere']]}
        onChange={v => void set('grid', { ...(grid ?? {}), show: v, alpha: v === 'none' ? 0 : (Number(grid?.alpha) > 0 ? grid!.alpha : .15) })}/>
      {gridShow !== 'none' && <>
        <Slider label="Line brightness" value={Number(grid?.alpha ?? .15)} min={0} max={1} onCommit={v => void set('grid', { ...grid, alpha: +v.toFixed(2) })}/>
        <label className="ptp__row"><span>Line color</span><input type="color" value={rgbHex(grid?.color, '#ffffff')} onChange={e => void set('grid', { ...grid, color: hexRgb(e.target.value) })}/></label>
      </>}
      <div className="ptp__actions">
        <button type="button" className="au-button au-button--sm" onClick={() => void op('rules_delete', { path: 'fogOfWar' }).then(read).then(() => refreshSnapshot(ctx)).catch(e => setError(message(e)))}>
          <RotateCcw size={13}/>Reset to defaults</button>
        <span className="ptp__hint">rules.json → fogOfWar · Play the game to see it</span>
      </div>
    </div>
  </div>;
}

/* ---------------- Scripts (Lua) ---------------- */
interface ScriptFile { path: string; role: string }
export function ScriptsPanel(ctx: Ctx) {
  const path = ctx.snapshot?.project.path ?? '';
  const [files, setFiles] = useState<ScriptFile[]>([]);
  const [missing, setMissing] = useState<string[]>([]);
  const [current, setCurrent] = useState('');
  const [text, setText] = useState('');
  const [saved, setSaved] = useState('');
  const [status, setStatus] = useState<{ ok: boolean; text: string } | null>(null);
  const [check, setCheck] = useState<{ errors: string[]; log: string[]; ok: boolean } | null>(null);
  const [busy, setBusy] = useState(false);
  const [newPath, setNewPath] = useState<string | null>(null);
  const list = useCallback(async () => {
    try { const r = await op('script_list'); setFiles(r.files ?? []); setMissing(r.missing ?? []); }
    catch (e) { setStatus({ ok: false, text: message(e) }); }
  }, []);
  useEffect(() => { if (path) void list(); else { setFiles([]); setCurrent(''); } }, [path, list]);
  const dirty = text !== saved;
  const open = async (p: string) => {
    if (dirty && !window.confirm('Discard unsaved changes to ' + current + '?')) return;
    try { const r = await op('script_read', { path: p }); setCurrent(p); setText(r.text ?? ''); setSaved(r.text ?? ''); setStatus(null); }
    catch (e) { setStatus({ ok: false, text: message(e) }); }
  };
  const save = async (p = current, body = text, register = false) => {
    if (!p) return;
    setBusy(true);
    try { await op('script_write', { path: p, text: body, ...(register ? { register: true } : {}) }); setSaved(body); setCurrent(p); setStatus({ ok: true, text: 'Saved ' + p }); await list(); refreshSnapshot(ctx); }
    catch (e) { setStatus({ ok: false, text: message(e) }); }
    finally { setBusy(false); }
  };
  const runCheck = async () => {
    setBusy(true); setCheck(null);
    try { const r = await op('script_check', { seconds: 30 }); setCheck({ ok: !!r.ok, errors: r.errors ?? [], log: r.log ?? [] }); }
    catch (e) { setStatus({ ok: false, text: message(e) }); }
    finally { setBusy(false); }
  };
  const create = async () => {
    let p = (newPath ?? '').trim();
    if (!p) return;
    if (!p.startsWith('scripts/')) p = 'scripts/' + p;
    if (!p.endsWith('.lua')) p += '.lua';
    setNewPath(null);
    await save(p, '-- ' + p + '\n', true);
    setText('-- ' + p + '\n');
  };
  const onKey = (e: KeyboardEvent<HTMLTextAreaElement>) => {
    if ((e.metaKey || e.ctrlKey) && e.key === 's') { e.preventDefault(); void save(); return; }
    if (e.key === 'Tab') {
      e.preventDefault();
      const el = e.currentTarget, s = el.selectionStart, end = el.selectionEnd;
      const next = text.slice(0, s) + '  ' + text.slice(end);
      setText(next);
      requestAnimationFrame(() => { el.selectionStart = el.selectionEnd = s + 2; });
    }
  };
  const lines = useMemo(() => text.split('\n').length, [text]);
  if (!path) return <div className="tool__empty">Open a project to edit its Lua scripts.</div>;
  return <div className="ptp ptp--split">
    <aside className="ptp__files">
      <div className="ptp__fileshead"><b>Scripts</b>
        <button type="button" className="au-button au-button--icon au-button--ghost au-button--sm" title="New script" onClick={() => setNewPath('')}><FilePlus2 size={13}/></button>
        <button type="button" className="au-button au-button--icon au-button--ghost au-button--sm" title="Refresh" onClick={() => void list()}><RefreshCw size={13}/></button></div>
      {newPath !== null && <form className="ptp__new" onSubmit={e => { e.preventDefault(); void create(); }}>
        <input autoFocus className="au-input" placeholder="scripts/name.lua" value={newPath} onChange={e => setNewPath(e.target.value)}
          onKeyDown={e => e.key === 'Escape' && setNewPath(null)}/></form>}
      {files.map(f => <button key={f.path} type="button" className={'ptp__file' + (f.path === current ? ' is-on' : '')} onClick={() => void open(f.path)} title={f.role}>
        <span>{f.path.replace(/^scripts\//, '')}</span><small>{f.role.startsWith('module') ? 'module' : f.role}</small></button>)}
      {missing.map(m => <div key={m} className="ptp__file is-missing" title="Listed in rules or the map, file not found"><AlertTriangle size={11}/><span>{m}</span></div>)}
      {!files.length && !missing.length && <p className="ptp__hint">No scripts yet.</p>}
    </aside>
    <section className="ptp__editor">
      <div className="ptp__edhead">
        <b>{current || 'No script open'}{dirty && ' •'}</b>
        <button type="button" className="au-button au-button--sm au-button--primary" disabled={!current || !dirty || busy} onClick={() => void save()}><Save size={13}/>Save</button>
        <button type="button" className="au-button au-button--sm" disabled={busy} onClick={() => void runCheck()} title="Compile every script and run a 30 s bot match on the open map">
          {busy ? <StudioLoader size={16}/> : <Play size={13}/>}Check</button>
      </div>
      {status && <div className={'ptp__status ' + (status.ok ? 'is-ok' : 'is-error')} role={status.ok ? 'status' : 'alert'}>{status.text}</div>}
      <div className="ptp__code">
        <pre className="ptp__gutter" aria-hidden>{Array.from({ length: lines }, (_, i) => i + 1).join('\n')}</pre>
        <textarea spellCheck={false} value={text} disabled={!current} onChange={e => setText(e.target.value)} onKeyDown={onKey}
          onScroll={e => { const g = e.currentTarget.previousElementSibling as HTMLElement | null; if (g) g.scrollTop = e.currentTarget.scrollTop; }}/>
      </div>
      {check && <div className={'ptp__check-out ' + (check.ok ? 'is-ok' : 'is-error')}>
        <b>{check.ok ? <><CheckCircle2 size={12}/> No errors</> : <><AlertTriangle size={12}/> {check.errors.length} error{check.errors.length > 1 ? 's' : ''}</>}</b>
        {check.errors.map((e, i) => <div key={'e' + i} className="ptp__err">{e}</div>)}
        {check.log.slice(-40).map((l, i) => <div key={'l' + i}>{l}</div>)}
      </div>}
    </section>
  </div>;
}

/* ---------------- Input actions (input/actions.json) ---------------- */
type InputAction = StudioInputAsset['actions'][number];
type InputContext = StudioInputAsset['contexts'][number];
type InputMapping = InputContext['mappings'][number];
export function InputPanel(ctx: Ctx) {
  const path = ctx.snapshot?.project.path ?? '';
  const [info, setInfo] = useState<StudioInputInfo | null>(null);
  const [asset, setAsset] = useState<StudioInputAsset>({ version: 1, actions: [], contexts: [] });
  const [dirty, setDirty] = useState(false);
  const [status, setStatus] = useState<{ ok: boolean; text: string } | null>(null);
  const load = useCallback(async () => {
    try { const r = await invoke('studio.inputGet', {}); setInfo(r); setAsset(r.asset); setDirty(false); setStatus(r.error ? { ok: false, text: r.error } : null); }
    catch (e) { setStatus({ ok: false, text: message(e) }); }
  }, []);
  useEffect(() => { if (path) void load(); }, [path, load]);
  const edit = (next: StudioInputAsset) => { setAsset(next); setDirty(true); };
  const save = async () => {
    try { const r = await invoke('studio.inputSet', { asset }); setInfo(r); setAsset(r.asset); setDirty(false); setStatus({ ok: true, text: 'Saved ' + r.path }); }
    catch (e) { setStatus({ ok: false, text: message(e) }); }
  };
  if (!path) return <div className="tool__empty">Open a project to edit its input actions.</div>;
  const setAction = (i: number, a: Partial<InputAction>) => edit({ ...asset, actions: asset.actions.map((x, n) => n === i ? { ...x, ...a } : x) });
  const renameAction = (i: number, name: string) => {
    const old = asset.actions[i].name;
    edit({ actions: asset.actions.map((x, n) => n === i ? { ...x, name } : x), version: 1,
      contexts: asset.contexts.map(c => ({ ...c, mappings: c.mappings.map(m => m.action === old ? { ...m, action: name } : m) })) });
  };
  const setContext = (i: number, c: Partial<InputContext>) => edit({ ...asset, contexts: asset.contexts.map((x, n) => n === i ? { ...x, ...c } : x) });
  const setMapping = (ci: number, mi: number, m: Partial<InputMapping>) =>
    setContext(ci, { mappings: asset.contexts[ci].mappings.map((x, n) => n === mi ? { ...x, ...m } : x) });
  const codes = (device: string): string[] => device === 'keyboard' ? info?.keys ?? [] : device === 'gamepadButton' ? info?.padButtons ?? []
    : device === 'gamepadAxis' ? info?.padAxes ?? [] : device === 'mouseButton' ? ['left', 'right', 'middle', 'x1', 'x2']
    : device === 'mouseAxis' || device === 'mouseWheel' ? ['x', 'y'] : [];
  const unique = (base: string, taken: string[]) => { let n = base, i = 2; while (taken.includes(n)) n = base + i++; return n; };
  const nextId = () => 1 + Math.max(0, ...asset.contexts.flatMap(c => c.mappings.map(m => m.id ?? 0)));
  return <div className="ptp">
    <div className="ptp__edhead">
      <b>{info?.path ?? 'input/actions.json'}{!info?.exists && ' (new)'}{dirty && ' •'}</b>
      <button type="button" className="au-button au-button--sm au-button--primary" disabled={!dirty} onClick={() => void save()}><Save size={13}/>Save</button>
      <button type="button" className="au-button au-button--sm" disabled={!dirty} onClick={() => void load()}><RotateCcw size={13}/>Revert</button>
    </div>
    {status && <div className={'ptp__status ' + (status.ok ? 'is-ok' : 'is-error')} role={status.ok ? 'status' : 'alert'}>{status.text}</div>}
    <div className="ptp__scroll">
      <h4>Actions <small>what the player can do</small>
        <button type="button" className="au-button au-button--ghost au-button--sm" onClick={() => edit({ ...asset, actions: [...asset.actions, { name: unique('Action', asset.actions.map(a => a.name)), type: 'bool' }] })}><Plus size={12}/>Action</button></h4>
      <table className="ptp__table ptp__table--rows"><tbody>
        {asset.actions.map((a, i) => <tr key={i}>
          <td><input className="au-input" value={a.name} onChange={e => renameAction(i, e.target.value)}/></td>
          <td><select className="au-input" value={a.type} onChange={e => setAction(i, { type: e.target.value as InputAction['type'] })}>
            {['bool', 'axis1D', 'axis2D', 'axis3D'].map(t => <option key={t}>{t}</option>)}</select></td>
          <td className="ptp__meta">{(a.modifiers?.length ?? 0) + (a.triggers?.length ?? 0) ? `${a.modifiers?.length ?? 0} mod · ${a.triggers?.length ?? 0} trig` : ''}</td>
          <td><button type="button" className="au-button au-button--icon au-button--ghost au-button--sm" title="Delete action"
            onClick={() => edit({ ...asset, actions: asset.actions.filter((_, n) => n !== i), contexts: asset.contexts.map(c => ({ ...c, mappings: c.mappings.filter(m => m.action !== a.name) })) })}><Trash2 size={12}/></button></td>
        </tr>)}
      </tbody></table>
      {!asset.actions.length && <p className="ptp__hint">No actions. Add one, then bind keys to it in a context.</p>}
      <h4>Contexts <small>sets of bindings, higher priority wins</small>
        <button type="button" className="au-button au-button--ghost au-button--sm" onClick={() => edit({ ...asset, contexts: [...asset.contexts, { name: unique('Gameplay', asset.contexts.map(c => c.name)), priority: 0, active: true, mappings: [] }] })}><Plus size={12}/>Context</button></h4>
      {asset.contexts.map((c, ci) => <div key={ci} className="ptp__ctx">
        <div className="ptp__ctxhead">
          <input className="au-input" value={c.name} onChange={e => setContext(ci, { name: e.target.value })}/>
          <label>Priority <input className="au-input ptp__narrow" type="number" value={c.priority ?? 0} onChange={e => setContext(ci, { priority: Math.round(Number(e.target.value) || 0) })}/></label>
          <label className="ptp__check"><input type="checkbox" checked={c.active ?? false} onChange={e => setContext(ci, { active: e.target.checked })}/>Active</label>
          <button type="button" className="au-button au-button--icon au-button--ghost au-button--sm" title="Delete context" onClick={() => edit({ ...asset, contexts: asset.contexts.filter((_, n) => n !== ci) })}><Trash2 size={12}/></button>
        </div>
        <table className="ptp__table ptp__table--rows"><tbody>
          {c.mappings.map((m, mi) => <tr key={mi}>
            <td><select className="au-input" value={m.action} onChange={e => setMapping(ci, mi, { action: e.target.value })}>
              {asset.actions.map(a => <option key={a.name}>{a.name}</option>)}</select></td>
            <td><select className="au-input" value={m.control.device} onChange={e => setMapping(ci, mi, { control: { device: e.target.value, code: codes(e.target.value)[0] ?? 0 } })}>
              {(info?.devices ?? []).map(d => <option key={d}>{d}</option>)}</select></td>
            <td>{codes(m.control.device).length
              ? <select className="au-input" value={String(m.control.code)} onChange={e => setMapping(ci, mi, { control: { ...m.control, code: e.target.value } })}>
                  {codes(m.control.device).map(k => <option key={k}>{k}</option>)}</select>
              : <input className="au-input ptp__narrow" type="number" value={Number(m.control.code) || 0} onChange={e => setMapping(ci, mi, { control: { ...m.control, code: Math.round(Number(e.target.value) || 0) } })}/>}</td>
            <td className="ptp__consume"><label className="ptp__check" title="Stop lower-priority contexts from seeing this input"><input type="checkbox" checked={m.consume ?? false} onChange={e => setMapping(ci, mi, { consume: e.target.checked })}/>Consume</label></td>
            <td><button type="button" className="au-button au-button--icon au-button--ghost au-button--sm" title="Delete binding" onClick={() => setContext(ci, { mappings: c.mappings.filter((_, n) => n !== mi) })}><Trash2 size={12}/></button></td>
          </tr>)}
        </tbody></table>
        <button type="button" className="au-button au-button--ghost au-button--sm" disabled={!asset.actions.length}
          onClick={() => setContext(ci, { mappings: [...c.mappings, { id: nextId(), action: asset.actions[0].name, control: { device: 'keyboard', code: 'Space' }, consume: false }] })}><Plus size={12}/>Binding</button>
      </div>)}
      <p className="ptp__hint">Input actions are presentation only: the deterministic simulation never reads them.</p>
    </div>
  </div>;
}

/* ---------------- Asset Audit ---------------- */
export function AuditPanel(ctx: Ctx) {
  const path = ctx.snapshot?.project.path ?? '';
  const [result, setResult] = useState<{ missing: { source: string; reference: string }[]; scanned: number; truncated: boolean } | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const scan = async () => {
    setBusy(true);
    try { setResult(await invoke('studio.assetAudit', {})); setError(''); }
    catch (e) { setError(message(e)); }
    finally { setBusy(false); }
  };
  useEffect(() => { setResult(null); }, [path]);
  if (!path) return <div className="tool__empty">Open a project to audit its asset references.</div>;
  return <div className="ptp">
    <div className="ptp__edhead">
      <button type="button" className="au-button au-button--sm au-button--primary" disabled={busy} onClick={() => void scan()}>
        {busy ? <StudioLoader size={16}/> : <ScanSearch size={13}/>}Scan references</button>
      {result && <span className="ptp__meta">{result.scanned} files checked · {result.missing.length} missing{result.truncated && ' · limit reached, partial'}</span>}
    </div>
    {error && <div className="tool__error" role="alert">{error}</div>}
    <div className="ptp__scroll">
      {result && !result.missing.length && <p className="ptp__ok"><CheckCircle2 size={13}/>No broken references found. This is a text scan of JSON, Lua, RML and RCSS files, not a full asset graph.</p>}
      {!!result?.missing.length && <table className="ptp__table ptp__audit">
        <thead><tr><th>File</th><th>Missing reference</th></tr></thead>
        <tbody>{result.missing.map((r, i) => <tr key={i}><td>{r.source}</td><td className="ptp__err">{r.reference}</td></tr>)}</tbody>
      </table>}
      {!result && <p className="ptp__hint">Finds quoted paths to images, models, sounds, scripts and UI files that point at no file in the project or the engine.</p>}
    </div>
  </div>;
}
