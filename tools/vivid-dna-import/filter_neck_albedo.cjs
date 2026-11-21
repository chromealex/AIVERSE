// Opt-in cleanup for a known MetaHuman face atlas (neck in lower V range).
// This filters source color variation, not geometry, normals or rendered frames.
const {linear,srgb}=require('./bake_materials.cjs');
async function filterNeckAlbedo(image,sharp,{start=.75,end=.80,strength=1}={}) {
 if(!([start,end,strength].every(Number.isFinite)&&start>=0&&end>start&&end<=1&&strength>=0&&strength<=1))throw Error('Invalid neck filter');
 const {data,info}=await sharp(image,{limitInputPixels:67108864}).ensureAlpha().raw().toBuffer({resolveWithObject:true});
 const blurred=await sharp(data,{raw:{width:info.width,height:info.height,channels:4}})
  .blur(Math.max(.3,info.width/16)).raw().toBuffer();
 const out=Buffer.from(data);
 for(let y=0;y<info.height;y++) {
  const t=Math.max(0,Math.min(1,((y+.5)/info.height-start)/(end-start)));
  const weight=strength*t*t*(3-2*t);
  if(!weight)continue;
  for(let x=0;x<info.width;x++)for(let c=0;c<3;c++) {
   const k=(y*info.width+x)*4+c,a=linear(data[k]/255),b=linear(blurred[k]/255);
   out[k]=Math.round(srgb(a+(b-a)*weight)*255);
  }
 }
 return sharp(out,{raw:{width:info.width,height:info.height,channels:4}}).png().toBuffer();
}
module.exports={filterNeckAlbedo};
