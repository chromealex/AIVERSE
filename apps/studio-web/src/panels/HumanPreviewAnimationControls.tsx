// Pose & expression test for the Character Creator: every click applies at
// once, so a sculpted or blended face can be checked in motion.
import { useEffect, useState } from 'react';
import { invoke } from '../bridge';
import AssetReferencePicker from '../components/AssetReferencePicker';
import { StudioButton, StudioInput } from '../ui';
import type { HumanPreviewAnimationSettings } from '../types';
import { HcCard, HcNote, HcSegment, HcSwitch } from './HumanCreatorUi';

const defaults: HumanPreviewAnimationSettings = { pose: 'relaxed', playing: true, faceIdle: true, speed: 1, automaticLod: true, expression: 'idle' };
const POSES = [{ id: 'relaxed', label: 'Стойка' }, { id: 'a_pose', label: 'A-поза' }, { id: 't_pose', label: 'T-поза' }, { id: 'portrait', label: 'Портрет' }];
export const EXPRESSIONS = [
  { id: 'idle', label: 'Спокойное', emoji: '😐' }, { id: 'smile', label: 'Улыбка', emoji: '🙂' },
  { id: 'laugh', label: 'Смех', emoji: '😄' }, { id: 'surprise', label: 'Удивление', emoji: '😮' },
  { id: 'anger', label: 'Злость', emoji: '😠' }, { id: 'sad', label: 'Грусть', emoji: '😢' },
  { id: 'disgust', label: 'Отвращение', emoji: '🤢' }, { id: 'talk', label: 'Речь', emoji: '🗣️' }];

export default function HumanPreviewAnimationControls() {
  const [draft, setDraft] = useState(defaults), [ready, setReady] = useState(false), [busy, setBusy] = useState(false), [error, setError] = useState('');
  useEffect(() => {
    let live = true;
    void invoke('studio.humanPreviewAnimation', {}).then(s => { if (live) { setDraft({ ...defaults, ...s }); setReady(true); } })
      .catch(e => { if (live) setError(String(e)); });
    return () => { live = false; };
  }, []);
  const apply = async (settings: HumanPreviewAnimationSettings) => {
    setDraft(settings); setBusy(true); setError('');
    try { setDraft({ ...defaults, ...await invoke('studio.humanPreviewAnimation', { settings }) }); }
    catch (e) { setError(String(e)); } finally { setBusy(false); }
  };
  const [clips, setClips] = useState<string[]>([]);
  useEffect(() => {
    let live = true;
    const load = () => void invoke('studio.humanAppearanceInfo', {}).then(i => { if (live) setClips(i.preview.clips ?? []); }).catch(() => {});
    load(); const timer = setInterval(load, 3000);
    return () => { live = false; clearInterval(timer); };
  }, []);
  const [external, setExternal] = useState<{ name: string; file: string }[]>([]);
  const [file, setFile] = useState(''), [clipName, setClipName] = useState(''), [clipError, setClipError] = useState('');
  useEffect(() => { void invoke('studio.humanClipList', {}).then(r => setExternal(r.external)).catch(() => {}); }, []);
  const clipCall = (p: Promise<{ external: { name: string; file: string }[] }>) => { setClipError('');
    void p.then(r => { setExternal(r.external); setFile(''); setClipName(''); }).catch(e => setClipError(String(e).replace(/^Error: /, ''))); };
  const off = busy || !ready;
  return <>
    {error && <HcNote tone="error">{error}</HcNote>}
    <HcCard title="Мимика" hint="Проверьте, как лицо двигается после смешивания и скульпта">
      <div className="hc-chips">{EXPRESSIONS.map(e => <button key={e.id} type="button" className="hc-chip"
        aria-pressed={(draft.expression ?? 'idle') === e.id} disabled={off}
        onClick={() => void apply({ ...draft, expression: e.id })}><span aria-hidden="true">{e.emoji}</span>{e.label}</button>)}</div>
      <HcSwitch label="Моргание и лёгкая мимика" checked={draft.faceIdle} disabled={off} onChange={v => void apply({ ...draft, faceIdle: v })}/>
    </HcCard>
    <HcCard title="Анимации модели" hint={clips.length ? 'Клипы из файла персонажа — проверьте одежду и скиннинг в движении' : 'В модели нет своих анимаций. Добавьте клипы в glTF персонажа (Idle, Walk, Run…)'}>
      {clips.length > 0 && <div className="hc-chips">
        <button type="button" className="hc-chip" aria-pressed={!draft.clip} disabled={off} onClick={() => void apply({ ...draft, clip: '' })}>Поза превью</button>
        {clips.map(c => <button key={c} type="button" className="hc-chip" aria-pressed={draft.clip === c} disabled={off}
          onClick={() => void apply({ ...draft, clip: c, playing: true })}>▶ {c}</button>)}</div>}
      <details className="hc-item__more"><summary>Добавить анимацию из файла ({external.length})</summary>
        {clipError && <HcNote tone="error">{clipError}</HcNote>}
        {external.map(x => <div key={x.name} className="hc-row"><span className="hc-hint">{x.name} · {x.file}</span>
          <button type="button" className="hc-reset" title="Убрать" onClick={() => clipCall(invoke('studio.humanClipRemove', { name: x.name }))}>✕</button></div>)}
        <AssetReferencePicker value={file} kinds={['model']} label="Файл с анимацией (glb / gltf / fbx)" onChange={v => setFile(v)}/>
        <div className="hc-row"><StudioInput aria-label="Имя клипа" placeholder="Имя клипа" value={clipName} onChange={e => setClipName(e.target.value)}/>
          <StudioButton disabled={!file || !clipName.trim()} onClick={() => clipCall(invoke('studio.humanClipAdd', { file: file.replace(/^content\//, ''), name: clipName.trim() }))}>Добавить</StudioButton></div>
        <p className="hc-hint">Кости анимации должны называться как у персонажа (тот же скелет: Mixamo, UE и т.п.). Клип записывается в {'<модель>'}.clips.json, модель перезагружается.</p>
      </details>
    </HcCard>
    <HcCard title="Поза">
      <HcSegment label="Поза" value={draft.pose} options={POSES} disabled={off} onChange={pose => void apply({ ...draft, pose })}/>
      <HcSwitch label="Анимация" hint="Дыхание и микродвижения" checked={draft.playing} disabled={off} onChange={v => void apply({ ...draft, playing: v })}/>
      <HcSegment label="Скорость" value={String(draft.speed)} disabled={off}
        options={[{ id: '0.5', label: '×0.5' }, { id: '1', label: '×1' }, { id: '1.5', label: '×1.5' }, { id: '2', label: '×2' }]}
        onChange={v => void apply({ ...draft, speed: Number(v) })}/>
      <HcSwitch label="Максимальная детализация" hint="Всегда LOD0 вместо автоматического выбора" checked={draft.automaticLod === false} disabled={off}
        onChange={v => void apply({ ...draft, automaticLod: !v })}/>
    </HcCard>
  </>;
}
