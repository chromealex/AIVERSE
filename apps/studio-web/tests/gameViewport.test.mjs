import test from 'node:test';
import assert from 'node:assert/strict';
import {gameViewportSize} from '../src/panels/gameViewport.ts';
test('device screen fits landscape and portrait while preserving pointer scale',()=>{
  const portrait=gameViewportSize(800,600,375,812);
  assert.equal(portrait.height,600);
  assert.ok(Math.abs(portrait.width/portrait.height-375/812)<1e-10);
  const landscape=gameViewportSize(800,600,812,375);
  assert.equal(landscape.width,800);
  assert.ok(Math.abs(landscape.width/812-landscape.height/375)<1e-10);
});
test('free-size screen fills panel and hidden/invalid areas never produce a drawable',()=>{
  assert.deepEqual(gameViewportSize(800,600,0,0),{width:800,height:600});
  assert.deepEqual(gameViewportSize(0,600,375,812),{width:0,height:0});
  assert.deepEqual(gameViewportSize(NaN,600,375,812),{width:0,height:0});
});
