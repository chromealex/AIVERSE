import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { stripTypeScriptTypes } from 'node:module';
import vm from 'node:vm';
import test from 'node:test';

function bridge(range = 1) {
  const sent = [], timers = new Map();
  let timerId = 0;
  const window = { crypto: { getRandomValues(array) { array[0]=range; return array; } }, webkit: { messageHandlers: { aiverse: {
    postMessage: request => sent.push(request)
  } } } };
  const context = { window,
    setTimeout: callback => { timers.set(++timerId, callback); return timerId; },
    clearTimeout: id => timers.delete(id)
  };
  // Exercise the actual transport with a native-host and timer test double.
  const source = readFileSync(new URL('../src/bridge.ts', import.meta.url), 'utf8')
    .replace(/^import .*;\r?\n/gm, '').replace(/^export /gm, '');
  vm.runInNewContext(stripTypeScriptTypes(source) + '\nglobalThis.invoke = invoke;', context);
  return { ...context, sent, timers };
}

for (const method of ['studio.openProject','studio.openMap','studio.newMap','studio.saveAs','studio.save','studio.assetImportPreview']) {
  test(`${method} waits for native interaction without an RPC timeout`, async () => {
    const b = bridge();
    const result = b.invoke(method, {});
    assert.equal(b.timers.size, 0);
    b.window.__aiverseReceive({ id: b.sent[0].id, result: { cancelled: true } });
    assert.equal((await result).cancelled, true);
  });
}

test('slow native calls are logged, never failed, and late replies still resolve', async () => {
  const b = bridge();
  const result = b.invoke('studio.snapshot', {});
  assert.equal(b.timers.size, 1);
  [...b.timers.values()][0]();
  assert.equal(b.sent.length, 2);
  assert.equal(b.sent[1].method, 'studio.webDiagnostic');
  assert.match(b.sent[1].params.message, /studio.snapshot/);
  b.window.__aiverseReceive({ id: b.sent[0].id, result: { mode: 'edit' } });
  assert.equal((await result).mode, 'edit');
});

test('two WebViews with pending requests resolve only their own broadcast reply',async()=>{
  const first=bridge(0xffffffff), second=bridge(8);
  const atlas=first.invoke('studio.lightBakeAtlas',{}), snapshot=second.invoke('studio.snapshot',{});
  const atlasId=first.sent[0].id, sceneId=second.sent[0].id;
  assert.ok(Number.isSafeInteger(atlasId) && atlasId < 2**47);
  assert.notEqual(atlasId,sceneId);
  for(const web of [first,second])web.window.__aiverseReceive({id:atlasId,result:{png:'atlas'}});
  assert.equal((await atlas).png,'atlas');
  assert.equal(second.timers.size,1);
  second.window.__aiverseReceive({id:sceneId,result:{mode:'edit'}});
  assert.equal((await snapshot).mode,'edit');
});

test('native replies clear ordinary call timers', async () => {
  const b = bridge();
  const result = b.invoke('studio.snapshot', {});
  b.window.__aiverseReceive({ id: b.sent[0].id, result: { mode: 'edit' } });
  assert.equal((await result).mode, 'edit');
  assert.equal(b.timers.size, 0);
});

test('project navigation carries the exact opaque version and reports native conflict', async () => {
  const b = bridge();
  const result = b.invoke('studio.projectHistoryStep', {direction:'undo',expectedVersion:'/game|project|17'});
  assert.equal(b.sent[0].method,'studio.projectHistoryStep');
  assert.equal(b.sent[0].params.direction,'undo');
  assert.equal(b.sent[0].params.expectedVersion,'/game|project|17');
  b.window.__aiverseReceive({id:b.sent[0].id,error:'Project document changed'});
  await assert.rejects(result,/Project document changed/);
  assert.equal(b.timers.size,0);
});
