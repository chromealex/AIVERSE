// Viewport sculpt (MetaHuman-style): with the brush on, each click on the face
// in the Scene viewport pushes or pulls it; strokes mirror across X when
// symmetry is on. Each stroke is one native face landmark (studio.humanSculpt*).
import { useEffect, useState } from 'react';
import { Minus, MousePointerClick, Plus, RotateCcw, Undo2 } from 'lucide-react';
import { invoke, onNativeEvent } from '../bridge';
import type { HumanFaceShape, HumanSculptState } from '../types';
import { StudioButton } from '../ui';
import { sculptZone } from './humanCreatorSchema';
import { setSculptBrush } from './sculptBrushStore';
import { HcCard, HcHeader, HcNote, HcSegment, HcSlider, HcSwitch } from './HumanCreatorUi';

type Brush = Pick<HumanSculptState, 'armed' | 'radius' | 'strength' | 'mode' | 'symmetry'>;
export default function HumanSculptControls({ face, ready, onChanged, prepare }: { face: HumanFaceShape | null; ready: boolean; onChanged: () => void; prepare?: () => Promise<void> }) {
  const [brush, setBrush] = useState<Brush>({ armed: false, radius: .12, strength: .012, mode: 'push', symmetry: true });
  const [error, setError] = useState('');
  const [miss, setMiss] = useState(false);
  const call = async (params: { enabled?: boolean; radius?: number; strength?: number; mode?: 'push' | 'pull'; symmetry?: boolean }) => {
    setError('');
    try { if (params.enabled && prepare) await prepare(); const r = await invoke('studio.humanSculpt', params); setBrush({ armed: r.armed, radius: r.radius, strength: r.strength, mode: r.mode, symmetry: r.symmetry }); }
    catch (e) { setError(String(e).replace(/^Error: /, '')); }
  };
  const undo = (all: boolean) => void invoke('studio.humanSculptUndo', all ? { all: true } : {}).then(() => onChanged()).catch(e => setError(String(e)));
  useEffect(() => onNativeEvent((event, payload) => {
    if (event !== 'human.sculpt') return;
    const p = (payload ?? {}) as { hit?: boolean; error?: string };
    setError(p.error ?? ''); setMiss(!p.hit && !p.error);
    if (p.hit && !p.error) onChanged();
  }), [onChanged]);
  useEffect(() => () => { void invoke('studio.humanSculpt', { enabled: false }).catch(() => {}); }, []);
  // The Scene viewport paints drag strokes and the brush circle from this.
  useEffect(() => setSculptBrush({ armed: brush.armed, radius: brush.radius }), [brush.armed, brush.radius]);
  useEffect(() => () => setSculptBrush({ armed: false, radius: .12 }), []);
  const strokes = face?.landmarks ?? [];
  const hasFace = (face?.meshes.length ?? 0) > 0;
  return <>
    <HcHeader title="Скульпт" hint="Лепите лицо прямо во вьюпорте: каждый клик вытягивает или вдавливает поверхность, изменение плавно затухает к соседним областям."/>
    {error && <HcNote tone="error">{error}</HcNote>}
    {!hasFace && <HcNote tone="warn">Сначала задайте форму в «Лицо → Смешивание» — меш лица выберется автоматически.</HcNote>}

    <HcCard title="Кисть" hint={brush.armed ? 'Кликайте или ведите кистью по лицу во вьюпорте' : 'Включите, чтобы клики по лицу лепили форму'}>
      <button type="button" className={'hc-brush' + (brush.armed ? ' hc-brush--on' : '')} disabled={!hasFace && !prepare}
        aria-pressed={brush.armed} onClick={() => void call({ enabled: !brush.armed })}>
        <MousePointerClick size={18}/><span>{brush.armed ? 'Кисть включена' : 'Включить кисть'}</span>
        {brush.armed && !ready && <small>персонаж загружается…</small>}
      </button>
      <HcSegment label="Режим кисти" value={brush.mode} disabled={!hasFace && !prepare} onChange={mode => void call({ mode })}
        options={[{ id: 'push', label: 'Вытянуть', icon: <Plus size={14}/> }, { id: 'pull', label: 'Вдавить', icon: <Minus size={14}/> }]}/>
      <HcSwitch label="Симметрия по X" hint="Штрих повторяется на другой половине лица" checked={brush.symmetry}
        onChange={v => void call({ symmetry: v })}/>
    </HcCard>

    <HcCard title="Параметры кисти">
      <HcSlider id="sculpt-radius" label="Радиус" min={0.02} max={0.5} step={0.01} value={brush.radius}
        format={v => Math.round(v * 100) + '% головы'} onChange={v => setBrush(s => ({ ...s, radius: v }))} onCommit={() => void call({ radius: brush.radius })}/>
      <HcSlider id="sculpt-strength" label="Сила" min={0.002} max={0.04} step={0.001} value={brush.strength}
        format={v => Math.round(v / .04 * 100) + "%"} onChange={v => setBrush(s => ({ ...s, strength: v }))} onCommit={() => void call({ strength: brush.strength })}/>
    </HcCard>

    <HcCard title="История" hint={strokes.length ? 'Последние штрихи сверху' : undefined}
      actions={<div className="hc-row">
        <StudioButton size="sm" disabled={!strokes.length} onClick={() => undo(false)} aria-label="Отменить штрих"><Undo2 size={14}/></StudioButton>
        <StudioButton size="sm" disabled={!strokes.length} onClick={() => undo(true)} aria-label="Сбросить все штрихи"><RotateCcw size={14}/></StudioButton>
      </div>}>
      {strokes.length ? <ol className="hc-strokes">{strokes.slice(-8).reverse().map((l, i) => {
        const push = (l.to[2] - l.from[2]) >= 0;
        return <li key={strokes.length - i}><span className={'hc-dot ' + (push ? 'hc-dot--push' : 'hc-dot--pull')}>{push ? '+' : '−'}</span>
          <span>{sculptZone(l.from)}</span><small>{push ? 'вытянуть' : 'вдавить'}</small></li>; })}</ol>
        : <p className="hc-empty">{miss ? 'Промах — кликните по коже лица.' : 'Пока нет штрихов.'}</p>}
    </HcCard>
  </>;
}
