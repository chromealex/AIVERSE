// Offline material-channel conversion: source UE masks -> standard glTF PBR.
// No generated character art. Original skin/normal images remain unchanged.
const fs=require('node:fs');
const path=require('node:path');
const crypto=require('node:crypto');
const clamp=x=>Math.max(0,Math.min(1,x));
const linear=x=>x<=.04045?x/12.92:Math.pow((x+.055)/1.055,2.4);
const srgb=x=>x<=.0031308?x*12.92:1.055*Math.pow(x,1/2.4)-.055;
const mix=(a,b,t)=>a+(b-a)*t;
const smooth=(a,b,x)=>{const t=clamp((x-a)/(b-a));return t*t*(3-2*t);};
function sample(map,u,v,c=0,decode=false,repeat=true) {
  if(!map)return 1;
  u=repeat?u-Math.floor(u):clamp(u);v=repeat?v-Math.floor(v):clamp(v);
  const x=Math.min(map.width-1,Math.floor(u*map.width));
  const y=Math.min(map.height-1,Math.floor(v*map.height));
  const n=map.data[(y*map.width+x)*4+c]/255;
  return decode?linear(n):n;
}
async function bakeMaterials(doc,mapping,directory,sharp) {
 const report=[];
 const hairMaterials=new Set();
 const cache=new Map();
 async function read(relative) {
  if(!relative)return null;
  const file=path.resolve(directory,relative);
  if(!file.startsWith(path.resolve(directory)+path.sep))throw Error('Unsafe texture path');
  if(!cache.has(file)) {
   const raw=await sharp(file,{limitInputPixels:67108864}).ensureAlpha().raw().toBuffer({resolveWithObject:true});
   cache.set(file,{data:raw.data,width:raw.info.width,height:raw.info.height});
  }
  return cache.get(file);
 }
 async function texture(name,pixels,w,h) {
  const png=await sharp(pixels,{raw:{width:w,height:h,channels:4}}).png().toBuffer();
  return doc.createTexture(name).setImage(png).setMimeType('image/png');
 }
 for(const material of doc.getRoot().listMaterials()) {
  const spec=mapping.materials.find(s=>s.material_name===material.getName());
  if(!spec)continue;
  const p=spec.params,t=spec.textures||{};
  // Companion skin/teeth maps were outside the GLB: retain the source pixels.
  if(spec.kind==='skin' || spec.face_slot==='teeth') {
   const bindings = spec.kind==='skin'?['ao']:['basecolor','normal','mouth_occlusion'];
   for(const key of bindings) {
    if(!t[key])continue;
    const raw=await read(t[key]);
    const tex=await texture(material.getName()+'_'+key,raw.data,raw.width,raw.height);
    if(key==='basecolor')material.setBaseColorTexture(tex).setBaseColorFactor([1,1,1,1]);
    else if(key==='normal')material.setNormalTexture(tex);
    else material.setOcclusionTexture(tex).setOcclusionStrength(1);
    report.push({material:material.getName(),mode:'source '+key,path:t[key],width:raw.width,height:raw.height});
   }
  }
  if(spec.face_slot==='head_skin' && p.neck_albedo_smoothing!==undefined) {
   const source=material.getBaseColorTexture();
   if(!source)throw Error('Neck color filter requires a face atlas');
   const image=await require('./filter_neck_albedo.cjs').filterNeckAlbedo(source.getImage(),sharp,{strength:p.neck_albedo_smoothing});
   material.setBaseColorTexture(doc.createTexture(material.getName()+'_neck_evenness').setMimeType('image/png').setImage(image));
   report.push({material:material.getName(),mode:'opt-in lower-atlas neck color smoothing',strength:p.neck_albedo_smoothing});
  }
  // UE occlusion/lacrimal shaders are not reproduced by flat opaque colors.
  // Retain the geometry with a restrained portable translucent fallback.
  if(spec.face_slot==='eye_occlusion'||spec.face_slot==='wet') {
   const occlusion=spec.face_slot==='eye_occlusion';
   material.setBaseColorTexture(null).setMetallicRoughnessTexture(null)
    .setBaseColorFactor(occlusion?[.02,.015,.01,.12]:[.9,.88,.85,.04])
    .setAlphaMode('BLEND').setDoubleSided(true).setMetallicFactor(0)
    .setRoughnessFactor(occlusion?.8:.12);
   if(occlusion) {
    // This source shell follows the lids. Fade contact occlusion into the
    // opening instead of uniformly tinting the whole eyeball like dark glass.
    const size=256,pixels=Buffer.alloc(size*size*4);
    for(let y=0;y<size;y++)for(let x=0;x<size;x++) {
     const u=(x+.5)/size,v=(y+.5)/size,k=(y*size+x)*4;
     const upper=(1-smooth(.05,.62,v))*.48;
     const lower=smooth(.78,1,v)*.10;
     const corner=(1-smooth(0,.22,Math.min(u,1-u)))*.18;
     pixels[k]=pixels[k+1]=pixels[k+2]=255;pixels[k+3]=Math.round(clamp(upper+lower+corner)*255);
    }
    material.setBaseColorFactor([.015,.009,.006,1])
     .setBaseColorTexture(await texture(material.getName()+'_lid_occlusion',pixels,size,size));
   }
   report.push({material:material.getName(),mode:'portable eye overlay; original UE procedural shader unavailable'});
  }
  if(spec.face_slot==='cartilage') {
   // The exported lid strip has no UE cartilage shader. A pale glossy PBR
   // substitute produced bright triangle-sized flecks around both eyelids.
   material.setBaseColorTexture(null).setMetallicRoughnessTexture(null)
    .setBaseColorFactor([.12,.035,.028,1]).setMetallicFactor(0).setRoughnessFactor(.8)
    .setAlphaMode('OPAQUE');
   report.push({material:material.getName(),mode:'pigmented matte lid cartilage; geometry and morphs retained'});
  }
  let output=null,mode='',w=0,h=0;
  if(spec.kind==='cloth') {
   const mask=await read(t.mask),macro=await read(t.macro_overlay),ao=await read(t.ao);
   let detail=await read(t.detail_diffuse);
   if(!mask)throw Error('Cloth requires source mask');
   w=mask.width;h=mask.height;output=Buffer.alloc(w*h*4);
   // A highly tiled textile map needs a mip-like prefilter before baking.
   // Sampling its full-resolution pixels at 220 repeats aliases into large checks.
   if(detail) {
    const dw=Math.max(1,Math.min(detail.width,Math.floor(w/Math.max(1,p.detail_tiling))));
    const dh=Math.max(1,Math.min(detail.height,Math.floor(h/Math.max(1,p.detail_tiling))));
    const filtered=await sharp(detail.data,{raw:{width:detail.width,height:detail.height,channels:4}})
      .gamma().resize(dw,dh,{kernel:'lanczos3'}).raw().toBuffer();
    detail={data:filtered,width:dw,height:dh};
   }
   const channel={r:0,g:1,b:2,a:3}[p.mask_channel||'r'];
   if(channel===undefined)throw Error('Unknown mask channel');
   for(let y=0;y<h;y++)for(let x=0;x<w;x++) {
    const u=(x+.5)/w,v=(y+.5)/h,k=(y*w+x)*4;
    const factor=sample(mask,u,v,channel);
    for(let c=0;c<3;c++) {
     let value=mix(p.diffuse_color_1[c],p.diffuse_color_2[c],factor);
     if(detail)value*=mix(1,2*sample(detail,u*p.detail_tiling,v*p.detail_tiling,c,true),p.detail_strength);
     if(macro) {
      const m=sample(macro,u*p.macro_tiling,v*p.macro_tiling,c,true);
      const overlay=value<=.5?2*value*m:1-2*(1-value)*(1-m);
      value=mix(value,overlay,p.macro_strength);
     }
     if(ao)value*=mix(1,sample(ao,u,v),p.ao_amount);
     output[k+c]=Math.round(clamp(srgb(clamp(value)))*255);
    }
    output[k+3]=255;
   }
   material.setBaseColorFactor([1,1,1,1]).setMetallicFactor(p.metallic||0)
    .setRoughnessFactor(p.roughness).setMetallicRoughnessTexture(null);
   mode='cloth mask + authored colours + available detail/macro/AO';
  } else if(spec.kind==='hair'||/eyelashes/.test(material.getName())) {
   const source=spec.kind==='hair'?await read(t.alpha_r||t.alpha):await read(t.alpha||t.basecolor);
   if(!source)throw Error('Coverage texture missing: '+material.getName());
   w=source.width;h=source.height;output=Buffer.alloc(w*h*4);
   // Some exported coverage PNGs carry white RGB and the actual mask in A.
   // Respect explicit metadata; infer A only for that unambiguous layout.
   let whiteRGB=true,hasAlpha=false;
   for(let k=0;k<source.data.length;k+=4){
    whiteRGB&&=source.data[k]===255&&source.data[k+1]===255&&source.data[k+2]===255;
    hasAlpha||=source.data[k+3]!==255;
   }
   const inferred=spec.kind!=='hair'&&whiteRGB&&hasAlpha?'a':'r';
   const channel={r:0,g:1,b:2,a:3}[p.alpha_channel||inferred];
   if(channel===undefined)throw Error('Unknown coverage channel');
   for(let k=0;k<output.length;k+=4) {
    output[k]=output[k+1]=output[k+2]=255;
    if(spec.kind==='hair') {
     const rootChannel=channel===3?0:1;
     output[k]=Math.round(srgb(source.data[k+rootChannel]/255)*255);
     output[k+1]=Math.round(srgb(source.data[k+2]/255)*255);
    }
    output[k+3]=source.data[k+channel];
   }
   material.setBaseColorFactor(spec.kind==='hair'?[1,1,1,1]:[...p.base_color.slice(0,3),1])
    .setAlphaMode('BLEND').setDoubleSided(true).setMetallicFactor(0).setRoughnessFactor(p.roughness);
   if(spec.kind==='hair')hairMaterials.add(material);
   mode='source coverage channel -> alpha; smooth blended fringe';
  } else if(p.eye_pole_uv) {
   const atlas=await read(t.basecolor);if(!atlas)throw Error('Missing iris atlas');
   w=h=2048;output=Buffer.alloc(w*h*4);
   const radiusOuter=(p.iris_radius_in+p.iris_radius_out)*.5;
   for(let y=0;y<h;y++)for(let x=0;x<w;x++) {
    const u=(x+.5)/w,v=(y+.5)/h,k=(y*w+x)*4;
    const du=u-p.eye_pole_uv[0],dv=v-p.eye_pole_uv[1],radius=Math.hypot(du,dv);
    // Fit the authored atlas' entire iris inside the limbus. Its B channel
    // is a radial mask, not an all-over darkening amount for the iris.
    const au=.5+du*.46/radiusOuter,av=.5+dv*.46/radiusOuter;
    const fibers=sample(atlas,au,av,0,false,false),crypt=sample(atlas,au,av,1,false,false);
    const fibril=.30+1.15*Math.pow(clamp(fibers),.8);
    const limbus=smooth(radiusOuter*.83,radiusOuter,radius)*.8;
    const iris=smooth(p.iris_radius_in,p.iris_radius_out,radius);
    const pupilRadius=(p.pupil_radius_in+p.pupil_radius_out)*.5;
    const pupil=1-smooth(pupilRadius-.003,pupilRadius+.003,radius);
    const collar=Math.exp(-Math.pow((radius/radiusOuter-.34)/.12,2));
    const angle=Math.atan2(dv,du);
    // Restrained branching vascularity away from the limbus; never a white disc.
    const vein=Math.pow(Math.max(0,Math.sin(angle*37+Math.sin(radius*160)*.65)),24)*smooth(radiusOuter*1.1,radiusOuter*2.3,radius)*.11;
    for(let c=0;c<3;c++) {
     const detail=p.iris_color[c]*fibril*(1+collar*(crypt-.3)*.7);
     const sclera=p.sclera_color[c]*.43*(1-vein)+p.vein_color[c]*vein;
     const base=mix(mix(detail,p.iris_dark[c]*.35,limbus),sclera,iris);
     output[k+c]=Math.round(clamp(srgb(mix(base,.0015,pupil)))*255);
    } output[k+3]=255;
   }
   material.setBaseColorFactor([1,1,1,1]).setRoughnessFactor(.6).setMetallicFactor(0);
   material.setExtras({...material.getExtras(),aiverseEye:{version:1,optics:[...p.eye_pole_uv,radiusOuter,.012],ior:1.376,tearFilm:.7}});
   mode='authored iris fibers/crypts, radial limbus, sclera vascularity; layered native cornea with portable PBR fallback';
  }
  if(output) {
   material.setBaseColorTexture(await texture(material.getName()+'_vivid_baked',output,w,h));
   // Baked eye image already lives in original mesh UV; do not apply atlas zoom twice.
   if(p.eye_pole_uv) {
    const info=material.getBaseColorTextureInfo();
    const ext=info.getExtension('KHR_texture_transform');if(ext)ext.dispose();
   }
   report.push({material:material.getName(),mode,width:w,height:h,pixelSha256:crypto.createHash('sha256').update(output).digest('hex')});
  }
 }
 // Shared accessors and textures, separate depth-writing core and blended fringe.
 // Do not use a hard cutoff for lashes: it removes subpixel strand coverage.
 const cores=new Map();
 for(const mesh of doc.getRoot().listMeshes()) {
  for(const primitive of mesh.listPrimitives().slice()) {
   const outer=primitive.getMaterial();
   if(!hairMaterials.has(outer))continue;
   let core=cores.get(outer);
   if(!core) {
    core=outer.clone().setName(outer.getName()+'__coverage_core')
      .setAlphaMode('MASK').setAlphaCutoff(.5);
    cores.set(outer,core);
   }
   mesh.addPrimitive(primitive.clone().setMaterial(core));
  }
 }
 return report;
}
module.exports={bakeMaterials,linear,srgb,sample};
