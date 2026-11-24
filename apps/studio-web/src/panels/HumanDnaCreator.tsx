// Digital Human Creator window: all tools/vivid-creator authoring sections in
// Studio. Every edit is a validated native call (studio.humanCreator*); the
// native StudioSession owns the document, undo history and project files, and
// the Metal viewport regenerates the VIVID human from it. React never writes
// project files and never decides that GPU output is correct.
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { Redo2, Undo2 } from 'lucide-react';
import type { HumanCreatorDocument, HumanCreatorField, HumanCreatorInfo, HumanCreatorSection, HumanCreatorUndoTarget, StudioSnapshot } from '../types';
import { invoke, nativeConnected } from '../bridge';
import { StudioSection, StudioBadge, StudioAlert, StudioEmptyState, StudioButton,
  StudioField, StudioInput, StudioSelect, StudioTabs, StudioCheckbox, StudioLoader } from '../ui';
import { activeExpressionCount, creatorWireValue, hexToRgb, readCreatorValue, rgbToHex } from './humanCreatorSchema';

type Info = Required<HumanCreatorInfo>;
type Doc = Partial<HumanCreatorDocument> & { dna: Record<string, any> };

function NumberField({ field, value, disabled, commit }: { field: HumanCreatorField; value: number; disabled: boolean; commit: (v: unknown) => void }) {
  const [local, setLocal] = useState(value);
  useEffect(() => setLocal(value), [value]);
  const send = (v: number) => { const wire = creatorWireValue(field, v); if (wire !== null && wire !== value) commit(wire); };
  return <StudioField label={field.label} htmlFor={'hc-' + field.key}>
    <div className="hc-number">
      <input type="range" aria-label={field.label} min={field.min} max={field.max} step={field.step} value={local} disabled={disabled}
        onChange={e => setLocal(Number(e.target.value))} onPointerUp={() => send(local)} onKeyUp={() => send(local)}/>
      <StudioInput id={'hc-' + field.key} type="number" min={field.min} max={field.max} step={field.step} value={local} disabled={disabled}
        onChange={e => setLocal(Number(e.target.value))} onBlur={() => send(local)}
        onKeyDown={e => { if (e.key === 'Enter') send(local); }}/>
    </div>
  </StudioField>;
}

function TextField({ field, value, disabled, commit }: { field: HumanCreatorField; value: string; disabled: boolean; commit: (v: unknown) => void }) {
  const [local, setLocal] = useState(value);
  useEffect(() => setLocal(value), [value]);
  const send = () => { const wire = creatorWireValue(field, local); if (wire !== null && local !== value) commit(wire); };
  return <StudioField label={field.label} htmlFor={'hc-' + field.key}
    hint={field.kind === 'bones' ? 'Comma separated bone names' : undefined}>
    <StudioInput id={'hc-' + field.key} value={local} disabled={disabled} onChange={e => setLocal(e.target.value)}
      onBlur={send} onKeyDown={e => { if (e.key === 'Enter') send(); }}/>
  </StudioField>;
}

function SchemaField({ field, dna, disabled, commit }: { field: HumanCreatorField; dna: Record<string, unknown>; disabled: boolean; commit: (key: string, v: unknown) => void }) {
  const raw = readCreatorValue(dna, field.key);
  const send = (v: unknown) => commit(field.key, v);
  if (field.kind === 'color') {
    const rgb = Array.isArray(raw) ? raw.map(Number) : [0, 0, 0];
    return <StudioField label={field.label} htmlFor={'hc-' + field.key}>
      <input id={'hc-' + field.key} type="color" className="hc-color" value={rgbToHex(rgb)} disabled={disabled}
        onChange={e => { const next = hexToRgb(e.target.value); if (next) send(next); }}/>
    </StudioField>;
  }
  if (field.kind === 'number' || field.kind === 'integer')
    return <NumberField field={field.kind === 'integer' ? { ...field, min: 0, step: 1 } : field} value={Number(raw ?? 0)} disabled={disabled} commit={send}/>;
  const text = Array.isArray(raw) ? raw.join(', ') : String(raw ?? '');
  return <TextField field={field} value={text} disabled={disabled} commit={send}/>;
}

export default function HumanDnaCreator({ snapshot }: { snapshot: StudioSnapshot | null }) {
  const project = snapshot?.project.path ?? '';
  const [info, setInfo] = useState<Info | null>(null);
  const [tab, setTab] = useState('skin');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const [status, setStatus] = useState('');
  const [name, setName] = useState('character');
  const [saved, setSaved] = useState<string[]>([]);
  const [filter, setFilter] = useState('');
  const [activeOnly, setActiveOnly] = useState(false);
  const [rigNode, setRigNode] = useState('');
  const [rigOffset, setRigOffset] = useState<[number, number, number]>([0, 0, 0]);
  const [skinVertex, setSkinVertex] = useState(0);
  const [skinJoint, setSkinJoint] = useState('');
  const [skinWeight, setSkinWeight] = useState(1);
  const generation = useRef(0);

  const run = useCallback(async (action: () => Promise<Info | void>, done?: string) => {
    const gen = generation.current;
    setBusy(true); setError('');
    try {
      const next = await action();
      if (gen !== generation.current) return;
      if (next) setInfo(next);
      if (done) setStatus(done);
    } catch (e) { if (gen === generation.current) setError(String(e)); }
    finally { if (gen === generation.current) setBusy(false); }
  }, []);
  const refreshList = useCallback(() => {
    if (!project) return;
    void invoke('studio.humanCreatorList', {}).then(r => setSaved(r.humans)).catch(() => {});
  }, [project]);

  useEffect(() => {
    ++generation.current;
    setInfo(null); setError(''); setStatus('');
    if (!nativeConnected()) return;
    void run(() => invoke('studio.humanCreatorInfo', {}));
    refreshList();
    return () => { ++generation.current; };
  }, [project, run, refreshList]);

  const sections: HumanCreatorSection[] = info?.schema ?? [];
  const doc = (info?.document ?? { dna: {} }) as Doc;
  const dna = doc.dna ?? {};
  const catalog = info?.catalog;
  const previewOn = Boolean(info?.preview?.creator);
  const set = (key: string, value: unknown) => run(() => invoke('studio.humanCreatorSet', { key, value }));
  const history = (target: HumanCreatorUndoTarget, undo: boolean) =>
    run(() => invoke(undo ? 'studio.humanCreatorUndo' : 'studio.humanCreatorRedo', { target }));
  const validName = /^[A-Za-z0-9_-]{1,64}$/.test(name);
  const current = sections.find(s => s.id === tab) ?? sections[0];
  const expressionFields = useMemo(() => {
    const section = sections.find(s => s.id === 'expression');
    if (!section) return [];
    const q = filter.trim().toLowerCase();
    return section.fields.filter(f => (!q || f.label.toLowerCase().includes(q)) &&
      (!activeOnly || Number(readCreatorValue(dna, f.key) ?? 0) > 0));
  }, [sections, filter, activeOnly, dna]);

  if (!nativeConnected()) return <StudioAlert>Откройте нативный Studio: браузерная демо-версия не запускает VIVID.</StudioAlert>;
  if (!info)
    return error ? <StudioAlert tone="danger">{error}</StudioAlert> :
      <StudioEmptyState icon={<StudioLoader size={30}/>} title="Загрузка VIVID DNA" description="Читаю схему DNA из нативного Studio."/>;

  const historyButtons = (target: HumanCreatorUndoTarget, canUndo: boolean, canRedo: boolean) => <>
    <StudioButton size="sm" disabled={busy || !canUndo} onClick={() => void history(target, true)} aria-label="Undo"><Undo2 size={14}/></StudioButton>
    <StudioButton size="sm" disabled={busy || !canRedo} onClick={() => void history(target, false)} aria-label="Redo"><Redo2 size={14}/></StudioButton>
  </>;

  const body = () => {
    if (!current) return null;
    if (current.id === 'expression') return <StudioSection title={current.title}
      actions={<StudioBadge>{activeExpressionCount(info)} active</StudioBadge>}>
      <p className="hc-hint">{current.hint}</p>
      <div className="hc-row">
        <StudioInput placeholder="Filter channels (jaw, eye, mouth…)" value={filter} onChange={e => setFilter(e.target.value)}/>
        <StudioCheckbox label="Only active" checked={activeOnly} onChange={e => setActiveOnly(e.target.checked)}/>
      </div>
      <div className="hc-grid">{expressionFields.map(f => <SchemaField key={f.key} field={f} dna={dna} disabled={busy} commit={set}/>)}</div>
    </StudioSection>;
    if (current.id === 'routes') return <StudioSection title={current.title}>
      <p className="hc-hint">{current.hint}</p>
      {(doc.routes ?? []).map((r, i) => <div className="hc-row" key={i + ':' + r.channel + ':' + r.target}>
        <StudioSelect aria-label="Channel" value={r.channel} disabled={busy}
          onChange={e => void run(() => invoke('studio.humanCreatorRoute', { index: i, channel: e.target.value, target: r.target, gain: r.gain }))}>
          {(catalog?.channels ?? []).map(c => <option key={c}>{c}</option>)}</StudioSelect>
        <StudioSelect aria-label="Morph target" value={r.target} disabled={busy}
          onChange={e => void run(() => invoke('studio.humanCreatorRoute', { index: i, channel: r.channel, target: e.target.value, gain: r.gain }))}>
          {[...new Set([r.target, ...(catalog?.targets ?? [])])].map(t => <option key={t}>{t}</option>)}</StudioSelect>
        <StudioInput type="number" aria-label="Gain" min={0} max={4} step={0.05} defaultValue={r.gain} disabled={busy}
          onBlur={e => { const g = Number(e.target.value); if (Number.isFinite(g) && g !== r.gain) void run(() => invoke('studio.humanCreatorRoute', { index: i, channel: r.channel, target: r.target, gain: g })); }}/>
        <StudioButton size="sm" disabled={busy} onClick={() => void run(() => invoke('studio.humanCreatorRoute', { index: i, remove: true }))}>Remove</StudioButton>
      </div>)}
      <StudioButton disabled={busy || !catalog?.channels.length || !catalog?.targets.length}
        onClick={() => void run(() => invoke('studio.humanCreatorRoute', { index: doc.routes?.length ?? 0, channel: catalog!.channels[0], target: catalog!.targets[0], gain: 1 }))}>Add route</StudioButton>
    </StudioSection>;
    if (current.id === 'rigCorrections') return <StudioSection title={current.title}
      actions={historyButtons('rig', info.rigCanUndo, info.rigCanRedo)}>
      <p className="hc-hint">{current.hint}</p>
      <div className="hc-row">
        <StudioSelect aria-label="Rig node" value={rigNode} onChange={e => setRigNode(e.target.value)}>
          <option value="">Choose node…</option>{(catalog?.nodes ?? []).map(n => <option key={n}>{n}</option>)}</StudioSelect>
        {(['X', 'Y', 'Z'] as const).map((axis, k) => <StudioInput key={axis} type="number" aria-label={axis} min={-1} max={1} step={0.001}
          value={rigOffset[k]} onChange={e => { const v = [...rigOffset] as [number, number, number]; v[k] = Number(e.target.value); setRigOffset(v); }}/>)}
        <StudioButton variant="primary" disabled={busy || !rigNode || rigOffset.some(v => !Number.isFinite(v) || Math.abs(v) > 1)}
          onClick={() => void run(() => invoke('studio.humanCreatorRig', { node: rigNode, translation: rigOffset }), 'Rig correction applied')}>Apply</StudioButton>
      </div>
      {(doc.rigCorrections?.offsets ?? []).map(o => <div className="hc-row hc-list" key={o.node}>
        <span>{o.node}</span><span className="hc-mono">{o.translation.map(v => v.toFixed(3)).join(', ')}</span>
        <StudioButton size="sm" disabled={busy} onClick={() => void run(() => invoke('studio.humanCreatorRig', { node: o.node, translation: [0, 0, 0] }))}>Reset</StudioButton>
      </div>)}
    </StudioSection>;
    if (current.id === 'skinWeights') return <StudioSection title={current.title}
      actions={historyButtons('skin', info.skinCanUndo, info.skinCanRedo)}>
      <p className="hc-hint">{current.hint}. Head has {catalog?.vertices ?? 0} vertices. Brush painting on the viewport stays in tools/vivid-creator.</p>
      <div className="hc-row">
        <StudioInput type="number" aria-label="Vertex" min={0} max={Math.max(0, (catalog?.vertices ?? 1) - 1)} step={1} value={skinVertex}
          onChange={e => setSkinVertex(Math.trunc(Number(e.target.value)))}/>
        <StudioSelect aria-label="Bone" value={skinJoint} onChange={e => setSkinJoint(e.target.value)}>
          <option value="">Choose bone…</option>{(catalog?.nodes ?? []).map(n => <option key={n}>{n}</option>)}</StudioSelect>
        <StudioInput type="number" aria-label="Weight" min={0} max={1} step={0.01} value={skinWeight} onChange={e => setSkinWeight(Number(e.target.value))}/>
        <StudioButton variant="primary" disabled={busy || !skinJoint || skinVertex < 0 || skinVertex >= (catalog?.vertices ?? 0) || !(skinWeight >= 0 && skinWeight <= 1)}
          onClick={() => void run(() => invoke('studio.humanCreatorSkin', { vertex: skinVertex, joint: skinJoint, weight: skinWeight }), 'Skin weight set')}>Set weight</StudioButton>
      </div>
      {(doc.skinWeights?.edits ?? []).slice(0, 200).map(w => <div className="hc-row hc-list" key={w.vertex}>
        <span className="hc-mono">#{w.vertex}</span><span>{w.joint}</span><span className="hc-mono">{w.weight.toFixed(2)}</span>
        <StudioButton size="sm" disabled={busy} onClick={() => void run(() => invoke('studio.humanCreatorSkin', { vertex: w.vertex, erase: true }))}>Erase</StudioButton>
      </div>)}
    </StudioSection>;
    return <StudioSection title={current.title}>
      <p className="hc-hint">{current.hint}</p>
      <div className="hc-grid">{current.fields.map(f => <SchemaField key={f.key} field={f} dna={dna} disabled={busy} commit={set}/>)}</div>
    </StudioSection>;
  };

  return <div className="au-stack">
      <div className="hc-row hc-toolbar">
        <strong>{String(dna.displayName ?? 'VIVID Human')}</strong>
        <StudioBadge tone={info.dirty ? 'warning' : 'neutral'}>{info.dirty ? 'Не сохранено' : 'Сохранено'}</StudioBadge>
        <StudioBadge tone={info.preview?.runtimeReady ? 'success' : 'neutral'}>
          {previewOn ? (info.preview?.runtimeReady ? 'Во вьюпорте' : (info.preview?.reason ?? 'Загрузка превью')) : 'Превью выключено'}</StudioBadge>
        <span className="hc-spacer"/>{historyButtons('dna', info.canUndo, info.canRedo)}
      </div>
      {error && <StudioAlert tone="danger">{error}</StudioAlert>}
      {status && !error && <StudioAlert>{status}</StudioAlert>}
      {catalog?.error && <StudioAlert tone="warning">{catalog.error}</StudioAlert>}
      <div className="hc-row">
        <StudioCheckbox label="Показать во вьюпорте" checked={previewOn} disabled={busy}
          onChange={e => void run(() => invoke('studio.humanCreatorPreview', { enabled: e.target.checked }))}/>
        <StudioSelect aria-label="Preset" value="" disabled={busy}
          onChange={e => { const id = e.target.value; if (id) void run(() => invoke('studio.humanCreatorPreset', { id }), 'Preset ' + id + ' applied'); }}>
          <option value="">Пресет…</option>{(info.presets ?? []).map(p => <option key={p}>{p}</option>)}</StudioSelect>
        <StudioButton disabled={busy} onClick={() => void run(() => invoke('studio.humanCreatorNew', {}), 'New human')}>Новый</StudioButton>
      </div>
      {project ? <div className="hc-row">
        <StudioInput aria-label="Human name" value={name} onChange={e => setName(e.target.value)}/>
        <StudioButton variant="primary" disabled={busy || !validName}
          onClick={() => void run(async () => { const r = await invoke('studio.humanCreatorSave', { name }); refreshList(); return r; }, 'Сохранено: humans/' + name + '.json')}>Сохранить</StudioButton>
        <StudioSelect aria-label="Saved humans" value="" disabled={busy || !saved.length}
          onChange={e => { const n = e.target.value; if (n) { setName(n); void run(() => invoke('studio.humanCreatorLoad', { name: n }), 'Loaded ' + n); } }}>
          <option value="">{saved.length ? 'Загрузить…' : 'Нет сохранённых'}</option>{saved.map(n => <option key={n}>{n}</option>)}</StudioSelect>
      </div> : <StudioAlert>Откройте проект, чтобы сохранять персонажей в папку humans/.</StudioAlert>}
      <StudioTabs label="Creator sections" items={sections.map(s => ({ id: s.id, label: s.title.split(' / ')[0] }))}
        selected={current?.id ?? 'skin'} onChange={setTab}/>
      {body()}
    </div>;
}
