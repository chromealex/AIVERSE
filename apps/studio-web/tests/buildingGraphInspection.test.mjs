import {test} from 'node:test';
import assert from 'node:assert/strict';
import {projectBuildingInspection} from '../src/graphs/buildingGraphInspection.ts';
import {validateTopology} from '../src/graphs/graphFramework.ts';
const recipe={mode:'features',seed:7,style:'modern',floors:2,roomsX:2,roomsZ:1,features:[{kind:'personnel-door'},{kind:'window'}],materialBindings:{wall:'concrete',roof:'steel'}};
const inspection={seed:7,triangles:120,vertices:200,solids:42,connections:8,floors:2,roomsX:2,roomsZ:1,sockets:[{socketId:'entrance/0/0',kind:'personnel-door',side:'front',featureIndex:0,width:1.2,height:2.35,open:.5,outside:[3,0,-.4]},{socketId:'window/1/0',kind:'window',side:'back',featureIndex:1,width:1.6,height:1.1,open:0,outside:[4,4,18]}]};
test('native socket identity, feature provenance and counts; no fabricated room links',()=>{
 const r=projectBuildingInspection(recipe,inspection);
 assert.deepEqual(validateTopology(r.view,{cycles:'reject'}),[]);
 assert.equal(r.sockets[0].open,.5);assert.deepEqual(r.sockets[0].outside,[3,0,-.4]);
 assert.equal(r.connectionCount,8);assert.equal(r.solids,42);assert.equal(r.floors,2);
 assert.ok(r.view.links.some(l=>l.from==='feature:0'&&l.to==='socket:entrance/0/0'));
 assert.equal(r.view.nodes.filter(n=>n.id.startsWith('socket:')).length,2);
 assert.ok(!r.view.nodes.some(n=>n.id.startsWith('room:')));
 assert.ok(r.view.nodes.some(n=>n.id==='bindings'&&n.label.includes('not resolved')));
});
test('reject stale or malformed native inspection',()=>{
 assert.throws(()=>projectBuildingInspection(recipe,{...inspection,seed:8}),/seed/);
 assert.throws(()=>projectBuildingInspection(recipe,{...inspection,roomsX:1}),/grid/);
 assert.throws(()=>projectBuildingInspection(recipe,{...inspection,sockets:[inspection.sockets[0],inspection.sockets[0]]}),/Duplicate/);
 for(const patch of [{featureIndex:3},{kind:'window'},{open:1.2},{outside:[0,Infinity,0]},{outside:[0,0]}]){
  assert.throws(()=>projectBuildingInspection(recipe,{...inspection,sockets:[{...inspection.sockets[0],...patch}]}));
 }
 assert.throws(()=>projectBuildingInspection(recipe,{...inspection,sockets:new Array(257).fill(inspection.sockets[0])}),/sockets/);
});
test('preset does not invent structure data',()=>{
 const r=projectBuildingInspection({mode:'preset',seed:7,style:'modern'},{seed:7,triangles:12,vertices:24});
 assert.equal(r.solids,null);assert.equal(r.connectionCount,null);assert.deepEqual(r.sockets,[]);
 assert.deepEqual(validateTopology(r.view,{cycles:'reject'}),[]);
 assert.throws(()=>projectBuildingInspection({mode:'preset',seed:7,style:'modern'},inspection),/Preset/);
});
test('does not mutate native inputs; stable slot ordering',()=>{
 const before=JSON.stringify([recipe,inspection]);
 const a=projectBuildingInspection(recipe,inspection),b=projectBuildingInspection({...recipe,materialBindings:{roof:'steel',wall:'concrete'}},inspection);
 assert.deepEqual(a,b);assert.equal(JSON.stringify([recipe,inspection]),before);
});
