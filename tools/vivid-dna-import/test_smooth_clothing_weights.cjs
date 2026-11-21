const assert=require('node:assert/strict'),{smoothWeights}=require('./smooth_clothing_weights.cjs');
const p=new Float32Array([0,0,0,1,0,0,1,1,0,0,1,0]),ix=new Uint32Array([0,1,2,0,2,3]),j=new Uint16Array([0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0]),w=new Float32Array([1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0]);const before=Array.from(w),out=smoothWeights(p,ix,j,w);
assert.deepEqual(Array.from(w),before);for(let i=0;i<4;i++){assert.ok(Math.abs(out.weights.slice(i*4,i*4+4).reduce((a,b)=>a+b,0)-1)<1e-6);assert.ok(out.weights[i*4]<1);}
const rigid=smoothWeights(p,ix,new Uint16Array(16),w);for(let i=0;i<4;i++)assert.equal(rigid.weights[i*4],1);
assert.throws(()=>smoothWeights(p,new Uint32Array([0,1,99]),j,w),/triangle/);assert.throws(()=>smoothWeights(p,ix,j,w,0),/parameters/);
console.log('PASS normalized four-influence smoothing, unchanged source, rigid invariance, invalid input rejection');
