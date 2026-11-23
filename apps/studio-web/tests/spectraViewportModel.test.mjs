import test from 'node:test';
import assert from 'node:assert/strict';
import {fxViewportRect} from '../src/panels/spectraViewportModel.ts';

const box=(left,top,right,bottom)=>({left,top,right,bottom});
test('FX viewport stays inside the scroll pane and respects device scale',()=>{
  const p=fxViewportRect(box(100,200,900,600),box(0,100,1000,700),2,true);
  assert.deepEqual(p,{x:100,y:200,width:800,height:400,dpr:2,visible:true});
});
test('GPU surface is hidden when scrolled outside rather than painting over controls',()=>{
  const p=fxViewportRect(box(100,200,900,600),box(0,300,1000,440),2,true);
  assert.equal(p.visible,false);
  assert.equal(p.height,140);
  assert.equal(p.width,800);
  const removed=fxViewportRect(box(100,200,900,600),box(0,750,1000,900),2,true);
  assert.equal(removed.visible,false);
  assert.equal(removed.height,0);
});
test('viewport requires visible app and clips narrow/small surfaces',()=>{
  const a=fxViewportRect(box(0,0,500,300),box(0,0,500,300),1,false);
  assert.equal(a.visible,false);
  const b=fxViewportRect(box(0,0,90,70),box(0,0,90,70),1,true);
  assert.equal(b.visible,false);
});
test('invalid dimensions are rejected safely before native GPU placement',()=>{
  const a=fxViewportRect(box(NaN,0,400,200),box(0,0,800,600),3,true);
  assert.deepEqual(a,{x:0,y:0,width:0,height:0,dpr:1,visible:false});
  const b=fxViewportRect(box(0,0,600,300),box(0,0,800,600),9,true);
  assert.equal(b.dpr,4);
  const c=fxViewportRect(box(0,0,600,300),box(0,0,800,600),0.5,true);
  assert.equal(c.dpr,1);
});
