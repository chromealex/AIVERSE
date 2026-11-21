import test from 'node:test';
import assert from 'node:assert/strict';
import {
  FLOATING_KEY, floatOwner, floatTreeFor, floatingPanels, floatingParams, readFloatTree, readFloating, releasePanel,
  removeFloatWindow, saveFloatTree, saveFloating, setFloatPanels, treePanels, updateFloatingFrame, newFloatKey, validFloatKey
} from '../src/floating.ts';
import { splitDockStack, dockStacks } from '../src/nestedDock.ts';

function memory() { const m = new Map(); return { getItem: k => m.has(k) ? m.get(k) : null, setItem: (k, v) => m.set(k, String(v)), removeItem: k => m.delete(k), m }; }

test('window query carries a key and known panels', () => {
  assert.deepEqual(floatingParams('?window=w1&panels=inspector,console,nope,inspector'), { key: 'w1', panels: ['inspector', 'console'] });
  assert.deepEqual(floatingParams('?window=w2'), { key: 'w2', panels: [] });
  assert.equal(floatingParams('?window=../x&panels=scene'), null);
  assert.equal(floatingParams(''), null);
  assert.ok(validFloatKey(newFloatKey()));
});

test('floating list round-trips; junk, empty windows and duplicate panels are dropped', () => {
  const store = memory();
  saveFloating([{ key: 'a', panels: ['inspector', 'console'], x: 10, y: 20, width: 400, height: 500 }, { key: 'b', panels: [] }], store);
  assert.deepEqual(readFloating(store), [{ key: 'a', panels: ['inspector', 'console'], x: 10, y: 20, width: 400, height: 500 }]);
  store.setItem(FLOATING_KEY, JSON.stringify([{ key: 'a', panels: ['inspector'] }, { key: 'b', panels: ['inspector', 'game'] }, { key: 'a', panels: ['scene'] }, 5, { key: 'BAD', panels: ['scene'] }]));
  assert.deepEqual(readFloating(store), [{ key: 'a', panels: ['inspector'] }, { key: 'b', panels: ['game'] }]);
  store.setItem(FLOATING_KEY, '{bad');
  assert.deepEqual(readFloating(store), []);
});

test('panels belong to exactly one floating window', () => {
  let list = setFloatPanels([], 'a', ['inspector', 'console']);
  list = setFloatPanels(list, 'b', ['console', 'x']);
  assert.deepEqual(list, [{ key: 'a', panels: ['inspector'] }, { key: 'b', panels: ['console'] }]);
  assert.equal(floatOwner(list, 'console'), 'b');
  assert.deepEqual(floatingPanels(list), ['inspector', 'console']);
  assert.deepEqual(releasePanel(list, 'inspector'), [{ key: 'a', panels: [] }, { key: 'b', panels: ['console'] }]);
  list = updateFloatingFrame(list, { window: 'b', x: -1800, y: 100, width: 600, height: 400 });
  assert.deepEqual(list[1], { key: 'b', panels: ['console'], x: -1800, y: 100, width: 600, height: 400 });
  assert.equal(updateFloatingFrame(list, { window: 'zz', x: 0, y: 0, width: 1, height: 1 }), list);
  assert.deepEqual(removeFloatWindow(list, 'a'), [list[1]]);
});

test('a floating dock keeps its arrangement but holds exactly the given panels', () => {
  assert.deepEqual(treePanels(floatTreeFor(null, ['inspector', 'console'])), ['inspector', 'console']);
  let tree = floatTreeFor(null, ['inspector', 'console']);
  tree = splitDockStack(tree, 'console', dockStacks(tree)[0].id, 'bottom');
  assert.equal(dockStacks(tree).length, 2);
  const kept = floatTreeFor(JSON.parse(JSON.stringify(tree)), ['inspector', 'console', 'assets']);
  assert.equal(dockStacks(kept).length, 2);
  assert.deepEqual(treePanels(kept).sort(), ['assets', 'console', 'inspector']);
  assert.deepEqual(treePanels(floatTreeFor(tree, ['game'])), ['game']);
  const store = memory();
  saveFloatTree('w', tree, store);
  assert.equal(dockStacks(readFloatTree('w', ['inspector', 'console'], store)).length, 2);
});
