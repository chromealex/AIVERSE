import test from 'node:test';
import assert from 'node:assert/strict';
import { buildParams, fieldDraft, fieldLabel, opLabel, resultLine, schemaFields, stateOps } from '../src/panels/toolSchema.ts';
import { TOOL_PANELS, TOOL_PANEL_IDS } from '../src/panels/toolPanels.ts';
import { STUDIO_PANELS } from '../src/nestedDock.ts';

const schema = { properties: { type: { type: 'string' }, minQuality: { type: 'string', enum: ['low', 'medium', 'high'] },
  enabled: { type: 'boolean' }, radius: { type: 'number', minimum: 0, maximum: 10 }, count: { type: 'integer' }, params: { type: 'object' } },
  required: ['type'] };

test('schema fields: required first, kinds from the JSON schema', () => {
  const f = schemaFields(schema);
  assert.equal(f[0].key, 'type'); assert.ok(f[0].required);
  assert.deepEqual(f.map(x => x.kind), ['string', 'enum', 'boolean', 'number', 'integer', 'json']);
  assert.deepEqual(f[1].options, ['low', 'medium', 'high']);
  assert.deepEqual(schemaFields('{}'), []);
});
test('params: empty optional fields are left out, values typed and checked', () => {
  const f = schemaFields(schema);
  assert.deepEqual(buildParams(f, {}).errors, { type: 'Required' });
  const ok = buildParams(f, { type: 'bloom', enabled: 'true', radius: '2.5', count: '3', params: '{"a":1}', minQuality: '' });
  assert.deepEqual(ok.errors, {});
  assert.deepEqual(ok.params, { type: 'bloom', enabled: true, radius: 2.5, count: 3, params: { a: 1 } });
  const bad = buildParams(f, { type: 'x', radius: '11', count: '1.5', minQuality: 'ultra' });
  assert.deepEqual(Object.keys(bad.errors).sort(), ['count', 'minQuality', 'radius']);
});
test('labels', () => {
  assert.equal(fieldLabel('roadType'), 'Road type');
  assert.equal(opLabel('terrain_paint_material', 'terrain'), 'Paint material');
  assert.equal(opLabel('road_add', 'road'), 'Add');
  assert.equal(opLabel('map_info', 'terrain'), 'Map info');
  assert.equal(resultLine({ ok: true, message: 'Готово' }), 'Готово');
  assert.equal(resultLine({ ok: true, warnings: ['a'], chain: [] }), '1 warning · warnings, chain');
});
test('state ops: read-only and runnable without parameters', () => {
  const ops = [{ name: 'a_get', description: '', schema: {}, readOnly: true },
    { name: 'a_info', description: '', schema: { properties: { path: { type: 'string' } }, required: ['path'] }, readOnly: true },
    { name: 'a_set', description: '', schema: {}, readOnly: false }];
  assert.deepEqual(stateOps(ops, ['a_get', 'a_info', 'a_set', 'missing']).map(o => o.name), ['a_get']);
});
test('every tool panel is a registered dock panel', () => {
  for (const id of TOOL_PANEL_IDS) { assert.ok(STUDIO_PANELS.includes(id), id); assert.ok(TOOL_PANELS[id].open.length, id); }
});
test('choice fields start on their first option', () => {
  const [op] = schemaFields({ properties: { op: { type: 'string', enum: ['raise', 'lower'] } } });
  assert.equal(fieldDraft(op), 'raise');
  const [q] = schemaFields({ properties: { minQuality: { type: 'string', enum: ['low', 'high'] } } });
  assert.equal(fieldDraft(q), '');
});
import { agentLineKind } from '../src/panels/agentLogic.ts';
test('agent transcript line kinds', () => {
  assert.equal(agentLineKind('› make a river'), 'user');
  assert.equal(agentLineKind('→ water_set {"id":1}'), 'tool');
  assert.equal(agentLineKind('агент закончил'), 'done');
  assert.equal(agentLineKind('агент остановлен (код 1)'), 'error');
  assert.equal(agentLineKind('I added two mines.'), 'text');
});
import { jsonDiff, previewValue } from '../src/panels/jsonDiff.ts';
test('json diff: paths, kinds, float tolerance, limit', () => {
  const a = { w: 64, entities: [{ type: 'tree', x: 1 }, { type: 'mine', x: 2 }], extra: { fog: 1 } };
  const b = { w: 64, entities: [{ type: 'tree', x: 1.0000001 }, { type: 'mine', x: 3 }, { type: 'farm', x: 4 }], name: 'v2' };
  const d = jsonDiff(a, b);
  assert.deepEqual(d.map(c => c.kind + ' ' + c.path), ['changed entities[1].x', 'added entities[2]', 'removed extra', 'added name']);
  assert.equal(jsonDiff(a, a).length, 0);
  assert.equal(jsonDiff({ a: [1, 2, 3, 4] }, { a: [5, 6, 7, 8] }, 1e-6, 2).length, 2);
  assert.equal(previewValue('x'.repeat(100), 10).length, 10);
});
import { frameSummary, formatBytes } from '../src/panels/jsonDiff.ts';
test('frame summary and byte formatting', () => {
  const s = frameSummary([10, 20, 30, 40]);
  assert.equal(s.avg, 25); assert.equal(s.max, 40); assert.equal(s.min, 10); assert.equal(s.p95, 40);
  assert.equal(frameSummary([]).count, 0);
  assert.equal(formatBytes(2 * 1024 * 1024), '2.0 MB');
});
import { fromTrs, toTrs, IDENTITY, editAffine } from '../src/panels/attachMath.ts';
test('editing a socket offset or one basis component preserves the rest of a custom affine matrix',()=>{
 const source=[1,.25,0,0,-.3,2,.1,0,0,.2,3,0,4,5,6,1];
 const changed=editAffine(source,3,1,-.75);
 assert.deepEqual(changed,source.map((value,i)=>i===13?-.75:value));
 assert.equal(source[13],5);
 assert.deepEqual(editAffine(changed,1,2,8),changed.map((value,i)=>i===6?8:value));
});

test('attachment transforms: TRS round trip, custom matrices flagged', () => {
  const m = fromTrs([1, 2, 3], 90, 2);
  const t = toTrs(m);
  assert.deepEqual(t.t, [1, 2, 3]); assert.equal(Math.round(t.yaw), 90); assert.equal(t.scale, 2); assert.equal(t.custom, false);
  assert.equal(toTrs(IDENTITY).custom, false);
  const tilted = [...IDENTITY]; tilted[1] = 0.5;
  assert.equal(toTrs(tilted).custom, true);
});
import { clipDuration, clipKeyTimes, projectSkeleton } from '../src/panels/animLogic.ts';
test('animation timeline: duration, keys, skeleton fit', () => {
  const clip = { id: 'walk', channels: [{ node: 'hip', times: [0, 0.5, 1] }, { node: 'knee', times: [0, 0.5004, 1.2] }] };
  assert.equal(clipDuration(clip), 1.2);
  assert.deepEqual(clipKeyTimes(clip), [0, 0.5, 1, 1.2]);
  const doc = { nodes: [{ id: 'hip' }, { id: 'knee', parent: 'hip' }] };
  const s = projectSkeleton(doc, [{ id: 'hip', worldPosition: [0, 1, 0] }, { id: 'knee', worldPosition: [0, 0.5, 0] }], 'front', 260, 200);
  assert.equal(s.bones.length, 1); assert.equal(s.joints.length, 2);
  for (const [x, y] of s.joints) { assert.ok(x >= 0 && x <= 260 && y >= 0 && y <= 200); }
});
import { pickSky, splashStages } from '../src/components/projectSplashLogic.ts';
test('project splash stages', () => {
  assert.deepEqual(splashStages({ phase: 'project', done: 0, total: 0, message: '' }).steps.map(s => s.state), ['active', 'todo', 'todo', 'todo']);
  const m = splashStages({ phase: 'models', done: 5, total: 10, message: 'models/a/b.glb' });
  assert.deepEqual(m.steps.map(s => s.state), ['done', 'done', 'active', 'todo']);
  assert.equal(m.steps[2].label, 'Scene models 5 / 10'); assert.equal(m.detail, 'a/b.glb');
  assert.ok(Math.abs(m.ratio - 0.675) < 1e-9);
  assert.equal(splashStages({ phase: 'done', done: 0, total: 0, message: '' }).ratio, 1);
});
test('splash sky pick is stable and in range', () => {
  assert.equal(pickSky('Iron and Gold', 6), pickSky('Iron and Gold', 6));
  for (const n of ['a', 'Iron and Gold', '', 'Kingston']) { const i = pickSky(n, 6); assert.ok(i >= 0 && i < 6); }
  assert.equal(pickSky('x', 0), -1);
});
