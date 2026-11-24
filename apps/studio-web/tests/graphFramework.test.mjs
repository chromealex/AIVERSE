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

import {textureLayout,duplicateTextureNode,texturePreviewKey} from '../src/graphs/textureGraphAuthoring.ts';
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
