import test from 'node:test';
import assert from 'node:assert/strict';
import {BuildingCandidateGate,buildingDraftKey} from '../src/graphs/buildingCandidateCapture.ts';
const request=(seed=7)=>({seed,style:'modern',features:[{kind:'window',width:2}],materialBindings:{wall:'stone'},unused:undefined});
test('capture clones exact JSON wire recipe before native evaluation',()=>{
 const source=request(),gate=new BuildingCandidateGate();
 const ticket=gate.begin(buildingDraftKey('/p',source),[source]);
 source.features[0].width=9;source.materialBindings.wall='glass';
 assert.deepEqual(ticket.requests[0],{seed:7,style:'modern',features:[{kind:'window',width:2}],materialBindings:{wall:'stone'}});
 assert.ok(Object.isFrozen(ticket.requests[0].features[0]));
 assert.throws(()=>{ticket.requests[0].features[0].width=3;},TypeError);
});
test('new batches and invalidation retire late results, even identical seeds',()=>{
 const gate=new BuildingCandidateGate(),a=gate.begin('same',[request()]);
 const b=gate.begin('same',[request()]);
 assert.notEqual(a.id,b.id);assert.equal(gate.isCurrent(a),false);
 assert.equal(gate.finish(a),false);assert.equal(gate.isCurrent(b),true);
 assert.equal(gate.invalidate(),true);assert.equal(gate.isCurrent(b),false);
 assert.equal(gate.invalidate(),false);
 const c=gate.begin('next',[request()]);assert.equal(gate.finish(c),true);assert.equal(gate.isCurrent(c),false);
});
test('draft identity changes on project, recipe and material changes',()=>{
 const r=request(),key=buildingDraftKey('/p',r);
 assert.notEqual(key,buildingDraftKey('/other',r));
 assert.notEqual(key,buildingDraftKey('/p',{...r,seed:8}));
 r.features[0].width=3;assert.notEqual(key,buildingDraftKey('/p',r));
 const other=request();other.materialBindings.wall='glass';assert.notEqual(key,buildingDraftKey('/p',other));
});
test('bounded three-seed batch rejects duplicate and unsafe seeds',()=>{
 const gate=new BuildingCandidateGate();
 assert.deepEqual(gate.begin('scope',[request(2147483645),request(2147483646),request(2147483647)]).requests.map(r=>r.seed),[2147483645,2147483646,2147483647]);
 for(const values of [[],[request(7),request(7)],[request(-1)],[request(2147483648)],[request(1.2)],Array.from({length:4},(_,i)=>request(i))])assert.throws(()=>gate.begin('scope',values));
});
