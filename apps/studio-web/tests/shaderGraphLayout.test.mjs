import test from 'node:test';
import assert from 'node:assert/strict';
import {layoutShaderGraph,shaderValueComponents,setShaderValue} from '../src/panels/shaderGraphLayout.ts';
test('graph layout retains node ID and links two inputs',()=>{
 const graph={nodes:[{id:'source',op:'input',position:[10,20]},{id:'tint',op:'constant',position:[10,140]},
 {id:'result',op:'multiply',inputs:['source','tint'],position:[230,40]}]};
 const result=layoutShaderGraph(graph);
 assert.equal(result.nodes.length,3);
 assert.equal(result.edges.length,2);
 assert.equal(result.edges[0].x1,140);
 assert.equal(result.edges[0].x2,230);
});
test('empty graph renders finite canvas',()=>assert.deepEqual(layoutShaderGraph({}),
 {nodes:[],edges:[],width:520,height:320}));
test('malformed edges or extreme positions are bounded',()=>{
 const r=layoutShaderGraph({nodes:[{id:'a',op:'input',position:[Infinity,-20]},
  {id:'b',op:'add',inputs:['nope','a'],position:[50000,NaN]}]});
 assert.equal(r.nodes[0].x,0);assert.equal(r.nodes[1].x,2000);
 assert.equal(r.edges.length,1);assert.ok(r.width<=2200);
});

test('all sixteen input wires terminate inside their destination card',()=>{
 const inputs=Array(16).fill('a');
 const diagram=layoutShaderGraph({nodes:[{id:'a',op:'input'},{id:'b',op:'mix',inputs}]});
 const target=diagram.nodes[1];
 assert.equal(diagram.edges.length,16);
 assert.equal(new Set(diagram.edges.map(e=>e.y2)).size,16);
 assert.ok(diagram.edges.every(e=>e.from==='a'&&e.to==='b'&&e.y2>target.y&&e.y2<target.y+target.height));
});

test('value cards keep empty operand slots at their authored indices',()=>{
 const diagram=layoutShaderGraph({nodes:[
  {id:'value',op:'constant',value:[0.2,0.4,0.6,1,2]},
  {id:'target',op:'mix',inputs:[null,'value']}
 ]});
 assert.deepEqual(diagram.nodes[0].values,[0.2,0.4,0.6,1]);
 assert.deepEqual(diagram.nodes[1].inputs,['','value']);
 assert.equal(diagram.edges[0].key,'value>target:1');
});

test('editing a scalar uses the same broadcast as the native shader',()=>{
 const graph={nodes:[{id:'gain',op:'constant',value:0.5}]};
 assert.deepEqual(shaderValueComponents(0.5),[0.5,0.5,0.5,0.5]);
 assert.equal(setShaderValue(graph,'gain',[0.5,0.5,0.5,0.5]),graph);
 assert.deepEqual(setShaderValue(graph,'gain',[0.5,1,0.5,0.5]).nodes[0].value,[0.5,1,0.5,0.5]);
});
