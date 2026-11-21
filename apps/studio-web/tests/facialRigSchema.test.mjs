import test from 'node:test';
import assert from 'node:assert/strict';
import {
  ARKIT52, arkitFaceMapping, newFacialRig, validateFacialRigDraft, validRigFileName
} from '../src/panels/facialRigSchema.ts';
import { mockInvoke } from '../src/mock.ts';

test('ARKit template produces 52 explicit named mappings with no assumed geometry',()=>{
  assert.equal(ARKIT52.length,52);
  assert.equal(new Set(ARKIT52).size,52);
  const doc=arkitFaceMapping('Face.Mesh');
  assert.equal(doc.morphs.length,52);
  assert.equal(doc.morphs.find(m=>m.parameter==='jawOpen')?.target,'jawOpen');
  assert.equal(doc.morphs[0].node,'Face.Mesh');
  assert.deepEqual(validateFacialRigDraft(doc),[]);
});
test('authoring fails closed on duplicated morph destinations and bad parameters',()=>{
  const doc=arkitFaceMapping();
  doc.morphs[1].target=doc.morphs[0].target;
  assert.match(validateFacialRigDraft(doc).join(' '),/duplicate/);
  doc.morphs[1].target='browDownRight';
  doc.morphs[0].halfLife=Number.NaN;
  assert.match(validateFacialRigDraft(doc).join(' '),/numeric/);
  assert.ok(validateFacialRigDraft(newFacialRig()).length>0);
});
test('native file names reject traversal and file extensions',()=>{
  assert.equal(validRigFileName('hero_face-01'),true);
  for(const invalid of ['../hero','sub/hero','', 'hero.json',' spaced ','a'.repeat(65)])
    assert.equal(validRigFileName(invalid),false);
});
test('browser demo cannot claim facial model preview/real asset writes',async()=>{
  assert.deepEqual(await mockInvoke('studio.facialRigList',{}),{rigs:[]});
  await assert.rejects(mockInvoke('studio.facialRigGet',{name:'hero'}),/native AIVERSE host/);
  await assert.rejects(mockInvoke('studio.facialRigSave',{
    name:'hero',rig:arkitFaceMapping()
  }),/native AIVERSE host/);
  await assert.rejects(mockInvoke('studio.facialRigValidate',{
    rig:arkitFaceMapping()
  }),/native validation/);
});
