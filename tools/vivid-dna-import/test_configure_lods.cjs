const assert=require('node:assert/strict'),{validateChains}=require('./configure_lods.cjs');
const meshes=['face0','face1','face2'].map(name=>({name,skinned:true,skins:'0',morphs:['blink','smile']}));
const spec={version:1,chains:[{source:'face0',levels:[{mesh:'face1',maxPixels:500},{mesh:'face2',maxPixels:200}]}]};
assert.equal(validateChains(spec,meshes),spec);
const bad=edit=>{const s=JSON.parse(JSON.stringify(spec));edit(s);assert.throws(()=>validateChains(s,meshes));};
bad(s=>s.chains[0].levels[1].maxPixels=600);bad(s=>s.chains[0].levels[0].mesh='face0');bad(s=>s.chains.push({source:'face1',levels:[{mesh:'face2',maxPixels:100}]}));
assert.throws(()=>validateChains(spec,meshes.map((m,i)=>i===1?{...m,morphs:['blink']}:m)),/morph/);
assert.throws(()=>validateChains(spec,meshes.map((m,i)=>i===1?{...m,skins:'1'}:m)),/skin/);
console.log('PASS authored thresholds, cycles, morph semantics, skin identity');
