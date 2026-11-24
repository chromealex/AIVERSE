import test from 'node:test';
import assert from 'node:assert/strict';
import { historyStepParams } from '../src/panels/historyLogic.ts';

const base={scope:'project',steps:[{label:'Edit',done:true}],position:1,version:'/project|project|5',strokeActive:false,canUndo:true,canRedo:false,bytes:128,maxBytes:16777216,maxSteps:64};
test('project navigation uses only owner-authorized direction and exact version',()=>{
  assert.deepEqual(historyStepParams(base,'undo'),{direction:'undo',expectedVersion:base.version});
  assert.equal(historyStepParams(base,'redo'),null);
  assert.deepEqual(historyStepParams({...base,canUndo:false,canRedo:true,position:0},'redo'),{direction:'redo',expectedVersion:base.version});
});
test('missing, map, blocked, stale-unversioned and active stroke states cannot navigate project history',()=>{
  for(const state of [null,{...base,scope:'map'},{...base,version:''},{...base,strokeActive:true},{...base,blockedReason:'Reopen'}])
    assert.equal(historyStepParams(state,'undo'),null);
});
test('positions do not grant permissions when native owner refuses both directions',()=>{
  const state={...base,position:20,canUndo:false,canRedo:false};
  assert.equal(historyStepParams(state,'undo'),null);
  assert.equal(historyStepParams(state,'redo'),null);
});
