import test from 'node:test';
import assert from 'node:assert/strict';
import { fitWorkspaceTabs } from '../src/workspaceTabs.ts';

const widths = [50, 50, 50, 70, 90, 50, 60];

test('every tab stays in the bar when the row is wide enough', () => {
  assert.deepEqual(fitWorkspaceTabs(widths, 500, 4), [0, 1, 2, 3, 4, 5, 6]);
});

test('a narrow row keeps a prefix and leaves the rest for the menu', () => {
  const shown = fitWorkspaceTabs(widths, 180, 0);
  assert.ok(shown.includes(0));
  assert.ok(!shown.includes(6));
  assert.deepEqual(shown, [...shown].sort((a, b) => a - b));
});

test('the active tab stays visible when it would otherwise be clipped', () => {
  const shown = fitWorkspaceTabs(widths, 200, 4);
  assert.ok(shown.includes(4));
  assert.ok(!shown.includes(6));
});

test('nothing is shown when there is no room at all', () => {
  assert.deepEqual(fitWorkspaceTabs(widths, 0, 2), []);
});
