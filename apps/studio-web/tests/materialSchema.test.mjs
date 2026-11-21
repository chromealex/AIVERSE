import test from 'node:test';
import assert from 'node:assert/strict';
import {MATERIAL_PRESETS,MATERIAL_EDIT_KEYS,materialFields,materialNames,materialSwatchColor,editablePatch} from '../src/panels/materialSchema.ts';
test('PBR field metadata exactly matches native allowlist',()=>{
 assert.deepEqual(materialFields({}).map(f=>f.key),[...MATERIAL_EDIT_KEYS]);
});
test('draft values override material without mutating source',()=>{
 const source={metallic:.8,roughness:.15};
 const fields=materialFields(source,{roughness:.5});
 assert.equal(fields[0].value,.8);assert.equal(fields[1].value,.5);
 assert.deepEqual(source,{metallic:.8,roughness:.15});
});
test('invalid library entries are excluded',()=>{
 assert.deepEqual(materialNames({library:{gold:{metallic:1},bad:null,z:'string',wood:{}}}),['gold','wood']);
});
test('color swatch clamps invalid channels',()=>{
 assert.equal(materialSwatchColor({color:[1,0,.5]}),'rgb(255 0 128)');
 assert.equal(materialSwatchColor({color:[20,-1,NaN]}),'rgb(255 0 160)');
});
test('patch output strips unsupported fields',()=>{
 assert.deepEqual(editablePatch({roughness:.4,shaderGraph:'unsafe',texture:'0123456789abcdef0123456789abcdef'}),{roughness:.4,texture:'0123456789abcdef0123456789abcdef'});
});

test('creation preset registry stays within native whitelist',()=>{
 assert.deepEqual([...MATERIAL_PRESETS],['matte','metal','glass']);
});

test('receiver shadow diffusion defaults to zero and survives authoring patches',()=>{
 const field=materialFields({}).find(f=>f.key==='shadowDiffusion');
 assert.equal(field.value,0);assert.equal(field.min,0);assert.equal(field.max,1);
 assert.equal(materialFields({shadowDiffusion:.025}).find(f=>f.key==='shadowDiffusion').value,.025);
 assert.deepEqual(editablePatch({shadowDiffusion:.125}),{shadowDiffusion:.125});
});
test('subsurface controls default off and retain linear RGB',()=>{
 const fields=materialFields({sssColor:[1,.4,.2],sssStrength:.6});
 assert.equal(fields.find(f=>f.key==='sssStrength')?.value,.6);
 assert.equal(fields.find(f=>f.key==='sssColor')?.value,'1,0.4,0.2');
 assert.equal(materialFields({}).find(f=>f.key==='sssStrength')?.value,0);
 assert.equal(materialFields({}).find(f=>f.key==='sssThickness')?.value,0);
 assert.equal(materialFields({sssThickness:.06}).find(f=>f.key==='sssThickness')?.value,.06);
 assert.deepEqual(editablePatch({sssScale:.4,sssColor:'1,0.4,0.2'}),{sssScale:.4,sssColor:'1,0.4,0.2'});
});

test('texture reference slots share typed asset picker and have string defaults',()=>{
 const keys=['texture','normalMap','emissiveMap','teamMask','metallicRoughnessMap','occlusionMap'];
 const fields=materialFields({});
 for(const key of keys){
  const field=fields.find(f=>f.key===key);
  assert.equal(field?.type,'asset');
  assert.deepEqual(field?.assetKinds,['image']);
  assert.equal(field?.value,'');
 }
 const withPaths=materialFields({texture:'content/textures/stone.png'},{
  texture:'0123456789abcdef0123456789abcdef'
 });
 assert.equal(withPaths.find(f=>f.key==='texture')?.value,'0123456789abcdef0123456789abcdef');
});

test('thin transmission remains independent of the SSS draft',()=>{
 const fields=materialFields({thinTransmission:.35,sssScale:.8},{sssScale:.2});
 assert.equal(fields.find(f=>f.key==='thinTransmission')?.value,.35);
 assert.equal(fields.find(f=>f.key==='sssScale')?.value,.2);
 assert.equal(materialFields({}).find(f=>f.key==='thinTransmission')?.value,0);
 assert.deepEqual(editablePatch({thinTransmission:.35,sssScale:.8}),{thinTransmission:.35,sssScale:.8});
});
