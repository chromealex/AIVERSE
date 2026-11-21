const assert=require('node:assert/strict'),{createRequire}=require('node:module'),path=require('node:path');
const sharp=createRequire(path.resolve(process.argv[2],'package.json'))('sharp');
const {filterNeckAlbedo}=require('./filter_neck_albedo.cjs');
(async()=>{const src=Buffer.alloc(128*128*4);for(let y=0;y<128;y++)for(let x=0;x<128;x++){let k=(y*128+x)*4;src[k]=src[k+1]=src[k+2]=(x%4<2?60:180);src[k+3]=y;}
const png=await sharp(src,{raw:{width:128,height:128,channels:4}}).png().toBuffer();
const out=await sharp(await filterNeckAlbedo(png,sharp)).raw().toBuffer();
assert.deepEqual(out.subarray(0,94*128*4),src.subarray(0,94*128*4));
for(let i=3;i<out.length;i+=4)assert.equal(out[i],src[i]);
const row=125*128*4;assert.ok(Math.abs(out[row+40*4]-out[row+42*4])<60);
await assert.rejects(()=>filterNeckAlbedo(png,sharp,{start:.9,end:.2}),/Invalid/);
console.log('PASS: face pixels/alpha preserved, neck variation reduced, invalid region rejected');})();
