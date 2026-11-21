import test from 'node:test';
import assert from 'node:assert/strict';
import {
  DOCK_AREAS, DOCK_PANELS, defaultDockLayout, normalizeDockLayout, moveDockPanel,
  closeDockPanel, openDockPanel, setActiveDockPanel, dockAreaOf, migrateLegacyLayout
} from '../src/dockLayout.ts';

test('defaults contain each panel once', () => {
  const layout = defaultDockLayout();
  assert.deepEqual(DOCK_AREAS.flatMap(area => layout.areas[area]).sort(), [...DOCK_PANELS].sort());
  assert.equal(layout.active.center, 'scene');
});
test('moving tab across zones preserves identity and updates active selection', () => {
  let state = defaultDockLayout();
  state = moveDockPanel(state, 'inspector', 'bottom');
  assert.equal(dockAreaOf(state, 'inspector'), 'bottom');
  assert.equal(state.active.bottom, 'inspector');
  assert.equal(state.active.right, null);
  assert.equal(DOCK_AREAS.flatMap(area => state.areas[area]).filter(x => x === 'inspector').length, 1);
});
test('reordering tabs within same area preserves other tabs', () => {
  const state = moveDockPanel(defaultDockLayout(), 'animation', 'center', 'scene');
  assert.deepEqual(state.areas.center, ['animation', 'scene', 'game', 'materials']);
  assert.equal(state.active.center, 'animation');
});
test('close/open and activation support empty groups', () => {
  let state = closeDockPanel(defaultDockLayout(), 'console');
  assert.equal(state.active.bottom, null);
  assert.equal(dockAreaOf(state, 'console'), null);
  state = openDockPanel(state, 'console', 'right');
  assert.equal(dockAreaOf(state, 'console'), 'right');
  assert.equal(state.active.right, 'console');
  state = setActiveDockPanel(state, 'left', 'assets');
  assert.equal(state.active.left, 'assets');
});
test('invalid input ignores duplicate/unknown panel IDs and clamps dimensions', () => {
  const state = normalizeDockLayout({
    areas: { left: ['hierarchy', 'hierarchy', 'NOPE'], center: ['scene', 'hierarchy'], right: [], bottom: [] },
    active: { left: 'fake', center: 'scene' },
    leftSize: Infinity, rightSize: -900, bottomSize: 90000
  });
  assert.deepEqual(state.areas.left, ['hierarchy']);
  assert.deepEqual(state.areas.center, ['scene']);
  assert.equal(state.active.left, 'hierarchy');
  assert.equal(state.leftSize, 260);
  assert.equal(state.rightSize, 180);
  assert.equal(state.bottomSize, 480);
});
test('legacy v1 sidebar and tab selection migrates without game data', () => {
  const migrated = migrateLegacyLayout({ selectedTab: 'materials', leftTab: 'assets', leftSize: 333, showConsole: false });
  assert.equal(migrated.active.center, 'materials');
  assert.equal(migrated.active.left, 'assets');
  assert.equal(migrated.leftSize, 333);
  assert.deepEqual(migrated.areas.bottom, []);
  assert.equal(migrated.active.bottom, null);
});
test('unknown/missing layout returns safe defaults', () => {
  assert.deepEqual(normalizeDockLayout(null), defaultDockLayout());
  assert.deepEqual(normalizeDockLayout({}), defaultDockLayout());
});
test('source layout is not mutated when dragging tabs', () => {
  const base = defaultDockLayout();
  const after = moveDockPanel(base, 'assets', 'right');
  assert.deepEqual(base.areas.left, ['hierarchy', 'assets']);
  assert.deepEqual(after.areas.right, ['inspector', 'assets']);
});
