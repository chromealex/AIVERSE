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

test('project grouping uses native paths for filtered stable ids', async () => {
  const {groupedHierarchy}=await import('../src/hierarchyFilter.ts');
  const rows=['entity:@a','entity:@b','spawn:0'].map(id=>({node:e(id,id,'Object'),depth:0}));
  const paths={'entity:@a':['West','Scouts'],'entity:@b':['West','Scouts']};
  const view=groupedHierarchy(rows.slice(1),'project:0',new Set(),paths);
  assert.deepEqual(view[0].members,['entity:@b']);
  assert.equal(view[1].label,'Scouts');
  assert.equal(view[3].label,'Unassigned');
  assert.equal(groupedHierarchy(rows,'project:0',new Set([view[0].key]),paths)[1].label,'Unassigned');
});

test('one object can belong to multiple component groups under the same parent', async () => {
  const {groupedHierarchy}=await import('../src/hierarchyFilter.ts');
  const rows=['a','b'].map(id=>({node:e(id,id,'Unit'),depth:0}));
  const paths={a:['Map',['Transform','Health']],b:['Map',['Transform']]};
  const view=groupedHierarchy(rows,'project:1',new Set(),paths);
  assert.deepEqual(view.filter(r=>r.members).map(r=>[r.label,r.members]),
    [['Map',['a','b']],['Transform',['a','b']],['Health',['a']]]);
  const expanded=groupedHierarchy(rows,'project:1',new Set(view.filter(r=>r.collapsed).map(r=>r.key)),paths);
  const occurrences=expanded.filter(r=>r.node?.id==='a').map(r=>r.occurrence);
  assert.equal(new Set(occurrences).size,2);
  const hidden=groupedHierarchy(rows,'project:1',new Set([view[2].key]),paths);
  assert.deepEqual(hidden.filter(r=>r.node).map(r=>r.node.id),['a']);
});

test('nesting uses document identity and promotes filtered orphans', async () => {
  const {nestedDocumentRows}=await import('../src/hierarchyFilter.ts');
  const rows=['a','b','c','d'].map(id=>({node:e(id,'Oak','Unit'),depth:0}));
  const parents={b:'a',c:'b'};
  const show=(items,closed=[])=>nestedDocumentRows(items,parents,new Set(closed)).map(r=>[r.node.id,r.depth,r.children]);
  assert.deepEqual(show(rows),[['a',0,1],['b',1,1],['c',2,0],['d',0,0]]);
  assert.deepEqual(show(rows,['a']),[['a',0,1],['d',0,0]]);
  assert.deepEqual(show(rows.slice(1)),[['b',0,1],['c',1,0],['d',0,0]]);
});
