import test from 'node:test';
import assert from 'node:assert/strict';
import { parseHierarchyFilter, matchesHierarchy, rangeIds } from '../src/hierarchyFilter.ts';
const e = (id, name, kind, extra = {}) => ({ id, name, kind, ...extra });
test('filters by text, type, owner and flags', () => {
  const f = parseHierarchyFilter('bar t:unit owner:1');
  assert.deepEqual(f, { text: ['bar'], kinds: ['unit'], owners: [1], is: [] });
  assert.ok(matchesHierarchy(e('1', 'barracks', 'Unit', { owner: 1 }), f));
  assert.ok(!matchesHierarchy(e('2', 'barracks', 'Unit', { owner: 2 }), f));
  assert.ok(!matchesHierarchy(e('3', 'tower', 'Unit', { owner: 1 }), f));
  const hidden = parseHierarchyFilter('is:hidden');
  assert.ok(matchesHierarchy(e('4', 'a', 'Prop', { visible: false }), hidden));
  assert.ok(!matchesHierarchy(e('5', 'a', 'Prop'), hidden));
  assert.ok(matchesHierarchy(e('6', 'a', 'Prop'), parseHierarchyFilter('is:selected'), new Set(['6'])));
  assert.deepEqual(parseHierarchyFilter('owner:x').text, ['owner:x']);
});
test('shift ranges keep the clicked row last', () => {
  assert.deepEqual(rangeIds(['a', 'b', 'c', 'd'], 'b', 'd'), ['b', 'c', 'd']);
  assert.deepEqual(rangeIds(['a', 'b', 'c', 'd'], 'd', 'b'), ['c', 'd', 'b']);
  assert.deepEqual(rangeIds(['a'], 'zz', 'a'), ['a']);
});
