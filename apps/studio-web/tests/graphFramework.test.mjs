import test from 'node:test';
import assert from 'node:assert/strict';
import {GraphRegistry, validateTopology, DraftHistory} from '../src/graphs/graphFramework.ts';
const node=id=>({id,label:id,x:0,y:0,inputs:[{id:'in',type:'float'}],outputs:[{id:'out',type:'float'}]});
const edge=(id,from,to)=>({id,from,to,input:'in',output:'out'});
test('graph policy allows animation loops but rejects dataflow cycles',()=>{
 const g={nodes:[node('a'),node('b')],links:[edge('1','a','b'),edge('2','b','a')]};
 assert.deepEqual(validateTopology(g,{cycles:'allow'}),[]);
 assert.ok(validateTopology(g,{cycles:'reject'}).some(d=>d.code==='cycle'));
});
test('topology detects dangling ports and multiple single-port links',()=>{
 const g={nodes:[node('a'),node('b')],links:[edge('1','a','b'),edge('2','a','b'),edge('3','missing','b')]};
 const codes=validateTopology(g,{cycles:'reject'}).map(d=>d.code);
 assert.ok(codes.includes('dangling-link'));assert.ok(codes.includes('input-cardinality'));
});
test('typed compatibility is policy-owned',()=>{
 const a=node('a'), b=node('b');b.inputs[0].type='vec4';
 const view={nodes:[a,b],links:[edge('1','a','b')]};
 assert.ok(validateTopology(view,{cycles:'reject'}).some(d=>d.code==='port-type'));
 assert.deepEqual(validateTopology(view,{cycles:'reject',compatible:()=>true}),[]);
});
test('registry rejects duplicate domains and history clears redo on edit',()=>{
 const registry=new GraphRegistry();
 const adapter={domain:'shader',policy:{cycles:'reject'},project:x=>x,move:x=>x};
 registry.register(adapter);assert.equal(registry.get('shader'),adapter);
 assert.throws(()=>registry.register(adapter));
 const h=new DraftHistory(0,2);h.push(1);h.push(2);h.push(3);
 assert.equal(h.undo(),2);assert.equal(h.undo(),1);assert.equal(h.undo(),1);
 h.push(7);assert.equal(h.redo(),7);
});
