import test from 'node:test';
import assert from 'node:assert/strict';
import { typeValues, parseRuleInput, getPath, TYPE_FIELDS } from '../src/panels/rulesSchema.ts';
test('own vs inherited values and weapon paths', () => {
  const own = { extends: 'soldier', name: 'Archer', health: { max: 60 } };
  const resolved = { name: 'Archer', health: { max: 60, armor: 1 }, sight: 8, cost: { gold: 50, time: 12 }, weapons: [{ id: 'bow', damage: 7, range: 6 }] };
  const v = typeValues('archer', own, resolved, ['gold', 'wood']);
  const by = p => v.find(x => x.path === 'types.archer.' + p);
  assert.equal(by('health.max').own, 60); assert.ok(!by('health.max').inherited);
  assert.equal(by('health.armor').resolved, 1); assert.ok(by('health.armor').inherited);
  assert.equal(by('cost.gold').resolved, 50); assert.ok(by('cost.wood'));
  assert.equal(by('weapons[0].damage').resolved, 7);
  assert.ok(by('weapons[0].damage').field.label.startsWith('bow'));
  assert.equal(getPath(resolved, 'cost.time'), 12);
  assert.ok(TYPE_FIELDS.every(f => !f.path.includes('[')));
});
test('inputs: blank inherits, numbers validated', () => {
  const f = { path: 'sight', label: 'Sight', group: 'Vision', type: 'number', min: 0 };
  assert.deepEqual(parseRuleInput(f, ''), { ok: true, value: null });
  assert.deepEqual(parseRuleInput(f, '7.5'), { ok: true, value: 7.5 });
  assert.equal(parseRuleInput(f, '-1').ok, false);
  assert.equal(parseRuleInput(f, 'abc').ok, false);
  assert.deepEqual(parseRuleInput({ ...f, type: 'boolean' }, true), { ok: true, value: true });
});
