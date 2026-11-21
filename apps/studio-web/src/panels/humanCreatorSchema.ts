// Pure helpers for the Digital Human Creator window (no React, node-testable).
import type { HumanCreatorField, HumanCreatorInfo, HumanLook, HumanLookField } from '../types';

export type CreatorValue = number | string | number[] | string[];

/** Reads a dotted DNA path ("skin.melanin", "expressionRest.12") from the document DNA. */
export function readCreatorValue(dna: Record<string, unknown>, key: string): CreatorValue | undefined {
  let cursor: unknown = dna;
  for (const part of key.split('.')) {
    if (cursor === null || typeof cursor !== 'object') return undefined;
    cursor = Array.isArray(cursor) ? cursor[Number(part)] : (cursor as Record<string, unknown>)[part];
  }
  return cursor as CreatorValue | undefined;
}

/** Clamps and snaps a numeric field the way native validation accepts it; NaN -> null. */
export function normalizeCreatorNumber(field: HumanCreatorField, raw: number): number | null {
  if (!Number.isFinite(raw)) return null;
  const lo = field.min ?? -Infinity, hi = field.max ?? Infinity;
  let v = Math.min(hi, Math.max(lo, raw));
  if (field.step && field.step >= 1) v = Math.round(v);
  return v;
}

const hex2 = (c: number) => Math.round(Math.min(1, Math.max(0, c)) * 255).toString(16).padStart(2, '0');
/** Linear [r,g,b] 0..1 <-> #rrggbb for <input type=color>. */
export function rgbToHex(rgb: number[]): string {
  return '#' + [0, 1, 2].map(i => hex2(Number(rgb?.[i] ?? 0))).join('');
}
export function hexToRgb(hex: string): number[] | null {
  const m = /^#([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(hex);
  return m ? [1, 2, 3].map(i => Math.round(parseInt(m[i], 16) / 255 * 1000) / 1000) : null;
}

/** Field value for the native call: colours as numbers, bones list from comma text. */
export function creatorWireValue(field: HumanCreatorField, input: string | number | number[]): CreatorValue | null {
  if (field.kind === 'color') return Array.isArray(input) && input.length === 3 ? input : null;
  if (field.kind === 'number' || field.kind === 'integer') {
    const n = typeof input === 'number' ? input : Number(input);
    if (field.kind === 'integer') return Number.isInteger(n) && n >= 0 ? n : null;
    return normalizeCreatorNumber(field, n);
  }
  if (field.kind === 'bones') {
    const list = String(input).split(',').map(s => s.trim()).filter(Boolean);
    return list.length <= 128 ? list : null;
  }
  const text = String(input);
  return text.length > 0 && text.length <= 128 ? text : null;
}

/** Expression channels currently non-zero, for the "only active" filter. */
export function activeExpressionCount(info: HumanCreatorInfo | null): number {
  const rest = info?.document.dna.expressionRest;
  return Array.isArray(rest) ? rest.filter(v => Number(v) > 0).length : 0;
}

/** Every head part that must deform together with the face (skin, eyes,
 *  brows, lashes, hair cards, teeth): otherwise hair and brows float off a
 *  reshaped skull. LOD0 preferred; body and clothing never included. */
export function guessFaceMeshes(meshes: string[]): string[] {
  const head = meshes.filter(m => /face|head|eye|brow|lash|hair|teeth|tongue|cornea|beard|mustache|cartilage/i.test(m)
    && !/body|shirt|top|btm|slack|pant|shoe|feet|watch|chain|torso|neck_?wear/i.test(m));
  const lod0 = head.filter(m => /lod0/i.test(m) || !/lod\d/i.test(m));
  return (lod0.length ? lod0 : head).slice(0, 16);
}

/** MetaHuman-style blend of two appearance presets: t=0 -> a, t=1 -> b.
 *  Missing values use the schema default; material roles follow the nearer preset. */
export function blendLooks(a: HumanLook, b: HumanLook, t: number, schema: HumanLookField[]): HumanLook {
  const w = Math.min(1, Math.max(0, Number.isFinite(t) ? t : 0));
  const values: Record<string, number> = {};
  for (const f of schema) {
    const va = a.values[f.key] ?? f.initial, vb = b.values[f.key] ?? f.initial;
    const v = va + (vb - va) * w;
    if (Math.abs(v - f.initial) > 1e-6) values[f.key] = Math.min(f.max, Math.max(f.min, v));
  }
  return { version: 1, values, materials: { ...(w < .5 ? a.materials : b.materials) } };
}

/** MetaHuman-style face blend circle: archetypes sit on a ring, the handle's
 *  position gives inverse-distance weights; each region slider scales how much
 *  of the blend reaches that region's keys. Values stay inside the schema. */
export interface FaceArchetype { id: string; label: string; values: Record<string, number> }
export const FACE_ARCHETYPES: FaceArchetype[] = [
  { id: 'base', label: 'Базовое', values: {} },
  { id: 'narrow', label: 'Узкое', values: { width: -0.108, jawWidth: -0.09, cheekWidth: -0.072, noseWidth: -0.072, chinLength: 0.048, height: 0.048 } },
  { id: 'broad', label: 'Широкое', values: { width: 0.12, jawWidth: 0.132, cheekWidth: 0.09, noseWidth: 0.09, mouthWidth: 0.06, height: -0.03 } },
  { id: 'sharp', label: 'Острое', values: { cheekForward: 0.06, chinForward: 0.072, noseForward: 0.06, noseLength: 0.06, jawWidth: -0.048, browHeight: 0.036 } },
  { id: 'soft', label: 'Мягкое', values: { cheekForward: -0.048, lipVolume: 0.042, noseLength: -0.048, chinForward: -0.048, eyeSpacing: 0.03, width: 0.036 } },
  { id: 'long', label: 'Вытянутое', values: { height: 0.12, chinLength: 0.09, noseLength: 0.072, eyeHeight: 0.036, mouthHeight: -0.036, width: -0.048 } }];
export const FACE_REGIONS: Record<string, string[]> = {
  'Нос': ['noseWidth', 'noseLength', 'noseForward'],
  'Глаза': ['eyeSpacing', 'eyeHeight'],
  'Рот': ['mouthWidth', 'mouthHeight', 'lipVolume'],
  'Челюсть': ['jawWidth', 'chinLength', 'chinForward'],
  'Брови': ['browHeight'],
  'Скулы и овал': ['cheekWidth', 'cheekForward', 'width', 'height']};
/** Ring position of archetype i (unit circle, first at the top). */
export function archetypePoint(i: number, n = FACE_ARCHETYPES.length): [number, number] {
  const a = -Math.PI / 2 + (i * 2 * Math.PI) / n; return [Math.cos(a), Math.sin(a)];
}
export function blendWeights(handle: [number, number], n = FACE_ARCHETYPES.length): number[] {
  const d = Array.from({ length: n }, (_, i) => { const p = archetypePoint(i, n); return Math.hypot(p[0] - handle[0], p[1] - handle[1]); });
  const hit = d.findIndex(v => v < 1e-4);
  if (hit >= 0) return d.map((_, i) => i === hit ? 1 : 0);
  const inv = d.map(v => 1 / (v * v)); const sum = inv.reduce((a, b) => a + b, 0);
  return inv.map(v => v / sum);
}
export function blendFace(handle: [number, number], regions: Record<string, number>,
  schema: { key: string; min: number; max: number }[], archetypes = FACE_ARCHETYPES,
  perRegion: Record<string, [number, number]> = {}): Record<string, number> {
  // perRegion: MetaHuman-style independent blend of one face part (picked in
  // the viewport); parts without their own handle follow the main one.
  const w = blendWeights(handle, archetypes.length); const out: Record<string, number> = {};
  for (const f of schema) {
    const region = Object.entries(FACE_REGIONS).find(([, keys]) => keys.includes(f.key))?.[0];
    const rw = region && perRegion[region] ? blendWeights(perRegion[region], archetypes.length) : w;
    let v = 0; archetypes.forEach((a, i) => { v += (a.values[f.key] ?? 0) * rw[i]; });
    const scale = region ? Math.min(1, Math.max(0, regions[region] ?? 1)) : 1;
    v = Math.min(f.max, Math.max(f.min, v * scale));
    if (Math.abs(v) > 1e-5) out[f.key] = Math.round(v * 1e4) / 1e4;
  }
  return out;
}

/** Human-readable face zone of a normalized sculpt point (x 0..1 left->right, y 0..1 up). */
export function sculptZone(p: [number, number, number]): string {
  const [x, y] = p; const side = x < .44 ? ' слева' : x > .56 ? ' справа' : '';
  const zone = y > .78 ? 'Лоб' : y > .66 ? 'Брови' : y > .56 ? 'Глаза' : y > .45 ? (side ? 'Скула' : 'Нос') : y > .34 ? (side ? 'Щека' : 'Нос') : y > .22 ? 'Рот' : y > .1 ? 'Подбородок' : 'Шея';
  return zone + side;
}

/** Face-blend region under a head point (viewport pick), or null off-face. */
export function faceRegionAt(p: [number, number, number]): string | null {
  const zone = sculptZone(p).replace(/ (слева|справа)$/, '');
  const map: Record<string, string> = { 'Лоб': 'Брови', 'Брови': 'Брови', 'Глаза': 'Глаза', 'Нос': 'Нос', 'Скула': 'Скулы и овал',
    'Щека': 'Скулы и овал', 'Рот': 'Рот', 'Подбородок': 'Челюсть' };
  return map[zone] ?? null;
}

// ---- Random character (with locked parts) and text description drafts ----
export type LookPart = 'face' | 'skin' | 'eyes' | 'hair' | 'body';
export const LOOK_PARTS: { id: LookPart; label: string }[] = [
  { id: 'face', label: 'Лицо' }, { id: 'skin', label: 'Кожа' }, { id: 'eyes', label: 'Глаза' }, { id: 'hair', label: 'Волосы' }, { id: 'body', label: 'Тело' }];
const EYE_COLOURS: [number, number, number][] = [[.176, .077, .045], [.3, .15, .06], [.25, .45, .7], [.2, .45, .25], [.45, .5, .52], [.4, .3, .12]];
/** Plausible random look: only artistic fields move, within believable
 *  ranges; locked parts keep the current values. rnd returns [0,1). */
export function randomLook(current: Record<string, number>, locked: LookPart[], rnd: () => number = Math.random): Record<string, number> {
  const out = { ...current }; const r = (a: number, b: number) => +(a + (b - a) * rnd()).toFixed(4);
  if (!locked.includes('skin')) Object.assign(out, { 'skin.exposure': r(-.6, .6), 'skin.saturation': r(.85, 1.15), 'skin.redness': r(0, .35), 'skin.freckles': rnd() < .3 ? r(.2, .8) : 0 });
  if (!locked.includes('eyes')) { const c = EYE_COLOURS[Math.floor(rnd() * EYE_COLOURS.length)]; Object.assign(out, { 'eyes.irisR': c[0], 'eyes.irisG': c[1], 'eyes.irisB': c[2], 'eyes.redness': r(0, .15) }); }
  if (!locked.includes('hair')) Object.assign(out, { 'hair.melanin': r(.1, .98), 'hair.redness': rnd() < .2 ? r(.6, .95) : r(0, .25), 'hair.gray': rnd() < .2 ? r(.3, .9) : 0 });
  if (!locked.includes('body')) for (const k of ['height', 'width', 'shoulders', 'chest', 'waist', 'hips', 'legs']) out['body.' + k] = r(.93, 1.07);
  return out;
}
export function randomHandle(rnd: () => number = Math.random): [number, number] {
  const a = rnd() * Math.PI * 2, d = Math.sqrt(rnd()) * .85; return [+(Math.cos(a) * d).toFixed(3), +(Math.sin(a) * d).toFixed(3)];
}
interface Rule { re: RegExp; values?: Record<string, number>; face?: string; label: string }
const RULES: Rule[] = [
  { re: /стар|пожил|в возрасте/, values: { 'hair.gray': .6, 'skin.roughness': .78, 'skin.redness': .2 }, label: 'возраст' },
  { re: /молод|юн/, values: { 'hair.gray': 0, 'skin.roughness': .55 }, label: 'молодой' },
  { re: /сед/, values: { 'hair.gray': .9, 'hair.grayLightness': .75 }, label: 'седина' },
  { re: /рыж/, values: { 'hair.melanin': .45, 'hair.redness': .9 }, label: 'рыжие волосы' },
  { re: /блонд|светловолос|белокур|светлые волосы/, values: { 'hair.melanin': .15, 'hair.redness': .2 }, label: 'светлые волосы' },
  { re: /брюнет|темноволос|черноволос|тёмные волосы|темные волосы|чёрные волосы|черные волосы/, values: { 'hair.melanin': .95, 'hair.redness': .05 }, label: 'тёмные волосы' },
  { re: /шатен|русые/, values: { 'hair.melanin': .65, 'hair.redness': .2 }, label: 'русые волосы' },
  { re: /голуб|синие глаза|синеглаз/, values: { 'eyes.irisR': .25, 'eyes.irisG': .45, 'eyes.irisB': .7 }, label: 'голубые глаза' },
  { re: /зелен|зелён/, values: { 'eyes.irisR': .2, 'eyes.irisG': .45, 'eyes.irisB': .25 }, label: 'зелёные глаза' },
  { re: /кари|кареглаз/, values: { 'eyes.irisR': .3, 'eyes.irisG': .15, 'eyes.irisB': .06 }, label: 'карие глаза' },
  { re: /серые глаза|сероглаз/, values: { 'eyes.irisR': .45, 'eyes.irisG': .5, 'eyes.irisB': .52 }, label: 'серые глаза' },
  { re: /загорел|смугл/, values: { 'skin.exposure': -.45, 'skin.redness': .2 }, label: 'загар' },
  { re: /бледн/, values: { 'skin.exposure': .45, 'skin.saturation': .8 }, label: 'бледная кожа' },
  { re: /веснушк|конопат/, values: { 'skin.freckles': .7, 'skin.freckleDensity': .55 }, label: 'веснушки' },
  { re: /обветр|груб.. кож|морщин/, values: { 'skin.roughness': .82, 'skin.redness': .35, 'skin.pores': .35 }, label: 'обветренная кожа' },
  { re: /румян/, values: { 'skin.redness': .45 }, label: 'румянец' },
  { re: /щетин|небрит/, values: { 'makeup.stubble': .7, 'makeup.stubbleDensity': .6 }, label: 'щетина' },
  { re: /помад|красн.. губ|накрашен/, values: { 'makeup.lips': .7 }, label: 'помада' },
  { re: /макияж|тени|накрашен/, values: { 'makeup.shadow': .5, 'makeup.blush': .4 }, label: 'макияж' },
  { re: /высок/, values: { 'body.height': 1.1, 'body.legs': 1.05 }, label: 'высокий' },
  { re: /невысок|низк|коротыш/, values: { 'body.height': .92 }, label: 'невысокий' },
  { re: /худ|строй|тощ/, values: { 'body.width': .9, 'body.waist': .85, 'body.hips': .93 }, label: 'худой' },
  { re: /полн|толст|тучн|грузн/, values: { 'body.width': 1.15, 'body.waist': 1.2, 'body.depth': 1.12 }, label: 'полный' },
  { re: /широкоплеч|мускулист|атлет|качок|мощн/, values: { 'body.shoulders': 1.12, 'body.chest': 1.1 }, label: 'атлетичный' },
  { re: /узк.. лиц|худ.. лиц|узколиц/, face: 'narrow', label: 'узкое лицо' },
  { re: /широк.. лиц|кругл.. лиц|скуласт/, face: 'broad', label: 'широкое лицо' },
  { re: /остр.. черт|резк.. черт|хищн/, face: 'sharp', label: 'острые черты' },
  { re: /мягк.. черт|добр.. лиц/, face: 'soft', label: 'мягкие черты' },
  { re: /вытянут|длинн.. лиц/, face: 'long', label: 'вытянутое лицо' }];
/** Offline draft from a Russian description: look values, an optional face
 *  archetype and the phrases that were understood. */
export function describeToLook(text: string): { values: Record<string, number>; face: string | null; matched: string[] } {
  const t = text.toLowerCase().replace(/ё/g, 'е'); const values: Record<string, number> = {}; const matched: string[] = []; let face: string | null = null;
  for (const rule of RULES) if (rule.re.test(t.replace(/ё/g, 'е'))) {
    if (rule.values) Object.assign(values, rule.values);
    if (rule.face) face = rule.face;
    matched.push(rule.label);
  }
  return { values, face, matched };
}
/** Prompt for an agent with the AIVERSE MCP tools (human_look_*). */
export function agentLookPrompt(text: string, name: string): string {
  return `Создай черновик персонажа для Character Creator в AIVERSE по описанию: «${text.trim()}».\n` +
    `1) Вызови human_look_schema и выбери значения полей (кожа, глаза, волосы, тело).\n` +
    `2) Сохрани их через human_look_save с name "${name}" (overwrite: true).\n` +
    `3) Попроси меня применить образ в Studio (Пресеты), затем вызови studio_character_snapshot и посмотри PNG; поправь значения, если не похоже.\n` +
    `4) Коротко перечисли, какие поля изменил и почему. Форму лица задаёт художник кругом смешивания в Studio.`;
}

/** One "age" control (18..85): moves grey hair, skin roughness, pores,
 *  redness and saturation together; neutral at 30. */
export function ageLook(age: number): Record<string, number> {
  const a = Math.min(85, Math.max(18, age)), t = Math.max(0, (a - 30) / 55), y = Math.max(0, (30 - a) / 12);
  const r = (v: number) => Math.round(v * 1e4) / 1e4;
  return { 'hair.gray': r(Math.min(1, Math.max(0, (a - 38) / 40))), 'hair.grayLightness': r(.6 + .25 * t),
    'skin.roughness': r(.62 + .25 * t - .06 * y), 'skin.pores': r(.1 + .4 * t), 'skin.redness': r(.08 * y + .25 * t),
    'skin.saturation': r(1 - .18 * t + .05 * y), 'skin.cavity': r(Math.min(1, .8 + .2 * t)), 'skin.oiliness': r(.06 + .08 * y - .04 * t) };
}
/** Inverse estimate of the age a look shows (for the slider position). */
export function lookAge(values: Record<string, number>): number {
  const g = values['hair.gray'] ?? 0, r = values['skin.roughness'] ?? .62;
  return Math.round(Math.min(85, Math.max(18, g > 0 ? 38 + g * 40 : 30 + Math.max(-12, ((r - .62) / .25) * 55))));
}
