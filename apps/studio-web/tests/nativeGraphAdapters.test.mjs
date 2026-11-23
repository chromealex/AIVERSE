import test from 'node:test';
import assert from 'node:assert/strict';
import {fxProjection,fxConnectArgs,animationProjection,connectAnimationPose,replaceAnimationEntry,graphNames,graphReply} from '../src/graphs/nativeGraphAdapters.ts';

test('SpectraFX separates execution and named property links',()=>{
 const doc={version:2,nodes:[{id:'spawn',type:'Spawn'},{id:'init',type:'Initialize'},{id:'value',type:'Float'}],
 links:[{from:'spawn',to:'init'},{from:'value',to:'init',toPort:'speed'}],outputs:[]};
 const catalog=[{type:'Spawn',group:'Pipeline',ports:[],v2Only:false},{type:'Initialize',group:'Pipeline',ports:['speed'],v2Only:false},{type:'Float',group:'Values',ports:[],v2Only:true}];
 const view=fxProjection(doc,catalog);
 assert.deepEqual(view.links.map(x=>[x.output,x.input]),[['exec','exec'],['value','data:speed']]);
 assert.deepEqual(view.nodes[1].inputs.map(p=>p.id),['exec','data:speed']);
 assert.deepEqual(fxConnectArgs({node:'value',port:'value'},{node:'init',port:'data:speed'}),{action:'connect',from:'value',to:'init',toPort:'speed'});
 assert.equal(fxConnectArgs({node:'value',port:'value'},{node:'init',port:'exec'}),null);
 assert.equal(fxConnectArgs({node:'init',port:'exec'},{node:'init',port:'exec'}),null);
});
test('animation pose ordering is independent from looping state transitions',()=>{
 const doc={model:'hero.glb',nodes:[{id:'idle',kind:'clip',clip:'Idle'},{id:'walk',kind:'clip',clip:'Walk'},{id:'blend',kind:'blend',a:'idle',b:'walk',weight:.5}],
 states:[{id:'a',output:'idle',transitions:[{to:'b',when:[]}]},{id:'b',output:'blend',transitions:[{to:'a',when:[]}]}],initial:'a'};
 const view=animationProjection(doc,'states');
 assert.equal(view.links.length,2);
 assert.deepEqual(view.links.map(e=>[e.from,e.to]),[['a','b'],['b','a']]);
 const pose=animationProjection(doc,'pose');
 assert.equal(pose.links.length,2);
 const changed=connectAnimationPose(doc,{node:'walk',port:'pose'},{node:'blend',port:'a'});
 assert.equal(changed.nodes[2].a,'walk');assert.equal(doc.nodes[2].a,'idle');
 assert.equal(connectAnimationPose(doc,{node:'blend',port:'pose'},{node:'idle',port:'input'}),doc);
 const state=replaceAnimationEntry(doc,'states','a',{...doc.states[0],output:'walk'});
 assert.equal(state.states[0].output,'walk');assert.equal(state.model,'hero.glb');
 assert.equal(replaceAnimationEntry(doc,'states','a',{id:'different'}),doc);
});
test('native result parsing handles both actual graph list formats',()=>{
 assert.deepEqual(graphNames({graphs:[{name:'smoke'},'walk']}),['smoke','walk']);
 assert.deepEqual(graphReply({ok:true,graph:{version:1}}),{version:1});
 assert.throws(()=>graphReply({ok:false,error:'invalid'}));
 assert.throws(()=>graphNames({}));
});
