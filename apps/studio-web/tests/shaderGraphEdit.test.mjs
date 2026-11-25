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

import {shaderNodeCatalog,connectShaderInput,disconnectShaderInput,setShaderValue,duplicateShaderNode} from '../src/panels/shaderGraphLayout.ts';
const ops=[{id:'constant',inputs:0,domains:['material']},{id:'add',inputs:2,domains:['material']},{id:'particle',inputs:0,domains:['particle']}];
test('catalog filters native domain and search',()=>{
 assert.deepEqual(shaderNodeCatalog(ops,'material','ADD').map(x=>x.id),['add']);
});
test('connection preserves input slot and refuses a cycle',()=>{
 const src={domain:'material',nodes:[{id:'a',op:'constant'},{id:'b',op:'add',inputs:['a','']},{id:'c',op:'add',inputs:['b','a']}]};
 const next=connectShaderInput(src,ops,'a','b',1);
 assert.deepEqual(next.nodes[1].inputs,['a','a']);
 assert.equal(connectShaderInput(next,ops,'c','b',0),next);
 assert.equal(connectShaderInput(next,ops,'a','b',2),next);
 assert.deepEqual(disconnectShaderInput(next,'b',0).nodes[1].inputs,['','a']);
 assert.deepEqual(src.nodes[1].inputs,['a','']);
});
test('value editing and duplication preserve source',()=>{
 const src={nodes:[{id:'a',op:'constant',value:[1,2,3,4]}]};
 assert.equal(setShaderValue(src,'a',[1,2,NaN,4]),src);
 const next=setShaderValue(src,'a',[4,3,2,1]);
 const copy=duplicateShaderNode(next,'a');
 assert.equal(copy.nodes.length,2);assert.notEqual(copy.nodes[1].id,'a');
 assert.deepEqual(copy.nodes[1].value,[4,3,2,1]);
 assert.deepEqual(src.nodes[0].value,[1,2,3,4]);
});

import {copyShaderNodes,pasteShaderNodes,arrangeShaderNodes} from '../src/panels/shaderGraphLayout.ts';
test('paste remaps internal edges and clears external links without modifying source',()=>{
 const source={domain:'material',nodes:[{id:'a',op:'constant'},{id:'b',op:'add',inputs:['a','outside']}]};
 const clip=copyShaderNodes(source,['a','b']);
 const dest={domain:'material',nodes:[{id:'node1',op:'constant'}]};
 const result=pasteShaderNodes(dest,clip,ops);
 assert.deepEqual(result.ids,['node2','node3']);
 assert.deepEqual(result.graph.nodes[2].inputs,['node2','']);
 assert.equal(dest.nodes.length,1);
 const other={domain:'particle',nodes:[]};
 assert.equal(pasteShaderNodes(other,clip,ops).graph,other);
});
test('auto layout preserves dependencies and refuses cycles',()=>{
 const graph={nodes:[{id:'a',op:'constant'},{id:'b',op:'add',inputs:['a','a']}]};
 const next=arrangeShaderNodes(graph);
 assert.ok(next.nodes[1].position[0]>next.nodes[0].position[0]);
 assert.deepEqual(next.nodes[1].inputs,['a','a']);
 const cyclic={nodes:[{id:'a',op:'add',inputs:['a']}]};
 assert.equal(arrangeShaderNodes(cyclic),cyclic);
});

import {shaderTintTemplate,setShaderOutput} from '../src/panels/shaderGraphLayout.ts';
test('template requires real catalog entries and preserves existing output bindings',()=>{
 const catalog=[...ops,{id:'input',inputs:0,domains:['material']},{id:'multiply',inputs:2,domains:['material']}];
 assert.equal(shaderTintTemplate('particle',catalog),null);
 const template=shaderTintTemplate('material',catalog);
 const graph={domain:'material',nodes:[{id:'a',op:'constant'}],outputs:{color:'a'}};
 const result=pasteShaderNodes(graph,template,catalog);
 assert.equal(result.ids.length,3);assert.deepEqual(result.graph.outputs,{color:'a'});
 assert.equal(setShaderOutput(result.graph,'color','missing'),result.graph);
 const next=setShaderOutput(result.graph,'color',result.ids[2]);
 assert.equal(next.outputs.color,result.ids[2]);assert.equal(graph.outputs.color,'a');
});
