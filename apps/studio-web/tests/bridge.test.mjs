import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { stripTypeScriptTypes } from 'node:module';
import vm from 'node:vm';
import test from 'node:test';

function bridge() {
  const sent = [], timers = new Map();
  let timerId = 0;
  const window = { webkit: { messageHandlers: { aiverse: {
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

test('ordinary native calls still time out and ignore late replies', async () => {
  const b = bridge();
  const result = b.invoke('studio.snapshot', {});
  assert.equal(b.timers.size, 1);
  const rejected = assert.rejects(result, /Native bridge timeout: studio.snapshot/);
  [...b.timers.values()][0]();
  await rejected;
  b.window.__aiverseReceive({ id: b.sent[0].id, result: {} });
});

test('native replies clear ordinary call timers', async () => {
  const b = bridge();
  const result = b.invoke('studio.snapshot', {});
  b.window.__aiverseReceive({ id: b.sent[0].id, result: { mode: 'edit' } });
  assert.equal((await result).mode, 'edit');
  assert.equal(b.timers.size, 0);
});
