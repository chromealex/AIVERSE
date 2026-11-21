// Character Creator (MetaHuman-style): a category rail (Presets & Blend, Face,
// Sculpt, Skin, Eyes, Hair, Body, Clothing, Pose & Light), camera framing
// (face / torso / full), a one-click "character studio" layout with the Scene
// viewport as the stage, and the procedural VIVID DNA source. Every write is
// an allowlisted native StudioSession call; React never touches files.
import { useCallback, useEffect, useMemo, useState } from 'react';
import { Brush, Eye, Lightbulb, PersonStanding, Redo2, ScanFace, Scissors, Shirt, Sparkles, Undo2,
  UserRound, LayoutDashboard, Palette, SplitSquareHorizontal, Paintbrush } from 'lucide-react';
import type { HumanFaceShape, HumanLook, StudioSnapshot } from '../../types';
import { invoke, nativeConnected } from '../../bridge';
import { StudioPanel, StudioAlert, StudioEmptyState, StudioButton,
  StudioField, StudioInput, StudioSelect, StudioTabs, StudioCheckbox, StudioBadge } from '../../ui';
import AssetReferencePicker from '../../components/AssetReferencePicker';
import HumanFaceControls from '../HumanFaceControls';
import HumanWardrobeControls from '../HumanWardrobeControls';
import HumanPreviewAnimationControls from '../HumanPreviewAnimationControls';
import HumanStudioLightingControls from '../HumanStudioLightingControls';
import HumanSculptControls from '../HumanSculptControls';
import HumanMakeupControls from '../HumanMakeupControls';
import HumanHairstyleControls from '../HumanHairstyleControls';
import HumanCrowdControls from '../HumanCrowdControls';
import HumanReferenceControls from '../HumanReferenceControls';
import HumanPackageControls from '../HumanPackageControls';
import { useOutfit } from '../useOutfit';
import HumanFaceBlend from '../HumanFaceBlend';
import { HcCard, HcHeader, HcSlider, HcSwitch } from '../HumanCreatorUi';
import HumanDnaCreator from '../HumanDnaCreator';
import { useHumanLook, type Snapshot } from '../useHumanLook';
import { ageLook, lookAge, FACE_ARCHETYPES, FACE_REGIONS, LOOK_PARTS, agentLookPrompt, archetypePoint, blendFace, blendLooks, describeToLook, guessFaceMeshes, randomHandle, randomLook, type LookPart } from '../humanCreatorSchema';
import { DOCK_PRESET_EVENT } from '../../nestedDock';
import './HumanCreatorPanel.css';

type Source = 'model' | 'dna';
type Camera = 'face' | 'torso' | 'full';
type Category = 'presets' | 'face' | 'sculpt' | 'skin' | 'makeup' | 'eyes' | 'hair' | 'body' | 'clothing' | 'pose';
const CATEGORIES: { id: Category; label: string; icon: typeof Sparkles; camera: Camera }[] = [
  { id: 'presets', label: 'Пресеты', icon: Sparkles, camera: 'full' },
  { id: 'face', label: 'Лицо', icon: ScanFace, camera: 'face' },
  { id: 'sculpt', label: 'Скульпт', icon: Brush, camera: 'face' },
  { id: 'skin', label: 'Кожа', icon: Palette, camera: 'face' },
  { id: 'makeup', label: 'Макияж', icon: Paintbrush, camera: 'face' },
  { id: 'eyes', label: 'Глаза', icon: Eye, camera: 'face' },
  { id: 'hair', label: 'Волосы', icon: Scissors, camera: 'face' },
  { id: 'body', label: 'Тело', icon: PersonStanding, camera: 'full' },
  { id: 'clothing', label: 'Одежда', icon: Shirt, camera: 'full' },
  { id: 'pose', label: 'Поза и свет', icon: Lightbulb, camera: 'full' }];

function LookSliders({ groups, h, live }: { groups: string[]; h: ReturnType<typeof useHumanLook>; live: boolean }) {
  const fields = h.info?.schema.filter(f => groups.some(g => f.key.startsWith(g + '.'))) ?? [];
  if (!fields.length) return <p className="hc-empty">Выберите персонажа в «Пресетах» и загрузите его профиль.</p>;
  return <div className="hc-vsliders">{fields.map(f => {
    const value = h.draft.values[f.key] ?? f.initial; const changed = Math.abs(value - f.initial) > 1e-6;
    return <div key={f.key} className={'hc-vrow' + (changed ? ' hc-vrow--changed' : '')}>
      <HcSlider id={'hl-' + f.key.replace('.', '-')} label={f.label} min={f.min} max={f.max} step={(f.max - f.min) > 10 ? 1 : .001}
        value={value} disabled={h.busy} onChange={v => { const next = { ...h.draft, values: { ...h.draft.values, [f.key]: v } }; h.edit(next); if (live) h.live(next, h.face); }}/>
      <button type="button" className="hc-reset" title="Сбросить к исходному" aria-label={'Сбросить ' + f.label} disabled={!changed || h.busy}
        onClick={() => { const v = { ...h.draft.values }; delete v[f.key]; h.edit({ ...h.draft, values: v }); }}>↺</button>
    </div>;
  })}</div>;
}

export default function HumanCreatorPanel({ snapshot }: { snapshot: StudioSnapshot | null }) {
  const project = snapshot?.project.path ?? '';
  const h = useHumanLook(project);
  const [source, setSource] = useState<Source>('model');
  const [category, setCategory] = useState<Category>('presets');
  const [camera, setCamera] = useState<Camera>('full');
  const [autoApply, setAutoApply] = useState(true);
  const [profileName, setProfileName] = useState('character');
  const [blendA, setBlendA] = useState(''), [blendB, setBlendB] = useState('');
  const [looks, setLooks] = useState<Record<string, HumanLook>>({});
  const [thumbs, setThumbs] = useState<Record<string, string>>({});
  const loadThumbs = useCallback(() => { void invoke('studio.humanAppearanceThumbnails', {}).then(setThumbs).catch(() => {}); }, []);
  useEffect(() => { if (project) loadThumbs(); }, [project, loadThumbs]);
  const [blend, setBlend] = useState(.5);
  const [faceTab, setFaceTab] = useState<'blend' | 'shape'>('blend');
  const pv = h.info?.preview;
  const bound = Boolean(pv?.bound) && pv?.guid === h.guid && Boolean(h.guid);
  const ready = Boolean(pv?.runtimeReady) && bound;
  const refresh = useCallback(() => { void h.refresh().catch(() => {}); }, [h]);
  const frame = useCallback((view: Camera) => {
    setCamera(view); void invoke('studio.humanCamera', { view }).catch(() => {});
  }, []);
  const pick = (c: typeof CATEGORIES[number]) => { setCategory(c.id); if (bound) frame(c.camera); };
  // One history for look, face blend, proportions and sculpt; restoring a
  // step re-applies it to the viewport.
  const restore = (snap: Snapshot | null) => { if (snap && bound) void h.apply(snap.face, snap.look); };
  const undo = () => restore(h.undoLook());
  const redo = () => restore(h.redoLook());
  const keys = (e: React.KeyboardEvent) => {
    if (!(e.metaKey || e.ctrlKey) || e.key.toLowerCase() !== 'z') return;
    if ((e.target as HTMLElement).closest('input[type=text],input:not([type]),textarea')) return;
    e.preventDefault(); if (e.shiftKey) redo(); else undo();
  };
  const [locked, setLocked] = useState<LookPart[]>([]);
  const [age, setAge] = useState<number | null>(null);
  const [description, setDescription] = useState('');
  const [understood, setUnderstood] = useState<string[] | null>(null);
  // Random / described drafts: look values plus an optional face handle.
  const applyDraft = (values: Record<string, number>, handle: [number, number] | null) => void h.run(async () => {
    const look = { ...h.draft, values: { ...h.draft.values, ...values } };
    let f = h.face;
    if (handle) {
      let meshes = h.face?.meshes ?? [];
      if (!meshes.length && h.guid) meshes = guessFaceMeshes((await invoke('studio.humanWardrobeInfo', { guid: h.guid })).meshes);
      if (meshes.length) f = { version: 1, meshes, landmarks: h.face?.landmarks ?? [],
        values: blendFace(handle, Object.fromEntries(Object.keys(FACE_REGIONS).map(k => [k, 1])), h.info?.faceSchema ?? []) };
    }
    h.edit(look); if (f !== h.face) h.editFace(f);
    if (bound) await h.apply(f, look); else h.setStatus('Черновик готов — нажмите «Показать»');
  });
  const outfit = useOutfit(h.guid);
  const [comparing, setComparing] = useState(false);
  const compare = (original: boolean) => { setComparing(original); void invoke('studio.humanCompare', { original }).catch(() => {}); };
  const applyNow = () => void h.apply(h.face, h.draft);
  const commit = () => { if (autoApply && bound) applyNow(); };

  // Saved presets for the Blend view.
  useEffect(() => {
    let live = true;
    for (const name of [blendA, blendB]) if (name && !looks[name])
      void invoke('studio.humanAppearanceLoad', { name }).then(r => { if (live) setLooks(l => ({ ...l, [name]: r.look })); }).catch(() => {});
    return () => { live = false; };
  }, [blendA, blendB, looks]);
  const blended = useMemo(() => blendA && blendB && looks[blendA] && looks[blendB] && h.info
    ? blendLooks(looks[blendA], looks[blendB], blend, h.info.schema) : null, [blendA, blendB, looks, blend, h.info]);

  if (!project) return <StudioPanel title="Character Creator">
    <StudioEmptyState icon={<UserRound size={22}/>} title="Откройте проект" description="Персонажи, одежда и пресеты хранятся в проекте."/></StudioPanel>;
  if (!nativeConnected()) return <StudioPanel title="Character Creator">
    <StudioAlert>Откройте нативный Studio: персонаж рендерится в Metal-вьюпорте.</StudioAlert></StudioPanel>;

  const content = () => {
    switch (category) {
      case 'presets': return <>
        <HcHeader title="Пресеты" hint="Выберите модель персонажа, затем применяйте и смешивайте сохранённые образы."/>
        <HcCard title="Персонаж" hint={bound ? 'Показан во вьюпорте' : 'Выберите модель и нажмите «Показать»'}>
          <AssetReferencePicker value={h.guid} onChange={v => { h.setGuid(v); h.setFace(null); h.setStatus('Загрузите профиль модели'); }}
            kinds={['model']} label="Модель персонажа" disabled={h.busy}/>
          <div className="hc-row">
            <StudioButton disabled={h.busy || !h.guid} onClick={() => void h.loadAsset()}>Загрузить профиль</StudioButton>
            <StudioButton variant="primary" disabled={h.busy || !h.guid || !h.info} onClick={applyNow}>Показать</StudioButton>
          </div>
          <StudioCheckbox label="Применять изменения сразу" checked={autoApply} onChange={e => setAutoApply(e.target.checked)}/>
        </HcCard>
        <HcCard title="Возраст" hint="Седина, фактура и цвет кожи вместе; форму лица меняйте в «Лице»">
          <HcSlider id="hc-age" label="Возраст" min={18} max={85} step={1} value={age ?? lookAge(h.draft.values)} disabled={!h.info}
            format={v => Math.round(v) + ' лет'} onChange={v => { setAge(v); const look = { ...h.draft, values: { ...h.draft.values, ...ageLook(v) } }; h.edit(look); if (autoApply && bound) h.live(look, h.face); }}
            onCommit={() => { setAge(null); commit(); }}/>
        </HcCard>
        <HcCard title="Случайный персонаж" hint="Закреплённые части не меняются">
          <div className="hc-chips">{LOOK_PARTS.map(p => <button key={p.id} type="button" className="hc-chip" aria-pressed={locked.includes(p.id)}
            title={locked.includes(p.id) ? 'Закреплено' : 'Будет меняться'}
            onClick={() => setLocked(l => l.includes(p.id) ? l.filter(x => x !== p.id) : [...l, p.id])}>{locked.includes(p.id) ? '🔒' : '🎲'} {p.label}</button>)}</div>
          <StudioButton variant="primary" disabled={h.busy || !h.info} onClick={() => applyDraft(
            randomLook({ ...Object.fromEntries((h.info?.schema ?? []).map(f => [f.key, h.draft.values[f.key] ?? f.initial])) }, locked),
            locked.includes('face') ? null : randomHandle())}>Сгенерировать</StudioButton>
        </HcCard>
        <HcCard title="По описанию" hint="Опишите персонажа словами — получится черновик, который можно доработать">
          <textarea className="hc-textarea" rows={3} value={description} placeholder="Например: пожилой рыбак, обветренная кожа, седые волосы, голубые глаза, широкое лицо"
            onChange={e => { setDescription(e.target.value); setUnderstood(null); }}/>
          <div className="hc-row">
            <StudioButton variant="primary" disabled={h.busy || !h.info || !description.trim()} onClick={() => {
              const d = describeToLook(description); setUnderstood(d.matched);
              const i = d.face ? FACE_ARCHETYPES.findIndex(a => a.id === d.face) : -1;
              if (d.matched.length) applyDraft(d.values, i >= 0 ? archetypePoint(i) : null);
            }}>Создать черновик</StudioButton>
            <StudioButton disabled={!description.trim()} title="Запрос для агента с MCP-инструментами AIVERSE"
              onClick={() => { void navigator.clipboard?.writeText(agentLookPrompt(description, profileName)); h.setStatus('Запрос для агента скопирован'); }}>Запрос агенту</StudioButton>
          </div>
          {understood && <p className="hc-hint">{understood.length ? 'Понял: ' + understood.join(', ') : 'Ничего не распознал — уточните цвет волос, глаз, кожу, телосложение или форму лица, либо отправьте запрос агенту.'}</p>}
        </HcCard>
        <HcCard title="Образы" hint="Клик применяет образ к персонажу">
          <div className="hc-cards">{h.saved.map(n => <button key={n} type="button" className="hc-card" disabled={h.busy}
            onClick={() => void h.run(async () => { const r = await invoke('studio.humanAppearanceLoad', { name: n }); h.edit(r.look); setProfileName(n); if (autoApply && bound) await h.apply(h.face, r.look); })}>
            {thumbs[n] ? <img className="hc-card__thumb" src={thumbs[n]} alt=""/> : <UserRound size={22}/>}<span>{n}</span></button>)}
            {!h.saved.length && <span className="hc-hint">Сохраните первый пресет ниже.</span>}</div>
          <div className="hc-row">
            <StudioInput aria-label="Имя пресета" value={profileName} onChange={e => setProfileName(e.target.value)}/>
            <StudioButton disabled={h.busy || !/^[A-Za-z0-9_-]{1,64}$/.test(profileName)} onClick={() => void h.run(async () => {
              await invoke('studio.humanAppearanceSave', { name: profileName, look: h.draft });
              h.setSaved((await invoke('studio.humanAppearanceList', {})).looks); h.setStatus('Сохранён пресет ' + profileName);
              if (bound) {
                // Picture for the preset card: face camera, one frame, back.
                const back = camera; frame('face');
                await new Promise(r => setTimeout(r, 450));
                await invoke('studio.humanLookCapture', { name: profileName }).catch(() => {});
                await new Promise(r => setTimeout(r, 900));
                frame(back); loadThumbs();
              }
            })}>Сохранить</StudioButton>
          </div>
        </HcCard>
        <HumanCrowdControls info={h.info} draft={h.draft} face={h.face} guid={h.guid} name={profileName} locked={locked} outfit={outfit} onSaved={h.setSaved}/>
        <HumanPackageControls look={profileName} onImported={() => { void invoke('studio.humanAppearanceList', {}).then(r => h.setSaved(r.looks)); loadThumbs(); }}/>
        <HcCard title="Смешать два образа" hint="Выберите A и B, затем ведите ползунок">
          <div className="hc-row">
            <StudioSelect aria-label="Пресет A" value={blendA} onChange={e => setBlendA(e.target.value)}><option value="">Пресет A…</option>{h.saved.map(n => <option key={n}>{n}</option>)}</StudioSelect>
            <StudioSelect aria-label="Пресет B" value={blendB} onChange={e => setBlendB(e.target.value)}><option value="">Пресет B…</option>{h.saved.map(n => <option key={n}>{n}</option>)}</StudioSelect>
          </div>
          <div className="hc-slider"><label htmlFor="hc-blend">A ↔ B</label>
            <input id="hc-blend" type="range" min={0} max={1} step={.01} value={blend} disabled={!blended}
              onChange={e => { setBlend(Number(e.target.value)); }}
              onPointerUp={() => { if (blended) { h.edit(blended); if (autoApply && bound) void h.apply(h.face, blended); } }}/>
            <output className="hc-mono">{Math.round(blend * 100)}%</output><span/></div>
        </HcCard>
      </>;
      case 'face': return <>
        <HcHeader title="Лицо" hint="Смешивайте архетипы или задавайте пропорции точно; импортируйте лицо из скана, фото или отдельной головы."/>
        <StudioTabs className="hc-subtabs" label="Лицо" items={[{ id: 'blend', label: 'Смешивание' }, { id: 'shape', label: 'Пропорции и импорт' }]}
          selected={faceTab} onChange={setFaceTab}/>
        <HumanReferenceControls/>
        {faceTab === 'blend'
          ? <HumanFaceBlend guid={h.guid} face={h.face} schema={h.info?.faceSchema ?? []} disabled={h.busy || !h.guid}
              onCommit={f => { h.editFace(f); if (bound) void h.apply(f, h.draft); else h.setStatus('Форма готова — нажмите «Показать»'); }}/>
          : <HumanFaceControls key={'face' + project + h.guid} guid={h.guid} face={h.face}
              schema={h.info?.faceSchema ?? []} onChange={f => { h.editFace(f); if (autoApply && bound) void h.apply(f, h.draft); }}
              onImported={g => { h.setGuid(g); h.setFace(null); }}/>}</>;
      case 'makeup': return <HumanMakeupControls info={h.info} draft={h.draft}
          onChange={(look, live) => { h.edit(look); if (autoApply && bound) { if (live) h.live(look, h.face); else void h.apply(h.face, look); } }}/>;
      case 'sculpt': return <HumanSculptControls face={h.info?.face ?? h.face} ready={ready} onChanged={refresh}
          prepare={async () => {
            // A freshly shown (e.g. assembled) model has no face selection yet:
            // pick its head meshes automatically so the brush works at once.
            if (h.info?.face?.meshes.length || !h.guid) return;
            const meshes = guessFaceMeshes((await invoke('studio.humanWardrobeInfo', { guid: h.guid })).meshes);
            if (!meshes.length) throw Error('Не нашёл меш лица: отметьте его в «Лицо → Пропорции и импорт»');
            const f: HumanFaceShape = { version: 1, meshes, landmarks: h.face?.landmarks ?? [], values: h.face?.values ?? {} };
            h.setFace(f); await h.apply(f, h.draft);
          }}/>;
      case 'skin': return <><HcHeader title="Кожа" hint="Тон, текстура и блеск кожи. Изменения применяются, когда вы отпускаете ползунок."/>
        <HcCard title="Тон и поверхность"><LookSliders groups={['skin']} h={h} live={autoApply && bound}/></HcCard></>;
      case 'eyes': return <><HcHeader title="Глаза" hint="Цвет радужки, склера и блеск. Камера приближена к лицу."/>
        <HcCard title="Радужка и склера"><LookSliders groups={['eyes']} h={h} live={autoApply && bound}/></HcCard></>;
      case 'hair': return <><HcHeader title="Волосы" hint="Цвет и финиш прядей, бровей и ресниц. Форму причёски задаёт модель волос."/>
        <HcCard title="Волосы и ресницы"><LookSliders groups={['hair', 'lashes']} h={h} live={autoApply && bound}/></HcCard><HumanHairstyleControls guid={h.guid} outfit={outfit}/></>;
      case 'body': return <><HcHeader title="Тело" hint="Рост и пропорции. Одежда следует за скелетом — при крайних значениях проверьте пересечения."/>
        <HcCard title="Пропорции"><LookSliders groups={['body']} h={h} live={autoApply && bound}/></HcCard><HcCard title="Рост" hint={pv?.baseHeight ? 'Исходная модель: ' + Math.round(pv.baseHeight * 100) + ' см' : 'Покажите персонажа, чтобы узнать рост'}>
          {pv?.baseHeight ? <div className="hc-row">
            <StudioInput type="number" aria-label="Рост, см" min={Math.round(pv.baseHeight * 80)} max={Math.round(pv.baseHeight * 120)} step={1}
              value={Math.round(pv.baseHeight * 100 * (h.draft.values['body.height'] ?? 1))}
              onChange={e => { const cm = e.target.valueAsNumber; if (!Number.isFinite(cm) || !pv.baseHeight) return;
                const k = Math.min(1.2, Math.max(.8, cm / (pv.baseHeight * 100)));
                const look = { ...h.draft, values: { ...h.draft.values, 'body.height': Math.round(k * 1e4) / 1e4 } }; h.edit(look); if (autoApply && bound) h.live(look, h.face); }}/>
            <span className="hc-hint">см · от {Math.round(pv.baseHeight * 80)} до {Math.round(pv.baseHeight * 120)}</span></div> : null}
          <HcSwitch label="Линейка во вьюпорте" hint="Шкала рядом с персонажем, деления по 10 см" checked={Boolean(pv?.ruler)} disabled={!bound}
            onChange={v => void invoke('studio.humanRuler', { enabled: v }).then(() => refresh())}/>
        </HcCard></>;
      case 'clothing': return <HumanWardrobeControls key={project + h.guid} guid={h.guid} look={h.draft} face={h.face} outfit={outfit}
        onShow={g => { h.setGuid(g); h.setFace(null); h.setStatus('Показан собранный персонаж'); }}/>;
      case 'pose': return <><HcHeader title="Поза и свет" hint="Мимика и поза для проверки лица в движении, студийный свет превью."/><HumanPreviewAnimationControls key={'anim' + project}/><HumanStudioLightingControls key={'light' + project}/>
        <details><summary>Роли материалов ({Object.keys(h.draft.materials).length})</summary><div className="hc-grid">
          {Object.entries(h.draft.materials).map(([m, role]) => <StudioField key={m} label={m} htmlFor={'role-' + m}>
            <StudioSelect id={'role-' + m} value={role} disabled={h.busy} onChange={e => h.edit({ ...h.draft, materials: { ...h.draft.materials, [m]: e.target.value } })}>
              {['skin', 'hair', 'lashes', 'eyes', 'cloth'].map(r => <option key={r}>{r}</option>)}</StudioSelect></StudioField>)}
        </div></details></>;
    }
  };

  return <StudioPanel className="studio-panel-human_creator" title="Character Creator" eyebrow="VIVID"
    actions={<div className="hc-row">
      <StudioTabs label="Источник" items={[{ id: 'model', label: 'Модель' }, { id: 'dna', label: 'VIVID DNA' }]} selected={source} onChange={setSource}/>
      <StudioButton size="sm" title="Студия персонажа: редактор слева, вьюпорт по центру"
        onClick={() => window.dispatchEvent(new CustomEvent(DOCK_PRESET_EVENT, { detail: 'character' }))}><LayoutDashboard size={14}/></StudioButton>
    </div>}
    footer={source === 'model' ? <div className={'hc-status' + (h.error ? ' hc-status--error' : '')} role="status" title={h.error || h.status || pv?.reason || ''}>
      <StudioBadge tone={ready ? 'success' : bound ? 'warning' : 'neutral'}>{ready ? 'Во вьюпорте' : bound ? 'Загрузка' : 'Не показан'}</StudioBadge>
      <span>{h.error || (bound && !ready ? (pv?.reason ?? 'Загрузка персонажа…') : h.status) || 'Готово'}</span></div> : undefined}>
    {source === 'dna' ? <HumanDnaCreator snapshot={snapshot}/> : <div className="hc-shell" onKeyDown={keys}>
      <nav className="hc-rail" aria-label="Категории персонажа">{CATEGORIES.map(c => { const Icon = c.icon;
        return <button key={c.id} type="button" className="hc-rail__item" aria-current={category === c.id ? 'page' : undefined}
          onClick={() => pick(c)} title={c.label}><Icon size={18}/><span>{c.label}</span></button>; })}</nav>
      <div className="hc-main">
        <div className="hc-row hc-toolbar">
          <StudioTabs label="Камера" items={[{ id: 'face', label: 'Лицо' }, { id: 'torso', label: 'Торс' }, { id: 'full', label: 'Рост' }]}
            selected={camera} onChange={v => frame(v)}/>
          <span className="hc-spacer"/>
          <StudioButton size="sm" title="Удерживайте, чтобы увидеть исходную модель" aria-pressed={comparing} disabled={!ready}
            onPointerDown={() => compare(true)} onPointerUp={() => compare(false)} onPointerLeave={() => { if (comparing) compare(false); }}>
            <SplitSquareHorizontal size={14}/> До/после</StudioButton>
          <StudioButton size="sm" aria-label="Отменить (⌘Z)" title="Отменить (⌘Z)" disabled={h.busy || !h.undo.length} onClick={undo}><Undo2 size={14}/></StudioButton>
          <StudioButton size="sm" aria-label="Повторить (⇧⌘Z)" title="Повторить (⇧⌘Z)" disabled={h.busy || !h.redo.length} onClick={redo}><Redo2 size={14}/></StudioButton>
        </div>
        <div className="hc-content" onPointerUp={() => { if (['skin', 'makeup', 'eyes', 'hair', 'body'].includes(category)) commit(); }}>{content()}</div>
      </div>
    </div>}
  </StudioPanel>;
}
