/** Rules form: known unit-type fields of rules.json, read own vs resolved ("extends") values. */
export type RuleFieldType = 'number' | 'text' | 'boolean';
export interface RuleField { path: string; label: string; group: string; type: RuleFieldType; min?: number; max?: number; step?: number; hint?: string }
export const RULE_GROUPS = ['General', 'Health', 'Movement', 'Vision', 'Cost', 'Weapons', 'Flags'] as const;

export const TYPE_FIELDS: RuleField[] = [
  { path: 'name', label: 'Name', group: 'General', type: 'text' },
  { path: 'supply', label: 'Supply', group: 'General', type: 'number', min: 0, step: 1 },
  { path: 'xp', label: 'XP on kill', group: 'General', type: 'number', min: 0 },
  { path: 'score', label: 'Score', group: 'General', type: 'number', min: 0 },
  { path: 'buildPower', label: 'Build power', group: 'General', type: 'number', min: 0 },
  { path: 'health.max', label: 'Max health', group: 'Health', type: 'number', min: 0, step: 1 },
  { path: 'health.armor', label: 'Armor', group: 'Health', type: 'number', min: 0 },
  { path: 'health.regen', label: 'Regen / s', group: 'Health', type: 'number', min: 0 },
  { path: 'health.armorType', label: 'Armor type', group: 'Health', type: 'text' },
  { path: 'move.speed', label: 'Speed', group: 'Movement', type: 'number', min: 0, step: 0.1 },
  { path: 'move.radius', label: 'Radius', group: 'Movement', type: 'number', min: 0.01, step: 0.05 },
  { path: 'move.mass', label: 'Mass', group: 'Movement', type: 'number', min: 0.01 },
  { path: 'move.maxSlope', label: 'Max slope', group: 'Movement', type: 'number', min: 0.01 },
  { path: 'sight', label: 'Sight', group: 'Vision', type: 'number', min: 0, step: 0.5 },
  { path: 'vision.eye', label: 'Eye height', group: 'Vision', type: 'number', min: 0 },
  { path: 'vision.blocks', label: 'Blocks vision', group: 'Vision', type: 'number', min: 0 },
  { path: 'cost.time', label: 'Build time', group: 'Cost', type: 'number', min: 0, hint: 'seconds' },
  { path: 'cost.supply', label: 'Supply cost', group: 'Cost', type: 'number', min: 0, step: 1 },
  { path: 'worker', label: 'Worker', group: 'Flags', type: 'boolean' },
  { path: 'invulnerable', label: 'Invulnerable', group: 'Flags', type: 'boolean' },
  { path: 'capturable', label: 'Capturable', group: 'Flags', type: 'boolean' },
  { path: 'sellable', label: 'Sellable', group: 'Flags', type: 'boolean' },
  { path: 'unique', label: 'Unique', group: 'Flags', type: 'boolean' },
  { path: 'countsForVictory', label: 'Counts for victory', group: 'Flags', type: 'boolean' },
  { path: 'collider', label: 'Static collider', group: 'Flags', type: 'boolean' },
];
export const WEAPON_FIELDS: RuleField[] = [
  { path: 'damage', label: 'Damage', group: 'Weapons', type: 'number', min: 0 },
  { path: 'range', label: 'Range', group: 'Weapons', type: 'number', min: 0, step: 0.5 },
  { path: 'minRange', label: 'Min range', group: 'Weapons', type: 'number', min: 0 },
  { path: 'cooldown', label: 'Cooldown', group: 'Weapons', type: 'number', min: 0, step: 0.05, hint: 'seconds' },
  { path: 'projectileSpeed', label: 'Projectile speed', group: 'Weapons', type: 'number', min: 0 },
];

type Obj = Record<string, unknown>;
const isObj = (v: unknown): v is Obj => typeof v === 'object' && v !== null && !Array.isArray(v);
export function getPath(root: unknown, path: string): unknown {
  let cur: unknown = root;
  for (const part of path.split('.')) { if (!isObj(cur)) return undefined; cur = cur[part]; }
  return cur;
}
export interface RuleValue { field: RuleField; path: string; own: unknown; resolved: unknown; inherited: boolean }
/** Fields of one type: own value (written in this type) and resolved value (what the game loads). */
export function typeValues(typeId: string, own: unknown, resolved: unknown, resources: string[]): RuleValue[] {
  const out: RuleValue[] = [];
  const add = (field: RuleField, rel: string) => {
    const o = getPath(own, rel), r = getPath(resolved, rel);
    if (field.type === 'boolean' || o !== undefined || r !== undefined || ['name', 'health.max', 'sight'].includes(rel) || rel.startsWith('cost.'))
      out.push({ field, path: 'types.' + typeId + '.' + rel, own: o, resolved: r, inherited: o === undefined && r !== undefined });
  };
  for (const f of TYPE_FIELDS) add(f, f.path);
  for (const res of resources) add({ path: 'cost.' + res, label: res, group: 'Cost', type: 'number', min: 0, step: 1 }, 'cost.' + res);
  const weapons = (isObj(resolved) && Array.isArray(resolved.weapons)) ? resolved.weapons : isObj(own) && Array.isArray(own.weapons) ? own.weapons : [];
  weapons.forEach((w, i) => {
    const ownW = isObj(own) && Array.isArray(own.weapons) ? own.weapons[i] : undefined;
    for (const f of WEAPON_FIELDS) {
      const o = getPath(ownW, f.path), r = getPath(w, f.path);
      if (o === undefined && r === undefined) continue;
      const id = isObj(w) && typeof w.id === 'string' ? w.id : 'weapon ' + (i + 1);
      out.push({ field: { ...f, label: id + ' · ' + f.label }, path: 'types.' + typeId + '.weapons[' + i + '].' + f.path, own: o, resolved: r,
        // weapons are one array: an "extends" type edits a copy of the base array
        inherited: !Array.isArray(isObj(own) ? own.weapons : undefined) });
    }
  });
  return out;
}
/** Parses a form input into a rules value; "" clears the own value (null = delete → inherit). */
export function parseRuleInput(field: RuleField, text: string | boolean): { ok: true; value: unknown } | { ok: false; error: string } {
  if (field.type === 'boolean') return { ok: true, value: Boolean(text) };
  const raw = String(text).trim();
  if (raw === '') return { ok: true, value: null };
  if (field.type === 'text') return { ok: true, value: raw };
  const n = Number(raw);
  if (!Number.isFinite(n)) return { ok: false, error: 'Enter a number' };
  if (field.min !== undefined && n < field.min) return { ok: false, error: 'Minimum ' + field.min };
  if (field.max !== undefined && n > field.max) return { ok: false, error: 'Maximum ' + field.max };
  return { ok: true, value: n };
}
