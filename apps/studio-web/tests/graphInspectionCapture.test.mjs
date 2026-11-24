import test from 'node:test';
import assert from 'node:assert/strict';
import {GraphInspectionGate,graphScopeKey,parseGraphDocumentScope,validateCaveCapture} from '../src/graphs/graphInspectionCapture.ts';
const scope={projectPath:'/p',mapPath:'/p/maps/a.json',epoch:'2',revision:'18446744073709551615'};
test('scope uses exact uint64 decimal strings, including max revision',()=>{
 assert.deepEqual(parseGraphDocumentScope(scope),scope);
 assert.notEqual(graphScopeKey(scope),graphScopeKey({...scope,revision:'18446744073709551614'}));
 assert.equal(parseGraphDocumentScope({...scope,mapPath:'',revision:'0'}).mapPath,'');
 for(const bad of [{...scope,epoch:'0'},{...scope,revision:9},{...scope,revision:'18446744073709551616'},
  {...scope,revision:'01'},{...scope,projectPath:''},{...scope,mapPath:null}])assert.throws(()=>parseGraphDocumentScope(bad));
});
test('latest request wins; finish of a retired request cannot retire its replacement',()=>{
 const gate=new GraphInspectionGate(),source={...scope};
 const a=gate.begin(source);source.mapPath='changed';assert.equal(a.scope.mapPath,scope.mapPath);
 const b=gate.begin(scope);assert.equal(gate.isCurrent(a),false);
 gate.finish(a);assert.equal(gate.isCurrent(b),true);
 gate.invalidate();assert.equal(gate.isCurrent(b),false);
 const c=gate.begin(scope);gate.finish(c);assert.equal(gate.isCurrent(c),false);
});
test('response must match captured project, map, lifetime and revision exactly',()=>{
 const report={ok:true};
 const ready={scope,status:'ready',report};
 assert.deepEqual(validateCaveCapture(scope,ready),ready);
 for(const patch of [{projectPath:'/other'},{mapPath:'/p/maps/b.json'},{epoch:'3'},{revision:'4'}])
  assert.throws(()=>validateCaveCapture(scope,{...ready,scope:{...scope,...patch}}));
 assert.throws(()=>validateCaveCapture(scope,{scope,status:'ready'}));
});
test('unavailable, unsupported and absent never synthesize a report',()=>{
 for(const status of ['unavailable','unsupported','absent']){
  assert.deepEqual(validateCaveCapture(scope,{scope,status}),{scope,status});
  assert.throws(()=>validateCaveCapture(scope,{scope,status,report:{}}));
 }
 assert.throws(()=>validateCaveCapture(scope,{scope,status:'unknown'}));
});
