// UE5.7 companion card metadata -> standard glTF RGBA, preserving authored RGB.
// node prepare_companion_coverage.cjs tooling-dir input.gltf mapping.json maps-dir new-output.glb
const fs=require('node:fs'),path=require('node:path'),{createRequire}=require('node:module');
(async()=>{const [tooling,input,mapping,maps,out]=process.argv.slice(2);if(!out||fs.existsSync(out))throw Error('Expected new output');
const load=createRequire(path.resolve(tooling,'package.json')),{NodeIO}=load('@gltf-transform/core'),sharp=load('sharp');const io=new NodeIO().registerExtensions(load('@gltf-transform/extensions').ALL_EXTENSIONS),doc=await io.read(input),r=doc.getRoot();
for(const spec of JSON.parse(fs.readFileSync(mapping,'utf8')).materials){const m=r.listMaterials().find(m=>m.getName()===spec.material_name);if(!m)throw Error('Missing material '+spec.material_name);const p=spec.params;
if(!p.alpha_clip)continue;const name=p.alpha_stem?p.alpha_stem+'.png':path.basename(spec.textures.alpha||'');if(!name)throw Error('Missing coverage');const file=path.resolve(maps,name);if(!file.startsWith(path.resolve(maps)+path.sep))throw Error('Unsafe coverage path');
const {data,info}=await sharp(file,{limitInputPixels:67108864}).ensureAlpha().raw().toBuffer({resolveWithObject:true});const channel={r:0,g:1,b:2,a:3}[p.alpha_channel||'r'];if(channel===undefined)throw Error('Invalid coverage channel');
let white=true,alpha=false;for(let k=0;k<data.length;k+=4){white&&=data[k]===255&&data[k+1]===255&&data[k+2]===255;alpha||=data[k+3]!==255;}
const actual=!p.alpha_channel&&white&&alpha?3:channel;for(let k=0;k<data.length;k+=4){const a=data[k+actual];data[k]=data[k+1]=data[k+2]=255;data[k+3]=a;}
const tex=doc.createTexture(m.getName()+'_authored_coverage').setImage(await sharp(data,{raw:{width:info.width,height:info.height,channels:4}}).png().toBuffer()).setMimeType('image/png');
m.setBaseColorTexture(tex).setBaseColorFactor(p.base_color).setAlphaMode('BLEND').setAlphaCutoff(.15).setDoubleSided(true).setRoughnessFactor(.7).setMetallicFactor(0);
}
const head=r.listNodes().find(n=>n.getName()==='vivid_face_head');if(!head)throw Error('Missing head');for(const n of r.listNodes())if(n.getMesh()&&!n.getSkin()&&/Hair_|Eyebrows_|Beard_|Mustache_/i.test(n.getMesh().getName()))require('./attach_grooms.cjs').attachRigidGroom(doc,n,head);
for(const m of r.listMaterials()){
 if(/^MID_M_/.test(m.getName()))m.setMetallicFactor(0).setRoughnessFactor(.85).setMetallicRoughnessTexture(null).setNormalScale(.35);

 if(/MI_Eye[LR]_Baked/.test(m.getName()))m.setExtras({...m.getExtras(),aiverseEye:{version:1,optics:[.5,.5,.14,.012],ior:1.376,tearFilm:.4}}).setRoughnessFactor(.3);
 // Exported eye shell is an Unreal shader mask, not a standard clear cornea.
 if(m.getName()==='MI_Face_EyeShell')m.setBaseColorFactor([1,1,1,0]).setAlphaMode('MASK').setAlphaCutoff(.5);
}
await io.write(out,doc);console.log('PASS authored card coverage, groom attachment and eye-shell conversion');
})().catch(e=>{console.error(e);process.exitCode=1;});
