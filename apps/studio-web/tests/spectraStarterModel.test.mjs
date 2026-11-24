import test from 'node:test';
import assert from 'node:assert/strict';
import {
  createBlankGraph, createBurstStarter, nextUnusedGraphName
} from '../src/panels/spectraStarterModel.ts';

// Mirrors the native spectra::authoring::newGraphV2 factory; the browser
// does not own or regenerate that schema. This is a contract-shaped fixture.
const nativeStarter=()=>({
  version:2,
  parameters:{},
  nodes:[
    {id:'spawn',type:'Spawn',properties:{burst:16},position:[20,120]},
    {id:'init',type:'Initialize',properties:{life:1,speed:1,size:0.4},position:[250,120]},
    {id:'out',type:'SpriteOutput',properties:{texture:'builtin:soft'},position:[480,120]}
  ],
  links:[{from:'spawn',to:'init'},{from:'init',to:'out'}],
  outputs:['out'],
  metadata:{keep:'native'}
});
test('first use accepts the playable native v2 factory without client-side graph rewriting',()=>{
  const original=nativeStarter();
  const result=createBurstStarter(original);
  assert.equal(result,original);
  assert.deepEqual(result.nodes.map(n=>n.type),['Spawn','Initialize','SpriteOutput']);
  assert.equal(result.nodes[2].properties.texture,'builtin:soft');
  assert.equal(result.metadata,original.metadata);
});
test('blank variant clears authored stages but preserves native schema, metadata and original',()=>{
  const original=nativeStarter();
  const value=createBlankGraph(original);
  assert.notEqual(value,original);
  assert.deepEqual(value.nodes,[]);
  assert.deepEqual(value.links,[]);
  assert.deepEqual(value.outputs,[]);
  assert.deepEqual(value.parameters,{});
  assert.equal(value.metadata,original.metadata);
  assert.equal(original.nodes.length,3);
  assert.equal(original.links.length,2);
  assert.equal(original.outputs.length,1);
});
test('native factory not playable must not be advertised as a tested starter',()=>{
  const base=createBlankGraph(nativeStarter());
  assert.equal(createBurstStarter(base),null);
  assert.equal(createBurstStarter({...base,version:1}),null);
});
test('new VFX names are safe, unique and bounded even after many drafts',()=>{
  assert.equal(nextUnusedGraphName([]),'preview_fx');
  const existing=[{name:'preview_fx'},{name:'preview_fx_2'},{name:'other'}];
  assert.equal(nextUnusedGraphName(existing),'preview_fx_3');
  assert.equal(nextUnusedGraphName(existing,'invalid/name'),'preview_fx_3');
  assert.equal(nextUnusedGraphName([{name:'fx'}]),'preview_fx');
  const max='p'.repeat(48);
  assert.equal(nextUnusedGraphName([{name:max}],max),max.slice(0,46)+'_2');
  assert.equal(existing[0].name,'preview_fx');
});
