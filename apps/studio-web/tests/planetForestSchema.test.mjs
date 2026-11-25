import test from 'node:test';
import assert from 'node:assert/strict';
import {patchRow,removeRow,nextId,collisionMode} from '../src/panels/planetForestSchema.ts';
test('forest draft changes preserve unknown fields and original records',()=>{
 const d={species:[{id:1,custom:'keep'},{id:3}],unknown:true};
 const changed=patchRow(d,'species',0,{id:2});
 assert.equal(changed.species[0].custom,'keep');
 assert.equal(d.species[0].id,1);
 assert.equal(changed.unknown,true);
 assert.equal(nextId(d.species,'id'),2);
 assert.equal(removeRow(changed,'species',1).species.length,1);
});
test('ARBOR and manual dimensions remain mutually exclusive',()=>{
 const x=collisionMode({id:1,naturalFallBody:{radiusCm:10,halfHeightCm:20,density:800}},'arbor');
 assert.deepEqual(x.naturalFallBody,{density:800,frictionPermille:500,fromArbor:true});
 assert.equal(collisionMode(x,'capsule').naturalFallBody.fromArbor,undefined);
 assert.equal(collisionMode(x,'none').naturalFallBody,undefined);
});
