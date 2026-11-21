import test from 'node:test';
import assert from 'node:assert/strict';
import {parseFieldDraft, validateBooleanChange, isSameFieldValue} from '../src/propertyLogic.ts';
const number = {key:'x',label:'X',type:'number',value:10,min:-100,max:100,step:0.1};
test('empty numeric draft is never zero',()=>assert.deepEqual(parseFieldDraft(number, '  '), {ok:false,error:'Enter a number'}));
test('invalid numeric value is rejected',()=>{
  assert.equal(parseFieldDraft(number,'Infinity').ok,false);
  assert.equal(parseFieldDraft(number,'NaN').ok,false);
  assert.equal(parseFieldDraft(number,'2o').ok,false);
});
test('bounds are enforced, without implicit rounding',()=>{
  assert.equal(parseFieldDraft(number,'-101').ok,false);
  assert.equal(parseFieldDraft(number,'101').ok,false);
  assert.deepEqual(parseFieldDraft(number,'-10.25'),{ok:true,value:-10.25});
});
test('integer semantics preserve safe values',()=>{
  const f={...number,integer:true};
  assert.equal(parseFieldDraft(f,'1.3').ok,false);
  assert.equal(parseFieldDraft(f,'9007199254740992').ok,false);
  assert.deepEqual(parseFieldDraft(f,'-24'),{ok:true,value:-24});
});
test('readonly fields do not generate writes',()=>{
  assert.equal(parseFieldDraft({...number,editable:false},'25').ok,false);
  assert.equal(validateBooleanChange({key:'enabled',label:'Enabled',type:'boolean',value:false,editable:false},true).ok,false);
});
test('boolean control accepts only bool fields',()=>{
  assert.equal(validateBooleanChange(number,true).ok,false);
  assert.deepEqual(validateBooleanChange({key:'enabled',label:'Enabled',type:'boolean',value:false},true),{ok:true,value:true});
});
test('numeric and textual options match values and types',()=>{
  const f={...number,options:[{label:'one',value:1},{label:'two',value:2}]};
  assert.equal(parseFieldDraft(f,'3').ok,false);
  assert.deepEqual(parseFieldDraft(f,'2'),{ok:true,value:2});
  const str={key:'mode',label:'Mode',type:'text',value:'day',options:[{label:'Day',value:'day'},{label:'Night',value:'night'}]};
  assert.deepEqual(parseFieldDraft(str,'night'),{ok:true,value:'night'});
  assert.equal(parseFieldDraft(str,'sunrise').ok,false);
});
test('same-value check avoids redundant undo snapshots',()=>{
  assert.equal(isSameFieldValue(10,10),true);
  assert.equal(isSameFieldValue(10,'10'),false);
  assert.equal(isSameFieldValue(false,false),true);
});

import { scrubValue } from '../src/propertyLogic.ts';
test('drag-to-scrub moves by step per pixel, fine, snap and clamps', () => {
  assert.equal(scrubValue(1, 10), 2);
  assert.equal(scrubValue(1, 10, { fine: true }), 1.1);
  assert.equal(scrubValue(0, 37, { step: 1 }), 37);
  assert.equal(scrubValue(0, 37, { step: 1, snap: 15 }), 30);
  assert.equal(scrubValue(1, -50, { min: 0.001 }), 0.001);
  assert.equal(scrubValue(5, 3.4, { integer: true }), 8);
  assert.equal(scrubValue(0.2, 1), 0.3);
});
