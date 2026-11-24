import test from 'node:test';
import assert from 'node:assert/strict';
import {projectBuildingMaterialDependencies} from '../src/graphs/buildingMaterialDependencies.ts';
const guid='a'.repeat(32),asset={guid,path:'textures/set/color.png',bytes:20,importer:'texture'};
const library={library:{brick:{texture:guid,normalMap:'content/textures/set/color.png',emissiveMap:'missing.png'}}};
test('shared material and texture deduplicate, channels stay separate',()=>{
 const p=projectBuildingMaterialDependencies({wall:'brick',roof:'brick'},library,[asset]);
 assert.equal(p.materials.size,1);assert.equal(p.textures.size,2);
 assert.equal(p.view.links.filter(l=>l.to==='asset:'+guid).length,2);
 assert.ok(!p.view.nodes.some(n=>n.id.startsWith('bake:')));
});
test('only matching recorded provenance adds bake node',()=>{
 const provenance={guid,path:asset.path,manifestFound:true,manifestPath:'textures/set/texture-set.json',output:'color'};
 const build=p=>projectBuildingMaterialDependencies({wall:'brick'},library,[asset],p).view;
 assert.ok(build(provenance).nodes.some(n=>n.id==='bake:'+guid));
 assert.ok(!build({...provenance,path:'textures/other/color.png'}).nodes.some(n=>n.id.startsWith('bake:')));
 assert.ok(!build({...provenance,manifestFound:false}).nodes.some(n=>n.id.startsWith('bake:')));
});
test('missing material and unsafe or non-texture asset stay unresolved',()=>{
 const p=projectBuildingMaterialDependencies({wall:'brick',roof:'gone'},library,[{...asset,importer:'model'}]);
 assert.equal(p.textures.get('unresolved:'+guid).asset,undefined);
 assert.ok(p.view.nodes.some(n=>n.label==='Missing · gone'));
});
