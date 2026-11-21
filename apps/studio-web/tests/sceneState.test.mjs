import test from 'node:test';
import assert from 'node:assert/strict';
import {studioSceneMode} from '../src/panels/sceneState.ts';

test('native startup without project displays a welcome state, not a GPU map', () => {
  assert.equal(studioSceneMode(true), 'welcome');
  assert.equal(studioSceneMode(true, null), 'welcome');
  assert.equal(studioSceneMode(true, ''), 'welcome');
  assert.equal(studioSceneMode(true, '  '), 'welcome');
});
test('a real native project enables the Scene renderer', () => {
  assert.equal(studioSceneMode(true, '/Users/example/Projects/Game'), 'renderer');
});
test('browser demo remains separate from native project UI', () => {
  assert.equal(studioSceneMode(false, undefined), 'demo');
  assert.equal(studioSceneMode(false, '/fake/demo'), 'demo');
});
