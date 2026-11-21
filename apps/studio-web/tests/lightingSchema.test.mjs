import test from 'node:test';
import assert from 'node:assert/strict';
import {LIGHTING_KEYS, LIGHT_EDITABLE_KEYS, lightingProperties, lightProperties} from '../src/panels/lightingSchema.ts';
import {parseFieldDraft} from '../src/propertyLogic.ts';

test('lighting schema exposes exactly the native allowlisted keys',()=>{
  assert.deepEqual(lightingProperties({lighting:null,lights:[]}).map(x=>x.key),[...LIGHTING_KEYS]);
});
test('lighting defaults are safe, bounded numeric properties',()=>{
  const data=lightingProperties({lighting:null,lights:[]});
  assert.equal(data[0].value,.5);
  assert.equal(data[0].min,0);
  assert.equal(data[0].max,1);
  assert.equal(parseFieldDraft(data[0],'1.01').ok,false);
  assert.equal(parseFieldDraft(data[0],'0.75').ok,true);
});
test('project lighting values override defaults without mutating source',()=>{
  const source={lighting:{storm:.6},lights:[]};
  const result=lightingProperties(source);
  assert.equal(result.find(x=>x.key==='storm').value,.6);
  assert.equal(result.find(x=>x.key==='clouds').value,0);
  assert.deepEqual(source,{lighting:{storm:.6},lights:[]});
});
test('light properties match the native API key allowlist',()=>{
  assert.deepEqual(lightProperties({}).map(x=>x.key),[...LIGHT_EDITABLE_KEYS]);
});
test('enum options reject unsupported modes',()=>{
  const fields=lightProperties({type:'spot',bake:'mixed'});
  const type=fields.find(f=>f.key==='type');
  assert.deepEqual(parseFieldDraft(type,'point'),{ok:true,value:'point'});
  assert.equal(parseFieldDraft(type,'area').ok,false);
  const bake=fields.find(f=>f.key==='bake');
  assert.deepEqual(parseFieldDraft(bake,'baked'),{ok:true,value:'baked'});
  assert.equal(parseFieldDraft(bake,'unknown').ok,false);
});
test('numeric fields reject invalid values',()=>{
  const fields=lightProperties({intensity:2});
  const intensity=fields.find(f=>f.key==='intensity');
  assert.equal(parseFieldDraft(intensity,'-1').ok,false);
  assert.deepEqual(parseFieldDraft(intensity,'0'),{ok:true,value:0});
  const radius=fields.find(f=>f.key==='radius');
  assert.equal(parseFieldDraft(radius,'0').ok,false);
});
