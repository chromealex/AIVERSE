import test from 'node:test';
import assert from 'node:assert/strict';
import {validThumbnail,evictThumbnailIds} from '../src/panels/spectraThumbnailCache.ts';
test('validates bounded native JPEG records',()=>{
  const row={id:'project/graph',signature:'{}',image:'data:image/jpeg;base64,abc',updated:1};
  assert.equal(validThumbnail(row),true);
  assert.equal(validThumbnail({...row,image:'data:image/png;base64,abc'}),false);
  assert.equal(validThumbnail({...row,image:'data:image/jpeg;base64,'+'x'.repeat(900000)}),false);
  assert.equal(validThumbnail({...row,signature:''}),false);
});
test('eviction is stable and bounded',()=>{
  const rows=[{id:'a',updated:1},{id:'b',updated:2},{id:'c',updated:2},{id:'d',updated:3}];
  assert.deepEqual(evictThumbnailIds(rows,2),['c','a']);
  assert.equal(evictThumbnailIds(rows,64).length,0);
});
