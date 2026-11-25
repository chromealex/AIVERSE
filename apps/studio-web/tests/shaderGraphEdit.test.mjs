import test from 'node:test';
import assert from 'node:assert/strict';
import {moveShaderGraphNode,addShaderGraphNode,removeShaderGraphNode,setShaderInput,setShaderOutput,setShaderComponent,layoutShaderGraph} from '../src/panels/shaderGraphLayout.ts';
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

test('rewiring preserves slot order and rejects cycles and missing sources',()=>{
 const graph={nodes:[{id:'a'},{id:'b'},{id:'c',inputs:['a','a']},{id:'d',inputs:['c']}]};
 assert.deepEqual(setShaderInput(graph,'c',1,'b').nodes[2].inputs,['a','b']);
 assert.deepEqual(graph.nodes[2].inputs,['a','a']);
 for(const id of ['c','d','unknown'])assert.equal(setShaderInput(graph,'c',0,id),graph);
 assert.equal(setShaderInput(graph,'c',2,'a'),graph);
});
test('output bindings preserve the required color and respect graph domain',()=>{
 const graph={domain:'material',nodes:[{id:'a'}],outputs:{color:'a'}};
 const next=setShaderOutput(graph,'normal','a');
 assert.deepEqual(next.outputs,{normal:'a',color:'a'});
 assert.deepEqual(setShaderOutput(next,'normal','').outputs,graph.outputs);
 assert.equal(setShaderOutput(graph,'color',''),graph);
 assert.equal(setShaderOutput(graph,'color','unknown'),graph);
 const particle={...graph,domain:'particle'};
 assert.equal(setShaderOutput(particle,'normal','a'),particle);
});

test('editing an overridden scalar splats the original across the other components',()=>{
 const graph={parameters:{tint:0.5},nodes:[{id:'p',op:'parameter',name:'tint',value:[1,1,1,1]}]};
 const next=setShaderComponent(graph,'p',2,-2);
 assert.deepEqual(next.parameters.tint,[0.5,0.5,-2,0.5]);
 assert.deepEqual(next.nodes,graph.nodes);
 assert.deepEqual(layoutShaderGraph(next).nodes[0].values,[0.5,0.5,-2,0.5]);
 assert.equal(graph.parameters.tint,0.5);
 assert.equal(setShaderComponent(graph,'p',0,NaN),graph);
});

import {emptyShaderHistory,shaderHistory} from '../src/panels/shaderGraphHistory.ts';
test('graph undo groups pointer moves, survives save and drops redo on a new edit',()=>{
 const start={nodes:[]},a={nodes:[{id:'a'}]},b={nodes:[{id:'b'}]},c={nodes:[{id:'c'}]};
 let h=shaderHistory(emptyShaderHistory,{type:'load',graph:start});
 for(const graph of [a,b,c])h=shaderHistory(h,{type:'move',graph});
 h=shaderHistory(h,{type:'drop'});
 assert.deepEqual(h.past,[start]);
 h=shaderHistory(h,{type:'saved',graph:c});
 h=shaderHistory(h,{type:'undo'});assert.equal(h.graph,start);
 h=shaderHistory(h,{type:'redo'});assert.equal(h.graph,c);
 h=shaderHistory(h,{type:'undo'});
 h=shaderHistory(h,{type:'edit',graph:b});assert.equal(h.future.length,0);
 h=shaderHistory(h,{type:'load',graph:a});assert.equal(h.past.length,0);
});
