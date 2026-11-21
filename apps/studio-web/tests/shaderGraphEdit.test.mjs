import test from 'node:test';
import assert from 'node:assert/strict';
import {moveShaderGraphNode,addShaderGraphNode,removeShaderGraphNode} from '../src/panels/shaderGraphLayout.ts';
test('dragging changes only the targeted node coordinate',()=>{
 const source={domain:'material',nodes:[{id:'a',op:'input',position:[30,40]},{id:'b',op:'constant',position:[200,70]}],outputs:{color:'b'}};
 const next=moveShaderGraphNode(source,'a',90,110);
 assert.deepEqual(next.nodes[0].position,[90,110]);
 assert.deepEqual(next.nodes[1].position,[200,70]);
 assert.deepEqual(source.nodes[0].position,[30,40]);
 assert.deepEqual(next.outputs,{color:'b'});
});
test('invalid drags do not change source and canvas is bounded',()=>{
 const g={nodes:[{id:'a',op:'input',position:[0,0]}]};
 assert.equal(moveShaderGraphNode(g,'a',NaN,0),g);
 assert.deepEqual(moveShaderGraphNode(g,'a',999999,-100).nodes[0].position,[2000,0]);
});

test('native-catalog node append preserves source and uses unique IDs',()=>{
 const src={domain:'material',nodes:[{id:'source',op:'input',position:[0,0]}],outputs:{color:'source'}};
 const next=addShaderGraphNode(src,{id:'multiply',inputs:2});
 assert.equal(next.nodes.length,2);
 assert.deepEqual(next.nodes[1].inputs,['source','source']);
 assert.equal(src.nodes.length,1);
});
test('bounded add rejects invalid op names, counts and oversized graph',()=>{
 const src={nodes:[]};
 assert.equal(addShaderGraphNode(src,{id:'../bad',inputs:0}),src);
 assert.equal(addShaderGraphNode(src,{id:'multiply',inputs:200}),src);
 const full={nodes:Array.from({length:128},(_,i)=>({id:'x'+i,op:'constant'}))};
 assert.equal(addShaderGraphNode(full,{id:'constant',inputs:0}),full);
});
test('delete is refused when graph links or output still depend on node',()=>{
 const src={nodes:[{id:'source',op:'input'},{id:'mix',op:'multiply',inputs:['source']}],outputs:{color:'mix'}};
 assert.equal(removeShaderGraphNode(src,'source'),src);
 assert.equal(removeShaderGraphNode(src,'mix'),src);
 const changed={...src,outputs:{color:'source'}};
 const out=removeShaderGraphNode(changed,'mix');
 assert.equal(out.nodes.length,1);
 assert.equal(changed.nodes.length,2);
});
