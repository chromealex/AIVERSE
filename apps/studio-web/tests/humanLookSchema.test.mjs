import test from 'node:test';
import assert from 'node:assert/strict';
import {validateHumanLook} from '../src/panels/humanLookSchema.ts';
const schema=[{key:'skin.roughness',label:'Roughness',min:.15,max:1,initial:.62}];
const doc={version:1,values:{'skin.roughness':.6},materials:{Face:'skin'}};
test('appearance accepts bounded source mappings and optional defaults',()=>{
 assert.ok(validateHumanLook(doc,schema));
 assert.ok(validateHumanLook({...doc,values:{}},schema));
});
test('appearance import rejects values which would corrupt the editor',()=>{
 for(const value of [NaN,Infinity,-1,2,'0.6',null])assert.equal(validateHumanLook({...doc,values:{'skin.roughness':value}},schema),false);
 for(const value of [null,[],{...doc,version:2},{...doc,values:[]},{...doc,materials:{Face:'unknown'}},{...doc,values:{unknown:1}}])assert.equal(validateHumanLook(value,schema),false);
});
