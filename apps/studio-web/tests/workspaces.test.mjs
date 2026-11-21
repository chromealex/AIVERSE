import test from 'node:test';
import assert from 'node:assert/strict';
import { WORKSPACES, WORKSPACE_INFO, workspaceDockTree, legacyWorkspace, readWorkspaceDock, saveWorkspaceDock, readActiveWorkspace, saveActiveWorkspace } from '../src/workspaces.ts';
import { dockStacks, panelStack, STUDIO_PANELS } from '../src/nestedDock.ts';
import { LEGACY_WINDOWS_NOT_MIGRATED } from '../src/legacyWindows.ts';
import { fuzzyScore, parseQuery, searchCommands } from '../src/commandSearch.ts';

function memory() { const m = new Map(); return { getItem: k => m.has(k) ? m.get(k) : null, setItem: (k, v) => m.set(k, String(v)), m }; }

test('every workspace has Inspector and a center view; panels appear once', () => {
  for (const id of WORKSPACES) {
    const tree = workspaceDockTree(id);
    const tabs = dockStacks(tree).flatMap(s => s.tabs);
    assert.equal(new Set(tabs).size, tabs.length, id);
    assert.ok(panelStack(tree, 'inspector'), id + ' inspector');
    assert.ok(tabs.every(t => STUDIO_PANELS.includes(t)));
  }
  assert.ok(panelStack(workspaceDockTree('look'), 'materials'));
  assert.ok(panelStack(workspaceDockTree('world'), 'nereid_flow'));
});
test('every legacy window belongs to exactly one workspace', () => {
  for (const w of LEGACY_WINDOWS_NOT_MIGRATED) assert.ok(legacyWorkspace(w.id), w.id);
  const all = WORKSPACES.flatMap(id => WORKSPACE_INFO[id].legacy);
  assert.equal(new Set(all).size, all.length);
  assert.equal(all.length, LEGACY_WINDOWS_NOT_MIGRATED.length);
});
test('layouts are stored per workspace; scene inherits the old layout', () => {
  const store = memory();
  store.setItem('aiverse.studio.dock.v3', JSON.stringify(workspaceDockTree('look')));
  assert.ok(panelStack(readWorkspaceDock('scene', store), 'materials'), 'migrated');
  saveWorkspaceDock('world', workspaceDockTree('animate'), store);
  assert.ok(panelStack(readWorkspaceDock('world', store), 'animation'));
  assert.equal(panelStack(readWorkspaceDock('publish', store), 'animation'), null);
  store.setItem('aiverse.studio.dock.v3.logic', '{broken');
  assert.ok(panelStack(readWorkspaceDock('logic', store), 'runtime'));
  assert.equal(readActiveWorkspace(store), 'scene');
  saveActiveWorkspace('look', store); assert.equal(readActiveWorkspace(store), 'look');
  store.setItem('aiverse.studio.workspace', 'nope'); assert.equal(readActiveWorkspace(store), 'scene');
});
test('fuzzy search ranks prefix and word starts first', () => {
  assert.ok(fuzzyScore('ter', 'Terrain') > fuzzyScore('ter', 'Shader graph: Filter'));
  assert.ok(fuzzyScore('sg', 'Shader Graph') > 0);
  assert.equal(fuzzyScore('xyz', 'Terrain'), -1);
  assert.deepEqual(parseQuery('@ bar'), { text: 'bar', only: ['entity'] });
  const run = () => {};
  const entries = [
    { id: 'c1', kind: 'command', title: 'Save', run }, { id: 'w1', kind: 'window', title: 'Terrain', legacy: true, disabled: true, run },
    { id: 'e1', kind: 'entity', title: 'barracks_02', run }, { id: 'a1', kind: 'asset', title: 'terrain_grass.mat', run },
  ];
  const groups = searchCommands(entries, 'ter');
  assert.deepEqual(groups.map(g => g.kind), ['window', 'asset']);
  assert.deepEqual(searchCommands(entries, '').map(g => g.kind), ['command', 'window'], 'entities/assets need a query');
  assert.deepEqual(searchCommands(entries, '@bar').flatMap(g => g.items.map(i => i.id)), ['e1']);
  assert.deepEqual(searchCommands(entries, '', ['e1']).find(g => g.kind === 'entity').items[0].id, 'e1');
});
