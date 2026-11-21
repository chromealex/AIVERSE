import test from 'node:test';
import assert from 'node:assert/strict';
import { mockInvoke } from '../src/mock.ts';

test('typed mock snapshots remain valid for browser demo', async () => {
  const result = await mockInvoke('studio.snapshot', {});
  assert.equal(result.project.name, 'AIVERSE Demo');
  assert.ok(Array.isArray(result.scene.entities));
  assert.ok(result.history && typeof result.history.canUndo === 'boolean');
});
test('mock Shader Graph endpoints return method-specific result shapes', async () => {
  assert.deepEqual(await mockInvoke('studio.shaderGraphs', {}), {graphs: []});
  assert.deepEqual(await mockInvoke('studio.shaderGraphCatalog', {}), {nodes: []});
  await assert.rejects(mockInvoke('studio.shaderGraphGet', {name: 'test'}), /native project/);
});
test('mock assets, materials and lighting use separate typed envelopes', async () => {
  assert.deepEqual(await mockInvoke('studio.assets', {}), {files: [], folders: [], root: 'content'});
  assert.deepEqual(await mockInvoke('studio.materials', {}), {library: {}});
  assert.deepEqual(await mockInvoke('studio.lighting', {}), {lighting: null, lights: []});
});
test('mock viewport acknowledges layout, not a synthetic GPU frame', async () => {
  assert.deepEqual(await mockInvoke('studio.viewportRect', {
    id:'scene', x:0, y:0, width:320, height:180, dpr:2, visible:true
  }), {ok: true});
});
test('native-only editing rejects in DEMO mode', async () => {
  await assert.rejects(mockInvoke('studio.materialCreate', {name:'steel',preset:'metal'}), /native AIVERSE host/);
  await assert.rejects(mockInvoke('studio.lightAdd', {}), /native AIVERSE host/);
  await assert.rejects(mockInvoke('studio.save', {}), /native AIVERSE host/);
});

test('entity authoring never writes through the browser demo', async () => {
  assert.deepEqual(await mockInvoke('studio.entityCatalog', {}), {types: [], mapEntities: false});
  await assert.rejects(mockInvoke('studio.entityCreate', {type:'unit',x:0,z:0,owner:0}), /native project/);
  await assert.rejects(mockInvoke('studio.entityDelete', {entityId:'entity:0'}), /native project/);
  await assert.rejects(mockInvoke('studio.entityDuplicate', {entityId:'entity:0'}), /native project/);
});

test('document mutation requires a native project', async () => {
  assert.equal((await mockInvoke('studio.project', {})).canCreateMap, false);
  await assert.rejects(mockInvoke('studio.newMap', {name:'Map',width:32,height:24}), /native project/);
  await assert.rejects(mockInvoke('studio.openMap', {file:'maps/map.json'}), /native project/);
  await assert.rejects(mockInvoke('studio.saveAs', {}), /native project/);
});

test('browser demo never simulates native gameplay', async () => {
  assert.equal((await mockInvoke('studio.gameEntities',{offset:0,limit:50,query:''})).active,false);
  await assert.rejects(mockInvoke('studio.gameInspect',{epoch:'0',ref:'1'}),/native gameplay/);
  assert.equal((await mockInvoke('studio.gameState',{})).active,false);
  assert.equal((await mockInvoke('studio.gameState',{})).canStep,false);
  await assert.rejects(mockInvoke('studio.gameControl',{action:'step'}),/native AIVERSE/);
  await assert.rejects(mockInvoke('studio.gameStart',{entry:'map'}),/native AIVERSE/);
  await assert.rejects(mockInvoke('studio.gameStop',{}),/native AIVERSE/);
});

test('demo inspector groups entity fields into components and reorder keeps entities', async () => {
  const { mockInvoke } = await import('../src/mock.ts');
  const inspector = await mockInvoke('studio.inspect', { entityId: 'player' });
  assert.equal(inspector.id, 'player');
  assert.ok(inspector.components.some(component => component.id === 'Transform'));
  const before = (await mockInvoke('studio.snapshot', {})).scene.entities.map(e => e.id);
  const moved = await mockInvoke('studio.entityMove', { entityId: 'player', targetId: 'root', placement: 'before' });
  const after = moved.scene.entities.map(e => e.id);
  assert.equal(after[0], 'player');
  assert.deepEqual([...after].sort(), [...before].sort());
  assert.equal(moved.inspector?.id, 'player');
});
