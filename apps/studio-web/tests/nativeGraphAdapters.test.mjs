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

import {addAnimationTransition,editAnimationTransition,setAnimationInitial,modelProjection,connectModelParent,modelingReply} from '../src/graphs/nativeGraphAdapters.ts';
test('state transitions preserve priority, allow loops and reject invalid times',()=>{
 const doc={states:[{id:'idle',output:'pose',transitions:[{to:'walk',when:[]}]},{id:'walk',output:'pose',transitions:[]}],initial:'idle'};
 const next=addAnimationTransition(doc,{node:'walk',port:'transition'},{node:'idle',port:'enter'});
 assert.deepEqual(next.states[1].transitions,[{to:'idle',fade:.2,after:0,when:[]}]);
 assert.equal(addAnimationTransition(next,{node:'walk',port:'transition'},{node:'idle',port:'enter'}),next);
 assert.equal(editAnimationTransition(next,'walk',0,{fade:-1}),next);
 const changed=editAnimationTransition(next,'walk',0,{fade:.5});
 assert.equal(changed.states[1].transitions[0].fade,.5);
 assert.equal(editAnimationTransition(changed,'walk',0,null).states[1].transitions.length,0);
 assert.equal(setAnimationInitial(doc,'walk').initial,'walk');
 assert.equal(doc.initial,'idle');
});
test('model parent graph preserves geometry and rejects hierarchy cycles',()=>{
 const doc={nodes:[{id:'root'},{id:'body',parent:'root',geometry:{kind:'box',size:[1,2,3]}},{id:'other'}],materials:[{id:'mat'}]};
 assert.equal(modelProjection(doc).links[0].from,'root');
 assert.equal(connectModelParent(doc,{node:'body',port:'children'},{node:'root',port:'parent'}),doc);
 const next=connectModelParent(doc,{node:'other',port:'children'},{node:'body',port:'parent'});
 assert.equal(next.nodes[1].parent,'other');assert.equal(doc.nodes[1].parent,'root');
 assert.deepEqual(next.nodes[1].geometry,doc.nodes[1].geometry);
 assert.deepEqual(next.materials,doc.materials);
 assert.deepEqual(modelingReply({ok:true,document:doc,revision:7}),{document:doc,revision:7});
 assert.throws(()=>modelingReply({ok:true,document:doc,revision:0}));
});

import {geometryProjection,connectGeometryOperand,setGeometryBoolean,moveGeometryModifier,animationScalarNames,setAnimationScalar,editAnimationCondition} from '../src/graphs/nativeGraphAdapters.ts';
test('geometry graph distinguishes CSG operands from transform parents',()=>{
 const doc={nodes:[{id:'a',geometry:{kind:'box'}},{id:'b',parent:'a',geometry:{kind:'sphere'}},{id:'c',geometry:{kind:'boolean',a:'a',b:'b',operation:'union',modifiers:[{kind:'normals'},{kind:'uv'}]}}]};
 const view=geometryProjection(doc);
 assert.deepEqual(view.links.map(l=>[l.from,l.to,l.input]),[['a','c','a'],['b','c','b']]);
 const next=connectGeometryOperand(doc,{node:'b',port:'mesh'},{node:'c',port:'a'});
 assert.equal(next.nodes[2].geometry.a,'b');
 assert.equal(doc.nodes[2].geometry.a,'a');
 assert.equal(connectGeometryOperand(doc,{node:'c',port:'mesh'},{node:'c',port:'a'}),doc);
 assert.equal(setGeometryBoolean(doc,'c','invalid'),doc);
 assert.equal(setGeometryBoolean(doc,'c','subtract').nodes[2].geometry.operation,'subtract');
 assert.deepEqual(moveGeometryModifier(doc,'c',1,-1).nodes[2].geometry.modifiers.map(m=>m.kind),['uv','normals']);
 assert.equal(moveGeometryModifier(doc,'c',0,-1),doc);
});
test('typed animation conditions preserve graph parameter types and transition order',()=>{
 const doc={parameters:{speed:0,moving:false,direction:[0,0,1]},states:[{id:'idle',transitions:[{to:'run',when:[]}]},{id:'run'}]};
 assert.deepEqual(animationScalarNames(doc),['speed','moving']);
 assert.equal(setAnimationScalar(doc,'direction',1),doc);
 assert.equal(setAnimationScalar(doc,'speed',Infinity),doc);
 const next=editAnimationCondition(doc,'idle',0,0,{parameter:'speed',op:'gt',value:0.5});
 assert.deepEqual(next.states[0].transitions[0].when,[{parameter:'speed',op:'gt',value:.5}]);
 assert.equal(editAnimationCondition(next,'idle',0,0,{parameter:'direction'}),next);
 assert.equal(editAnimationCondition(next,'idle',0,0,{op:'bad'}),next);
 assert.equal(editAnimationCondition(next,'idle',0,0,null).states[0].transitions[0].when.length,0);
 assert.deepEqual(doc.states[0].transitions[0].when,[]);
});

import {addGeometryModifier,removeGeometryModifier,modelMaterialIds,bindModelMaterial} from '../src/graphs/nativeGraphAdapters.ts';
import {terrainRecipeProjection,buildingRecipeProjection,caveSnapshotProjection} from '../src/graphs/generatorProjections.ts';
test('modifier presets and source material assignment retain native fields',()=>{
 const doc={nodes:[{id:'a',geometry:{kind:'box',size:[1,2,3]}}],materials:[{id:'steel'}]};
 const next=addGeometryModifier(doc,'a','mirror');
 assert.deepEqual(next.nodes[0].geometry.modifiers,[{kind:'mirror',axis:'x'}]);
 assert.equal(addGeometryModifier(doc,'a','constructor'),doc);
 assert.deepEqual(removeGeometryModifier(next,'a',0).nodes[0].geometry.modifiers,[]);
 assert.deepEqual(modelMaterialIds(doc),['steel']);
 assert.equal(bindModelMaterial(doc,'a','missing'),doc);
 assert.equal(bindModelMaterial(doc,'a','steel').nodes[0].material,'steel');
 assert.equal(doc.nodes[0].material,undefined);
});
test('owner recipe projections are bounded and never mutate source',()=>{
 const terrain={seed:7,octaves:5};
 assert.equal(terrainRecipeProjection(terrain,{enabled:true}).links.length,2);
 assert.equal(buildingRecipeProjection({style:'modern'}).nodes.length,5);
 const snapshot={version:1,settings:{seed:7},stamps:Array.from({length:100},(_,i)=>({id:i+1,mode:'carve',radiusCm:400})),entrances:[]};
 const view=caveSnapshotProjection(snapshot);
 assert.equal(view.nodes.length,66);assert.equal(snapshot.stamps.length,100);
 assert.throws(()=>caveSnapshotProjection({version:2}));
});

import {materialGraph,textureRecipeParams,textureChannelForOutput} from '../src/graphs/materialGraph.ts';
test('material projection preserves distinct texture channels and draft override',()=>{
 const source={texture:'content/textures/a.png',normalMap:'content/textures/n.png',base:'parent',shaderGraph:{version:1}};
 const view=materialGraph('wall',source,{texture:''});
 assert.equal(view.links.some(l=>l.input==='texture'),false);
 assert.equal(view.links.some(l=>l.input==='normalMap'),true);
 assert.equal(view.links.some(l=>l.input==='shader'),true);
 assert.equal(view.links.some(l=>l.input==='base'),true);
 assert.equal(source.texture,'content/textures/a.png');
});
test('texture recipe is bounded and requests native no-overwrite generation',()=>{
 const recipe={name:'wall',pattern:'brick',output:'normal',seed:7,size:256,scale:8};
 const params=textureRecipeParams(recipe,['brick']);
 assert.equal(params.path,'content/textures/wall.png');
 assert.equal(params.overwrite,false);
 assert.equal(textureChannelForOutput('normal'),'normalMap');
 assert.throws(()=>textureRecipeParams({...recipe,name:'../wall'},['brick']));
 assert.throws(()=>textureRecipeParams({...recipe,size:4096},['brick']));
 assert.throws(()=>textureRecipeParams({...recipe,pattern:'unknown'},['brick']));
});

import {parseStrataAnalysis,strataReportGraph} from '../src/graphs/strataAnalysis.ts';
import {inspectGraph} from '../src/graphs/graphDiagnostics.ts';
test('STRATA report retains native CPU metrics and rejects incomplete results',()=>{
 const result={ok:true,eligiblePrimitives:2,clusterNodes:16,meshlets:8,auxiliaryIndexBytes:2048,seamCertifiedPrimitives:2,seamRejectedParents:1,sharedBoundarySegments:3,sourceSeamsCertified:true,skipped:[{mesh:0,primitive:1,reason:'animated'}],note:'CPU only'};
 const report=parseStrataAnalysis(result);
 assert.equal(report.clusterNodes,16);assert.equal(report.auxiliaryIndexBytes,2048);
 assert.equal(strataReportGraph(report).nodes.length,6);
 assert.throws(()=>parseStrataAnalysis({...result,meshlets:-1}));
 assert.throws(()=>parseStrataAnalysis({ok:true}));
});
test('diagnostics honor separate execution and data cycle rules',()=>{
 const port=(id,type)=>({id,type});
 const nodes=['a','b'].map(id=>({id,label:id,x:0,y:0,inputs:[port('exec','exec'),port('data','data')],outputs:[port('exec','exec'),port('data','data')]}));
 const links=[{id:'1',from:'a',to:'b',output:'exec',input:'exec'},{id:'2',from:'b',to:'a',output:'data',input:'data'}];
 assert.equal(inspectGraph({nodes,links},{cycles:'reject',cycleGroups:['exec','data']}).issues.length,0);
 assert.equal(inspectGraph({nodes,links},{cycles:'reject'}).issues.some(i=>i.code==='cycle'),true);
 const loop=[...links,{id:'3',from:'b',to:'a',output:'exec',input:'exec'}];
 assert.equal(inspectGraph({nodes,links:loop},{cycles:'reject',cycleGroups:['exec','data']}).issues.some(i=>i.code==='cycle'&&i.subject==='exec'),true);
 assert.equal(inspectGraph({nodes,links:loop},{cycles:'allow'}).issues.some(i=>i.code==='cycle'),false);
});
