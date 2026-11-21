// Makeup & skin detail layers (lipstick, blush, eyeshadow, stubble): each is a
// soft UV ellipse on the skin texture with strength, colour and position.
// Position is aligned once per model ("Положение"), then saved with the look.
import { useState } from 'react';
import { Wand2 } from 'lucide-react';
import { invoke } from '../bridge';
import { StudioButton } from '../ui';
import type { HumanLook, HumanLookInfo } from '../types';
import { HcCard, HcHeader, HcNote, HcSlider } from './HumanCreatorUi';
import { hexToRgb, rgbToHex } from './humanCreatorSchema';

interface Layer { id: string; title: string; hint: string; strength: string; color?: [string, string, string]; place: { key: string; label: string }[] }
const LAYERS: Layer[] = [
  { id: 'lips', title: 'Помада', hint: 'Цвет губ', strength: 'makeup.lips', color: ['makeup.lipR', 'makeup.lipG', 'makeup.lipB'],
    place: [{ key: 'makeup.lipV', label: 'Высота' }, { key: 'makeup.lipW', label: 'Ширина' }, { key: 'makeup.lipH', label: 'Толщина' }] },
  { id: 'blush', title: 'Румяна', hint: 'Обе щеки сразу', strength: 'makeup.blush', color: ['makeup.blushR', 'makeup.blushG', 'makeup.blushB'],
    place: [{ key: 'makeup.blushOffset', label: 'От центра' }, { key: 'makeup.blushV', label: 'Высота' }, { key: 'makeup.blushSize', label: 'Размер' }] },
  { id: 'shadow', title: 'Тени для век', hint: 'Оба глаза сразу', strength: 'makeup.shadow', color: ['makeup.shadowR', 'makeup.shadowG', 'makeup.shadowB'],
    place: [{ key: 'makeup.shadowOffset', label: 'От центра' }, { key: 'makeup.shadowV', label: 'Высота' }, { key: 'makeup.shadowW', label: 'Ширина' }, { key: 'makeup.shadowH', label: 'Высота пятна' }] },
  { id: 'stubble', title: 'Щетина', hint: 'Тень бороды на подбородке и щеках', strength: 'makeup.stubble',
    place: [{ key: 'makeup.stubbleDensity', label: 'Густота' }, { key: 'makeup.stubbleV', label: 'Высота' }, { key: 'makeup.stubbleSize', label: 'Размер' }] }];

export default function HumanMakeupControls({ info, draft, onChange }:
  { info: HumanLookInfo | null; draft: HumanLook; onChange: (look: HumanLook, live: boolean) => void }) {
  const [aligning, setAligning] = useState(false), [alignError, setAlignError] = useState(''), [aligned, setAligned] = useState(false);
  const align = () => { setAligning(true); setAlignError('');
    void invoke('studio.humanMakeupAlign', {}).then(r => { onChange({ ...draft, values: { ...draft.values, ...r.values } }, false); setAligned(true); })
      .catch(e => setAlignError(String(e).replace(/^Error: /, ''))).finally(() => setAligning(false)); };
  const field = (key: string) => info?.schema.find(f => f.key === key);
  const value = (key: string) => draft.values[key] ?? field(key)?.initial ?? 0;
  const set = (patch: Record<string, number>, live = true) => onChange({ ...draft, values: { ...draft.values, ...patch } }, live);
  if (!info) return <><HcHeader title="Макияж" hint="Помада, румяна, тени и щетина поверх кожи."/><p className="hc-empty">Выберите персонажа в «Пресетах» и загрузите его профиль.</p></>;
  if (!field('makeup.lips')) return <HcNote tone="warn">Эта сборка Studio не знает слоёв макияжа — пересоберите её.</HcNote>;
  return <>
    <HcHeader title="Макияж" hint="Слои поверх кожи. Сначала разместите их по лицу персонажа — положение сохраняется в образе."/>
    <HcCard title="Положение слоёв" hint={aligned ? 'Размещено по лицу — подправьте вручную в «Положении», если нужно' : 'Губы, щёки, веки и подбородок находятся по геометрии лица'}>
      {alignError && <HcNote tone="error">{alignError}</HcNote>}
      <StudioButton variant="primary" disabled={aligning} onClick={align}><Wand2 size={14}/> {aligning ? 'Ищу…' : 'Разместить по лицу'}</StudioButton>
    </HcCard>
    {LAYERS.map(l => {
      const f = field(l.strength)!;
      return <HcCard key={l.id} title={l.title} hint={l.hint}
        actions={l.color && <label className="hc-item__color" title="Цвет"><input type="color" aria-label={'Цвет: ' + l.title}
          value={rgbToHex(l.color.map(value))} onChange={e => { const c = hexToRgb(e.target.value); if (c) set({ [l.color![0]]: c[0], [l.color![1]]: c[1], [l.color![2]]: c[2] }); }}/></label>}>
        <HcSlider id={'mk-' + l.id} label="Сила" min={f.min} max={f.max} step={.01} value={value(l.strength)}
          format={v => Math.round(v * 100) + '%'} onChange={v => set({ [l.strength]: v })}/>
        <details className="hc-item__more"><summary>Положение</summary>
          {l.place.map(p => { const pf = field(p.key)!; return <HcSlider key={p.key} id={'mk-' + p.key} label={p.label} min={pf.min} max={pf.max} step={.001}
            value={value(p.key)} format={v => v.toFixed(3)} onChange={v => set({ [p.key]: v })}/>; })}
          {l.id === 'lips' && <HcSlider id="mk-faceU" label="Центр лица (U)" min={0} max={1} step={.001} value={value('makeup.faceU')}
            format={v => v.toFixed(3)} onChange={v => set({ 'makeup.faceU': v })}/>}
        </details>
      </HcCard>;
    })}
  </>;
}
