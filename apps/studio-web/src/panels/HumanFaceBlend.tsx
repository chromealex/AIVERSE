// MetaHuman-style face blend: drag the handle between six face archetypes;
// region sliders limit which parts of the face follow the blend.
import { useEffect, useRef, useState } from 'react';
import type { HumanFaceShape, HumanLookInfo } from '../types';
import { invoke, onNativeEvent } from '../bridge';
import { StudioAlert } from '../ui';
import { HcCard } from './HumanCreatorUi';
import { FACE_ARCHETYPES, FACE_REGIONS, archetypePoint, blendFace, blendWeights, faceRegionAt, guessFaceMeshes } from './humanCreatorSchema';
import { HcSwitch } from './HumanCreatorUi';

const R = 110, C = 150; // ring radius and centre in the 300x300 SVG
export default function HumanFaceBlend({ guid, face, schema, disabled, onCommit }:
  { guid: string; face: HumanFaceShape | null; schema: HumanLookInfo['faceSchema']; disabled: boolean; onCommit: (f: HumanFaceShape) => void }) {
  const [handle, setHandle] = useState<[number, number]>([0, -.999]);
  const [regions, setRegions] = useState<Record<string, number>>(() => Object.fromEntries(Object.keys(FACE_REGIONS).map(k => [k, 1])));
  const [error, setError] = useState('');
  // Per-part blend: a face part picked in the viewport gets its own handle.
  const [focus, setFocus] = useState<string | null>(null);
  const [partHandles, setPartHandles] = useState<Record<string, [number, number]>>({});
  const [picking, setPicking] = useState(false);
  const dragging = useRef(false);
  const shown: [number, number] = focus ? partHandles[focus] ?? handle : handle;
  const weights = blendWeights(shown);
  const setShown = (h: [number, number]) => { if (focus) setPartHandles(p => ({ ...p, [focus]: h })); else setHandle(h); };
  const pickMode = (on: boolean) => {
    setError('');
    void invoke('studio.humanFacePick', { enabled: on }).then(r => setPicking(r.enabled)).catch(e => { setPicking(false); setError(String(e).replace(/^Error: /, '')); });
  };
  useEffect(() => () => { void invoke('studio.humanFacePick', { enabled: false }).catch(() => {}); }, []);
  useEffect(() => onNativeEvent((event, payload) => {
    if (event !== 'human.facePick') return;
    const p = (payload ?? {}) as { hit?: boolean; point?: [number, number, number]; error?: string };
    if (p.error) { setError(p.error); return; }
    if (!p.hit || !p.point) return;
    const region = faceRegionAt(p.point);
    if (region) setFocus(region);
  }), []);
  const toUnit = (e: React.PointerEvent<SVGSVGElement>): [number, number] => {
    const r = e.currentTarget.getBoundingClientRect();
    let x = ((e.clientX - r.left) / r.width * 300 - C) / R, y = ((e.clientY - r.top) / r.height * 300 - C) / R;
    const l = Math.hypot(x, y); if (l > 1) { x /= l; y /= l; }
    return [x, y];
  };
  const commit = async (h: [number, number], reg = regions) => {
    setError('');
    try {
      let meshes = face?.meshes ?? [];
      if (!meshes.length) {
        if (!guid) throw Error('Сначала выберите персонажа в «Пресетах»');
        meshes = guessFaceMeshes((await invoke('studio.humanWardrobeInfo', { guid })).meshes);
        if (!meshes.length) throw Error('Не нашёл меш лица: отметьте его во вкладке «Пропорции»');
      }
      const parts = focus ? { ...partHandles, [focus]: h } : partHandles;
      onCommit({ version: 1, meshes, landmarks: face?.landmarks ?? [], values: blendFace(focus ? handle : h, reg, schema, FACE_ARCHETYPES, parts) });
    } catch (e) { setError(String(e)); }
  };
  const top = weights.map((w, i) => ({ w, a: FACE_ARCHETYPES[i] })).sort((a, b) => b.w - a.w).filter(x => x.w > .05).slice(0, 3);
  return <><HcCard title={focus ? 'Смешивание: ' + focus : 'Круг смешивания'}
    hint={focus ? 'Ручка меняет только эту часть лица' : 'Тяните ручку к архетипам — лицо смешается после отпускания'}>
    <HcSwitch label="Выбирать часть лица во вьюпорте" hint={picking ? 'Кликните по носу, губам, глазам, скулам или подбородку' : 'Смешивайте нос, рот, глаза отдельно'}
      checked={picking} disabled={disabled} onChange={pickMode}/>
    <div className="hc-chips">
      <button type="button" className="hc-chip" aria-pressed={!focus} onClick={() => setFocus(null)}>Всё лицо</button>
      {Object.keys(FACE_REGIONS).map(r => <button key={r} type="button" className="hc-chip" aria-pressed={focus === r}
        onClick={() => setFocus(r)}>{r}{partHandles[r] ? ' •' : ''}</button>)}
    </div>
    {error && <StudioAlert tone="danger">{error}</StudioAlert>}
    <svg className="hc-blend" viewBox="0 0 300 300" role="slider" aria-label="Круг смешивания формы лица"
      aria-valuetext={top.map(t => t.a.label + ' ' + Math.round(t.w * 100) + '%').join(', ')}
      onPointerDown={e => { if (disabled) return; dragging.current = true; e.currentTarget.setPointerCapture(e.pointerId); setShown(toUnit(e)); }}
      onPointerMove={e => { if (dragging.current) setShown(toUnit(e)); }}
      onPointerUp={e => { if (!dragging.current) return; dragging.current = false; const h = toUnit(e); setShown(h); void commit(h); }}>
      <circle cx={C} cy={C} r={R} className="hc-blend__ring"/>
      <circle cx={C} cy={C} r={R * .62} className="hc-blend__inner"/>
      {FACE_ARCHETYPES.map((a, i) => { const [x, y] = archetypePoint(i); return <line key={a.id} x1={C} y1={C} x2={C + x * R} y2={C + y * R} className="hc-blend__spoke"/>; })}
      <line x1={C} y1={C} x2={C + shown[0] * R} y2={C + shown[1] * R} className="hc-blend__arm"/>
      {FACE_ARCHETYPES.map((a, i) => { const [x, y] = archetypePoint(i); const active = weights[i] > .3;
        return <g key={a.id} className={'hc-blend__node' + (active ? ' hc-blend__node--active' : '')}>
          <circle cx={C + x * R} cy={C + y * R} r={25}/>
          <text x={C + x * R} y={C + y * R + 4} textAnchor="middle">{a.label}</text></g>; })}
      <circle cx={C + shown[0] * R} cy={C + shown[1] * R} r={11} className="hc-blend__handle"/>
    </svg>
    <p className="hc-hint hc-center">{top.map(t => t.a.label + ' ' + Math.round(t.w * 100) + '%').join(' · ')}</p>
    </HcCard>
    <HcCard title="Области" hint="Сколько смешивания получает каждая часть лица">
    <div className="hc-sliders">{Object.keys(FACE_REGIONS).map(r => <div key={r} className="hc-slider">
      <label htmlFor={'blend-' + r}>{r}</label>
      <input id={'blend-' + r} type="range" min={0} max={1} step={.01} value={regions[r]} disabled={disabled}
        onChange={e => setRegions(v => ({ ...v, [r]: Number(e.target.value) }))}
        onPointerUp={() => void commit(shown, regions)}/>
      <output className="hc-mono">{Math.round(regions[r] * 100)}%</output><span/></div>)}</div>
  </HcCard></>;
}
