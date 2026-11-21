import test from 'node:test';
import assert from 'node:assert/strict';
import { resetValues, canReset, copyValues, pasteValues, canRemove, missingOption } from '../src/panels/inspectorActions.ts';
const n = (key, value, extra = {}) => ({ key, label: key, type: 'number', value, ...extra });
const transform = { id: 'Transform', title: 'Transform', properties: [n('x', 5), n('y', 2), n('z', 1), n('rotationX', 10), n('rotationY', 0), n('rotationZ', 0), n('scaleX', 2), n('scaleY', 1), n('scaleZ', 1)] };
test('reset keeps position, zeroes rotation, ones scale', () => {
  assert.deepEqual(resetValues(transform), { rotationX: 0, scaleX: 1 });
  assert.ok(canReset(transform));
  assert.equal(resetValues({ id: 'Unit', title: 'Unit', properties: [n('owner', 1)] }), null);
  const mesh = { id: 'MeshRenderer', title: 'Mesh', properties: [{ key: 'asset', label: 'Mesh', type: 'asset', value: 'content/a.glb' }, { key: 'material:*', label: 'M', type: 'text', value: 'stone', options: [{ label: 'stone', value: 'stone' }] }] };
  assert.deepEqual(resetValues(mesh), { asset: 'builtin:cube', 'material:*': '' });
  assert.ok(canRemove(mesh)); assert.ok(!canRemove(transform));
});
test('paste only writes editable keys that differ on the same component type', () => {
  const copied = copyValues(transform);
  const target = { ...transform, properties: transform.properties.map(p => p.key === 'x' ? { ...p, value: 9 } : p.key === 'y' ? { ...p, editable: false, value: 7 } : p) };
  assert.deepEqual(pasteValues(copied, target), { x: 5 });
  assert.equal(pasteValues(copied, transform), null, 'nothing differs');
  assert.equal(pasteValues(copied, { id: 'Unit', title: 'Unit', properties: [] }), null);
});
test('missing material option detection', () => {
  const opts = [{ label: 'Default', value: '' }, { label: 'stone', value: 'stone' }];
  assert.ok(missingOption({ key: 'material:*', label: 'M', type: 'text', value: 'gone', options: opts }));
  assert.ok(!missingOption({ key: 'material:*', label: 'M', type: 'text', value: 'stone', options: opts }));
  assert.ok(!missingOption({ key: 'material:*', label: 'M', type: 'text', value: '', options: opts }));
});

import { mergeInspectors } from '../src/panels/inspectorActions.ts';
test('multi-selection keeps common components and marks differing values', () => {
  const a = { id: 'a', name: 'A', kind: 'tree', components: [transform, { id: 'Unit', title: 'Unit', properties: [n('owner', 1)] }] };
  const b = { id: 'b', name: 'B', kind: 'rock', components: [{ ...transform, properties: transform.properties.map(p => p.key === 'x' ? { ...p, value: 7 } : p.key === 'y' ? { ...p, editable: false } : p) }] };
  const m = mergeInspectors([a, b]);
  assert.equal(m.name, '2 objects'); assert.equal(m.kind, 'Mixed');
  assert.deepEqual(m.components.map(c => c.id), ['Transform']);
  const x = m.components[0].properties.find(p => p.key === 'x');
  const z = m.components[0].properties.find(p => p.key === 'z');
  assert.ok(x.mixed); assert.ok(!z.mixed);
  assert.equal(m.components[0].properties.find(p => p.key === 'y').editable, false);
  assert.equal(mergeInspectors([a]), a);
});
