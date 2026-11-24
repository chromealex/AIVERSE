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

import {selectionRect,selectInRect,selectNode,moveGroup} from '../src/graphs/graphSelection.ts';
test('selection rectangle and modifiers are deterministic',()=>{
 const rect=selectionRect({x:100,y:100},{x:0,y:0});
 assert.deepEqual(selectInRect([{id:'a',x:20,y:20,width:10,height:10},{id:'b',x:110,y:110,width:20,height:20}],rect),['a']);
 assert.deepEqual(selectNode(['a'],'b',true),['a','b']);
 assert.deepEqual(selectNode(['a','b'],'a',true),['b']);
});
test('group movement clamps without distorting relative spacing',()=>{
 const next=moveGroup([{id:'a',x:10,y:20},{id:'b',x:100,y:120}],-100,5000);
 assert.deepEqual(next,[{id:'a',x:0,y:1900},{id:'b',x:90,y:2000}]);
});

import {graphGeometry,samePort} from '../src/graphs/graphGeometry.ts';
test('multiple named output ports have distinct anchors and follow preview positions',()=>{
 const a={id:'a',label:'A',x:10,y:20,inputs:[],outputs:[{id:'color',type:'float4'},{id:'mask',type:'float'}]};
 const b={id:'b',label:'B',x:300,y:40,inputs:[{id:'mask',type:'float'}],outputs:[]};
 const view={nodes:[a,b],links:[{id:'wire',from:'a',output:'mask',to:'b',input:'mask'}]};
 const base=graphGeometry(view), moved=graphGeometry(view,[{id:'a',x:30,y:50}]);
 assert.equal(base.wires[0].y1,59);
 assert.equal(moved.wires[0].y1,89);
 assert.equal(moved.wires[0].x1,160);
 assert.equal(moved.wires[0].x2,300);
 assert.equal(a.x,10);
 assert.equal(samePort(null,{node:'a',port:'mask'}),false);
 assert.equal(samePort({node:'a',port:'color'},{node:'a',port:'mask'}),false);
});

import {textureLayout,duplicateTextureNode,texturePreviewKey,instantiateTextureGraph,textureFunctionSpec,defineTextureFunction,addTextureFunctionCall,removeTextureFunction,checkFunctionDraft,makeTextureFunctionAsset,linkTextureFunctionAsset,refreshTextureFunctionAsset,detachTextureFunctionAsset} from '../src/graphs/textureGraphAuthoring.ts';
test('texture DAG layout orders dependencies and has bounded nonoverlapping positions',()=>{
 const g={nodes:['c','a','b'].map(node),links:[edge('1','a','b'),edge('2','b','c')]};
 const p=textureLayout(g);assert.ok(p.get('a')[0]<p.get('b')[0]);assert.ok(p.get('b')[0]<p.get('c')[0]);
 const chain={nodes:Array.from({length:32},(_,i)=>node(String(i))),links:Array.from({length:31},(_,i)=>edge(String(i),String(i),String(i+1)))};
 const coords=[...textureLayout(chain).values()];assert.equal(new Set(coords.map(x=>x.join(','))).size,32);assert.ok(coords.every(p=>p.every(v=>v>=0&&v<=2000)));
});
test('texture duplicate preserves source connections but never rebinds outputs',()=>{
 const g={nodes:[{id:'a',op:'constant',inputs:[],properties:{value:1}},{id:'b',op:'remap',inputs:['a'],properties:{low:.2}}],outputs:{color:'b'}};
 const d=duplicateTextureNode(g,'b');assert.equal(d.graph.nodes.length,3);assert.deepEqual(d.graph.nodes[2].inputs,['a']);assert.equal(d.graph.outputs.color,'b');assert.equal(g.nodes.length,2);
 d.graph.nodes[2].properties.low=.8;assert.equal(g.nodes[1].properties.low,.2);
});
test('texture preview key ignores coordinates but includes parameters and output',()=>{
 const g={nodes:[{id:'a',op:'constant',position:[0,0],properties:{value:1}}]};
 const key=x=>texturePreviewKey(x,'{}','color',128,[true]);
 assert.equal(key(g),key({nodes:[{...g.nodes[0],position:[90,30]}]}));
 assert.notEqual(key(g),key({nodes:[{...g.nodes[0],properties:{value:2}}]}));
 assert.notEqual(key(g),texturePreviewKey(g,'{}','normal',128,[true]));
});

test('thumbnail geometry reserves an extra 72 pixels without moving ports',()=>{
 const n=node('a'),a=graphGeometry({nodes:[n],links:[]}),b=graphGeometry({nodes:[{...n,thumbnail:'data:image/png;base64,AA'}],links:[]});
 assert.equal(b.nodes[0].height,a.nodes[0].height+72);assert.deepEqual(a.ports,b.ports);
});

test('saved texture graphs instantiate independent IDs and parameter bindings',()=>{
 const src={nodes:[{id:'a',op:'constant',inputs:[],properties:{value:.2}},{id:'b',op:'invert',inputs:['a'],position:[10,20]}],outputs:{height:'b'},parameters:{level:{default:.2,bindings:[{node:'a',property:'value'}]}}};
 const dest={nodes:[{id:'a',op:'constant',inputs:[]}],outputs:{height:'a'},parameters:{level:{default:.5,bindings:[{node:'a',property:'value'}]}}};
 const first=instantiateTextureGraph(dest,src),second=instantiateTextureGraph(first.graph,src);
 assert.deepEqual(first.graph.outputs,dest.outputs);
 assert.equal(first.graph.nodes[2].inputs[0],first.ids[0]);
 assert.equal(first.outputs.height,first.ids[1]);
 assert.equal(first.graph.parameters[first.parameters.level].bindings[0].node,first.ids[0]);
 assert.notEqual(first.parameters.level,second.parameters.level);
 assert.equal(new Set(second.graph.nodes.map(n=>n.id)).size,5);
 first.graph.nodes[1].properties.value=.8;assert.equal(src.nodes[0].properties.value,.2);
 assert.equal(dest.nodes.length,1);
});
test('saved texture graph insertion rejects dangling references and exceeded budgets atomically',()=>{
 const target={nodes:[],outputs:{}};
 assert.throws(()=>instantiateTextureGraph(target,{nodes:[{id:'a',op:'invert',inputs:['missing']}],outputs:{height:'a'}}),/unresolved/);
 const src={nodes:[{id:'a',op:'constant',inputs:[]}],outputs:{height:'a'}};
 const full={nodes:Array.from({length:64},(_,i)=>({id:'n'+i,op:'constant',inputs:[]}))};
 assert.throws(()=>instantiateTextureGraph(full,src),/64-node/);
 assert.throws(()=>instantiateTextureGraph({size:1024,nodes:full.nodes.slice(0,4)},src),/pixel-node/);
 assert.throws(()=>instantiateTextureGraph(target,{...src,parameters:{x:{default:0,bindings:[{node:'missing',property:'value'}]}}}),/binding/);
 assert.deepEqual(target,{nodes:[],outputs:{}});
});

test('selected texture nodes form a typed reusable function with local parameters',()=>{
 const catalog=[{id:'constant',inputs:[],output:'scalar'},{id:'multiply',inputs:['scalar','scalar'],output:'scalar'}];
 const graph={size:32,nodes:[{id:'external',op:'constant',inputs:[]},{id:'gain',op:'constant',inputs:[],properties:{value:.5}},{id:'result',op:'multiply',inputs:['external','gain']}],outputs:{height:'result'},parameters:{level:{default:.5,bindings:[{node:'gain',property:'value'}]}}};
 const next=defineTextureFunction(graph,catalog,['gain','result'],'result','scale');
 assert.deepEqual(next.functions.scale.inputs,[{id:'input_1',type:'scalar'}]);
 assert.deepEqual(next.functions.scale.nodes[1].inputs,['input_1','gain']);
 assert.deepEqual(next.functions.scale.parameters.level.bindings,[{node:'gain',property:'value'}]);
 assert.equal(graph.functions,undefined);assert.equal(next.nodes.length,3);
 const call=addTextureFunctionCall(next,catalog,'scale','value');
 assert.deepEqual(call.graph.nodes.at(-1).inputs,['']);
 const spec=textureFunctionSpec(call.graph,call.graph.nodes.at(-1),catalog);
 assert.equal(spec.output,'scalar');assert.deepEqual(spec.inputs,['scalar']);
 assert.throws(()=>removeTextureFunction(call.graph,'scale'),/instances/);
 assert.deepEqual(removeTextureFunction(next,'scale').functions,{});
 assert.throws(()=>defineTextureFunction(call.graph,catalog,[call.id],call.id,'nested'),/Nested/);
 assert.throws(()=>addTextureFunctionCall({...next,size:1024},catalog,'scale','value'),/budget/);
});

test('function clipboard drafts cannot reference a deleted definition or bypass expanded limits',()=>{
 const graph={size:32,nodes:[{id:'call',op:'function',inputs:[],properties:{function:'missing',output:'value'}}]};
 assert.throws(()=>checkFunctionDraft(graph),/Missing function definition/);
 const body=Array.from({length:33},(_,i)=>({id:'n'+i,op:'constant',inputs:[]}));
 const calls={...graph,functions:{missing:{nodes:body}},nodes:[...graph.nodes,{...graph.nodes[0],id:'call2'}]};
 assert.throws(()=>checkFunctionDraft(calls),/budget/);
});

test('standalone function assets link pinned snapshots and refresh compatible revisions',()=>{
 const catalog=[{id:'constant',inputs:[],output:'scalar',properties:{value:{type:'number'}}},{id:'multiply',inputs:['scalar','scalar'],output:'scalar',properties:{}}];
 const definition={inputs:[{id:'x',type:'scalar'}],nodes:[{id:'gain',op:'constant',inputs:[],properties:{value:.5}},{id:'out',op:'multiply',inputs:['x','gain']}],outputs:{value:'out'},parameters:{gain:{default:.5,bindings:[{node:'gain',property:'value'}]}}};
 const [asset]=makeTextureFunctionAsset({functions:{scale:definition}},catalog,'scale');
 assert.equal(asset.functionAsset.definition,'scale');assert.equal(asset.nodes.length,2);
 const base={size:32,nodes:[{id:'source',op:'constant',inputs:[]}],outputs:{height:'source'}};
 const linked=linkTextureFunctionAsset(base,'scale_asset',asset,catalog);
 const call=addTextureFunctionCall(linked.graph,catalog,linked.alias,'value');
 call.graph.nodes.at(-1).inputs=['source'];call.graph.nodes.at(-1).properties.arguments={gain:.3};
 const revision=structuredClone(asset);revision.functions.scale.parameters.gain.default=.8;
 const updated=refreshTextureFunctionAsset(call.graph,linked.alias,revision,catalog);
 assert.equal(updated.functions.scale_asset.parameters.gain.default,.8);
 assert.equal(call.graph.functions.scale_asset.parameters.gain.default,.5);
 assert.deepEqual(updated.nodes,call.graph.nodes);assert.deepEqual(updated.outputs,base.outputs);
 const incompatible=structuredClone(revision);incompatible.functions.scale.inputs[0].id='renamed';
 assert.throws(()=>refreshTextureFunctionAsset(call.graph,linked.alias,incompatible,catalog),/Incompatible/);
 assert.throws(()=>linkTextureFunctionAsset(linked.graph,'scale_asset',asset,catalog),/alias/);
 const detached=detachTextureFunctionAsset(updated,linked.alias);assert.deepEqual(detached.functionLinks,{});assert.deepEqual(detached.functions,updated.functions);
});
