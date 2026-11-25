import test from 'node:test';
import assert from 'node:assert/strict';
import { WORKSPACES, WORKSPACE_INFO, workspaceDockTree, legacyWorkspace, readWorkspaceDock, saveWorkspaceDock, readActiveWorkspace, saveActiveWorkspace } from '../src/workspaces.ts';
import { dockStacks, panelStack, STUDIO_PANELS, STUDIO_PANEL_TITLES } from '../src/nestedDock.ts';
import { windowPanelGroups, PANEL_WINDOW_GROUP } from '../src/windowGroups.ts';
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

test('the Windows filter lists every panel while general search stays compact', () => {
  const windows = Array.from({ length: 20 }, (_, i) => ({
    id: String(i), title: 'Panel ' + i, kind: 'window', run() {},
  }));
  const entries = [...windows, { id: 'save', title: 'Save', kind: 'command', run() {} }];
  assert.equal(searchCommands(entries, '/ ')[0].items.length, windows.length);
  assert.deepEqual(searchCommands(entries, '/Panel 19')[0].items.map(item => item.id), ['19']);
  assert.equal(searchCommands(entries, '').find(group => group.kind === 'window').items.length, 8);
});

test('an exact window name wins over a recently used partial match', () => {
  const entries = ['Water', 'NEREID Water'].map(title => ({ id: title, title, kind: 'window', run() {} }));
  assert.equal(searchCommands(entries, '/Water', ['NEREID Water'])[0].items[0].title, 'Water');
});

test('Window groups contain every registered panel exactly once and use English Wind title', () => {
  const groups = windowPanelGroups();
  const panels = groups.flatMap(group => group.panels);
  assert.equal(new Set(groups.map(group => group.id)).size, groups.length);
  assert.equal(new Set(panels).size, panels.length);
  assert.deepEqual([...panels].sort(), [...STUDIO_PANELS].sort());
  for (const group of groups) {
    assert.ok(group.panels.length > 0, group.id);
    for (const panel of group.panels) assert.equal(PANEL_WINDOW_GROUP[panel], group.id);
    const titles = group.panels.map(panel => STUDIO_PANEL_TITLES[panel]);
    assert.deepEqual(titles, [...titles].sort((a, b) => a.localeCompare(b, 'en')));
  }
  assert.equal(STUDIO_PANEL_TITLES.wind, 'Wind');
  assert.equal(PANEL_WINDOW_GROUP.wind, 'fx');
});
