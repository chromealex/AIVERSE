import test from 'node:test';
import assert from 'node:assert/strict';
import { panelStack } from '../src/nestedDock.ts';
import { readWorkspaceDock, saveWorkspaceDock } from '../src/workspaces.ts';
import { factoryPresets, sceneAssetsLeft, readSavedPresets, saveLayoutPreset, deleteLayoutPreset, resetWorkspaceLayouts, serializeLayoutFile, parseLayoutFile, layoutDownloadName } from '../src/layoutPresets.ts';

function memory() {
  const data = new Map();
  return { getItem: key => data.has(key) ? data.get(key) : null, setItem: (key, value) => data.set(key, value) };
}

test('Scene factory preset puts Assets in a full-height left column', () => {
  const tree = sceneAssetsLeft();
  assert.equal(panelStack(tree, 'assets'), 'dock-assets');
  assert.equal(panelStack(tree, 'hierarchy'), 'dock-left');
  assert.equal(panelStack(tree, 'scene'), 'dock-center');
  assert.notEqual(panelStack(tree, 'assets'), 'dock-bottom');
  assert.equal(factoryPresets('scene').some(preset => preset.id === 'scene:assets-left'), true);
  assert.equal(factoryPresets('world').some(preset => preset.id === 'scene:assets-left'), false);
  assert.equal(factoryPresets('world')[0].title, 'World default');
});

test('saved presets stay in this browser and can be deleted', () => {
  const store = memory();
  const tree = sceneAssetsLeft();
  const saved = saveLayoutPreset('  My scene  ', 'scene', tree, store);
  assert.equal(saved.length, 1);
  assert.equal(saved[0].workspace, 'scene');
  assert.equal(saved[0].title, 'My scene');
  assert.equal(panelStack(saved[0].tree, 'assets'), 'dock-assets');
  assert.equal(readSavedPresets('world', store).length, 0);
  assert.equal(deleteLayoutPreset(saved[0].id, store).length, 0);
  assert.equal(readSavedPresets(null, store).length, 0);
});

test('reset puts every workspace back on its factory dock and keeps named presets', () => {
  const store = memory();
  saveWorkspaceDock('scene', sceneAssetsLeft(), store);
  saveLayoutPreset('Kept', 'scene', sceneAssetsLeft(), store);
  resetWorkspaceLayouts(store);
  assert.equal(panelStack(readWorkspaceDock('scene', store), 'assets'), 'dock-bottom');
  assert.equal(readSavedPresets(null, store)[0].title, 'Kept');
});

test('a layout file round-trips and rejects anything else', () => {
  const text = serializeLayoutFile('Assets on the left', 'scene', sceneAssetsLeft());
  const parsed = parseLayoutFile(text);
  assert.equal(parsed.title, 'Assets on the left');
  assert.equal(parsed.workspace, 'scene');
  assert.equal(panelStack(parsed.tree, 'assets'), 'dock-assets');
  assert.equal(parseLayoutFile('{"kind":"nope"}'), null);
  assert.equal(parseLayoutFile('not json'), null);
  assert.equal(layoutDownloadName('Assets on the left'), 'Assets-on-the-left.layout.json');
});
