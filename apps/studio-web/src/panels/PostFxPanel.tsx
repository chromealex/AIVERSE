import { useCallback, useEffect, useMemo, useState } from 'react';
import { Eye, EyeOff, Plus, Trash2, Wrench } from 'lucide-react';
import { invoke } from '../bridge';
import type { StudioSnapshot } from '../types';
import ToolPanel from './ToolPanel';
import PrismaPreviewControls from './PrismaPreviewControls';
import PrismaFrameDiagnostics from './PrismaFrameDiagnostics';
import PrismaVolumeEditor from './PrismaVolumeEditor';
import PrismaCompositorEditor from './PrismaCompositorEditor';
import './ToolPanel.css';
import './ProjectToolPanels.css';
import './PostFxPanel.css';

type Scope = 'game' | 'map';
export interface CatalogParam { name: string; kind: string; range?: [number, number]; default?: unknown; help: string }
export interface CatalogEffect { type: string; help: string; class: string; defaultMinQuality: string; params: CatalogParam[]; msPerFrame: Record<string, Record<string, number>> }
interface Catalog { effects: CatalogEffect[]; presets: Array<{ id: string; help: string }>; profiles?: Array<{ id: string; device: string }> }
interface ChainTotal { device: string; tier: string; ms: number; fpsAt60: number; effects: number; passes: number }
interface PostFxState { gameVolumes: string; mapVolumes: string; game: any; map: any; chain: { effects: any[]; total: Record<string, ChainTotal>; warnings: string[]; enabled: boolean; issues?: string[] } }

const message = (e: unknown) => e instanceof Error ? e.message : String(e);
const op = async (name: string, params: Record<string, unknown> = {}) => (await invoke('studio.op', { op: name, params })).result as any;
const hex = (c: unknown) => Array.isArray(c) ? '#' + c.slice(0, 3).map(v => Math.round(Math.min(1, Math.max(0, Number(v))) * 255).toString(16).padStart(2, '0')).join('') : '#ffffff';
const rgb = (h: string) => [1, 3, 5].map(i => +(parseInt(h.slice(i, i + 2), 16) / 255).toFixed(3));

function NumberParam({ p, value, onCommit }: { p: CatalogParam; value: number; onCommit: (v: number) => void }) {
  const [v, setV] = useState(value);
  useEffect(() => setV(value), [value]);
  const [lo, hi] = p.range ?? [0, 1];
  const step = (hi - lo) / 200;
  return <label className="ptp__row" title={p.help}><span>{p.name}</span>
    <input type="range" min={lo} max={hi} step={step} value={v} onChange={e => setV(Number(e.target.value))}
      onPointerUp={() => v !== value && onCommit(+v.toFixed(3))} onKeyUp={() => v !== value && onCommit(+v.toFixed(3))}/>
    <output>{Math.abs(v) >= 10 ? v.toFixed(1) : v.toFixed(2)}</output></label>;
}

/** PRISMA post-processing: the chain as cards (enable, quality tier, parameters), add / presets, cost per device class. */
export default function PostFxPanel({ snapshot, commitSnapshot }: { snapshot: StudioSnapshot | null; commitSnapshot: (s: StudioSnapshot) => void }) {
  const path = snapshot?.project.path ?? '';
  const [catalog, setCatalog] = useState<Catalog | null>(null);
  const [state, setState] = useState<PostFxState | null>(null);
  const [scope, setScope] = useState<Scope>('game');
  const [error, setError] = useState('');
  const [advanced, setAdvanced] = useState(false);
  const [adding, setAdding] = useState(false);
  const read = useCallback(async () => {
    try { setState(await op('postfx_get')); setError(''); } catch (e) { setError(message(e)); }
  }, []);
  useEffect(() => {
    if (!path) return;
    void op('postfx_catalog').then(setCatalog).catch(e => setError(message(e)));
    void read();
  }, [path, read]);
  useEffect(() => { if (path) void read(); }, [snapshot]); // eslint-disable-line react-hooks/exhaustive-deps
  const run = async (name: string, params: Record<string, unknown>) => {
    try {
      const r = await op(name, { ...params, scope });
      if (Array.isArray(r?.warnings) && r.warnings.length) setError(r.warnings.join(' · ')); else setError('');
      await read();
      void invoke('studio.snapshot', {}).then(commitSnapshot).catch(() => {});
    } catch (e) { setError(message(e)); }
  };
  const block = scope === 'game' ? state?.game : state?.map;
  const effects: any[] = Array.isArray(block?.effects) ? block.effects : [];
  const byType = useMemo(() => new Map((catalog?.effects ?? []).map(e => [e.type, e])), [catalog]);
  if (!path) return <div className="tool__empty">Open a project to edit post-processing.</div>;
  if (advanced) return <div className="pfx__adv">
    <button type="button" className="au-button au-button--sm au-button--ghost" onClick={() => setAdvanced(false)}>← Back to the effect list</button>
    <ToolPanel panel="postfx" snapshot={snapshot} commitSnapshot={commitSnapshot}/></div>;
  const totals = Object.entries(state?.chain.total ?? {});
  const present = new Set(effects.map(e => e.type));
  return <div className="ptp">
    <div className="ptp__edhead">
      <div className="ptp__seg" role="radiogroup" aria-label="Scope">
        <button type="button" role="radio" aria-checked={scope === 'game'} className={scope === 'game' ? 'is-on' : ''} onClick={() => setScope('game')}>Game</button>
        <button type="button" role="radio" aria-checked={scope === 'map'} className={scope === 'map' ? 'is-on' : ''} onClick={() => setScope('map')}>This map</button>
      </div>
      <label className="ptp__check" title="Turn the whole chain on or off"><input type="checkbox" checked={state?.chain.enabled ?? true}
        onChange={e => void run('postfx_enable', { enabled: e.target.checked })}/>Enabled</label>
      <button type="button" className="au-button au-button--icon au-button--ghost au-button--sm" title="All operations" onClick={() => setAdvanced(true)}><Wrench size={13}/></button>
    </div>
    {error && <div className="tool__error" role="alert">{error}</div>}
    <div className="ptp__scroll">
      <div className="pfx__cost">{totals.map(([id, t]) => {
        const budget = id === 'desktop' ? 1.5 : 3;
        return <div key={id} className={'pfx__tier' + (t.ms > budget ? ' is-over' : '')} title={`${t.effects} effects, ${t.passes} passes on ${t.tier}`}>
          <span>{t.device}</span><b>{t.ms.toFixed(2)} ms</b><small>60 → {t.fpsAt60.toFixed(0)} fps</small>
          <i style={{ width: Math.min(100, (t.ms / (budget * 2)) * 100) + '%' }}/></div>;
      })}</div>
      <PrismaPreviewControls key={path} project={path}/>
      <PrismaFrameDiagnostics key={JSON.stringify([path,snapshot?.graphDocument?.epoch])} project={path}/>
      {scope === 'map' && <p className="ptp__hint">Map effects override the game's chain for this map only.</p>}
      {state && snapshot?.graphDocument && <PrismaVolumeEditor key={JSON.stringify([path,snapshot.graphDocument.epoch,scope])}
        scope={scope} catalog={catalog?.effects??[]} project={path} stored={scope==='map'?state.mapVolumes:state.gameVolumes}
        save={async (block, expected) => {
          const doc = snapshot.graphDocument!;
          await invoke('studio.op',{op:'postfx_volumes_set',params:{scope,block,expected},
            expectedProject:path,expectedDocumentEpoch:doc.epoch,expectedDocumentRevision:doc.revision});
          await read();
          commitSnapshot(await invoke('studio.snapshot',{}));
        }}/>}
      {effects.map(e => {
        const def = byType.get(e.type);
        return <section key={e.type} className={'pfx__fx' + (e.enabled === false ? ' is-off' : '')}>
          <header>
            <button type="button" className="au-button au-button--icon au-button--ghost au-button--sm" title={e.enabled === false ? 'Enable' : 'Disable'}
              onClick={() => void run('postfx_set', { type: e.type, enabled: e.enabled === false })}>{e.enabled === false ? <EyeOff size={13}/> : <Eye size={13}/>}</button>
            <b title={def?.help}>{e.type}</b>
            {def && <span className={'pfx__class pfx__class--' + def.class}>{def.class}</span>}
            <div className="ptp__seg pfx__q" role="radiogroup" aria-label="Lowest quality tier">
              {(['low', 'medium', 'high'] as const).map(q => <button key={q} type="button" role="radio" aria-checked={(e.minQuality ?? def?.defaultMinQuality) === q}
                className={(e.minQuality ?? def?.defaultMinQuality) === q ? 'is-on' : ''} title={'Runs on ' + q + ' and above'}
                onClick={() => void run('postfx_set', { type: e.type, minQuality: q })}>{q}</button>)}
            </div>
            <button type="button" className="au-button au-button--icon au-button--ghost au-button--sm" title="Remove" onClick={() => void run('postfx_remove', { type: e.type })}><Trash2 size={12}/></button>
          </header>
          {(def?.params ?? []).map(p => {
            const value = e[p.name] ?? p.default;
            if (p.kind === 'number') return <NumberParam key={p.name} p={p} value={Number(value ?? 0)} onCommit={v => void run('postfx_set', { type: e.type, params: { [p.name]: v } })}/>;
            if (p.kind.startsWith('color')) return <label key={p.name} className="ptp__row" title={p.help}><span>{p.name}</span>
              <input type="color" value={hex(value)} onChange={ev => void run('postfx_set', { type: e.type, params: { [p.name]: rgb(ev.target.value) } })}/></label>;
            if (p.kind === 'bool') return <label key={p.name} className="ptp__check" title={p.help}><input type="checkbox" checked={!!value}
              onChange={ev => void run('postfx_set', { type: e.type, params: { [p.name]: ev.target.checked } })}/>{p.name}</label>;
            return null;
          })}
        </section>;
      })}
      {!effects.length && <p className="ptp__hint">No post effects {scope === 'map' ? 'override this map' : 'in the game yet'}.</p>}
      <h4>Add effect <button type="button" className="au-button au-button--ghost au-button--sm" onClick={() => setAdding(!adding)}><Plus size={12}/>{adding ? 'Hide' : 'Show'}</button></h4>
      {adding && <div className="tbrush__chips">{(catalog?.effects ?? []).filter(e => !present.has(e.type)).map(e =>
        <button key={e.type} type="button" title={e.help + ' · ' + e.class} onClick={() => void run('postfx_add', { type: e.type })}>{e.type}<em className={'pfx__dot pfx__class--' + e.class}/></button>)}</div>}
      {state && <PrismaCompositorEditor key={path+'|'+scope+'|'+(scope==='map'?snapshot?.graphDocument?.mapPath:'')}
        scope={scope} stored={block?.compositor} save={async params=>{
          await op('postfx_compositor_set',{...params,scope});
          await read();
          void invoke('studio.snapshot',{}).then(commitSnapshot).catch(()=>{});
        }}/>}
      <h4>Presets</h4>
      <div className="tbrush__chips">{(catalog?.presets ?? []).map(p =>
        <button key={p.id} type="button" title={p.help + ' (replaces the chain)'} onClick={() => void run('postfx_preset', { preset: p.id, mode: 'replace' })}>{p.id}</button>)}</div>
      {!!state?.chain.warnings?.length && <div className="pfx__warn">{state.chain.warnings.map((w, i) => <p key={i}>{w}</p>)}</div>}
    </div>
  </div>;
}
