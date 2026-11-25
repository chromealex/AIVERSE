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

test('owner and type groups retain membership and display order after collapse and filtering', async () => {
  const {groupedHierarchy} = await import('../src/hierarchyFilter.ts');
  const rows = [e('a','Alpha','Worker',{owner:1}),e('b','Mine','Prop',{owner:0}),
    e('c','Beta','Worker',{owner:1}),e('d','Torch','Light')].map(node=>({node,depth:0}));
  assert.equal(groupedHierarchy(rows,'scene',new Set()),rows);
  const list=groupedHierarchy(rows,'owner-type',new Set());
  assert.deepEqual(list[0].members,['a','c']);
  assert.equal(list[1].label,'Worker');
  const hidden=groupedHierarchy(rows,'owner-type',new Set([list[1].key]));
  assert.deepEqual(hidden.filter(r=>r.node).map(r=>r.node.id),['b','d']);
  assert.equal(hidden.at(-3).label,'No owner');
  const filtered=groupedHierarchy(rows.slice(2),'type',new Set());
  assert.deepEqual(filtered[0].members,['c']);
});
